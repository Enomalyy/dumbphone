/*
 * ui_sidebar.h — the options sidebar overlay (spec §1: SHOULDER-L).
 *
 * A flat panel over the left part of the content area showing only the
 * options relevant to the CURRENT page (data from ui_options.c). This module
 * owns the panel widget, its static row pool, the selection state, and its
 * row styling (through the shared ui_row_style()). It does NOT execute
 * options or decide key meanings: the shell routes keys and runs the
 * selected option's action, because both are navigation.
 */
#pragma once

#include <stdbool.h>

#include "lvgl.h"

#include "ui_keymap.h"    /* keymap_t        */
#include "ui_shell.h"     /* ui_screen_id_t  */

#ifdef __cplusplus
extern "C" {
#endif

/* Create the panel under `parent` (the root screen), hidden. Built last so
 * it draws above the content screens. UI thread. */
void ui_sidebar_build(lv_obj_t *parent);

/* Populate the rows from `screen`'s option table and show the panel. */
void ui_sidebar_open_for(ui_screen_id_t screen);

/* Hide the panel (idempotent). */
void ui_sidebar_close(void);

/* Whether the panel is currently open — swipes and page keys defer to it. */
bool ui_sidebar_is_open(void);

/* Move the selection by `step`, clamped to the table (no wrap). */
void ui_sidebar_move(int step);

/* Index of the selected option (always valid while open: 0 when empty). */
int ui_sidebar_sel(void);

/* Number of options in the open table (0 when closed). */
int ui_sidebar_count(void);

/* The sidebar's own contextual keymap while open: SHOULDER-L re-press and
 * SHOULDER-R back both close it WITHOUT popping the page behind; END closes
 * and goes home; CENTER executes the selected option; UP/DOWN move. */
const keymap_t *ui_sidebar_keymap(void);

/* Install the row-activation handler: called with the tapped/activated
 * option index. The shell supplies it because executing an option's action
 * is navigation (ui_shell.c), not panel state. Tap executes on RELEASE, and
 * a drag that started on a row does not execute it. */
void ui_sidebar_set_activate_cb(void (*cb)(int idx));

#ifdef __cplusplus
}
#endif
