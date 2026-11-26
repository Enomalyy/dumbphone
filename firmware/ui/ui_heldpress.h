/*
 * ui_heldpress.h — held-press (long-press) key config (spec §2).
 *
 * "Held presses on L/R/UP/DOWN/T9 = custom actions (configurable in
 * options)." Sixteen slots, one per configurable key: SHOULDER-L/R, d-pad
 * UP/DOWN, and the twelve T9 keys. Each slot holds a page target — the
 * action a LONG press of that key performs (opening that page). PAGE_NONE
 * means the key is unconfigured: a long press behaves exactly like a short
 * one (the shell fires the page's normal keymap action).
 *
 * The shell detects the hold (key held >= LONG_PRESS_MS between down and up)
 * and asks ui_heldpress_target() what to do; an unconfigured key falls back
 * to its usual keymap action, so this feature is purely additive.
 *
 * State is static and in-memory — no persistence yet (the local store
 * arrives with the WAHA backbone, spec §9). Configuring a slot means
 * cycling it through a fixed preset list from the settings sub-page, so it
 * works without a text keyboard: CENTER on the sub-page row advances the
 * slot to the next preset (wrapping back to unconfigured).
 *
 * The sub-page is one canonical menu_t (ui_menu.h): one row per key, the
 * live preset in the row's meta column — the exact pattern of the settings
 * tree and the speed-dial sub-page.
 */
#pragma once

#include <stdint.h>

#include "hal/hal_input.h"
#include "ui_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HELDPRESS_N 16   /* L, R, UP, DOWN + T9 (0..9, *, #) */

/* The page a long press of `key` opens (PAGE_NONE = unconfigured).
 * UI thread. */
page_id_t ui_heldpress_target(hal_key_t key);

/* Settings sub-page: cycle slot i to the next preset (wraps to PAGE_NONE).
 * UI thread. */
void ui_heldpress_cycle(uint8_t i);

/* The Held Presses settings sub-page as a canonical menu_t: one row per
 * configurable key, the live preset in the row's meta column; CENTER cycles
 * it (the shell calls ui_heldpress_cycle() + ui_list_refresh(), same
 * pattern as the settings tree and speed-dial sub-page). */
menu_t *ui_heldpress_page(void);

#ifdef __cplusplus
}
#endif
