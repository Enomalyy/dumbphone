/*
 * ui_calls.c — real content for the Calls category (spec §2).
 *
 * Two canonical menu_t pages over static mock data:
 *   - Call History: recent calls as two-line rows (name + direction, time in
 *     the renderer's meta column) — the same row shape as the chat list.
 *   - Contacts: name + number (the number in the renderer's meta column).
 *
 * Pages are data (spec §4): this file fills in the blanks; the generic
 * renderer (ui_menu.c) draws the rows and the shell routes keys through each
 * page's own keymap. Rows are inert (target PAGE_NONE) — opening a call or
 * composing a number is a later leg (charter scope: stubs are fine).
 *
 * The data lives in the model layer (model/call_log.c, model/contacts.c,
 * spec §16.4): static mock for now, standing in for the on-device store until
 * the WAHA backbone lands (spec §9). These pages read it through the
 * accessors and never own a copy.
 */

#include <stddef.h>

#include "model/call_log.h"
#include "model/contacts.h"
#include "ui/ui_calls.h"
#include "ui/ui_icons.h"
#include "ui/ui_keymap.h"

/* Both pages share one keymap: back with shoulder-R, home with END,
 * up/down moves the selection. CENTER is inert — the rows have no target. */
static const keymap_t km_calls = {
    .shoulder_l = ACT_OPEN_OPTIONS,
    .shoulder_r = ACT_GO_BACK,
    .end        = ACT_GOTO_HOME,
    .up         = ACT_MOVE_SEL_UP,
    .down       = ACT_MOVE_SEL_DOWN,
};

/* ------------------------------------------------------------------ */
/* Call History — recent calls, two-line rows like the chat list.     */
/* ------------------------------------------------------------------ */

/* The row pool is sized for the model's cap; the live count comes from the
 * model at page build time (fill_history), so real data (spec §9) needs no
 * page change. */
static menu_item_t g_hist_items[MODEL_CALLS_MAX];

static menu_t g_hist_page = {
    .title   = "CALL HISTORY",
    .items   = g_hist_items,
    .n_items = MODEL_CALLS_MAX,   /* fixed up by fill_history() */
    .sel     = 0,
    .cats    = NULL,
    .n_cats  = 0,
    .cat_sel = 0,
    .keys    = km_calls,
};

/* Repoint each row at its entry: name + direction as the two lines, time in
 * the meta column (the renderer draws both from the same structure). */
static void fill_history(void)
{
    int n = model_calls_count();
    if (n > MODEL_CALLS_MAX) {
        n = MODEL_CALLS_MAX;
    }
    g_hist_page.n_items = (uint8_t)n;
    for (int i = 0; i < n; i++) {
        g_hist_items[i].label  = model_call_name(i);
        g_hist_items[i].sub    = model_call_dir(i);
        g_hist_items[i].icon   = I_PHONE;
        g_hist_items[i].meta   = model_call_time(i);
        g_hist_items[i].target = PAGE_NONE;   /* opening a call: later leg */
    }
}

menu_t *ui_calls_history_page(void)
{
    fill_history();
    return &g_hist_page;
}

/* ------------------------------------------------------------------ */
/* Contacts — name + number (number in the meta column).              */
/* ------------------------------------------------------------------ */

/* The row pool is sized for the model's cap; the live count comes from the
 * model at page build time (fill_contacts; numbers stay hyphen-free — see
 * model/contacts.c). */
static menu_item_t g_contact_items[MODEL_CONTACTS_MAX];

static menu_t g_contact_page = {
    .title   = "CONTACTS",
    .items   = g_contact_items,
    .n_items = MODEL_CONTACTS_MAX,   /* fixed up by fill_contacts() */
    .sel     = 0,
    .cats    = NULL,
    .n_cats  = 0,
    .cat_sel = 0,
    .keys    = km_calls,
};

static void fill_contacts(void)
{
    int n = model_contacts_count();
    if (n > MODEL_CONTACTS_MAX) {
        n = MODEL_CONTACTS_MAX;
    }
    g_contact_page.n_items = (uint8_t)n;
    for (int i = 0; i < n; i++) {
        g_contact_items[i].label  = model_contact_name(i);
        g_contact_items[i].icon   = I_USER;
        g_contact_items[i].meta   = model_contact_number(i);
        g_contact_items[i].target = PAGE_NONE;   /* no compose screen yet */
    }
}

menu_t *ui_calls_contacts_page(void)
{
    fill_contacts();
    return &g_contact_page;
}
