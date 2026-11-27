/*
 * ui_messaging.c — Messaging: the unified WhatsApp chat list (spec §2).
 *
 * The chat list is a PAGE reached from the main menu's MESSAGING category —
 * not HOME. Owns: the chat data adapter (the target's chat source resolved
 * into per-category item slices), the page's menu_t + its contextual keymap,
 * and the category cycling. Exposes: ui_messaging_build / ui_messaging_page /
 * ui_messaging_keymap / ui_messaging_cycle_cat / ui_messaging_unread_total
 * (ui_messaging.h). Must not touch: navigation state, the screen stack, key
 * routing — those live in ui_shell.c; appearance in ui_theme.h + ui_menu.c.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#include "chat_model.h"
#include "ui_icons.h"
#include "ui_keymap.h"
#include "ui_menu.h"
#include "ui_messaging.h"

#define CHAT_N_CATS     5
#define CHAT_POOL_CAT   (UI_CHAT_MAX + 1)   /* per-category slice size */

static const menu_cat_t g_chat_cats[] = {
    { "ALL",       I_INBOX   },
    { "FAMILY",    I_USERS   },
    { "UNREAD",    I_BOLT    },
    { "FAVORITES", I_HEART   },
    { "ARCHIVED",  I_ARCHIVE },
};

/* Shared row text store: one entry per chat, shared by every category
 * slice that contains it (no duplicated strings). The last three slots
 * hold the special rows (No chats / New Chat / Archived). */
typedef struct {
    char name[64];
    char prev[136];
    char time[8];
} chat_text_t;

static chat_text_t g_chat_text[UI_CHAT_MAX + 3];
static menu_item_t g_chat_items[CHAT_N_CATS * CHAT_POOL_CAT];

static int chat_cat_off[CHAT_N_CATS];   /* pool offset per category  */
static int chat_cat_n[CHAT_N_CATS];     /* item count per category   */

/* The page this module owns. */
static list_page_t *g_chat;
static int g_unread_total = 0;   /* sum of unread, filled at build time */

/* Messaging is a LIST page (spec §2): d-pad L/R cycles its category strip,
 * up/down moves the row selection, CENTER activates the selected row
 * (opening a conversation: later leg). No call/speed-dial actions here —
 * those live on HOME. */
static const keymap_t km_messaging = {
    .shoulder_l = ACT_OPEN_OPTIONS, /* options sidebar (spec §1)     */
    .shoulder_r = ACT_GO_BACK,     /* back to the menu — Messaging is a
                                    * page under the menu, not a root;
                                    * only HOME is root (Eli, 26/11)  */
    .center     = ACT_ACTIVATE,    /* open the selected chat          */
    .call       = ACT_NOOP,        /* CALL lives on HOME              */
    .end        = ACT_GOTO_HOME,   /* END returns to HOME from any page */
    .up         = ACT_MOVE_SEL_UP,
    .down       = ACT_MOVE_SEL_DOWN,
    .left       = ACT_CYCLE_CAT_PREV,   /* previous chat category     */
    .right      = ACT_CYCLE_CAT_NEXT,   /* next chat category         */
    .t9         = { ACT_NOOP, ACT_NOOP, ACT_NOOP, ACT_NOOP, ACT_NOOP,
                    ACT_NOOP, ACT_NOOP, ACT_NOOP, ACT_NOOP,
                    ACT_NOOP, ACT_NOOP, ACT_NOOP },
};   /* T9 is unbound on this page. */

/* The chat list page: strip populated, items repointed on cycle. */
static menu_t g_chat_m = {
    .title   = "CHATS",
    .items   = NULL,      /* set by build_chat_items()            */
    .n_items = 0,
    .sel     = 0,
    .cats    = g_chat_cats,
    .n_cats  = CHAT_N_CATS,
    .cat_sel = 0,
    .keys    = km_messaging,
};

/* Append one row to category `cat`'s slice, pointing at shared text slot
 * `tidx`. A full slice drops the overflow (the source is capped anyway). */
static void add_chat_item(int cat, const ui_chat_t *c, int tidx)
{
    if (chat_cat_n[cat] >= CHAT_POOL_CAT) {
        return;
    }
    int i = chat_cat_off[cat] + chat_cat_n[cat]++;
    menu_item_t *it = &g_chat_items[i];
    it->label  = g_chat_text[tidx].name;
    it->sub    = g_chat_text[tidx].prev;
    it->icon   = c ? (c->group ? I_USERS : I_USER) : I_ENVELOPE;
    it->meta   = g_chat_text[tidx].time;
    it->badge  = c ? (int)c->unread : 0;
    it->target = PAGE_NONE;   /* opening a conversation: later leg */
}

/* Resolve the chat source into per-category item slices. ALL gets every
 * chat plus a trailing New Chat row; ARCHIVED is one summary row. */
static void build_chat_items(void)
{
    memset(g_chat_items, 0, sizeof g_chat_items);
    for (int c = 0; c < CHAT_N_CATS; c++) {
        chat_cat_off[c] = 0;
        chat_cat_n[c] = 0;
    }
    g_unread_total = 0;

    ui_chat_list_t list;
    if (ui_chat_source_load(&list) != 0) {
        /* No data: one honest row in ALL instead of invented chats. */
        snprintf(g_chat_text[UI_CHAT_MAX].name, sizeof g_chat_text[0].name,
                 "No chats");
        snprintf(g_chat_text[UI_CHAT_MAX].prev, sizeof g_chat_text[0].prev,
                 "source unavailable");
        add_chat_item(0, NULL, UI_CHAT_MAX);
        g_chat_items[chat_cat_off[0]].icon = I_INBOX;
    } else {
        /* Pass 1: count rows per category (capped at the slice size). */
        for (int i = 0; i < list.count; i++) {
            const ui_chat_t *c = &list.chats[i];
            if (chat_cat_n[0] < CHAT_POOL_CAT) chat_cat_n[0]++;   /* ALL    */
            if (c->family && chat_cat_n[1] < CHAT_POOL_CAT) chat_cat_n[1]++;
            if (c->unread && chat_cat_n[2] < CHAT_POOL_CAT) chat_cat_n[2]++;
            if (c->favorite && chat_cat_n[3] < CHAT_POOL_CAT) chat_cat_n[3]++;
        }
        if (chat_cat_n[0] < CHAT_POOL_CAT) chat_cat_n[0] += 1;    /* New Chat */
        int has_archived = (list.archived_count > 0);             /* ARCHIVED */
        chat_cat_n[4] = has_archived ? 1 : 0;

        /* Pass 2: fix the offsets, then fill. add_chat_item() advances
         * chat_cat_n as it writes, so zero the counters first — pass 1's
         * counts have served their purpose (the offsets are now frozen). */
        int off = 0;
        for (int c = 0; c < CHAT_N_CATS; c++) {
            chat_cat_off[c] = off;
            off += chat_cat_n[c];
        }
        memset(chat_cat_n, 0, sizeof chat_cat_n);
        for (int i = 0; i < list.count; i++) {
            const ui_chat_t *c = &list.chats[i];
            snprintf(g_chat_text[i].name, sizeof g_chat_text[i].name,
                     "%s", c->name);
            if (c->mine && c->last[0]) {
                snprintf(g_chat_text[i].prev, sizeof g_chat_text[i].prev,
                         "You: %s", c->last);
            } else {
                snprintf(g_chat_text[i].prev, sizeof g_chat_text[i].prev,
                         "%s", c->last);
            }
            snprintf(g_chat_text[i].time, sizeof g_chat_text[i].time,
                     "%s", c->time);
            add_chat_item(0, c, i);                       /* ALL              */
            if (c->family)   add_chat_item(1, c, i);      /* FAMILY           */
            if (c->unread)   add_chat_item(2, c, i);      /* UNREAD           */
            if (c->favorite) add_chat_item(3, c, i);      /* FAVORITES        */
            g_unread_total += c->unread;                  /* for HOME widget  */
        }

        /* Trailing New Chat row in ALL. */
        snprintf(g_chat_text[UI_CHAT_MAX + 1].name,
                 sizeof g_chat_text[0].name, "New Chat");
        add_chat_item(0, NULL, UI_CHAT_MAX + 1);

        /* ARCHIVED: one summary row. (has_archived — chat_cat_n was
         * zeroed for the fill pass above.) */
        if (has_archived) {
            snprintf(g_chat_text[UI_CHAT_MAX + 2].name,
                     sizeof g_chat_text[0].name, "Archived");
            snprintf(g_chat_text[UI_CHAT_MAX + 2].prev,
                     sizeof g_chat_text[0].prev, "%d chats",
                     list.archived_count);
            add_chat_item(4, NULL, UI_CHAT_MAX + 2);
            g_chat_items[chat_cat_off[4]].icon = I_ARCHIVE;
        }
    }

    /* Point the page at ALL (the default category) before it is built. */
    g_chat_m.items   = &g_chat_items[chat_cat_off[0]];
    g_chat_m.n_items = (uint8_t)chat_cat_n[0];
    g_chat_m.sel     = 0;
}

/* Point the chat list at category `cat`: repoint its item slice, reset the
 * selection, then refresh the strip + rows (same as the main menu). */
static void chat_set_cat(int cat)
{
    if (cat < 0) {
        cat = 0;
    }
    if (cat >= CHAT_N_CATS) {
        cat = CHAT_N_CATS - 1;
    }
    g_chat_m.items   = &g_chat_items[chat_cat_off[cat]];
    g_chat_m.n_items = (uint8_t)chat_cat_n[cat];
    g_chat_m.sel     = 0;
    ui_list_set_category(g_chat, (uint8_t)cat);
}

static void chat_cycle_cat(int dir)
{
    int c = (g_chat_m.cat_sel + dir + CHAT_N_CATS) % CHAT_N_CATS;
    chat_set_cat(c);
}

/* Horizontal swipe on the row area cycles the category strip (spec §1):
 * swipe left = next, swipe right = previous — the d-pad L/R equivalents. */
static void chat_gesture(list_page_t *p, int dir)
{
    (void)p;
    chat_cycle_cat(dir);
}

/* The strip's tap callback (spec §14.1): the tapped cell's index goes to the
 * same setter the d-pad cycle drives — one behaviour, no second path. */
static void chat_cat_tap(list_page_t *p, int idx)
{
    (void)p;
    chat_set_cat(idx);
}

list_page_t *ui_messaging_build(lv_obj_t *parent)
{
    build_chat_items();   /* resolve the chat source into category slices */
    g_chat = ui_list_build(parent, &g_chat_m);
    ui_list_set_gesture_cb(g_chat, chat_gesture);   /* swipes cycle cats */
    ui_list_set_cat_cb(g_chat, chat_cat_tap);       /* tappable strip (§14.1) */
    return g_chat;
}

list_page_t *ui_messaging_page(void)
{
    return g_chat;
}

const keymap_t *ui_messaging_keymap(void)
{
    return &km_messaging;
}

void ui_messaging_cycle_cat(int dir)
{
    chat_cycle_cat(dir);
}

int ui_messaging_unread_total(void)
{
    return g_unread_total;
}
