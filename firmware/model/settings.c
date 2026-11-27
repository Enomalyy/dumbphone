/*
 * settings.c — the one home for user-configurable state (spec §16.4).
 *
 * Seeded with the factory defaults — the exact state every golden frame was
 * shot with. Every mutator makes its change durable through hal_storage, so
 * a tweak on the device survives the next boot; model_settings_load() is what
 * main.c calls at startup (after hal_storage_init) to restore it.
 */
#include "model/settings.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal/hal_input.h"     /* hal_key_t: the held-press key table */
#include "hal/hal_storage.h"

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

/* The factory defaults — the exact state every golden frame was shot with.
 * Every process starts from this (no load call needed for sane state); */
/* model_settings_load() overlays saved values on top of it.             */
static const model_settings_t g_factory = {
    .menu_style = MODEL_MENU_CAROUSEL,
    .call_pref  = MODEL_CALL_PREF_WHATSAPP,
    .theme      = MODEL_THEME_DARK,
    .softkeys   = true,
    /* The shipped font roles — Silkscreen 24 / VT323 24 / Pixelify 16,
     * ladder entry 0 in ui_theme.h (face ids follow ui_face_t order). */
    .fonts = {
        [0] = { 1, 24 },   /* HEADER  Silkscreen 24 */
        [1] = { 0, 24 },   /* TEXT    VT323 24      */
        [2] = { 2, 16 },   /* SUBTEXT Pixelify 16   */
    },
    .speeddial = {
        [0] = "Mom",
        [1] = "Dad",
    },
    .heldpress = {
        [0] = MODEL_HP_NEW_MESSAGE,   /* SHOULDER-L: quick compose */
    },
};

static model_settings_t g_settings = g_factory;

static void seed_defaults(void)
{
    g_settings = g_factory;
}

const model_settings_t *model_settings(void)
{
    return &g_settings;
}

void model_settings_reset(void)
{
    seed_defaults();
}

/* ------------------------------------------------------------------ */
/* Mutators                                                            */
/* ------------------------------------------------------------------ */

static void save(void);   /* persistence, below */

void model_settings_set_menu_style(uint8_t style)
{
    if (style >= MODEL_MENU_COUNT) {
        return;
    }
    g_settings.menu_style = style;
    save();
}

void model_settings_set_call_pref(uint8_t pref)
{
    if (pref >= MODEL_CALL_PREF_COUNT) {
        return;
    }
    g_settings.call_pref = pref;
    save();
}

void model_settings_set_theme(uint8_t theme)
{
    if (theme >= MODEL_THEME_COUNT) {
        return;
    }
    g_settings.theme = theme;
    save();
}

void model_settings_set_softkeys(bool shown)
{
    if (g_settings.softkeys == shown) {
        return;
    }
    g_settings.softkeys = shown;
    save();
}

void model_settings_set_font(int role, uint8_t face, uint8_t px)
{
    if (role < 0 || role >= MODEL_SETTINGS_FONT_ROLES) {
        return;
    }
    g_settings.fonts[role].face = face;
    g_settings.fonts[role].px = px;
    save();
}

void model_settings_set_speeddial(uint8_t i, const char *name)
{
    if (i >= MODEL_SETTINGS_SPEEDDIAL_N || !name) {
        return;
    }
    snprintf(g_settings.speeddial[i], sizeof g_settings.speeddial[i],
             "%.*s", MODEL_SETTINGS_NAME_LEN - 1, name);
    save();
}

/* The preset names a dial can cycle through (domain data: what a speed
 * dial is allowed to be). */
static const char *const g_sd_presets[] = {
    "", "Mom", "Dad", "Work", "Doctor", "School", "Friend", "Family",
    "Emergency",
};
#define N_SD_PRESETS ((int)(sizeof g_sd_presets / sizeof g_sd_presets[0]))

void model_settings_cycle_speeddial(uint8_t i)
{
    if (i >= MODEL_SETTINGS_SPEEDDIAL_N) {
        return;
    }
    int cur = 0;   /* unknown value: treat as the first preset */
    for (int p = 0; p < N_SD_PRESETS; p++) {
        if (strcmp(g_settings.speeddial[i], g_sd_presets[p]) == 0) {
            cur = p;
            break;
        }
    }
    snprintf(g_settings.speeddial[i], sizeof g_settings.speeddial[i], "%s",
             g_sd_presets[(cur + 1) % N_SD_PRESETS]);
    save();
}

void model_settings_set_heldpress(uint8_t i, uint8_t action)
{
    if (i >= MODEL_SETTINGS_HELDPRESS_N || action >= MODEL_HP_COUNT) {
        return;
    }
    g_settings.heldpress[i] = action;
    save();
}

/* The preset actions a slot can cycle through, in cycle order. */
static const uint8_t g_hp_presets[] = {
    MODEL_HP_NONE, MODEL_HP_NEW_MESSAGE, MODEL_HP_NEW_CALL,
    MODEL_HP_MAP_SEARCH, MODEL_HP_CALL_HISTORY, MODEL_HP_CONTACTS,
};
#define N_HP_PRESETS ((int)(sizeof g_hp_presets / sizeof g_hp_presets[0]))

void model_settings_cycle_heldpress(uint8_t i)
{
    if (i >= MODEL_SETTINGS_HELDPRESS_N) {
        return;
    }
    int cur = 0;   /* unknown value: treat as none */
    for (int p = 0; p < N_HP_PRESETS; p++) {
        if (g_settings.heldpress[i] == g_hp_presets[p]) {
            cur = p;
            break;
        }
    }
    g_settings.heldpress[i] = g_hp_presets[(cur + 1) % N_HP_PRESETS];
    save();
}

/* The configurable keys, in slot order (the sub-page's row order). */
static const int g_hp_keys[MODEL_SETTINGS_HELDPRESS_N] = {
    HAL_KEY_SHOULDER_L, HAL_KEY_SHOULDER_R,
    HAL_KEY_UP,         HAL_KEY_DOWN,
    HAL_KEY_DIGIT_0, HAL_KEY_DIGIT_1, HAL_KEY_DIGIT_2, HAL_KEY_DIGIT_3,
    HAL_KEY_DIGIT_4, HAL_KEY_DIGIT_5, HAL_KEY_DIGIT_6, HAL_KEY_DIGIT_7,
    HAL_KEY_DIGIT_8, HAL_KEY_DIGIT_9, HAL_KEY_STAR,   HAL_KEY_HASH,
};

int model_settings_heldpress_slot(int hal_key)
{
    for (int i = 0; i < MODEL_SETTINGS_HELDPRESS_N; i++) {
        if (g_hp_keys[i] == hal_key) {
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Persistence (hal_storage, USER partition, file "settings")          */
/*                                                                     */
/* Format: plain text, one key=value per line, first line is the       */
/* format version. Parsing is lenient: unknown keys are ignored,       */
/* missing keys keep their factory default, a bad/missing version      */
/* re-seeds everything (the file is treated as absent).                */
/* ------------------------------------------------------------------ */

#define SETTINGS_FILE  "settings"
#define SETTINGS_VER   "1"

/* Append one formatted line, never moving `off` past the buffer: a full
 * buffer stops taking text instead of running the pointer off the end (the
 * old `off += snprintf()` pattern grew `off` past the array once the text
 * outgrew it, and the next `buf + off` was out of bounds). */
static void app(char *buf, size_t cap, int *off, const char *fmt, ...)
{
    if (*off < 0 || (size_t)*off + 1 >= cap) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *off, cap - (size_t)*off, fmt, ap);
    va_end(ap);
    if (n > 0) {
        *off += n;
        if ((size_t)*off >= cap) {
            *off = (int)cap - 1;   /* keep the terminator's byte */
        }
    }
}

int model_settings_save(void)
{
    char buf[768];
    int off = 0;
    const model_settings_t *s = &g_settings;

    app(buf, sizeof buf, &off, "%s\n", SETTINGS_VER);
    app(buf, sizeof buf, &off, "menu_style=%u\n", s->menu_style);
    app(buf, sizeof buf, &off, "call_pref=%u\n", s->call_pref);
    app(buf, sizeof buf, &off, "theme=%u\n", s->theme);
    app(buf, sizeof buf, &off, "softkeys=%d\n", s->softkeys ? 1 : 0);
    for (int r = 0; r < MODEL_SETTINGS_FONT_ROLES; r++) {
        const char *k = r == 0 ? "font_header" : r == 1 ? "font_text"
                        : "font_subtext";
        app(buf, sizeof buf, &off, "%s=%u:%u\n", k, s->fonts[r].face,
            s->fonts[r].px);
    }
    for (int i = 0; i < MODEL_SETTINGS_SPEEDDIAL_N; i++) {
        app(buf, sizeof buf, &off, "speeddial_%d=%s\n", i, s->speeddial[i]);
    }
    for (int i = 0; i < MODEL_SETTINGS_HELDPRESS_N; i++) {
        app(buf, sizeof buf, &off, "heldpress_%d=%u\n", i, s->heldpress[i]);
    }
    return hal_storage_write_file(HAL_STORAGE_USER, SETTINGS_FILE, buf,
                                  (size_t)off);
}

int model_settings_load(void)
{
    seed_defaults();   /* missing keys keep their factory value */

    char buf[1024];
    size_t len = 0;
    if (hal_storage_read_file(HAL_STORAGE_USER, SETTINGS_FILE, buf,
                              sizeof buf, &len) != 0) {
        return 0;   /* no saved state: factory defaults stand */
    }
    if (len >= sizeof buf) {
        seed_defaults();   /* truncated read: treat the file as corrupt */
        return 0;
    }
    buf[len] = '\0';

    int version_ok = 0;
    char *line = strtok(buf, "\n");
    while (line) {
        if (!version_ok) {
            /* The first line must be the format version; anything else */
            /* voids the whole file (the defaults already stand).      */
            version_ok = (strcmp(line, SETTINGS_VER) == 0);
            line = strtok(NULL, "\n");
            continue;
        }
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            const char *key = line;
            const char *val = eq + 1;
            int v;
            if (strcmp(key, "menu_style") == 0) {
                v = atoi(val);
                if (v >= 0 && v < MODEL_MENU_COUNT) g_settings.menu_style = (uint8_t)v;
            } else if (strcmp(key, "call_pref") == 0) {
                v = atoi(val);
                if (v >= 0 && v < MODEL_CALL_PREF_COUNT) g_settings.call_pref = (uint8_t)v;
            } else if (strcmp(key, "theme") == 0) {
                v = atoi(val);
                if (v >= 0 && v < MODEL_THEME_COUNT) g_settings.theme = (uint8_t)v;
            } else if (strcmp(key, "softkeys") == 0) {
                g_settings.softkeys = (atoi(val) != 0);
            } else if (strncmp(key, "font_", 5) == 0) {
                uint8_t face, px;
                int r = key[5] == 'h' ? 0 : key[5] == 't' ? 1 : 2;
                if (sscanf(val, "%hhu:%hhu", &face, &px) == 2 &&
                    r >= 0 && r < MODEL_SETTINGS_FONT_ROLES && px >= 8 && px <= 160) {
                    g_settings.fonts[r].face = face;
                    g_settings.fonts[r].px = px;
                }
            } else if (strncmp(key, "speeddial_", 10) == 0) {
                int i = atoi(key + 10);
                if (i >= 0 && i < MODEL_SETTINGS_SPEEDDIAL_N) {
                    snprintf(g_settings.speeddial[i],
                             sizeof g_settings.speeddial[i], "%.*s",
                             MODEL_SETTINGS_NAME_LEN - 1, val);
                }
            } else if (strncmp(key, "heldpress_", 10) == 0) {
                int i = atoi(key + 10);
                v = atoi(val);
                if (i >= 0 && i < MODEL_SETTINGS_HELDPRESS_N &&
                    v >= 0 && v < MODEL_HP_COUNT) {
                    g_settings.heldpress[i] = (uint8_t)v;
                }
            }
            /* unknown keys: ignored */
        }
        line = strtok(NULL, "\n");
    }
    return 0;
}

static void save(void)
{
    (void)model_settings_save();
}
