/*
 * ui_home.h — HOME screen (spec §2): wallpaper + honest widgets.
 *
 * HOME is its own screen, not a list page: a flat pixel-art background
 * motif, one row of working widgets fed by data the shell already has
 * (date from the status-bar clock, unread total from the chat source,
 * battery from the shell's single value), and its own keymap — CENTER
 * opens the MAIN MENU, T9 1-9 are speed dials, CALL jumps to Calls.
 *
 * Owns: the home root object + widgets, its keymap. Exposes:
 * ui_home_build / ui_home_root / ui_home_keymap / ui_home_set_date (this
 * header). Must not touch: the navigation stack, the status/softkey bars,
 * any other screen.
 */
#pragma once

#include "lvgl.h"
#include "ui_keymap.h"

/* Build HOME under `parent` (the shell's content area). Returns the root
 * object; NULL on failure. Call AFTER ui_messaging_build() — the unread
 * widget reads the chat source's total. */
lv_obj_t *ui_home_build(lv_obj_t *parent);

/* The home root object (NULL until built). */
lv_obj_t *ui_home_root(void);

/* The contextual keymap for this screen. */
const keymap_t *ui_home_keymap(void);

/* Appearance apply pass: re-wear the current palette + faces. UI thread. */
void ui_home_restyle(void);

/* Re-layout after the content area changed height (softkey bar toggle). */
void ui_home_relayout(void);

/* Update the DATE widget (the shell's clock tick feeds it; same format as
 * the status bar / lock screen date). */
void ui_home_set_date(const char *date);
