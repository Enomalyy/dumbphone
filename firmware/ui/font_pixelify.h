/*
 * font_pixelify.h — baked Pixelify Sans body face (8-bit, plain).
 *
 * Source: Pixelify Sans OFL 1.1 (https://fonts.google.com/specimen/Pixelify+Sans),
 * instantiated at static weight 400, subset to printable ASCII (U+0020..U+007E),
 * and baked to an LVGL 8bpp bitmap font with lv_font_conv --no-compress (see
 * host/assets/fonts/README.md for the exact commands).
 *
 *   lv_font_pixelify_m — 16 px, preview / conversational text (UI_FONT_BODY_M_PX)
 *
 * The large body face (UI_FONT_BODY_L_PX) is VT323 loaded as a TTF by the shell
 * (ui_shell.c) — Eliyahu's call, 26/11: Pixelify for the small letters, VT323
 * for the big ones.
 *
 * The descriptor is const rodata. ui_shell_init() takes a mutable shallow copy
 * and chains Droid Sans Hebrew -> Noto Emoji behind it by writing the copy's
 * `fallback` field (bitmaps/dsc tables stay shared).
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t lv_font_pixelify_m;

#ifdef __cplusplus
}
#endif
