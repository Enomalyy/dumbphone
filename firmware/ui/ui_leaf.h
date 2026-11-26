/*
 * ui_leaf.h — the bespoke leaf screens: LOCKED and the stub page.
 *
 * Two pages that are NOT list pages, so they share this one module: the lock
 * screen (spec §1: flat readout — lock glyph, large live clock, date line,
 * unlock hint) and the stub leaf ("title + coming soon") that stands in for
 * pages whose real content arrives in a later leg.
 *
 * This module owns both screens' widgets and their contextual keymaps. It
 * does not decide navigation: unlocking and opening the stub page are shell
 * decisions, so the shell pushes screens and seeds the text.
 */
#pragma once

#include "lvgl.h"

#include "ui_keymap.h"   /* keymap_t  */
#include "ui_menu.h"     /* page_id_t */

#ifdef __cplusplus
extern "C" {
#endif

/* LOCKED (spec §1) — built under `parent` (the content area), hidden. */
void ui_leaf_build_lock(lv_obj_t *parent);

/* Appearance apply pass: re-wear the palette + faces on both leaf screens.
 * UI thread. */
void ui_leaf_restyle(void);

/* Re-layout after the content area changed height (softkey bar toggle). */
void ui_leaf_relayout(void);

/* The lock screen's root object (the shell shows/hides it). */
lv_obj_t *ui_leaf_lock_root(void);

/* The lock screen's shared live clock: the shell's 1-second tick writes the
 * same time it puts in the status bar, so the two never disagree. Either
 * argument may be NULL (only the non-NULL one updates). */
void ui_leaf_set_lock_time(const char *hhmm, const char *date);

/* LOCKED (spec §1): the keypad unlocks — any T9 key; every other control
 * (d-pad, shoulders, CALL, END, CENTER) is inert while locked. */
const keymap_t *ui_leaf_lock_keymap(void);

/* The stub leaf page — built under `parent` (the content area), hidden. */
void ui_leaf_build_stub(lv_obj_t *parent);

/* The stub page's root object (the shell shows/hides it). */
lv_obj_t *ui_leaf_stub_root(void);

/* Set the stub page's title. The caller pushes the screen (navigation is
 * the shell's). */
void ui_leaf_set_stub_title(const char *title);

/* The title a stub stands in for: the page's real name when it has one
 * (so the stub reads "Routes", not a generic line), else "Coming Soon". */
const char *ui_leaf_stub_title_for(page_id_t id);

/* Stub leaf page: options with shoulder-L, back with shoulder-R, home with
 * END; nothing else. */
const keymap_t *ui_leaf_stub_keymap(void);

#ifdef __cplusplus
}
#endif
