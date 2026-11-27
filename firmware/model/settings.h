/*
 * settings.h — user-configurable phone state (spec §16.4).
 *
 * The ONE place settings live: menu style, call preference, theme, the
 * three font roles, the softkey bar, the nine speed dials and the sixteen
 * held-press bindings. The UI reads it and writes it; no page module owns a
 * copy. Persistence through hal_storage (USER partition) is declared here
 * and implemented in settings.c.
 *
 * Model values are plain C — no LVGL, no ui/ headers. Where a value maps to
 * a UI enum (theme id, menu style, font face), the numeric values are
 * intentionally identical and the UI casts at the boundary.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MODEL_SETTINGS_SPEEDDIAL_N  9
#define MODEL_SETTINGS_HELDPRESS_N  16
#define MODEL_SETTINGS_FONT_ROLES   3
#define MODEL_SETTINGS_NAME_LEN     16

typedef enum {
    MODEL_THEME_DARK = 0,
    MODEL_THEME_LIGHT,
    MODEL_THEME_AMBER,
    MODEL_THEME_SLATE,
    MODEL_THEME_MONO,
    MODEL_THEME_COUNT
} model_theme_t;

typedef enum {
    MODEL_MENU_CAROUSEL = 0,
    MODEL_MENU_GRID,
    MODEL_MENU_MOVING,
    MODEL_MENU_COUNT
} model_menu_style_t;

typedef enum {
    MODEL_CALL_PREF_WHATSAPP = 0,
    MODEL_CALL_PREF_STANDARD,
    MODEL_CALL_PREF_COUNT
} model_call_pref_t;

/* What a long press does. The UI maps each action to the page that
 * implements it (ui_heldpress.c). */
typedef enum {
    MODEL_HP_NONE = 0,
    MODEL_HP_NEW_MESSAGE,
    MODEL_HP_NEW_CALL,
    MODEL_HP_MAP_SEARCH,
    MODEL_HP_CALL_HISTORY,
    MODEL_HP_CONTACTS,
    MODEL_HP_COUNT
} model_heldpress_t;

/* A font role's choice: face id + pixel size. Face ids follow the ui_face_t
 * order (ui/ui_theme.h); the UI casts at the boundary. */
typedef struct {
    uint8_t face;
    uint8_t px;
} model_font_pref_t;

typedef struct {
    uint8_t menu_style;              /* model_menu_style_t */
    uint8_t call_pref;               /* model_call_pref_t  */
    uint8_t theme;                   /* model_theme_t      */
    bool    softkeys;                /* action bar shown   */
    model_font_pref_t fonts[MODEL_SETTINGS_FONT_ROLES];
    char     speeddial[MODEL_SETTINGS_SPEEDDIAL_N][MODEL_SETTINGS_NAME_LEN];
    uint8_t  heldpress[MODEL_SETTINGS_HELDPRESS_N];      /* model_heldpress_t */
} model_settings_t;

/* The live settings — one home for every value the user can change. Seeded
 * with the factory defaults at process start (the state every golden frame
 * was shot with). */
const model_settings_t *model_settings(void);

/* Re-seed the factory defaults in memory (no save). */
void model_settings_reset(void);

/* Mutators — each makes the change durable through hal_storage, so a tweak
 * on the device survives the next boot. Out-of-range values are ignored. */
void model_settings_set_menu_style(uint8_t style);
void model_settings_set_call_pref(uint8_t pref);
void model_settings_set_theme(uint8_t theme);
void model_settings_set_softkeys(bool shown);
void model_settings_set_font(int role, uint8_t face, uint8_t px);
void model_settings_set_speeddial(uint8_t i, const char *name);
void model_settings_cycle_speeddial(uint8_t i);
void model_settings_set_heldpress(uint8_t i, uint8_t action);
void model_settings_cycle_heldpress(uint8_t i);

/* Which held-press slot a physical key owns: 0..15, or -1 for keys with no
 * binding (the HAL key table lives here — it is domain data, not UI). */
int model_settings_heldpress_slot(int hal_key);

/* Persistence (hal_storage, USER partition, file "settings"): load replaces
 * the live state with what is stored (factory defaults when the file is
 * absent or unreadable); save writes the live state. Both return 0 on
 * success. */
int model_settings_load(void);
int model_settings_save(void);
