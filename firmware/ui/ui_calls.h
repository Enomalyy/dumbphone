/*
 * ui_calls.h — the Calls category pages (spec §2): Call History + Contacts.
 *
 * Both pages are canonical menu_t instances on the generic renderer
 * (ui_menu.c) — the same pattern as the settings tree and the config
 * sub-pages: rows carry live data, the shell routes keys through each page's
 * own keymap. State is static mock data in-memory (the real call log /
 * contact store arrives with the WAHA backbone, spec §9).
 */
#pragma once

#include "ui_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The Call History page: recent calls as two-line rows (name + direction,
 * time in the renderer's meta column) — the same row shape as the chat list.
 * Rows are inert (target PAGE_NONE): opening a conversation is a later leg,
 * the same bar as the chat list today. */
menu_t *ui_calls_history_page(void);

/* The Contacts page: name + number (the number in the renderer's meta
 * column). Rows are inert — no compose screen yet (charter scope). */
menu_t *ui_calls_contacts_page(void);

#ifdef __cplusplus
}
#endif
