/*
 * ui_settings.c — the Settings page as a canonical menu_t with working toggles.
 *
 * Pages are data (spec §4): this file fills in one menu_t. Each row's value
 * lives in its `meta` field, which the generic renderer (ui_menu.c) already
 * draws right-aligned — so a "settings row" needs no new rendering path.
 * CENTER on a two-state row flips it and repoints `meta` at the new value
 * string; the shell then calls ui_list_refresh() to redraw the rows.
 *
 * State is static, in-memory: no persistence yet (spec §9 — the local store
 * arrives with the WAHA backbone).
 */

#include "ui/ui_settings.h"
#include "ui/ui_icons.h"
#include "ui/ui_keymap.h"

/* Display strings for each two-state value, in state order. */
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
 * refresh_values()). Speed Dials and Held Presses have no value — their
 * targets open their config sub-pages. */
static menu_item_t g_items[N_SETTINGS] = {
    { .label = "Menu Style",      .icon = I_TH,    .target = PAGE_NONE },
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

/* Repoint each row's `meta` at the current value string. */
static void refresh_values(void)
{
    g_items[SETTING_MENU_STYLE].meta = g_menu_style_vals[g_menu_style];
    g_items[SETTING_CALL_PREF].meta  = g_call_pref_vals[g_call_pref];
}

menu_t *ui_settings_page(void)
{
    refresh_values();
    return &g_page;
}

void ui_settings_activate(uint8_t sel)
{
    if (sel >= N_SETTINGS) {
        return;
    }
    switch (sel) {
    case SETTING_MENU_STYLE: g_menu_style ^= 1; break;
    case SETTING_CALL_PREF:  g_call_pref ^= 1;  break;
    default:                 return;   /* navigable row: the shell opened it */
    }
    refresh_values();
}

uint8_t ui_settings_menu_style(void) { return g_menu_style; }

void ui_settings_set_menu_style(uint8_t style)
{
    g_menu_style = style ? MENU_STYLE_GRID : MENU_STYLE_CAROUSEL;
}
