/*
 * ui_home.h — the HOME screen: the unified WhatsApp chat list (spec §2).
 *
 * HOME is one canonical menu_t with a populated category strip
 * (ALL · FAMILY · UNREAD · FAVORITES · ARCHIVED, cycled with d-pad L/R).
 * This module owns the chat data adapter: it resolves the target's chat
 * source (chat_model.h) into per-category item slices and points the page's
 * `items` at the active slice. The renderer (ui_menu.c) draws it; the shell
 * (ui_shell.c) routes its keys through ui_home_keymap().
 *
 * Data + one build call: no navigation state, no key handling here.
 */
#pragma once

#include "ui_menu.h"     /* menu_t, list_page_t */
#include "ui_keymap.h"   /* keymap_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Build the HOME chat-list page under `parent` (UI thread). Returns the
 * rendered page (NULL only if the renderer's pool is full — the caller
 * checks, as it does for every list page). */
list_page_t *ui_home_build(lv_obj_t *parent);

/* The built page (NULL before ui_home_build). Navigation code moves its
 * selection / reads its target through the ui_list_* accessors. */
list_page_t *ui_home_page(void);

/* This page's contextual keymap: d-pad L/R cycle the category strip, T9
 * 1..9 are speed dials, CENTER opens the main menu, END = home. */
const keymap_t *ui_home_keymap(void);

/* Cycle the category strip by `dir` (+1 = next): repoint the page's item
 * slice and refresh (the same mechanism as the main-menu carousel). */
void ui_home_cycle_cat(int dir);

#ifdef __cplusplus
}
#endif
