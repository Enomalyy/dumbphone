/*
 * ui_settings.c — the settings state and its Display/Font pages, as
 * canonical menu_t pages with working values.
 *
 * The settings list itself is the main menu's SETTINGS category (spec
 * §14.13 — there is no settings-tree page): its rows carry their real
 * targets, and Call Preference toggles in place through ui_settings_
 * activate(). This module owns the two pages that category reaches:
 *
 *   DISPLAY    Theme              DARK | LIGHT
 *              Font               -> HEADERS / TEXT / SUBTEXT
 *              Menu Style         CAROUSEL | GRID | MOVING
 *              Softkey bar        SHOWN | HIDDEN
 *
 *   FONT       Headers            "<FACE> <px>", CENTER = next ladder entry
 *              Text
 *              Subtext
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
 * This module is a READER + WRITER for the settings state: every value lives
 * in the model layer (model/settings.c, spec §16.4), which persists it
 * through hal_storage.
 */

#include <stdio.h>

#include "model/settings.h"
#include "ui/ui_settings.h"
#include "ui/ui_icons.h"
#include "ui/ui_keymap.h"
#include "ui/ui_theme.h"

/* Display strings. Row metas must fit the measured value column; the face
 * names are short on purpose (see ui_face_short) because "SILKSCREEN 24"
 * does not fit 104 px and truncating it would defeat the point. */
/* One cycling control (spec §14.3): Grid → Carousel → Moving carousel.
 * The ring is the same in any order; values follow the enum. */
static const char *const g_menu_style_vals[] = { "CAROUSEL", "GRID", "MOVING" };
static const char *const g_call_pref_vals[]  = { "WHATSAPP", "STANDARD" };

/* The live values live in the model layer (spec §16.4); this page reads
 * them through model_settings() and writes through its mutators. */

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
    const model_settings_t *s = model_settings();

    g_disp_items[DISPLAY_THEME].meta =
        ui_theme_name((ui_theme_id_t)(ui_theme_get() == UI_THEME_DARK ? 0 : 1));

    g_disp_items[DISPLAY_MENU_STYLE].meta = g_menu_style_vals[s->menu_style];

    g_disp_items[DISPLAY_SOFTKEYS].meta =
        g_softkey_vals[ui_geom_softkeys() ? 0 : 1];

    refresh_font_meta();
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

const char *ui_settings_call_pref_name(void)
{
    return g_call_pref_vals[model_settings()->call_pref];
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
    const model_settings_t *s = model_settings();
    switch (sel) {
    case SETTING_CALL_PREF:
        /* The main menu's Call Preference row toggles in place here (spec
         * §14.13); the menu repoints its meta and refreshes the row. */
        model_settings_set_call_pref(s->call_pref ^ 1);
        break;
    default:                return UI_SETTING_EFFECT_NONE;  /* navigable */
    }
    return UI_SETTING_EFFECT_NONE;
}

ui_setting_effect_t ui_settings_display_activate(uint8_t sel)
{
    if (sel >= N_DISPLAY_SETTINGS) {
        return UI_SETTING_EFFECT_NONE;
    }
    const model_settings_t *s = model_settings();
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
        uint8_t next = (uint8_t)((s->menu_style + 1) % 3);
        model_settings_set_menu_style(next);
        ui_settings_set_menu_style(next);   /* main menu rebuilds itself */
        effect = UI_SETTING_EFFECT_MENU_STYLE;
        break;
    case DISPLAY_SOFTKEYS:
        ui_geometry_set_softkeys(!s->softkeys);
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

uint8_t ui_settings_menu_style(void) { return model_settings()->menu_style; }

void ui_settings_set_menu_style(uint8_t style)
{
    if (style != MENU_STYLE_GRID && style != MENU_STYLE_MOVING) {
        style = MENU_STYLE_CAROUSEL;
    }
    model_settings_set_menu_style(style);
}
