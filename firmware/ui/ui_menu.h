/*
 * ui_menu.h — the canonical menu object (ui-architecture.md §4).
 *
 * Every list-like page is an instance of menu_t: a title, an active item
 * list, an optional top category strip, and this page's contextual keymap.
 * The renderer (ui_menu.c) is generic; pages differ only in data. Adding a
 * page = fill in the blanks, not routing code.
 *
 * The MAIN MENU is one menu_t whose `cats` strip is populated: cycling the
 * category (d-pad L/R) repoints `items` to that category's list and live-
 * swaps the rows below it (LG-VX reference layout). A plain list page has
 * `cats == NULL`.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hal/hal_input.h"
#include "ui_keymap.h"

/* Forward-declare LVGL's object handle so this header stays LVGL-light; the
 * renderer (ui_menu.c) includes lvgl.h for the real definition. */
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Largest list page: the chat list's ALL slice (UI_CHAT_MAX chats + New
 * Chat = 33). Every row array in the renderer is this size. */
#define MENU_MAX_ITEMS 33
#define MENU_MAX_CATS  4

/* Where a menu item's CENTER goes. The shell maps page_id_t -> a screen. */
typedef enum {
    PAGE_NONE = 0,
    PAGE_CHATLIST,      /* unified WhatsApp surface (the chat list)     */
    PAGE_NEW_MESSAGE,   /* Messaging: compose                          */
    PAGE_CALL_HISTORY,  /* Calls                                       */
    PAGE_NEW_CALL,      /* Calls                                       */
    PAGE_CONTACTS,      /* Calls                                       */
    PAGE_MAP_SEARCH,    /* Maps                                        */
    PAGE_TRANSIT_PAY,   /* Maps                                        */
    PAGE_ROUTES,        /* Maps                                        */
    PAGE_LINES,         /* Maps                                        */
    PAGE_STATIONS,      /* Maps                                        */
    PAGE_SETTINGS,      /* Settings tree                               */
    PAGE_SPEED_DIALS,   /* Settings: speed-dial config sub-page        */
    PAGE_HELD_PRESS,    /* Settings: held-press key config sub-page    */
} page_id_t;

/* One row of a list page. `sub`, `meta` and `badge` are optional: the
 * renderer draws them only when present, so a plain one-line item (the main
 * menu) and a two-line chat row (name + preview + time + unread) are the
 * same structure. */
typedef struct {
    const char *label;   /* primary line ("Chats", a chat name)  */
    const char *sub;     /* optional second line (preview text) */
    const char *icon;    /* Font Awesome glyph string           */
    const char *meta;    /* optional right-aligned text (time)  */
    int         badge;   /* >0: unread count, drawn as [n]      */
    page_id_t   target;  /* where CENTER goes                   */
} menu_item_t;

typedef struct {
    const char *label;   /* "MESSAGING"                */
    const char *icon;    /* Font Awesome glyph string  */
} menu_cat_t;

/* How a page's item list is rendered (spec §2: the main menu's carousel and
 * grid are two renderings of the SAME menu_t data — only this differs). */
typedef enum {
    LAYOUT_LIST = 0,   /* vertical numbered rows (the default)     */
    LAYOUT_GRID       /* 3-column icon grid (classic app layout)   */
} menu_layout_t;

/* One list-like page. `items` points at the ACTIVE item list; when a category
 * strip is present, swapping categories repoints `items` to that category's
 * list (the shell does this) and calls ui_list_set_category() to refresh. */
typedef struct {
    const char        *title;
    const menu_item_t *items;   /* active item list            */
    uint8_t            n_items;
    uint8_t            sel;     /* selection within items      */
    const menu_cat_t  *cats;    /* category strip (NULL = none)*/
    uint8_t            n_cats;
    uint8_t            cat_sel; /* selected category           */
    keymap_t           keys;    /* this page's contextual keys */
    menu_layout_t      layout;  /* list rows | icon grid       */
} menu_t;

/* A rendered list page: the LVGL objects behind one menu_t. Built from a
 * small static pool (pages live for the app's lifetime). Rows are tappable:
 * a tap selects the row (touch is the universal fallback; keypad first).
 * The row area scrolls when the list is longer than the screen holds.
 *
 * Pointer swipes (spec §1): a vertical drag on a long list scrolls the row
 * area natively (LVGL scroll target); a horizontal swipe is detected by the
 * shell from the HAL touch points and fires this page's optional gesture
 * callback, so category strips can be cycled by dragging — the touch
 * equivalent of d-pad L/R. */
typedef struct ui_list_page list_page_t;

/* Horizontal-swipe handler for a page (set per page). Called with dir = +1
 * for a swipe to the left (next), -1 for a swipe to the right (previous).
 * Vertical swipes scroll natively and never reach it. */
typedef void (*ui_list_gesture_cb)(list_page_t *p, int dir);

/* Paint one list-style row's base look — banded fill, green selection with
 * its bright edge, flat corners — the ONE shared "paint a row" helper: the
 * renderer uses it for every list row, and any overlay that draws list-
 * style rows (the options sidebar) must use it too, so the two can never
 * drift apart. Callers add their own text/icon styling on top. */
void ui_row_style(lv_obj_t *row, int idx, bool sel);

/* Build a rendered page under `parent` from `m`. The caller owns m (static
 * data); the page borrows its pointers. Returns NULL if the pool is full —
 * LIST_POOL in ui_menu.c bounds how many list pages the app can hold, so a
 * new page also needs that constant raised (the shell checks for NULL). */
list_page_t *ui_list_build(lv_obj_t *parent, menu_t *m);

/* Set the active category: restyles the strip and rebuilds the rows from the
 * current `items`. The shell must have repointed m->items / n_items / sel to
 * that category's list first. No-op when the page has no category strip. */
void ui_list_set_category(list_page_t *p, uint8_t cat);

/* Move / set selection within the active item list (clamped). */
void ui_list_move_sel(list_page_t *p, int step);
void ui_list_select(list_page_t *p, int idx);

/* Rebuild the rows from the page's current `items` after the data changed in
 * place (e.g. a settings value flipped). The selection is kept. */
void ui_list_refresh(list_page_t *p);

/* The page id the selected item opens (PAGE_NONE if none). */
page_id_t ui_list_selected_target(const list_page_t *p);

/* Accessors. */
uint8_t ui_list_sel(const list_page_t *p);
uint8_t ui_list_cat(const list_page_t *p);

/* The page's root LVGL object (the shell shows/hides it as a screen). */
lv_obj_t *ui_list_root(const list_page_t *p);

/* Set the page's horizontal-swipe handler (NULL = no swipe handling). The
 * shell wires it for pages with a category strip. */
void ui_list_set_gesture_cb(list_page_t *p, ui_list_gesture_cb cb);

/* The page's horizontal-swipe handler (NULL if none). */
ui_list_gesture_cb ui_list_page_gesture_cb(const list_page_t *p);

#ifdef __cplusplus
}
#endif
