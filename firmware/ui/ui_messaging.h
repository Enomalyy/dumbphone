/*
 * ui_messaging.h — Messaging: the unified WhatsApp surface (spec §2).
 *
 * The chat list is a PAGE, not HOME: it is reached from the main menu's
 * MESSAGING category and renders the chat source (host/data/chats.json on
 * desktop) through the shared renderer as one item slice per category —
 * ALL / FAMILY / UNREAD / FAVORITES / ARCHIVED.
 *
 * Owns: the page's menu_t, its keymap, the category cycling state, and the
 * unread total (for widgets). Exposes: ui_messaging_build / page / keymap /
 * cycle_cat / unread_total (this header). Must not touch: the navigation
 * stack, the status/softkey bars, or any other screen.
 */
#pragma once

#include "lvgl.h"
#include "ui_keymap.h"
#include "ui_menu.h"

/* Build the page under `parent` (the shell's content area). Returns the
 * list_page_t; NULL when the chat source is unavailable — the shell then
 * renders an empty state and the row pool stays hidden. */
list_page_t *ui_messaging_build(lv_obj_t *parent);

/* The page object (NULL until build succeeds). */
list_page_t *ui_messaging_page(void);

/* The contextual keymap for this screen. */
const keymap_t *ui_messaging_keymap(void);

/* Step the category strip by `dir` (-1/1) and re-render the rows. */
void ui_messaging_cycle_cat(int dir);

/* Total unread across all categories (0 when the source is unavailable).
 * Static per boot — the desktop source is a file read once at build time. */
int ui_messaging_unread_total(void);
