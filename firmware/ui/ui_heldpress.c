/*
 * ui_heldpress.c — held-press (long-press) key config (spec §2).
 *
 * See ui_heldpress.h for the contract. Sixteen configurable keys, each
 * holding a page target: a long press of that key opens the page. The
 * preset list is short on purpose — these are quick actions reachable from
 * any page without touching the main menu, and the display strings must fit
 * the renderer's 104 px meta column (UI_ROW_META_W).
 *
 * The bindings live in the model layer (model/settings.c, spec §16.4) and
 * persist through hal_storage: this page reads the actions and maps each to
 * the page that implements it.
 */

#include <string.h>

#include "model/settings.h"
#include "ui/ui_heldpress.h"
#include "ui/ui_icons.h"
#include "ui/ui_keymap.h"

/* Row labels for the sub-page (the key name). The key->slot table itself is
 * model data (model_settings_heldpress_slot). */
static const char *const g_key_lbl[HELDPRESS_N] = {
    "SH L", "SH R", "UP", "DOWN",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "#",
};

/* Display strings for the preset actions, in model_heldpress_t order. Kept
 * to 8 chars so they fit the renderer's meta column on one line (like
 * "CAROUSEL"). */
static const char *const g_preset_lbl[MODEL_HP_COUNT] = {
    "(none)", "MSG", "CALL", "MAPS", "HIST", "PEOPLE",
};

/* The page that implements each preset action. */
static page_id_t hp_page(model_heldpress_t a)
{
    switch (a) {
    case MODEL_HP_NEW_MESSAGE:  return PAGE_NEW_MESSAGE;
    case MODEL_HP_NEW_CALL:     return PAGE_NEW_CALL;
    case MODEL_HP_MAP_SEARCH:   return PAGE_MAP_SEARCH;
    case MODEL_HP_CALL_HISTORY: return PAGE_CALL_HISTORY;
    case MODEL_HP_CONTACTS:     return PAGE_CONTACTS;
    default:                    return PAGE_NONE;
    }
}

/* ------------------------------------------------------------------ */
/* Slot accessors                                                      */
/* ------------------------------------------------------------------ */

page_id_t ui_heldpress_target(hal_key_t key)
{
    int i = model_settings_heldpress_slot((int)key);
    if (i < 0) {
        return PAGE_NONE;
    }
    return hp_page((model_heldpress_t)model_settings()->heldpress[i]);
}

void ui_heldpress_cycle(uint8_t i)
{
    model_settings_cycle_heldpress(i);
}

/* ------------------------------------------------------------------ */
/* The settings sub-page: one canonical menu_t, one row per key.       */
/* ------------------------------------------------------------------ */

/* This page's contextual keymap: back with shoulder-R, home with END,
 * up/down moves the selection, CENTER cycles the selected slot. */
static const keymap_t km_heldpress = {
    .shoulder_l = ACT_OPEN_OPTIONS,
    .shoulder_r = ACT_GO_BACK,
    .center     = ACT_ACTIVATE,
    .end        = ACT_GOTO_HOME,
    .up         = ACT_MOVE_SEL_UP,
    .down       = ACT_MOVE_SEL_DOWN,
};

static menu_item_t g_items[HELDPRESS_N];

static menu_t g_page = {
    .title   = "HELD PRESSES",
    .items   = g_items,
    .n_items = HELDPRESS_N,
    .sel     = 0,
    .cats    = NULL,
    .n_cats  = 0,
    .cat_sel = 0,
    .keys    = km_heldpress,
};

/* Repoint each row at its key name (label) and live preset (meta). */
static void refresh_rows(void)
{
    for (int i = 0; i < HELDPRESS_N; i++) {
        model_heldpress_t a = (model_heldpress_t)model_settings()->heldpress[i];
        if (a >= MODEL_HP_COUNT) {
            a = MODEL_HP_NONE;   /* corrupt value: render as unconfigured */
        }
        g_items[i].label  = g_key_lbl[i];
        g_items[i].icon   = I_BOLT;
        g_items[i].meta   = g_preset_lbl[a];
        g_items[i].target = PAGE_NONE;   /* activation = cycle, not a page */
    }
}

menu_t *ui_heldpress_page(void)
{
    refresh_rows();
    return &g_page;
}
