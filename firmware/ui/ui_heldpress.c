/*
 * ui_heldpress.c — held-press (long-press) key config (spec §2).
 *
 * See ui_heldpress.h for the contract. Sixteen configurable keys, each
 * holding a page target: a long press of that key opens the page. The
 * preset list is short on purpose — these are quick actions reachable from
 * any page without touching the main menu, and the display strings must fit
 * the renderer's 104 px meta column (UI_ROW_META_W).
 */

#include <string.h>

#include "ui/ui_heldpress.h"
#include "ui/ui_icons.h"
#include "ui/ui_keymap.h"

/* The configurable keys, in slot order (the sub-page's row order). */
static const hal_key_t g_keys[HELDPRESS_N] = {
    HAL_KEY_SHOULDER_L, HAL_KEY_SHOULDER_R,
    HAL_KEY_UP,         HAL_KEY_DOWN,
    HAL_KEY_DIGIT_0, HAL_KEY_DIGIT_1, HAL_KEY_DIGIT_2, HAL_KEY_DIGIT_3,
    HAL_KEY_DIGIT_4, HAL_KEY_DIGIT_5, HAL_KEY_DIGIT_6, HAL_KEY_DIGIT_7,
    HAL_KEY_DIGIT_8, HAL_KEY_DIGIT_9, HAL_KEY_STAR,   HAL_KEY_HASH,
};

/* Row labels for the sub-page (the key name). */
static const char *const g_key_lbl[HELDPRESS_N] = {
    "SH L", "SH R", "UP", "DOWN",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "#",
};

/* Preset actions a slot can take, in cycle order (wraps back to none). */
static const page_id_t g_presets[] = {
    PAGE_NONE,         /* unconfigured: long press = short press   */
    PAGE_NEW_MESSAGE,  /* Messaging: compose                      */
    PAGE_NEW_CALL,     /* Calls: new call                         */
    PAGE_MAP_SEARCH,   /* Maps: search                            */
    PAGE_CALL_HISTORY, /* Calls: history                          */
    PAGE_CONTACTS,     /* Calls: contacts                         */
};
#define N_PRESETS ((int)(sizeof g_presets / sizeof g_presets[0]))

/* Display strings for the presets, in preset order. Kept to 8 chars so
 * they fit the renderer's meta column on one line (like "CAROUSEL"). */
static const char *const g_preset_lbl[] = {
    "(none)", "MSG", "CALL", "MAPS", "HIST", "PEOPLE",
};

/* The sixteen slots. Seeded with one action so the feature is visible out
 * of the box (mock data, in the spirit of the speed-dial seeding). */
static page_id_t g_slots[HELDPRESS_N] = {
    [0] = PAGE_NEW_MESSAGE,   /* SHOULDER-L: quick compose         */
};

/* ------------------------------------------------------------------ */
/* Slot accessors                                                      */
/* ------------------------------------------------------------------ */

static int slot_of(hal_key_t key)
{
    for (int i = 0; i < HELDPRESS_N; i++) {
        if (g_keys[i] == key) {
            return i;
        }
    }
    return -1;
}

page_id_t ui_heldpress_target(hal_key_t key)
{
    int i = slot_of(key);
    return i >= 0 ? g_slots[i] : PAGE_NONE;
}

void ui_heldpress_cycle(uint8_t i)
{
    if (i >= HELDPRESS_N) {
        return;
    }
    int cur = 0;   /* unknown value (cannot happen): treat as none */
    for (int p = 0; p < N_PRESETS; p++) {
        if (g_slots[i] == g_presets[p]) {
            cur = p;
            break;
        }
    }
    g_slots[i] = g_presets[(cur + 1) % N_PRESETS];
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
        int p = 0;   /* unknown value (cannot happen): treat as none */
        for (int q = 1; q < N_PRESETS; q++) {
            if (g_slots[i] == g_presets[q]) {
                p = q;
                break;
            }
        }
        g_items[i].label  = g_key_lbl[i];
        g_items[i].icon   = I_BOLT;
        g_items[i].meta   = g_preset_lbl[p];
        g_items[i].target = PAGE_NONE;   /* activation = cycle, not a page */
    }
}

menu_t *ui_heldpress_page(void)
{
    refresh_rows();
    return &g_page;
}
