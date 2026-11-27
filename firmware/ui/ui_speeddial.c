/*
 * ui_speeddial.c — HOME speed dials (spec §2) + their settings sub-page.
 *
 * Nine slots, one per T9 key 1..9. Each slot holds a contact name; an empty
 * slot is inert. The names live in the model layer (model/settings.c,
 * spec §16.4) and persist through hal_storage. A slot's name cycles through
 * a fixed preset list, so configuring a dial works without a text keyboard:
 * CENTER on the sub-page row advances the slot to the next preset (wrapping
 * back to empty).
 *
 * The sub-page is one canonical menu_t (ui_menu.h): one row per slot, the
 * live name in the row label — the exact pattern of the settings tree
 * (rows carry live data; CENTER mutates; the shell refreshes the rows).
 */

#include <stdio.h>

#include "model/settings.h"
#include "ui/ui_speeddial.h"
#include "ui/ui_icons.h"
#include "ui/ui_keymap.h"

const char *ui_speeddial_name(uint8_t i)
{
    return i < SPEEDDIAL_N ? model_settings()->speeddial[i] : "";
}

bool ui_speeddial_set(uint8_t i)
{
    return i < SPEEDDIAL_N && model_settings()->speeddial[i][0] != '\0';
}

void ui_speeddial_cycle(uint8_t i)
{
    model_settings_cycle_speeddial(i);
}

/* ------------------------------------------------------------------ */
/* The settings sub-page: one canonical menu_t, one row per slot.      */
/* ------------------------------------------------------------------ */

/* Shown in the row label of an empty slot (the renderer draws "" as
 * nothing). */
static const char g_empty_lbl[] = "(none)";

/* This page's contextual keymap: back with shoulder-R, home with END,
 * up/down moves the selection, CENTER cycles the selected slot. */
static const keymap_t km_speeddial = {
    .shoulder_l = ACT_OPEN_OPTIONS,
    .shoulder_r = ACT_GO_BACK,
    .center     = ACT_ACTIVATE,
    .end        = ACT_GOTO_HOME,
    .up         = ACT_MOVE_SEL_UP,
    .down       = ACT_MOVE_SEL_DOWN,
};

static menu_item_t g_items[SPEEDDIAL_N];

static menu_t g_page = {
    .title   = "SPEED DIALS",
    .items   = g_items,
    .n_items = SPEEDDIAL_N,
    .sel     = 0,
    .cats    = NULL,
    .n_cats  = 0,
    .cat_sel = 0,
    .keys    = km_speeddial,
};

/* Repoint each row's label at the slot's live name (or "(none)"). */
static void refresh_rows(void)
{
    for (int i = 0; i < SPEEDDIAL_N; i++) {
        const char *name = model_settings()->speeddial[i];
        g_items[i].label  = name[0] ? name : g_empty_lbl;
        g_items[i].icon   = I_USER;
        g_items[i].target = PAGE_NONE;   /* activation = cycle, not a page */
    }
}

menu_t *ui_speeddial_page(void)
{
    refresh_rows();
    return &g_page;
}
