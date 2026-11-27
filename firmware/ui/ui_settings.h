/*
 * ui_settings.h — the settings state and its Display / Font pages.
 *
 * The settings list itself lives in the main menu's SETTINGS category
 * (spec §14.13 — there is no settings-tree page); this module owns the
 * pages that category reaches, all canonical menu_t pages rendered by the
 * same generic renderer as every other list page:
 *
 *   DISPLAY   Theme (submenu) · Font (sub-page) · Menu Style · Softkey bar
 *   THEME     one row per named palette; the active one shows ACTIVE
 *   FONT      Headers · Text · Subtext   (spec §14.12's three levels)
 *   FONT/role Face · Size — two independent controls (issue #16)
 *
 * A row without a `target` is a VALUE row: CENTER cycles it. The page
 * function that handles the press returns what the change needs from the
 * shell — a value was flipped and the rows just need a refresh, a theme
 * switch and restyle, a font reload, or a re-layout — because the shell owns
 * the LVGL objects and the appearance passes. This module is LVGL-free.
 *
 * State lives in the model layer (model/settings.c, spec §16.4) and persists
 * through hal_storage.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ui_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The SETTINGS tree's rows, in order. */
typedef enum {
    SETTING_DISPLAY = 0,      /* sub-page: appearance           */
    SETTING_CALL_PREF,        /* prefer WhatsApp | standard     */
    SETTING_SPEED_DIALS,      /* sub-page: T9 1..9 names        */
    SETTING_HELD_PRESS,       /* sub-page: long-press remap     */
    N_SETTINGS
} setting_id_t;

/* The DISPLAY page's rows, in order (spec §14.9–14.12). */
typedef enum {
    DISPLAY_THEME = 0,        /* submenu: the named palettes    */
    DISPLAY_FONT,             /* sub-page: the three font roles */
    DISPLAY_MENU_STYLE,       /* CAROUSEL | GRID                */
    DISPLAY_SOFTKEYS,         /* SHOWN | HIDDEN                 */
    N_DISPLAY_SETTINGS
} display_setting_id_t;

/* What a press asked the shell to do after the value changed. */
typedef enum {
    UI_SETTING_EFFECT_NONE = 0,   /* refresh the rows only             */
    UI_SETTING_EFFECT_THEME,      /* palette switched (already applied) */
    UI_SETTING_EFFECT_FONT,       /* faces changed (already applied)    */
    UI_SETTING_EFFECT_SOFTKEYS,   /* bar visibility changed: re-layout  */
    UI_SETTING_EFFECT_MENU_STYLE, /* main-menu view changed             */
} ui_setting_effect_t;

/* Two-state values (row `meta` shows the matching display string). */
#define MENU_STYLE_CAROUSEL 0
#define MENU_STYLE_GRID     1
#define MENU_STYLE_MOVING   2   /* §14.2: sliding category strip        */
#define CALL_PREF_WHATSAPP  0
#define CALL_PREF_STANDARD  1

/* The three pages (static data; the shell builds the renderer on top of
 * each once, and refreshes the rows after a change). */
menu_t *ui_settings_display_page(void);
menu_t *ui_settings_theme_page(void);
menu_t *ui_settings_font_page(void);

/* The role page behind a Font row (issue #16): two rows, Face and Size.
 * ui_settings_font_open() records which role CENTER selected; the page
 * function refreshes its rows and title from that role. */
void   ui_settings_font_open(uint8_t role);
menu_t *ui_settings_font_role_page(void);

/* The current Call Preference value as its display string ("WHATSAPP" /
 * "STANDARD"). The main menu's SETTINGS category shows it on its Call
 * Preference row's meta column. */
const char *ui_settings_call_pref_name(void);

/* CENTER on row `sel` of the settings list (a setting_id_t, from the main
 * menu's SETTINGS category). Applies the value and returns what the shell
 * still has to do (see ui_setting_effect_t). Inert for rows that open a
 * sub-page. UI thread or before it starts. */
ui_setting_effect_t ui_settings_activate(uint8_t sel);
ui_setting_effect_t ui_settings_display_activate(uint8_t sel);
ui_setting_effect_t ui_settings_theme_activate(uint8_t sel);
ui_setting_effect_t ui_settings_font_activate(uint8_t sel);
ui_setting_effect_t ui_settings_font_role_activate(uint8_t sel);

/* Current menu style (CAROUSEL / GRID / MOVING — one cycling control,
 * spec §14.3). The grid
 * renderer reads this to decide which main-menu view to draw. */
uint8_t ui_settings_menu_style(void);

/* Set the menu style directly (harness/shot mode: land on a known view
 * without driving the toggle). UI thread or before the UI thread starts. */
void ui_settings_set_menu_style(uint8_t style);

#ifdef __cplusplus
}
#endif
