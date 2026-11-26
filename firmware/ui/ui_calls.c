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
 * The data is static mock, in the spirit of chats.json: it stands in for the
 * on-device store until the WAHA backbone lands (spec §9).
 */

#include <stddef.h>

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

typedef struct {
    const char *name;
    const char *dir;    /* incoming | outgoing | missed        */
    const char *time;   /* short: fits the renderer's meta col  */
} call_entry_t;

#define N_CALLS ((int)(sizeof g_calls / sizeof g_calls[0]))

static const call_entry_t g_calls[] = {
    { "Mom",       "incoming", "09:12"      },
    { "Dad",       "outgoing", "08:47"      },
    { "Work",      "missed",   "08:30"      },
    { "Netanel",   "incoming", "yest 21:04" },
    { "Doctor",    "outgoing", "yest 09:15" },
    { "Rina",      "missed",   "mon 18:22"  },
    { "School",    "incoming", "sun 10:05"  },
    { "Shmuel",    "outgoing", "fri 16:40"  },
};

static menu_item_t g_hist_items[N_CALLS];

static menu_t g_hist_page = {
    .title   = "CALL HISTORY",
    .items   = g_hist_items,
    .n_items = N_CALLS,
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
    for (int i = 0; i < N_CALLS; i++) {
        g_hist_items[i].label  = g_calls[i].name;
        g_hist_items[i].sub    = g_calls[i].dir;
        g_hist_items[i].icon   = I_PHONE;
        g_hist_items[i].meta   = g_calls[i].time;
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

typedef struct {
    const char *name;
    const char *num;    /* short: fits the renderer's meta col  */
} contact_entry_t;

#define N_CONTACTS ((int)(sizeof g_contacts / sizeof g_contacts[0]))

/* Numbers are hyphen-free: the renderer's meta column is 100 px, and a
 * hyphen is a word-break opportunity for LVGL text layout — an 11-char
 * "052-1234567" wrapped to two lines. Ten digits fit one line (verified). */
static const contact_entry_t g_contacts[] = {
    { "Mom",       "0521234567" },
    { "Dad",       "0549876543" },
    { "Netanel",   "0501112223" },
    { "Work",      "026234567"  },
    { "Doctor",    "0587778889" },
    { "School",    "025301234"  },
    { "Rina",      "0534445556" },
    { "Shmuel",    "0592223334" },
};

static menu_item_t g_contact_items[N_CONTACTS];

static menu_t g_contact_page = {
    .title   = "CONTACTS",
    .items   = g_contact_items,
    .n_items = N_CONTACTS,
    .sel     = 0,
    .cats    = NULL,
    .n_cats  = 0,
    .cat_sel = 0,
    .keys    = km_calls,
};

static void fill_contacts(void)
{
    for (int i = 0; i < N_CONTACTS; i++) {
        g_contact_items[i].label  = g_contacts[i].name;
        g_contact_items[i].icon   = I_USER;
        g_contact_items[i].meta   = g_contacts[i].num;
        g_contact_items[i].target = PAGE_NONE;   /* no compose screen yet */
    }
}

menu_t *ui_calls_contacts_page(void)
{
    fill_contacts();
    return &g_contact_page;
}
