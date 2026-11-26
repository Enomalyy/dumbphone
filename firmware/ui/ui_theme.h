/*
 * ui_theme.h — the appearance layer and THE design tokens (ui-architecture.md §5).
 *
 * Two things live here, both appearance-only:
 *
 *   1. the PALETTE (colours), resolved through ui_palette();
 *   2. the DESIGN TOKENS: every shared visual measurement — panel and frame
 *      geometry, list/menu metrics, column positions, sidebar metrics and the
 *      pixel size of every font face — as named UI_* constants.
 *
 * "Make all menus smaller" or "make all fonts bigger" is ONE edit in this
 * file. Renderers never carry their own copies of these numbers: ui_menu.c,
 * ui_sidebar.c, ui_leaf.c and ui_shell.c alias the tokens they use (see the
 * geometry block at the top of each).
 *
 * A theme changes APPEARANCE only. It never touches layout semantics or key
 * bindings: navigation data (menu_t / keymap_t) never references the theme,
 * the renderer resolves appearance through ui_palette() + ui_font_*().
 *
 * The default theme is the flat pixel look from concept-art/07: charcoal-green
 * background, cyan as the active accent, green for selected fills, thin sage
 * borders. No gradients, no shadows — whole-pixel primitives only.
 *
 * Font LIFECYCLE is owned by the shell (ui_shell.c): the tiny_ttf glyph cache
 * must be created on the UI thread in the shell's LVGL context. The SIZES are
 * tokens here; the shell loads the faces at those sizes when it starts.
 */
#pragma once

#include "lvgl.h"
#include "ui/ui_geometry.h"   /* runtime panel geometry (spec §15) */

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/* Design tokens — geometry                                            */
/* ================================================================== */

/* Panel + stable frame (ui-architecture.md §1: status bar pinned top,
 * action bar pinned bottom; only the content area swaps).
 *
 * The panel size and the derived bar/content/row metrics are RUNTIME
 * values from ui_geometry.c — the single source of truth (spec §15 /
 * issue #1). The macros expand to function calls: usable anywhere a value
 * is needed, never as array sizes or compile-time constants. Every formula
 * is identity at the default 320x480, so the default build is unchanged. */
#define UI_PANEL_W        ui_geom_w()
#define UI_PANEL_H        ui_geom_h()
#define UI_STATUS_H       ui_status_h()    /* status bar height         */
#define UI_SOFTKEY_H      ui_softkey_h()   /* soft-key action bar height */

/* Softkey caps (spec §12.2): one per mirrored physical key — left under
 * SHOULDER-L, centre under OK, right under SHOULDER-R. */
#define UI_SK_CAP_W       98   /* cap width                            */
#define UI_SK_CAP_PITCH  104   /* cap left-edge to cap left-edge       */
#define UI_SK_CAP_INSET    6   /* first cap's inset from the bar edge  */
#define UI_SK_CAP_TOP      5   /* cap top inset within the bar         */
#define UI_CONTENT_Y  ui_content_y()
#define UI_CONTENT_H  ui_content_h()

/* List pages — the generic menu_t renderer (ui_menu.c). A row is the unit
 * of every list page: the chat list, the settings tree, the config
 * sub-pages, call history and contacts all use it. */
#define UI_ROW_H           ui_row_h()   /* one list row (runtime)       */
#define UI_ROW_INSET_X      4   /* row inset from the panel's left edge */
#define UI_ROW_INSET_W      8   /* row width = UI_PANEL_W - this        */

/* Columns inside a row (relative to the row box). The name column stops
 * where the badge column starts; the value/time column is right-aligned. */
#define UI_ROW_LABEL_X     62   /* primary line (name / item label)     */
#define UI_ROW_LABEL_W    142   /* ends where the value column starts  */
#define UI_ROW_SUB_X       62   /* optional second line (preview)       */
#define UI_ROW_SUB_W      242
#define UI_ROW_META_X     200   /* right-aligned value / time column    */
#define UI_ROW_META_W     104   /* right edge = X + W = 304, i.e. 12 px
                                 * inside the row border so the last
                                 * glyph is never clipped              */
#define UI_ROW_BADGE_X    214   /* unread badge "[n]"                   */
#define UI_ROW_BADGE_W     40

/* A page that has a category strip (the main menu, the chat list) reserves
 * the top of the content area for the icon tray + the category title. */
#define UI_TRAY_Y           2   /* icon tray row                        */
#define UI_TRAY_CELL_W     28
#define UI_TRAY_CELL_H     26
#define UI_TITLE_Y         28   /* centered category title row          */
#define UI_ROWS_Y          54   /* rows start (page HAS a strip)        */
#define UI_ROWS_Y_NC        2   /* rows start (page has NO strip)       */

/* Grid layout (LAYOUT_GRID): the classic 3-wide app grid, one cell per item
 * in reading order. Both layouts render the same `items` data. */
#define UI_GRID_COLS        3
#define UI_GRID_CELL_W ((ui_geom_w()) / UI_GRID_COLS)
#define UI_GRID_ROW_H      84   /* icon + up-to-two-line label         */

/* Options sidebar overlay (ui_sidebar.c). */
#define UI_SIDEBAR_W      ui_sidebar_w()     /* runtime               */
#define UI_SIDEBAR_ROWS_Y  34
#define UI_SIDEBAR_ROW_H  ui_sidebar_row_h() /* runtime               */
#define UI_SIDEBAR_ROWS_MAX 8   /* more than any page's table holds    */

/* HOME screen (ui_home.c): the widget row + wallpaper motif. The card
 * width works out whole with these insets/gaps: (320 - 2*11 - 2*5)/3 = 96,
 * so the row is symmetric (11 px clear on each side). */
#define UI_HOME_WIDGET_Y     18   /* widget row top                    */
#define UI_HOME_WIDGET_H     54   /* one card: label over value        */
#define UI_HOME_WIDGET_INSET 11   /* from the panel edges              */
#define UI_HOME_WIDGET_GAP    5   /* between cards                     */
#define UI_HOME_CARD_W ui_home_card_w()
#define UI_HOME_MOTIF_Y     150   /* wallpaper phone glyph, centred    */
#define UI_HOME_HINT_Y      372   /* key affordance line above the bar */

/* ================================================================== */
/* Design tokens — font sizes (pixels)                                 */
/* ================================================================== */

#define UI_FONT_CHROME_PX    16   /* Silkscreen: status bar, soft keys  */
#define UI_FONT_CHROME_S_PX  13   /* Silkscreen: tabs, row meta values  */
#define UI_FONT_TITLE_PX     24   /* Silkscreen: page / category title  */
#define UI_FONT_BODY_L_PX    24   /* VT323: primary list text           */
#define UI_FONT_BODY_M_PX    16   /* Pixelify Sans 8bpp: preview/second.*/
#define UI_FONT_ICON_PX      22   /* Font Awesome: icons                */
#define UI_FONT_ICON_S_PX    13   /* Font Awesome: carets/chevrons      */

/* Fallback faces for every text size (ui_text.h decides direction; the
 * shell loads these and chains them behind the pixel faces, so Latin never
 * leaves Silkscreen/VT323/Pixelify Sans). Hebrew: Droid Sans Hebrew
 * (Apache-2.0, 30,024 B).
 * Emoji: monochrome Noto Emoji subset (OFL, 753,788 B) - see the fonts README
 * for the subsetting recipe and the S3 flash note. */
#define UI_FONT_HEBREW_FILE  "DroidSansHebrew-Regular.ttf"
#define UI_FONT_EMOJI_FILE   "NotoEmoji-Subset.ttf"

/* ================================================================== */
/* Palette                                                             */
/* ================================================================== */

typedef struct {
    lv_color_t bg;        /* charcoal-green background       */
    lv_color_t bg_alt;    /* alternating row band            */
    lv_color_t sel;       /* selected row / sent fill (green)*/
    lv_color_t sel_edge;  /* bright green border             */
    lv_color_t cyan;      /* active accent: carets, icons, tab */
    lv_color_t text;      /* primary text                    */
    lv_color_t text_dim;  /* secondary text / sage card border */
    lv_color_t border;    /* soft-key / inactive-tab border  */
} ui_palette_t;

/* Set the palette. Call once on the UI thread before building any page.
 *
 * NOTE (themes on the fly): the palette is read when a page is BUILT, so
 * swapping it needs the built pages' styles re-applied (a rebuild pass).
 * Navigation never references colours, so a theme swap cannot break
 * routing — see ui-architecture.md §5. */
int ui_theme_init(void);

const ui_palette_t *ui_palette(void);

/* Font accessors — declared here (the appearance API) but defined in the
 * shell, which owns the LVGL font objects and their lifecycle. The renderer
 * (ui_menu.c) resolves every face through these so it stays theme-agnostic. */
lv_font_t *ui_font_chrome(void);    /* Silkscreen 16 — status bar, soft keys */
lv_font_t *ui_font_chrome_s(void);  /* Silkscreen 13 — category tabs         */
lv_font_t *ui_font_title(void);     /* Silkscreen 24 — page titles           */
lv_font_t *ui_font_body_l(void);    /* VT323 24 — primary list text           */
lv_font_t *ui_font_body_m(void);    /* Pixelify Sans 8bpp 16 — preview/second.*/
lv_font_t *ui_font_icon(void);      /* Font Awesome solid 22                 */
lv_font_t *ui_font_icon_s(void);    /* Font Awesome solid 13 (carets)        */

#ifdef __cplusplus
}
#endif
