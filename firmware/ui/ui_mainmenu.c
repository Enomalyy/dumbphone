/*
 * ui_mainmenu.c — main menu: the category carousel (spec §2).
 * (layout: classic SMS menu / gen-09; language: concept 07)
 *
 * Owns: the category/item tables, the page's menu_t + its contextual
 * keymap, category cycling, and the settings-driven layout sync. Exposes:
 * ui_mainmenu_build / ui_mainmenu_keymap / ui_mainmenu_cycle_cat /
 * ui_mainmenu_sync_layout (ui_mainmenu.h). Must not touch: navigation
 * state, the screen stack, key routing — those live in ui_shell.c;
 * appearance in ui_theme.h + ui_menu.c.
 */

#include <stdint.h>

#include "lvgl.h"

#include "ui_icons.h"
#include "ui_keymap.h"
#include "ui_menu.h"
#include "ui_settings.h"   /* menu style: carousel vs grid */
#include "ui_mainmenu.h"

/* The page this module owns. */
static list_page_t *g_menu;

/* Main menu: d-pad L/R cycles the category strip, up/down moves the item
 * selection, center opens the selected item, shoulder-R goes back. */
static const keymap_t km_menu = {
    .shoulder_l = ACT_OPEN_OPTIONS, /* options sidebar (spec §1)     */
    .shoulder_r = ACT_GO_BACK,     /* back to the previous page       */
    .center     = ACT_ACTIVATE,
    .call       = ACT_NOOP,
    .end        = ACT_GOTO_HOME,
    .up         = ACT_MOVE_SEL_UP,
    .down       = ACT_MOVE_SEL_DOWN,
    .left       = ACT_CYCLE_CAT_PREV,   /* previous category           */
    .right      = ACT_CYCLE_CAT_NEXT,   /* next category               */
    .t9         = { ACT_NOOP },
};

/* Category strip: icon + label per category. Messaging is the default. */
static const menu_cat_t g_cats[] = {
    { "MESSAGING", I_COMMENT },
    { "CALLS",     I_PHONE   },
    { "MAPS",      I_MAP_PIN },
    { "SETTINGS",  I_GEAR    },
};

/* Per-category item lists. Chats leads Messaging (spec: chats first). */
static const menu_item_t cat_messaging[] = {
    { .label = "Chats",         .icon = I_COMMENT,  .target = PAGE_CHATLIST    },
    { .label = "New Message",   .icon = I_ENVELOPE, .target = PAGE_NEW_MESSAGE },
    { .label = "Others",        .icon = I_ELLIPSIS, .target = PAGE_NONE        },
};

static const menu_item_t cat_calls[] = {
    { .label = "Call History",  .icon = I_PHONE,    .target = PAGE_CALL_HISTORY },
    { .label = "New Call",      .icon = I_BOLT,     .target = PAGE_NEW_CALL     },
    { .label = "Contacts",      .icon = I_USERS,    .target = PAGE_CONTACTS     },
};

static const menu_item_t cat_maps[] = {
    { .label = "Search",           .icon = I_MAP_PIN,  .target = PAGE_MAP_SEARCH  },
    { .label = "Transit Payments", .icon = I_BOLT,     .target = PAGE_TRANSIT_PAY },
    { .label = "Routes",           .icon = I_CARET_R,  .target = PAGE_ROUTES      },
    { .label = "Lines",            .icon = I_SIGNAL,   .target = PAGE_LINES       },
    { .label = "Stations",         .icon = I_JOURNAL,  .target = PAGE_STATIONS    },
};

static const menu_item_t cat_settings[] = {
    { .label = "Menu Style",      .icon = I_GEAR,   .target = PAGE_SETTINGS },
    { .label = "Call Preference", .icon = I_PHONE,  .target = PAGE_SETTINGS },
    { .label = "Speed Dials",     .icon = I_USER,   .target = PAGE_SETTINGS },
    { .label = "Held Presses",    .icon = I_BOLT,   .target = PAGE_SETTINGS },
};

static const struct {
    const menu_item_t *items;
    int n;
} g_cat_items[] = {
    { cat_messaging,  3 },
    { cat_calls,      3 },
    { cat_maps,       5 },
    { cat_settings,   4 },
};

/* The main menu is one menu_t: a populated category strip whose item list
 * live-swaps as the category cycles. */
static menu_t g_menu_m = {
    .title    = "MENU",
    .items    = cat_messaging,
    .n_items  = 3,
    .sel      = 0,
    .cats     = g_cats,
    .n_cats   = 4,
    .cat_sel  = 0,
    .keys     = km_menu,
};

/* Point the menu at category `cat`: repoint its item list, reset selection,
 * then refresh the strip + rows. */
static void menu_set_cat(int cat)
{
    if (cat < 0) {
        cat = 0;
    }
    if (cat >= g_menu_m.n_cats) {
        cat = g_menu_m.n_cats - 1;
    }
    g_menu_m.items   = g_cat_items[cat].items;
    g_menu_m.n_items = (uint8_t)g_cat_items[cat].n;
    g_menu_m.sel     = 0;
    ui_list_set_category(g_menu, (uint8_t)cat);
}

static void menu_cycle_cat(int dir)
{
    int c = (g_menu_m.cat_sel + dir + g_menu_m.n_cats) % g_menu_m.n_cats;
    menu_set_cat(c);
}

/* Horizontal swipe on the row area cycles the category strip (spec §1):
 * swipe left = next, swipe right = previous — the d-pad L/R equivalents. */
static void menu_gesture(list_page_t *p, int dir)
{
    (void)p;
    menu_cycle_cat(dir);
}

list_page_t *ui_mainmenu_build(lv_obj_t *parent)
{
    /* The main menu renders in whatever layout the settings say (spec §2:
     * carousel/grid are two renderings of the same menu_t data). */
    g_menu_m.layout = ui_settings_menu_style() == MENU_STYLE_GRID ? LAYOUT_GRID
                                                                  : LAYOUT_LIST;
    g_menu   = ui_list_build(parent, &g_menu_m);
    ui_list_set_gesture_cb(g_menu, menu_gesture);   /* swipes cycle cats */
    return g_menu;
}

const keymap_t *ui_mainmenu_keymap(void)
{
    return &km_menu;
}

void ui_mainmenu_cycle_cat(int dir)
{
    menu_cycle_cat(dir);
}

/* Re-apply the settings' menu style to the main menu. The toggle lives on
 * the Settings page, so the layout is synced each time the menu is shown:
 * carousel/grid are two renderings of the same menu_t data (spec §2). */
void ui_mainmenu_sync_layout(void)
{
    if (!g_menu) {
        return;
    }
    menu_layout_t want = ui_settings_menu_style() == MENU_STYLE_GRID ? LAYOUT_GRID
                                                                     : LAYOUT_LIST;
    if (g_menu_m.layout != want) {
        g_menu_m.layout = want;
        ui_list_refresh(g_menu);   /* rebuild the rows in the new layout */
    }
}
