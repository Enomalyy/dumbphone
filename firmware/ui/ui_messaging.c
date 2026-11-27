/*
 * ui_messaging.c — Messaging: the unified WhatsApp chat list (spec §2).
 *
 * The chat list is a PAGE reached from the main menu's MESSAGING category —
 * not HOME. Owns: the page's menu_t + its contextual keymap, the category
 * cycling, and the row PRESENTATION (text store, icons, "You:" prefixes).
 * The data itself lives in the model layer (model/chat_model.c, spec
 * §16.4): the per-category slice builder resolves the target's chat source
 * into rows; this page maps each row to its menu_item_t. Exposes:
 * ui_messaging_build / ui_messaging_page / ui_messaging_keymap /
 * ui_messaging_cycle_cat / ui_messaging_unread_total (ui_messaging.h). Must
 * not touch: navigation state, the screen stack, key routing — those live in
 * ui_shell.c; appearance in ui_theme.h + ui_menu.c.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#include "model/chat_model.h"
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

/* The model chat index behind each row of each category (-1 = synthetic
 * row: New Chat / Archived / no data). Filled alongside add_chat_item(). */
static int g_row_chat[CHAT_N_CATS][CHAT_POOL_CAT];

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
    it->target = PAGE_NONE;   /* activation goes through the page's callback */
    g_row_chat[cat][i - chat_cat_off[cat]] = (c && tidx < UI_CHAT_MAX) ? tidx : -1;
}

/* Resolve the chat source into per-category item slices. The MODEL owns the
 * slicing (model_chats_slice, spec §16.4): ALL gets every chat plus a
 * trailing New Chat row; ARCHIVED is one summary row; no data is one honest
 * row. This page maps each row to its menu_item_t — the text store, icons
 * and "You:" prefixes are presentation and stay here. */
static void build_chat_items(void)
{
    memset(g_chat_items, 0, sizeof g_chat_items);
    for (int c = 0; c < CHAT_N_CATS; c++) {
        chat_cat_off[c] = 0;
        chat_cat_n[c] = 0;
    }

    ui_chat_list_t list;
    const ui_chat_list_t *have = NULL;
    if (ui_chat_source_load(&list) == 0) {
        have = &list;
    }
    g_unread_total = model_chats_unread_total(have);   /* for HOME widget */

    /* Categories are filled in order, each at the running offset — the
     * slices must not overlap in the shared item pool. */
    int off = 0;
    for (int c = 0; c < CHAT_N_CATS; c++) {
        model_chat_row_t rows[CHAT_POOL_CAT];
        int n = model_chats_slice(have, (model_chat_cat_t)c, rows,
                                  CHAT_POOL_CAT);
        chat_cat_off[c] = off;
        off += n;
        for (int i = 0; i < n; i++) {
            switch (rows[i].kind) {
            case MODEL_CHAT_ROW_CHAT: {
                const ui_chat_t *ch = rows[i].chat;
                int tidx = (int)(ch - list.chats);   /* shared text slot */
                snprintf(g_chat_text[tidx].name, sizeof g_chat_text[0].name,
                         "%s", ch->name);
                if (ch->mine && ch->last[0]) {
                    snprintf(g_chat_text[tidx].prev,
                             sizeof g_chat_text[0].prev, "You: %s", ch->last);
                } else {
                    snprintf(g_chat_text[tidx].prev,
                             sizeof g_chat_text[0].prev, "%s", ch->last);
                }
                snprintf(g_chat_text[tidx].time, sizeof g_chat_text[0].time,
                         "%s", ch->time);
                add_chat_item(c, ch, tidx);
                break;
            }
            case MODEL_CHAT_ROW_NEW_CHAT:
                snprintf(g_chat_text[UI_CHAT_MAX + 1].name,
                         sizeof g_chat_text[0].name, "New Chat");
                add_chat_item(c, NULL, UI_CHAT_MAX + 1);
                break;
            case MODEL_CHAT_ROW_ARCHIVED:
                snprintf(g_chat_text[UI_CHAT_MAX + 2].name,
                         sizeof g_chat_text[0].name, "Archived");
                snprintf(g_chat_text[UI_CHAT_MAX + 2].prev,
                         sizeof g_chat_text[0].prev, "%d chats",
                         rows[i].archived_count);
                add_chat_item(c, NULL, UI_CHAT_MAX + 2);
                g_chat_items[chat_cat_off[c] + chat_cat_n[c] - 1].icon = I_ARCHIVE;
                break;
            case MODEL_CHAT_ROW_NO_DATA:
            default:
                snprintf(g_chat_text[UI_CHAT_MAX].name,
                         sizeof g_chat_text[0].name, "No chats");
                snprintf(g_chat_text[UI_CHAT_MAX].prev,
                         sizeof g_chat_text[0].prev, "source unavailable");
                add_chat_item(c, NULL, UI_CHAT_MAX);
                g_chat_items[chat_cat_off[c] + chat_cat_n[c] - 1].icon = I_INBOX;
                break;
            }
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

int ui_messaging_selected_chat(void)
{
    if (!g_chat) {
        return -1;
    }
    int cat = ui_list_cat(g_chat);
    int row = ui_list_sel(g_chat);
    if (cat >= CHAT_N_CATS || row >= CHAT_POOL_CAT) {
        return -1;
    }
    return g_row_chat[cat][row];
}
