/*
 * ui_pages.h — the page registry (ui-architecture.md §16.3, issue #8).
 *
 * One row per page: the harness --screen name, both id enums, and the
 * callbacks the shell needs to show / keymap / activate / move / swipe /
 * option a page. The table itself (ui_pages[] in ui_shell.c) is the one
 * place that knows the app's page set — every dispatch in the shell is a
 * loop over it, and the renderer's list pool is sized from its length, so
 * a page not in the table cannot be reached and the pool cannot be
 * under-counted.
 */
#pragma once

#include "ui_keymap.h"  /* keymap_t */
#include "ui_menu.h"    /* page_id_t, lv_obj_t (forward) */
#include "ui_options.h" /* opt_entry_t */
#include "ui_shell.h"   /* ui_screen_id_t */

typedef struct {
    const char *name;                       /* harness --screen name        */
    page_id_t  id;                          /* target that opens it, or NONE */
    ui_screen_id_t screen;                  /* the screen on display        */
    lv_obj_t *(*build)(void);              /* build under the content area */
    const keymap_t *(*keymap)(void);        /* contextual keys for this page */
    void (*activate)(void);                 /* CENTER / tap semantics (NULL: none) */
    void (*cycle)(int dir);                 /* category-strip cycling (NULL: none) */
    const opt_entry_t *(*options)(int *n);  /* SHOULDER-L sidebar rows      */
    void (*on_show)(void);                  /* extra work when the page is shown (NULL: none) */
} ui_page_t;

extern const ui_page_t ui_pages[];

/* The table's length. UI_PAGE_COUNT is the compile-time constant usable as
 * an array bound in any TU (C forbids an extern const as a bound);
 * ui_page_count is the same value at runtime. A static assert in ui_shell.c
 * keeps the two in lockstep — adding a page is one row plus bumping this
 * number, and forgetting either half fails the build. */
#define UI_PAGE_COUNT 12
extern const int ui_page_count;             /* = ARRAY_LEN(ui_pages)       */

/* The row for a screen id (NULL if not a page). */
const ui_page_t *ui_page_for_screen(ui_screen_id_t screen);
/* The row whose target opens it (NULL: stub territory, see open_stub()). */
const ui_page_t *ui_page_for_target(page_id_t id);
/* The row with this harness --screen name (NULL if unknown). */
const ui_page_t *ui_page_by_name(const char *name);
