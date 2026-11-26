/*
 * ui_speeddial.h — HOME speed dials: T9 keys 1..9 -> contacts/actions.
 *
 * Nine slots, one per T9 key (spec §2: "speed dials: T9 keys 1–9 map to
 * configurable contacts/actions"). Each slot holds a contact name; an empty
 * slot is inert (its key does nothing on HOME). State is static and in-
 * memory — no persistence yet (the local store arrives with the WAHA
 * backbone, spec §9).
 *
 * "Configuring" a dial means cycling its name through a fixed preset list
 * from the settings sub-page, so it works without a text keyboard. The sub-
 * page itself is one canonical menu_t on the generic renderer — same
 * pattern as the settings tree (rows carry live data; CENTER mutates).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPEEDDIAL_N 9   /* one slot per T9 key 1..9 */

/* Name of slot i ("" when the slot is empty). UI thread. */
const char *ui_speeddial_name(uint8_t i);

/* True when the slot holds a name (its HOME key dials something). */
bool ui_speeddial_set(uint8_t i);

/* Settings sub-page: cycle slot i to the next preset name (wraps to empty).
 * UI thread. */
void ui_speeddial_cycle(uint8_t i);

/* The Speed Dials settings sub-page as a canonical menu_t: one row per
 * slot, the live name in the row label; CENTER cycles it (the shell calls
 * ui_speeddial_cycle() + ui_list_refresh(), same pattern as the settings
 * tree). */
menu_t *ui_speeddial_page(void);

#ifdef __cplusplus
}
#endif
