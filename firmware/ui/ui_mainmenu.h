/*
 * ui_mainmenu.h — the main menu: the category carousel (spec §2).
 *
 * The main menu is one canonical menu_t: a populated category strip
 * (MESSAGING · CALLS · MAPS · SETTINGS) whose item list live-swaps as the
 * category cycles, rendered as carousel or grid per the settings' menu
 * style. This module owns the category/item tables, the page's menu_t +
 * its contextual keymap, and the cycling/layout sync. The renderer
 * (ui_menu.c) draws it; the shell (ui_shell.c) routes its keys.
 *
 * Data + one build call: no navigation state, no key handling here.
 */
#pragma once

#include "ui_menu.h"     /* menu_t, list_page_t */
#include "ui_keymap.h"   /* keymap_t */

/* The SETTINGS category's index in the strip (MESSAGING · CALLS · MAPS ·
 * SETTINGS) — the sidebar's settings entry lands here (spec §14.13). */
#define MAINMENU_CAT_SETTINGS 3

#ifdef __cplusplus
extern "C" {
#endif

/* Build the main-menu page under `parent` (UI thread). It renders in
 * whatever layout the settings say — carousel/grid are two renderings of
 * the same menu_t data. Returns the rendered page (NULL only if the
 * renderer's pool is full — the caller checks, as it does for every list
 * page). */
list_page_t *ui_mainmenu_build(lv_obj_t *parent);

/* This page's contextual keymap: d-pad L/R cycle the category strip,
 * up/down moves the selection, CENTER opens the selected item. */
const keymap_t *ui_mainmenu_keymap(void);

/* Cycle the category strip by `dir` (+1 = next): repoint the page's item
 * list and refresh (the same mechanism a swipe uses). */
void ui_mainmenu_cycle_cat(int dir);

/* Select category `cat` directly: the tray-cell tap target (spec §14.1).
 * Lands in the same setter the d-pad cycle drives — one behaviour. */
void ui_mainmenu_select_cat(int cat);

/* The settings row (a setting_id_t) the selected item stands for, or -1
 * when the menu is not on its SETTINGS category. */
int ui_mainmenu_selected_setting(void);

/* Repoint the SETTINGS category's value metas at their live strings (the
 * Call Preference row). Called on build, on show, and after a toggle. */
void ui_mainmenu_refresh_meta(void);

/* Re-apply the settings' menu style to the main menu. The toggle lives on
 * the Settings page, so the layout is synced each time the menu is shown
 * or an option changes it (spec §2). */
void ui_mainmenu_sync_layout(void);

#ifdef __cplusplus
}
#endif
