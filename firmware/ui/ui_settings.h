/*
 * ui_settings.h — the Settings page: a tree of values with working toggles.
 *
 * The settings page is one canonical menu_t (ui_menu.h): each row is a
 * setting, its current value is drawn in the row's meta column, and CENTER
 * flips a two-state setting. The Speed Dials row carries a target page id
 * (PAGE_SPEED_DIALS) instead of a value: CENTER on it opens its sub-page
 * (ui_speeddial.c) — the shell routes rows with a target to open_page() and
 * rows without one to ui_settings_activate().
 *
 * State is static and in-memory: no persistence yet (the on-device store
 * arrives with the WAHA backbone, spec §9).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ui_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The settings the page holds, in row order. */
typedef enum {
    SETTING_MENU_STYLE = 0,   /* carousel | grid             */
    SETTING_CALL_PREF,        /* prefer WhatsApp | standard  */
    SETTING_SPEED_DIALS,      /* sub-page: T9 1..9 names     */
    SETTING_HELD_PRESS,       /* sub-page: long-press remap  */
    N_SETTINGS
} setting_id_t;

/* Two-state values (row `meta` shows the matching display string). */
#define MENU_STYLE_CAROUSEL 0
#define MENU_STYLE_GRID     1
#define CALL_PREF_WHATSAPP  0
#define CALL_PREF_STANDARD  1

/* The page's menu_t (static data; the shell builds the renderer on top of
 * it once, and refreshes the rows after a toggle). */
menu_t *ui_settings_page(void);

/* CENTER on row `sel`: flip a two-state setting and refresh its value text.
 * Inert for rows without a value. UI thread. */
void ui_settings_activate(uint8_t sel);

/* Current menu style (MENU_STYLE_CAROUSEL / MENU_STYLE_GRID). The grid
 * renderer reads this to decide which main-menu view to draw. */
uint8_t ui_settings_menu_style(void);

/* Set the menu style directly (harness/shot mode: land on a known view
 * without driving the toggle). UI thread or before the UI thread starts. */
void ui_settings_set_menu_style(uint8_t style);

#ifdef __cplusplus
}
#endif
