/*
 * ui_theme.c — the default theme's palette: flat charcoal + green + cyan
 * (concept-art/07 design language).
 *
 * Owns the PALETTE so every page resolves its colours from one place.
 * Navigation code never sees these values; it goes through ui_palette().
 *
 * Fonts are deliberately NOT loaded here: the tiny_ttf glyph cache must be
 * created on the UI thread in the shell's LVGL context (see ui_shell.c), and
 * loading them from a separate translation unit left the cache unallocated.
 * Colours are pure appearance data with no LVGL-lifecycle dependency, so they
 * live cleanly in the theme layer.
 */

#include "ui/ui_theme.h"

/* The flat pixel palette (concept-art/07). Populated in ui_theme_init() —
 * lv_color_hex() is not a compile-time constant. */
static ui_palette_t g_pal;

int ui_theme_init(void)
{
    g_pal.bg       = lv_color_hex(0x141B17);  /* charcoal-green            */
    g_pal.bg_alt   = lv_color_hex(0x1D2620);  /* alternating row band      */
    g_pal.sel      = lv_color_hex(0x2E6B3A);  /* selected / sent fill      */
    g_pal.sel_edge = lv_color_hex(0x59D95F);  /* bright green border       */
    g_pal.cyan     = lv_color_hex(0x3FD8E8);  /* active accent             */
    g_pal.text     = lv_color_hex(0xD8E2D6);  /* primary text              */
    g_pal.text_dim = lv_color_hex(0x8FA391);  /* secondary / sage border   */
    g_pal.border   = lv_color_hex(0x77897B);  /* soft-key / tab border     */
    return 0;
}

const ui_palette_t *ui_palette(void) { return &g_pal; }
