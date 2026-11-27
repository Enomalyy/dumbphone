/*
 * ui_chat_view.h — the CONVERSATION page (spec §14.7).
 *
 * One screen, reached by activating a chat row: the chat's messages as
 * incoming/outgoing bubbles over a scrollable column. The page owns its
 * own LVGL objects (built once at boot like every other page) and is
 * registered in the one-row ui_pages[] table (spec §16.3).
 *
 * Bubbles are NOT menu_t rows: variable height, side alignment, grouping
 * and dividers do not fit the fixed label/sub/meta/badge slots, so this
 * module renders its own bubble chain inside a native LVGL scroll target
 * (the shared 10 px drag dead zone applies, spec §14.4). Every data string
 * goes through ui_text_set() — Hebrew/RTL and emoji ride the existing
 * direction rule and font fallback chain; there is no second text path.
 */
#pragma once

#include "lvgl.h"
#include "ui_keymap.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the page's root (hidden) under parent. Called once at boot from
 * the page registry. */
lv_obj_t *ui_chatview_build(lv_obj_t *parent);

/* The page root (for show/hide by the shell). */
lv_obj_t *ui_chatview_root(void);

/* This page's contextual keymap (spec §4): SHOULDER-R = back to the chat
 * list, END = home. UP/DOWN are unbound — scrollback is by drag (§14.4);
 * compose is a later stage (spec §9). */
const keymap_t *ui_chatview_keymap(void);

/* Open chat `chat_idx`'s conversation: load its messages from the model,
 * rebuild the bubble chain and restyle it, and land scrolled to the latest
 * message. A chat with no messages shows an honest empty state. */
void ui_chatview_open(int chat_idx, const char *name);

/* Re-wear palette roles + font faces after a theme/font change (the shell's
 * appearance pass calls this). */
void ui_chatview_restyle(void);

/* Re-derive sizes after a geometry change (the shell's relayout pass calls
 * this). Bubble widths depend only on the panel width, which is fixed per
 * run; the scroll view height follows the content area. */
void ui_chatview_relayout(void);

#ifdef __cplusplus
}
#endif
