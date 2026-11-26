/*
 * ui_settings.c — the Settings tree and its Display/Font sub-pages, as
 * canonical menu_t pages with working values.
 *
 * Pages are data (spec §4): each page is one menu_t, and every row's value
 * lives in its `meta` field, which the generic renderer (ui_menu.c) draws
 * right-aligned. CENTER on a value row cycles it and repoints `meta` at the
 * new string; the shell then
 *
 *   - refreshes the page (ui_list_refresh), and
 *   - runs the EFFECT the change asked for: a theme switch, a font reload +
 *     restyle, a re-layout for the softkey bar.
 *
 * That split is deliberate: this file owns SETTINGS STATE (what the user
 * picked), the shell owns the LVGL objects and the appearance passes. The
 * settings module therefore has no LVGL dependency at all.
 *
 * Tree (spec §14.9-14.12):
 *
 *   SETTINGS   Display            -> THEME, FONT, MENU STYLE, SOFTKEY BAR
 *              Call Preference
 *              Speed Dials        (sub-page)
 *              Held Presses       (sub-page)
 *
 *   DISPLAY    Theme              DARK | LIGHT
 *              Font               -> HEADERS / TEXT / SUBTEXT
 *              Menu Style         CAROUSEL | GRID
 *              Softkey bar        SHOWN | HIDDEN
 *
 *   FONT       Headers            "<FACE> <px>", CENTER = next ladder entry
 *              Text
 *              Subtext
 *
 * State is static, in-memory: no persistence yet (spec §9 — the local store
 * arrives with the WAHA backbone).
 */

#include <stdio.h>

#include "ui/ui_settings.h"
#include "ui/ui_icons.h"
#include "ui/ui_keymap.h"
#include "ui/ui_theme.h"

/* Display strings. Row metas must fit the measured value column; the face
 * names are short on purpose (see ui_face_short) because "SILKSCREEN 24"
 * does not fit 104 px and truncating it would defeat the point. */
static const char *const g_menu_style_vals[] = { "CAROUSEL", "GRID" };
static const char *const g_call_pref_vals[]  = { "WHATSAPP", "STANDARD" };

/* In-memory state (no persistence yet). */
static uint8_t g_menu_style = MENU_STYLE_CAROUSEL;
static uint8_t g_call_pref  = CALL_PREF_WHATSAPP;

/* This page's contextual keymap: back with shoulder-R, home with END,
 * up/down moves the selection, CENTER toggles the selected setting. */
static const keymap_t km_settings = {
    .shoulder_l = ACT_OPEN_OPTIONS,
    .shoulder_r = ACT_GO_BACK,
    .center     = ACT_ACTIVATE,
    .end        = ACT_GOTO_HOME,
    .up         = ACT_MOVE_SEL_UP,
    .down       = ACT_MOVE_SEL_DOWN,
};

/* Rows: label + icon fixed; `meta` points at the live value text (set by
 * refresh_values()). Rows with a target open a sub-page instead. */
static menu_item_t g_items[N_SETTINGS] = {
    { .label = "Display",         .icon = I_PALETTE, .target = PAGE_DISPLAY },
    { .label = "Call Preference", .icon = I_PHONE, .target = PAGE_NONE },
    { .label = "Speed Dials",     .icon = I_USER,  .target = PAGE_SPEED_DIALS },
    { .label = "Held Presses",    .icon = I_BOLT,  .target = PAGE_HELD_PRESS },
};

static menu_t g_page = {
    .title   = "SETTINGS",
    .items   = g_items,
    .n_items = N_SETTINGS,
    .sel     = 0,
    .cats    = NULL,
    .n_cats  = 0,
    .cat_sel = 0,
    .keys    = km_settings,
};

/* ------------------------------------------------------------------ */
/* Display page                                                        */
/* ------------------------------------------------------------------ */

static menu_item_t g_disp_items[N_DISPLAY_SETTINGS] = {
    /* Icons say what the row is, or there is none: theme = dark/light
     * contrast, font = a face, menu style = the grid view, and the bar is
     * its own shape — no glyph pretends to summarise it. */
    { .label = "Theme",       .icon = I_ADJUST,  .target = PAGE_NONE },
    { .label = "Font",        .icon = I_FONT,    .target = PAGE_FONT },
    { .label = "Menu Style",  .icon = I_TH,      .target = PAGE_NONE },
    { .label = "Softkey bar", .icon = "",        .target = PAGE_NONE },
};

static menu_t g_display_page = {
    .title   = "DISPLAY",
    .items   = g_disp_items,
    .n_items = N_DISPLAY_SETTINGS,
    .sel     = 0,
    .cats    = NULL,
    .n_cats  = 0,
    .cat_sel = 0,
    .keys    = km_settings,
};

static const char *const g_softkey_vals[] = { "SHOWN", "HIDDEN" };

/* ------------------------------------------------------------------ */
/* Font page — the three roles of spec §14.12                          */
/* ------------------------------------------------------------------ */

/* Each row's LABEL is drawn in the level it names, at the level's current
 * face and size — so the page is its own preview: change Headers and the
 * word "Headers" changes with every title in the UI. */
static menu_item_t g_font_items[UI_FONT_ROLE_COUNT] = {
    { .label = "Headers", .icon = I_HEADING,   .target = PAGE_NONE,
      .font_role = MENU_FONT_HEADER },
    { .label = "Text",    .icon = I_FONT,      .target = PAGE_NONE,
      .font_role = MENU_FONT_TEXT },
    { .label = "Subtext", .icon = I_PARAGRAPH, .target = PAGE_NONE,
      .font_role = MENU_FONT_SUBTEXT },
};

static menu_t g_font_page = {
    .title   = "FONT",
    .items   = g_font_items,
    .n_items = UI_FONT_ROLE_COUNT,
    .sel     = 0,
    .cats    = NULL,
    .n_cats  = 0,
    .cat_sel = 0,
    .keys    = km_settings,
};

/* "<FACE SHORT> <px>", the font page's value text. One buffer per row: the
 * renderer keeps pointers, so the strings must outlive the call. */
static char g_font_meta[UI_FONT_ROLE_COUNT][16];

static void refresh_font_meta(void)
{
    for (int r = 0; r < UI_FONT_ROLE_COUNT; r++) {
        ui_font_pref_t p = ui_font_pref((ui_font_role_t)r);
        snprintf(g_font_meta[r], sizeof g_font_meta[r], "%s %u",
                 ui_face_short((ui_face_t)p.face), (unsigned)p.px);
        g_font_items[r].meta = g_font_meta[r];
    }
}

/* ------------------------------------------------------------------ */
/* Value text                                                          */
/* ------------------------------------------------------------------ */

static void refresh_values(void)
{
    g_items[SETTING_CALL_PREF].meta = g_call_pref_vals[g_call_pref];

    g_disp_items[DISPLAY_THEME].meta =
        ui_theme_name((ui_theme_id_t)(ui_theme_get() == UI_THEME_DARK ? 0 : 1));

    g_disp_items[DISPLAY_MENU_STYLE].meta = g_menu_style_vals[g_menu_style];

    g_disp_items[DISPLAY_SOFTKEYS].meta =
        g_softkey_vals[ui_geom_softkeys() ? 0 : 1];

    refresh_font_meta();
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

menu_t *ui_settings_page(void)
{
    refresh_values();
    return &g_page;
}

menu_t *ui_settings_display_page(void)
{
    refresh_values();
    return &g_display_page;
}

menu_t *ui_settings_font_page(void)
{
    refresh_values();
    return &g_font_page;
}

ui_setting_effect_t ui_settings_activate(uint8_t sel)
{
    if (sel >= N_SETTINGS) {
        return UI_SETTING_EFFECT_NONE;
    }
    switch (sel) {
    case SETTING_CALL_PREF: g_call_pref ^= 1; break;
    default:                return UI_SETTING_EFFECT_NONE;  /* navigable */
    }
    refresh_values();
    return UI_SETTING_EFFECT_NONE;
}

ui_setting_effect_t ui_settings_display_activate(uint8_t sel)
{
    if (sel >= N_DISPLAY_SETTINGS) {
        return UI_SETTING_EFFECT_NONE;
    }
    ui_setting_effect_t effect = UI_SETTING_EFFECT_NONE;

    switch (sel) {
    case DISPLAY_THEME:
        /* DARK <-> LIGHT. ui_theme_set() resolves the palette and runs the
         * shell's apply pass, so the whole UI repaints immediately. */
        ui_theme_set(ui_theme_get() == UI_THEME_DARK ? UI_THEME_LIGHT
                                                     : UI_THEME_DARK);
        effect = UI_SETTING_EFFECT_THEME;
        break;
    case DISPLAY_FONT:
        return UI_SETTING_EFFECT_NONE;   /* navigable row */
    case DISPLAY_MENU_STYLE:
        g_menu_style ^= 1;
        ui_settings_set_menu_style(g_menu_style);
        effect = UI_SETTING_EFFECT_MENU_STYLE;
        break;
    case DISPLAY_SOFTKEYS:
        ui_geometry_set_softkeys(!ui_geom_softkeys());
        effect = UI_SETTING_EFFECT_SOFTKEYS;
        break;
    default:
        return UI_SETTING_EFFECT_NONE;
    }

    refresh_values();
    return effect;
}

ui_setting_effect_t ui_settings_font_activate(uint8_t sel)
{
    if (sel >= UI_FONT_ROLE_COUNT) {
        return UI_SETTING_EFFECT_NONE;
    }
    /* The ladder advances face AND size; ui_font_pref_next() runs the apply
     * pass itself, so the panel shows the new face on the next paint. */
    ui_font_pref_next((ui_font_role_t)sel);
    refresh_values();
    return UI_SETTING_EFFECT_FONT;
}

uint8_t ui_settings_menu_style(void) { return g_menu_style; }

void ui_settings_set_menu_style(uint8_t style)
{
    g_menu_style = style ? MENU_STYLE_GRID : MENU_STYLE_CAROUSEL;
}
