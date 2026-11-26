/*
 * bezel.c — the physical keys around the simulated panel.
 *
 * Classic T9 dumb-phone layout:
 *
 *        [VOL+]                 [PWR]      <- side keys, beside the screen
 *        [VOL-]
 *      +-------------- panel --------------+
 *      |           W x H (runtime)         |
 *      +-----------------------------------+
 *              [ L ]   (d-pad)   [ R ]      <- shoulders flank the d-pad
 *                   [ CALL ] [ END ]
 *                   [ 1 ][ 2 ][ 3 ]
 *                   [ 4 ][ 5 ][ 6 ]         <- 3x4 keypad
 *                   [ 7 ][ 8 ][ 9 ]
 *                   [ * ][ 0 ][ # ]
 *
 * The panel size is a RUNTIME value (ui/ui_geometry.c, spec §15 / issue #1):
 * `bezel_layout()` computes every key position from it. At the default
 * 320x480 the result is exactly the classic table this file used to hardcode.
 *
 * To rearrange anything, edit `bezel_layout()`: all the drawing and
 * hit-testing below is generic over the resulting table.
 */

#include "bezel.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "hal/hal_input.h"

/* ------------------------------------------------------------------ */
/* Geometry (logical units: panel pixels)                             */
/* ------------------------------------------------------------------ */

#define SIDE_W       44          /* side bezels: volume + power       */
#define TOP_H        26          /* earpiece / grille                 */
#define DPAD_R       39
#define CALLEND_H    28
#define KEYPAD_KEY_W 56
#define KEYPAD_KEY_H 32
#define KEYPAD_GAP   5

/* Lower-deck offsets below the panel bottom, as ratios of the panel height
 * (identity at 480): d-pad centre +47, shoulders +33, call/end +94,
 * keypad top +130, window bottom +283. */
#define DECK(n) ((int)lroundf((float)(ph) * (float)(n) / 480.0f))

/* ------------------------------------------------------------------ */
/* THE BUTTON TABLE — filled by bezel_layout() for the runtime panel  */
/* ------------------------------------------------------------------ */

#define BUTTON_COUNT 24

static bezel_button_t s_buttons[BUTTON_COUNT];

static void bezel_layout(int pw, int ph)
{
    const int px = SIDE_W;                    /* panel recess top-left */
    const int py = TOP_H;
    const int win_w = SIDE_W * 2 + pw;
    const int vol_up_y   = py + DECK(94);
    const int vol_down_y = py + DECK(166);
    const int dpad_cx    = px + pw / 2;
    const int dpad_cy    = py + ph + DECK(47);
    const int shoulder_y = py + ph + DECK(33);
    const int callend_y  = py + ph + DECK(94);
    const int keypad_y0  = py + ph + DECK(130);
    const int keypad_x0  = dpad_cx - (KEYPAD_KEY_W * 3 + KEYPAD_GAP * 2) / 2;
    int i = 0;

    /* ---- side keys ---- */
    s_buttons[i++] = (bezel_button_t){ "vol-up",   "VOL+", NULL, HAL_KEY_VOLUME_UP,   4, vol_up_y, 36, 62, BEZEL_STYLE_SIDE,  BEZEL_SHAPE_RECT };
    s_buttons[i++] = (bezel_button_t){ "vol-down", "VOL-", NULL, HAL_KEY_VOLUME_DOWN, 4, vol_down_y, 36, 62, BEZEL_STYLE_SIDE,  BEZEL_SHAPE_RECT };
    s_buttons[i++] = (bezel_button_t){ "power",    "PWR",  NULL, HAL_KEY_POWER,       win_w - 40, vol_up_y, 36, 62, BEZEL_STYLE_POWER, BEZEL_SHAPE_RECT };

    /* ---- circular d-pad: hit rects inside the ring ---- */
    /* Cross of hit cells around the hub; w == h on the disc is its diameter,
     * so changing this row resizes the centre key. Cells stay disjoint. */
    s_buttons[i++] = (bezel_button_t){ "up",    NULL, NULL, HAL_KEY_UP,    dpad_cx - 13, dpad_cy - 39, 26, 19, BEZEL_STYLE_KEY, BEZEL_SHAPE_ARROW_UP    };
    s_buttons[i++] = (bezel_button_t){ "down",  NULL, NULL, HAL_KEY_DOWN,  dpad_cx - 13, dpad_cy + 20, 26, 19, BEZEL_STYLE_KEY, BEZEL_SHAPE_ARROW_DOWN  };
    s_buttons[i++] = (bezel_button_t){ "left",  NULL, NULL, HAL_KEY_LEFT,  dpad_cx - 39, dpad_cy - 13, 19, 26, BEZEL_STYLE_KEY, BEZEL_SHAPE_ARROW_LEFT  };
    s_buttons[i++] = (bezel_button_t){ "right", NULL, NULL, HAL_KEY_RIGHT, dpad_cx + 20, dpad_cy - 13, 19, 26, BEZEL_STYLE_KEY, BEZEL_SHAPE_ARROW_RIGHT };
    s_buttons[i++] = (bezel_button_t){ "ok",    "OK", NULL, HAL_KEY_OK,    dpad_cx - 20, dpad_cy - 20, 40, 40, BEZEL_STYLE_KEY, BEZEL_SHAPE_DISC        };

    /* ---- shoulders flanking the d-pad ---- */
    s_buttons[i++] = (bezel_button_t){ "shoulder-l", "L", NULL, HAL_KEY_SHOULDER_L, px + 16,      shoulder_y, 46, 28, BEZEL_STYLE_KEY, BEZEL_SHAPE_RECT };
    s_buttons[i++] = (bezel_button_t){ "shoulder-r", "R", NULL, HAL_KEY_SHOULDER_R, px + pw - 62, shoulder_y, 46, 28, BEZEL_STYLE_KEY, BEZEL_SHAPE_RECT };

    /* ---- call / end (pair centred on the panel) ---- */
    s_buttons[i++] = (bezel_button_t){ "call", "CALL", NULL, HAL_KEY_CALL, dpad_cx - 110, callend_y, 104, CALLEND_H, BEZEL_STYLE_CALL, BEZEL_SHAPE_RECT };
    s_buttons[i++] = (bezel_button_t){ "end",  "END",  NULL, HAL_KEY_END,  dpad_cx + 10,  callend_y, 104, CALLEND_H, BEZEL_STYLE_END,  BEZEL_SHAPE_RECT };

    /* ---- T9 keypad 3x4 (digit + letter cluster) ---- */
    static const char *ids[12] = { "digit-1", "digit-2", "digit-3", "digit-4",
                                   "digit-5", "digit-6", "digit-7", "digit-8",
                                   "digit-9", "star",    "digit-0", "hash" };
    static const char *digits[12] = { "1", "2", "3", "4", "5", "6",
                                      "7", "8", "9", "*", "0", "#" };
    static const char *subs[12] = { NULL, "ABC", "DEF", "GHI", "JKL", "MNO",
                                    "PQRS", "TUV", "WXYZ", NULL, " ", NULL };
    static const hal_key_t keys[12] = { HAL_KEY_DIGIT_1, HAL_KEY_DIGIT_2,
                                        HAL_KEY_DIGIT_3, HAL_KEY_DIGIT_4,
                                        HAL_KEY_DIGIT_5, HAL_KEY_DIGIT_6,
                                        HAL_KEY_DIGIT_7, HAL_KEY_DIGIT_8,
                                        HAL_KEY_DIGIT_9, HAL_KEY_STAR,
                                        HAL_KEY_DIGIT_0, HAL_KEY_HASH };
    for (int k = 0; k < 12; k++) {
        int col = k % 3, row = k / 3;
        s_buttons[i++] = (bezel_button_t){ ids[k], digits[k], subs[k], keys[k],
                                           keypad_x0 + col * (KEYPAD_KEY_W + KEYPAD_GAP),
                                           keypad_y0 + row * (KEYPAD_KEY_H + KEYPAD_GAP),
                                           KEYPAD_KEY_W, KEYPAD_KEY_H,
                                           BEZEL_STYLE_KEY, BEZEL_SHAPE_RECT };
    }
}

/* ------------------------------------------------------------------ */
/* State                                                              */
/* ------------------------------------------------------------------ */

static float s_scale = 1.0f;
static int   s_win_w;
static int   s_win_h;
static int   s_pw, s_ph;      /* runtime panel size (logical units)    */
static int   s_log_w, s_log_h; /* window size in logical units          */
static int   s_dpad_cx, s_dpad_cy;

typedef struct {
    Uint32 bg, rim, rim_hi, grille;
    Uint32 face, edge, lip, label, label_dim;
    Uint32 p_face, p_edge, p_label;
    Uint32 call_face, call_edge, call_p_face, call_p_edge;
    Uint32 end_face,  end_edge,  end_p_face,  end_p_edge;
} palette_t;

static palette_t s_pal;
static bool s_pal_ready;

/* ------------------------------------------------------------------ */
/* 5x7 pixel font — same flat aesthetic as the panel                  */
/* ------------------------------------------------------------------ */

typedef struct { char ch; uint8_t rows[7]; } glyph_t;

static const glyph_t s_font[] = {
    { 'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11} },
    { 'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E} },
    { 'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E} },
    { 'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E} },
    { 'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F} },
    { 'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10} },
    { 'G', {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F} },
    { 'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11} },
    { 'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E} },
    { 'J', {0x07,0x02,0x02,0x02,0x02,0x12,0x0C} },
    { 'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11} },
    { 'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F} },
    { 'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11} },
    { 'N', {0x11,0x19,0x19,0x15,0x13,0x13,0x11} },
    { 'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E} },
    { 'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10} },
    { 'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D} },
    { 'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11} },
    { 'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E} },
    { 'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04} },
    { 'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E} },
    { 'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04} },
    { 'W', {0x11,0x11,0x11,0x15,0x15,0x1B,0x11} },
    { 'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11} },
    { 'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04} },
    { 'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F} },
    { '0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E} },
    { '1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E} },
    { '2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F} },
    { '3', {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E} },
    { '4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02} },
    { '5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E} },
    { '6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E} },
    { '7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08} },
    { '8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E} },
    { '9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C} },
    { '+', {0x00,0x04,0x04,0x1F,0x04,0x04,0x00} },
    { '*', {0x04,0x15,0x0E,0x04,0x0E,0x15,0x04} },
    { '#', {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A} },
    { '-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00} },
    { '.', {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C} },
    { ' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00} },
};

#define GLYPH_W 5
#define GLYPH_H 7
#define GLYPH_ADV 6   /* 5 px + 1 px gap */

static const uint8_t *glyph_rows(char ch)
{
    if (ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - 'a' + 'A');
    }
    for (size_t i = 0; i < sizeof s_font / sizeof s_font[0]; i++) {
        if (s_font[i].ch == ch) {
            return s_font[i].rows;
        }
    }
    return s_font[sizeof s_font / sizeof s_font[0] - 1].rows;   /* space */
}

/* ------------------------------------------------------------------ */
/* Drawing helpers (logical units -> window pixels)                   */
/* ------------------------------------------------------------------ */

static void rect(SDL_Surface *s, float x, float y, float w, float h, Uint32 color)
{
    if (!s || w <= 0 || h <= 0) {
        return;
    }
    /* Round the EDGES, not the size, so adjacent rects never leave seams. */
    int x0 = (int)lroundf(x * s_scale);
    int x1 = (int)lroundf((x + w) * s_scale);
    int y0 = (int)lroundf(y * s_scale);
    int y1 = (int)lroundf((y + h) * s_scale);
    SDL_Rect r = { x0, y0, x1 - x0, y1 - y0 };
    SDL_FillRect(s, &r, color);
}

/* Filled circle, drawn as one-unit horizontal spans. */
static void disc(SDL_Surface *s, float cx, float cy, float r, Uint32 color)
{
    int n = (int)r;
    for (int dy = -n; dy <= n; dy++) {
        float dx = sqrtf(r * r - (float)dy * (float)dy);
        rect(s, cx - dx, cy + (float)dy, dx * 2.0f, 1.0f, color);
    }
}

/* Filled triangle for the d-pad arrows. dir: 0 up, 1 down, 2 left, 3 right. */
static void arrow_shape(SDL_Surface *s, float cx, float cy, int dir, float size, Uint32 color)
{
    int n = (int)size;
    if (n < 2) {
        return;
    }
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)(n - 1);
        float half = 1.0f + t * (size * 0.5f - 1.0f);
        float o = (float)i;
        switch (dir) {
        case 0: rect(s, cx - half, cy - size * 0.5f + o, half * 2.0f, 1.0f, color); break;
        case 1: rect(s, cx - half, cy + size * 0.5f - o - 1.0f, half * 2.0f, 1.0f, color); break;
        case 2: rect(s, cx - size * 0.5f + o, cy - half, 1.0f, half * 2.0f, color); break;
        default: rect(s, cx + size * 0.5f - o - 1.0f, cy - half, 1.0f, half * 2.0f, color); break;
        }
    }
}

static void text_centered(SDL_Surface *s, float cx, float cy, const char *text,
                          int px, Uint32 color)
{
    if (!text || !text[0]) {
        return;
    }
    int n = (int)strlen(text);
    float w = (float)(n * GLYPH_ADV - 1) * px;
    float h = (float)GLYPH_H * px;
    float x0 = cx - w / 2.0f;
    float y0 = cy - h / 2.0f;

    for (int i = 0; i < n; i++) {
        const uint8_t *rows = glyph_rows(text[i]);
        for (int row = 0; row < GLYPH_H; row++) {
            for (int col = 0; col < GLYPH_W; col++) {
                if (rows[row] & (1u << (GLYPH_W - 1 - col))) {
                    rect(s, x0 + (float)(i * GLYPH_ADV + col) * px,
                         y0 + (float)row * px,
                         (float)px, (float)px, color);
                }
            }
        }
    }
}

static void palette_init(SDL_PixelFormat *fmt)
{
    if (s_pal_ready) {
        return;
    }
    s_pal.bg        = SDL_MapRGB(fmt, 0x0C, 0x11, 0x0E);
    s_pal.rim       = SDL_MapRGB(fmt, 0x22, 0x2C, 0x26);
    s_pal.rim_hi    = SDL_MapRGB(fmt, 0x4A, 0x5D, 0x50);
    s_pal.grille    = SDL_MapRGB(fmt, 0x3A, 0x47, 0x3E);
    s_pal.face      = SDL_MapRGB(fmt, 0x27, 0x30, 0x29);
    s_pal.edge      = SDL_MapRGB(fmt, 0x6E, 0x82, 0x72);
    s_pal.lip       = SDL_MapRGB(fmt, 0x06, 0x09, 0x07);
    s_pal.label     = SDL_MapRGB(fmt, 0xD8, 0xE2, 0xD6);
    s_pal.label_dim = SDL_MapRGB(fmt, 0x8F, 0xA3, 0x91);
    s_pal.p_face    = SDL_MapRGB(fmt, 0x2E, 0x6B, 0x3A);
    s_pal.p_edge    = SDL_MapRGB(fmt, 0x59, 0xD9, 0x5F);
    s_pal.p_label   = SDL_MapRGB(fmt, 0xEA, 0xF6, 0xE7);

    s_pal.call_face   = SDL_MapRGB(fmt, 0x27, 0x4A, 0x2E);
    s_pal.call_edge   = SDL_MapRGB(fmt, 0x59, 0xD9, 0x5F);
    s_pal.call_p_face = SDL_MapRGB(fmt, 0x3E, 0x8C, 0x4C);
    s_pal.call_p_edge = SDL_MapRGB(fmt, 0x7C, 0xFF, 0x84);

    s_pal.end_face    = SDL_MapRGB(fmt, 0x4A, 0x24, 0x24);
    s_pal.end_edge    = SDL_MapRGB(fmt, 0xC0, 0x50, 0x50);
    s_pal.end_p_face  = SDL_MapRGB(fmt, 0x7A, 0x32, 0x32);
    s_pal.end_p_edge  = SDL_MapRGB(fmt, 0xE8, 0x6A, 0x6A);

    s_pal_ready = true;
}

/* ------------------------------------------------------------------ */
/* Button drawing                                                     */
/* ------------------------------------------------------------------ */

static int is_thin(const bezel_button_t *b)
{
    return b->style == BEZEL_STYLE_SIDE || b->style == BEZEL_STYLE_POWER;
}

static void style_colors(uint8_t style, bool pressed,
                         Uint32 *face, Uint32 *edge, Uint32 *label)
{
    switch (style) {
    case BEZEL_STYLE_CALL:
        *face = pressed ? s_pal.call_p_face : s_pal.call_face;
        *edge = pressed ? s_pal.call_p_edge : s_pal.call_edge;
        *label = pressed ? s_pal.p_label : s_pal.label;
        break;
    case BEZEL_STYLE_END:
        *face = pressed ? s_pal.end_p_face : s_pal.end_face;
        *edge = pressed ? s_pal.end_p_edge : s_pal.end_edge;
        *label = pressed ? s_pal.p_label : s_pal.label;
        break;
    default:
        *face = pressed ? s_pal.p_face : s_pal.face;
        *edge = pressed ? s_pal.p_edge : s_pal.edge;
        *label = pressed ? s_pal.p_label : s_pal.label;
        break;
    }
}

static void draw_rect_key(SDL_Surface *dst, const bezel_button_t *b, bool pressed)
{
    int border = is_thin(b) ? 2 : 3;
    int dy = pressed ? 3 : 0;
    Uint32 face, edge, lab;
    style_colors(b->style, pressed, &face, &edge, &lab);

    /* Cap body: flat outlined block with a hard dark lip at the bottom —
     * depth without gradients or blur. */
    rect(dst, b->x, b->y + dy, b->w, b->h - dy, edge);
    rect(dst, b->x + border, b->y + dy + border,
         b->w - 2 * border, b->h - dy - 2 * border, face);
    if (!pressed && !(b->sub && b->sub[0] && b->sub[0] != ' ')) {
        rect(dst, b->x + border, b->y + b->h - border - 4,
             b->w - 2 * border, 4, s_pal.lip);
    }

    float cx = b->x + b->w / 2.0f;
    bool has_sub = (b->sub && b->sub[0] && b->sub[0] != ' ');
    if (has_sub) {
        /* T9 cap: big digit on top, letter cluster below, stacked with
         * explicit offsets so neither collides with the cap edges. */
        text_centered(dst, cx, b->y + dy + 11.0f, b->label, 2, lab);
        text_centered(dst, cx, b->y + dy + 24.0f, b->sub, 1, pressed ? lab : s_pal.label_dim);
    } else {
        float cy = b->y + dy + (b->h - dy) / 2.0f;
        int px = is_thin(b) ? 1 : 2;
        text_centered(dst, cx, cy, b->label, px, lab);
    }
}

static const bezel_button_t *find_by_shape(int shape)
{
    for (size_t i = 0; i < BUTTON_COUNT; i++) {
        if (s_buttons[i].shape == shape) {
            return &s_buttons[i];
        }
    }
    return NULL;
}

/* The d-pad ring, its four arrows and the centre key. `held` is the shape of
 * the key currently pressed (or -1), drawn highlighted. */
static void draw_dpad(SDL_Surface *dst, int held)
{
    const int cx = s_dpad_cx, cy = s_dpad_cy, r = DPAD_R;

    disc(dst, cx, cy, r, s_pal.edge);
    disc(dst, cx, cy, r - 2, s_pal.face);

    /* Arrows sit just inside the ring, pointing outward, clear of the hub. */
    static const struct { int dir; int shape; float ax, ay; } arrows[4] = {
        { 0, BEZEL_SHAPE_ARROW_UP,    0.0f,  -28.0f },
        { 1, BEZEL_SHAPE_ARROW_DOWN,  0.0f,   28.0f },
        { 2, BEZEL_SHAPE_ARROW_LEFT, -28.0f,   0.0f },
        { 3, BEZEL_SHAPE_ARROW_RIGHT, 28.0f,   0.0f },
    };
    for (int i = 0; i < 4; i++) {
        bool on = (held == arrows[i].shape);
        arrow_shape(dst, (float)cx + arrows[i].ax, (float)cy + arrows[i].ay,
                    arrows[i].dir, 14.0f, on ? s_pal.p_edge : s_pal.label);
    }

    /* Centre key: a disc whose diameter is the table entry's w/h, so growing
     * it is a one-row edit. Ring + bright green face while held. */
    const bezel_button_t *ok = find_by_shape(BEZEL_SHAPE_DISC);
    float ok_cx = ok ? (float)ok->x + (float)ok->w / 2.0f : (float)cx;
    float ok_cy = ok ? (float)ok->y + (float)ok->h / 2.0f : (float)cy;
    float ok_r  = ok ? (float)(ok->w < ok->h ? ok->w : ok->h) / 2.0f : 15.0f;

    bool ok_on = (held == BEZEL_SHAPE_DISC);
    disc(dst, ok_cx, ok_cy, ok_r, ok_on ? s_pal.p_edge : s_pal.edge);
    disc(dst, ok_cx, ok_cy, ok_r - 3.0f, ok_on ? s_pal.p_face : s_pal.face);
    text_centered(dst, ok_cx, ok_cy, "OK", ok_r >= 18.0f ? 2 : 1,
                  ok_on ? s_pal.p_label : s_pal.label);
}

void bezel_draw_button(SDL_Surface *dst, size_t idx, bool pressed)
{
    if (!dst || idx >= BUTTON_COUNT) {
        return;
    }
    palette_init(dst->format);
    const bezel_button_t *b = &s_buttons[idx];

    switch (b->shape) {
    case BEZEL_SHAPE_DISC:
    case BEZEL_SHAPE_ARROW_UP:
    case BEZEL_SHAPE_ARROW_DOWN:
    case BEZEL_SHAPE_ARROW_LEFT:
    case BEZEL_SHAPE_ARROW_RIGHT:
        /* The d-pad is one drawing; repaint it with this key held. */
        draw_dpad(dst, pressed ? b->shape : -1);
        break;
    default:
        /* Clear the key's full footprint first (pressed caps shift down). */
        rect(dst, b->x - 1, b->y - 1, b->w + 2, b->h + 2, s_pal.bg);
        draw_rect_key(dst, b, pressed);
        break;
    }
}

void bezel_draw_background(SDL_Surface *dst)
{
    if (!dst) {
        return;
    }
    palette_init(dst->format);

    rect(dst, 0, 0, (float)s_log_w, (float)s_log_h, s_pal.bg);

    /* Recessed panel frame: dark well + a lit inner edge. */
    rect(dst, SIDE_W - 4, TOP_H - 4, (float)(s_pw + 8), (float)(s_ph + 8), s_pal.rim);
    rect(dst, SIDE_W - 1, TOP_H - 1, (float)(s_pw + 2), (float)(s_ph + 2), s_pal.rim_hi);

    /* Earpiece grille. */
    for (int i = 0; i < 9; i++) {
        rect(dst, SIDE_W + s_pw / 2 - 4 * 9 + i * 9, 9, 5, 3, s_pal.grille);
    }

    draw_dpad(dst, -1);

    for (size_t i = 0; i < BUTTON_COUNT; i++) {
        const bezel_button_t *b = &s_buttons[i];
        if (b->shape == BEZEL_SHAPE_RECT) {
            draw_rect_key(dst, b, false);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Geometry / hit testing                                             */
/* ------------------------------------------------------------------ */

/* Largest clean scale that fits: integer 2x when the screen allows it,
 * otherwise 1.5x (a clean 3:2 ratio); other fractions make pixel widths
 * uneven without buying much. The margin covers window decorations and
 * desktop panels, which SDL does not always subtract from "usable". */
static float fit_scale(void)
{
    static const float candidates[] = { 2.0f, 1.5f, 1.0f };
    const float margin = 80.0f;
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(0, &usable) != 0 || usable.h <= 0) {
        return 1.0f;
    }
    for (size_t i = 0; i < sizeof candidates / sizeof candidates[0]; i++) {
        if ((float)s_log_h * candidates[i] + margin <= (float)usable.h) {
            return candidates[i];
        }
    }
    return 1.0f;
}

void bezel_init(int panel_w, int panel_h, float scale)
{
    s_pw = panel_w;
    s_ph = panel_h;
    s_log_w = SIDE_W * 2 + s_pw;
    s_log_h = TOP_H + s_ph + (int)lroundf((float)s_ph * 283.0f / 480.0f);
    bezel_layout(s_pw, s_ph);

    if (scale <= 0.0f) {
        scale = fit_scale();
    }
    if (scale < 0.5f) {
        scale = 0.5f;
    }
    if (scale > 3.0f) {
        scale = 3.0f;
    }
    s_scale = scale;
    s_win_w = (int)lroundf((float)s_log_w * s_scale);
    s_win_h = (int)lroundf((float)s_log_h * s_scale);
}

void bezel_panel_logical(int *w, int *h)
{
    if (w) *w = s_pw;
    if (h) *h = s_ph;
}

float bezel_scale(void) { return s_scale; }
int bezel_window_w(void) { return s_win_w; }
int bezel_window_h(void) { return s_win_h; }

void bezel_panel_rect(int *x, int *y, int *w, int *h)
{
    if (x) *x = (int)lroundf((float)SIDE_W * s_scale);
    if (y) *y = (int)lroundf((float)TOP_H * s_scale);
    if (w) *w = (int)lroundf((float)s_pw * s_scale);
    if (h) *h = (int)lroundf((float)s_ph * s_scale);
}

size_t bezel_button_count(void) { return BUTTON_COUNT; }

const bezel_button_t *bezel_button(size_t idx)
{
    return idx < BUTTON_COUNT ? &s_buttons[idx] : NULL;
}

int bezel_hit(float lx, float ly)
{
    for (size_t i = 0; i < BUTTON_COUNT; i++) {
        const bezel_button_t *b = &s_buttons[i];
        if (lx >= b->x && lx < b->x + b->w && ly >= b->y && ly < b->y + b->h) {
            return (int)i;
        }
    }
    return -1;
}
