/*
 * ui_theme.c — the palette data and the appearance switch.
 *
 * THE PALETTE TABLES BELOW ARE THE ONLY COLOUR LITERALS IN THE UI TREE.
 * Every painter resolves a ROLE (ui_theme.h) through ui_color(); nothing
 * else names a colour. `just check-colours` enforces this: a hex colour
 * outside this table (or the bezel's own table, host/src/bezel.c — the
 * device shell is not the screen) fails the build.
 *
 * Fonts are deliberately NOT loaded here: the tiny_ttf glyph cache must be
 * created on the UI thread in the shell's LVGL context (see ui_shell.c), and
 * loading them from a separate translation unit left the cache unallocated.
 * Colours are pure appearance data with no LVGL-lifecycle dependency, so they
 * live cleanly in the theme layer.
 *
 * The CHOSEN theme and font preferences are not state of this file any more:
 * they live in the model layer (model/settings.c, spec §16.4) and persist
 * through hal_storage. This file owns the palette tables, the face data and
 * the test ladders — presentation machinery around the model's values.
 */

#include "model/settings.h"
#include "ui/ui_theme.h"

/* ================================================================== */
/* Palette tables — one row per role, one column per theme             */
/* ================================================================== */

/* Contrast check (>= 4.5:1 for text roles; computed at palette level, which
 * is where a palette is added or edited — see the note in ui_theme.h):
 *
 *   DARK   TEXT/GROUND 13.1   TEXT/SURFACE 11.7   TEXT_DIM/GROUND 6.5
 *          TEXT_DIM/SURFACE 5.8   ON_HIGHLIGHT/HIGHLIGHT 4.8
 *   LIGHT  TEXT/GROUND 16.9   TEXT/SURFACE 14.7   TEXT_DIM/GROUND 6.1
 *          TEXT_DIM/SURFACE 5.4   ON_HIGHLIGHT/HIGHLIGHT 7.3
 *
 * DARK is the concept-art/07 palette every golden frame was shot with; its
 * values are therefore the pre-role ones unchanged (bg/bg_alt/sel/sel_edge/
 * cyan/text/text_dim/border -> GROUND/SURFACE/HIGHLIGHT/ACCENT_ALT/ACCENT/
 * TEXT/TEXT_DIM/BORDER). ON_HIGHLIGHT is TEXT: ink on the highlight used to
 * be the PAGE background (2.7:1 — the one role the old palette got wrong). */
static const uint32_t g_theme_rgb[UI_THEME_COUNT][UI_ROLE_COUNT] = {
    [UI_THEME_DARK] = {
        [UI_ROLE_GROUND]       = 0x141B17,   /* charcoal-green           */
        [UI_ROLE_SURFACE]      = 0x1D2620,   /* alternating row band     */
        [UI_ROLE_BORDER]       = 0x77897B,   /* sage structural grey     */
        [UI_ROLE_TEXT]         = 0xD8E2D6,   /* off-white                */
        [UI_ROLE_TEXT_DIM]     = 0x8FA391,   /* sage                     */
        [UI_ROLE_ACCENT]       = 0x3FD8E8,   /* cyan                     */
        [UI_ROLE_HIGHLIGHT]    = 0x2E6B3A,   /* green fill               */
        [UI_ROLE_ACCENT_ALT]   = 0x59D95F,   /* bright green             */
        [UI_ROLE_ON_HIGHLIGHT] = 0xD8E2D6,   /* = TEXT: 4.8:1 on fill    */
    },
    [UI_THEME_LIGHT] = {
        [UI_ROLE_GROUND]       = 0xFFFFFF,   /* paper                    */
        [UI_ROLE_SURFACE]      = 0xEDF1EE,   /* faint green-grey band    */
        [UI_ROLE_BORDER]       = 0x5E6E78,   /* slate: 5.3:1 on ground   */
        [UI_ROLE_TEXT]         = 0x1A1E1A,   /* near-black               */
        [UI_ROLE_TEXT_DIM]     = 0x5C645F,   /* 6.1:1 on ground          */
        [UI_ROLE_ACCENT]       = 0xFF6A1A,   /* bright orange            */
        [UI_ROLE_HIGHLIGHT]    = 0xFF6A1A,   /* orange fill              */
        [UI_ROLE_ACCENT_ALT]   = 0x1F6FB2,   /* light blue               */
        [UI_ROLE_ON_HIGHLIGHT] = 0x000000,   /* black: 7.3:1 on fill     */
    },
};

static const char *const g_theme_name[UI_THEME_COUNT] = {
    [UI_THEME_DARK]  = "DARK",
    [UI_THEME_LIGHT] = "LIGHT",
};

static const char *const g_role_name[UI_ROLE_COUNT] = {
    [UI_ROLE_GROUND]       = "GROUND",
    [UI_ROLE_SURFACE]      = "SURFACE",
    [UI_ROLE_BORDER]       = "BORDER",
    [UI_ROLE_TEXT]         = "TEXT",
    [UI_ROLE_TEXT_DIM]     = "TEXT_DIM",
    [UI_ROLE_ACCENT]       = "ACCENT",
    [UI_ROLE_HIGHLIGHT]    = "HIGHLIGHT",
    [UI_ROLE_ACCENT_ALT]   = "ACCENT_ALT",
    [UI_ROLE_ON_HIGHLIGHT] = "ON_HIGHLIGHT",
};

/* ================================================================== */
/* Font preferences — data only (the shell owns the LVGL font objects)  */
/* ================================================================== */

/* Asset files for the runtime faces. Pixelify Sans has a baked descriptor
 * for its design size (16 px, font_pixelify.h) and an asset TTF for every
 * other size, so the shipped look stays pixel-exact while the size is still
 * adjustable. */
static const char *const g_face_file[UI_FACE_COUNT] = {
    [UI_FACE_VT323]      = "VT323-Regular.ttf",
    [UI_FACE_SILKSCREEN] = "Silkscreen-Regular.ttf",
    [UI_FACE_PIXELIFY]   = "PixelifySans-Regular.ttf",
    [UI_FACE_JERSEY]     = "Jersey10-Regular.ttf",
    [UI_FACE_DOTGOTHIC]  = "DotGothic16-ASCII.ttf",
};

static const char *const g_face_name[UI_FACE_COUNT] = {
    [UI_FACE_VT323]      = "VT323",
    [UI_FACE_SILKSCREEN] = "Silkscreen",
    [UI_FACE_PIXELIFY]   = "Pixelify",
    [UI_FACE_JERSEY]     = "Jersey 10",
    [UI_FACE_DOTGOTHIC]  = "DotGothic16",
};

static const char *const g_face_short[UI_FACE_COUNT] = {
    [UI_FACE_VT323]      = "VT323",
    [UI_FACE_SILKSCREEN] = "SILK",
    [UI_FACE_PIXELIFY]   = "PIXEL",
    [UI_FACE_JERSEY]     = "JERSEY",
    [UI_FACE_DOTGOTHIC]  = "GOTH",
};

static const char *const g_font_role_name[UI_FONT_ROLE_COUNT] = {
    [UI_FONT_HEADER]  = "Headers",
    [UI_FONT_TEXT]    = "Text",
    [UI_FONT_SUBTEXT] = "Subtext",
};

/* The TEST LADDERS. Entry 0 is the shipped default (pixel-identical), and
 * each press steps face AND size together so one CENTER is one visible
 * change on the panel. §14.12's final picker splits the two; this is the
 * mechanism under test, and every candidate face is reachable. */
typedef struct { uint8_t face; uint8_t px; } font_step_t;

static const font_step_t g_ladder[UI_FONT_ROLE_COUNT][6] = {
    [UI_FONT_HEADER] = {
        { UI_FACE_SILKSCREEN, UI_FONT_TITLE_PX },   /* shipped            */
        { UI_FACE_SILKSCREEN, 32 },
        { UI_FACE_VT323,      32 },
        { UI_FACE_JERSEY,     32 },
        { UI_FACE_DOTGOTHIC,  24 },
        { UI_FACE_PIXELIFY,   32 },
    },
    [UI_FONT_TEXT] = {
        { UI_FACE_VT323,      UI_FONT_BODY_L_PX },  /* shipped            */
        { UI_FACE_VT323,      20 },
        { UI_FACE_VT323,      28 },
        { UI_FACE_JERSEY,     24 },
        { UI_FACE_DOTGOTHIC,  24 },
        { UI_FACE_SILKSCREEN, 24 },
    },
    [UI_FONT_SUBTEXT] = {
        { UI_FACE_PIXELIFY,   UI_FONT_BODY_M_PX },  /* shipped (baked)    */
        { UI_FACE_PIXELIFY,   20 },
        { UI_FACE_VT323,      16 },
        { UI_FACE_JERSEY,     16 },
        { UI_FACE_DOTGOTHIC,  16 },
        { UI_FACE_SILKSCREEN, 16 },
    },
};
#define LADDER_N ((int)(sizeof g_ladder[0] / sizeof g_ladder[0][0]))

/* ================================================================== */
/* Live state                                                          */
/* ================================================================== */

static ui_palette_t  g_pal;
static void        (*g_apply_cb)(void);

ui_font_pref_t ui_font_pref(ui_font_role_t role)
{
    ui_font_pref_t p = { UI_FACE_SILKSCREEN, UI_FONT_TITLE_PX };
    if ((unsigned)role < UI_FONT_ROLE_COUNT) {
        const model_font_pref_t *m = &model_settings()->fonts[role];
        p.face = (ui_face_t)m->face;
        p.px   = m->px;
    }
    return p;
}

void ui_font_pref_next(ui_font_role_t role)
{
    if ((unsigned)role >= UI_FONT_ROLE_COUNT) {
        return;
    }
    /* The chosen (face, px) lives in the model; the ladder here is the ring
     * of candidates it steps through. Find the current entry (a stored value
     * outside the ladder starts the ring from the beginning), then step. */
    ui_font_pref_t cur = ui_font_pref(role);
    int idx = 0;
    for (int i = 0; i < LADDER_N; i++) {
        if (g_ladder[role][i].face == cur.face && g_ladder[role][i].px == cur.px) {
            idx = i;
            break;
        }
    }
    int next = (idx + 1) % LADDER_N;
    model_settings_set_font(role, g_ladder[role][next].face,
                            g_ladder[role][next].px);
    if (g_apply_cb) {
        g_apply_cb();   /* re-resolve the faces, then restyle every page */
    }
}

const char *ui_font_role_name(ui_font_role_t role)
{
    return (unsigned)role < UI_FONT_ROLE_COUNT ? g_font_role_name[role] : "?";
}

const char *ui_face_name(ui_face_t face)
{
    return (unsigned)face < UI_FACE_COUNT ? g_face_name[face] : "?";
}

const char *ui_face_short(ui_face_t face)
{
    return (unsigned)face < UI_FACE_COUNT ? g_face_short[face] : "?";
}

const char *ui_face_file(ui_face_t face)
{
    return (unsigned)face < UI_FACE_COUNT ? g_face_file[face] : NULL;
}

/* lv_color_hex() is not a compile-time constant, so the table is resolved
 * into the live palette once per switch. */
static void resolve(void)
{
    uint8_t theme = model_settings()->theme;
    for (int r = 0; r < UI_ROLE_COUNT; r++) {
        g_pal.c[r] = lv_color_hex(g_theme_rgb[theme][r]);
    }
}

int ui_theme_init(void)
{
    /* The chosen theme is model state (loaded from storage at startup); the
     * factory default is DARK. */
    resolve();
    return 0;
}

lv_color_t ui_color(ui_role_t role)
{
    if ((unsigned)role >= UI_ROLE_COUNT) {
        return g_pal.c[UI_ROLE_TEXT];   /* a bad role must not read OOB */
    }
    return g_pal.c[role];
}

void ui_theme_set(ui_theme_id_t id)
{
    if ((unsigned)id >= UI_THEME_COUNT || id == model_settings()->theme) {
        return;
    }
    model_settings_set_theme((uint8_t)id);
    resolve();
    if (g_apply_cb) {
        g_apply_cb();   /* the shell's restyle pass: built pages repaint */
    }
}

ui_theme_id_t ui_theme_get(void)
{
    return (ui_theme_id_t)model_settings()->theme;
}

const char *ui_theme_name(ui_theme_id_t id)
{
    return (unsigned)id < UI_THEME_COUNT ? g_theme_name[id] : "?";
}

const char *ui_role_name(ui_role_t role)
{
    return (unsigned)role < UI_ROLE_COUNT ? g_role_name[role] : "?";
}

void ui_appearance_on_apply(void (*cb)(void))
{
    g_apply_cb = cb;
}

void ui_appearance_apply(void)
{
    if (g_apply_cb) {
        g_apply_cb();
    }
}
