/*
 * ui_theme.h — the appearance layer and THE design tokens (ui-architecture.md §5).
 *
 * Two things live here, both appearance-only:
 *
 *   1. the PALETTE: nine ROLES, resolved through ui_color() from the
 *      theme tables in ui_theme.c;
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
 * the renderer resolves appearance through ui_color() + ui_font_*().
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

/* Columns inside a row (relative to the row box). The value/time column is
 * right-aligned; the label column's width is measured per row (see below). */
#define UI_ROW_LABEL_X     62   /* primary line (name / item label)     */
/* The label's WIDTH is not a token: it is measured per row from what the
 * row actually draws to the right of it (ui_menu.c label_width()). A fixed
 * reservation truncated labels that had room to spare. */
#define UI_ROW_SUB_X       62   /* optional second line (preview)       */
#define UI_ROW_SUB_W      242
#define UI_ROW_META_X     200   /* right-aligned value / time column    */
#define UI_ROW_META_W     104   /* right edge = X + W = 304, i.e. 12 px
                                 * inside the row border so the last
                                 * glyph is never clipped              */
#define UI_ROW_BADGE_X    214   /* unread badge "[n]"                   */
#define UI_ROW_BADGE_W     40
#define UI_ROW_RIGHT_PAD    8   /* right edge of every row column      */
#define UI_ROW_COL_GAP      6   /* min gap between the label and the
                                 * first right-hand column present     */

/* A page that has a category strip (the main menu, the chat list) reserves
 * the top of the content area for the icon tray + the category title. */
#define UI_TRAY_Y           2   /* icon tray row                        */
#define UI_TRAY_CELL_W     28
#define UI_TRAY_CELL_H     26
/* §14.5: the highlighted tray cell sits slightly larger than its
 * neighbours — same centre point, bigger box, bigger icon face. */
#define UI_TRAY_CELL_HI_W  34
#define UI_TRAY_CELL_HI_H  30
#define UI_TITLE_Y         28   /* centered category title row          */
#define UI_ROWS_Y          54   /* rows start (page HAS a strip)        */

/* Grid layout (LAYOUT_GRID): the classic 3-wide app grid, one cell per item
 * in reading order. Both layouts render the same `items` data. */
#define UI_GRID_COLS        3
#define UI_GRID_CELL_W ((ui_geom_w()) / UI_GRID_COLS)
#define UI_GRID_ROW_H      84   /* icon + up-to-two-line label         */
#define UI_CELL_ICON_GAP   8    /* §14.6: gap between a cell's icon and
                                  its label (both centred as one block) */

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
#define UI_HOME_HINT_BOTTOM  46   /* key affordance line, from the bottom
                                   * edge (identity at 320x480: 418-46) */

/* Conversation page (spec §14.7): a header slot over a scrollable bubble
 * column. Bubble widths are derived (UI_PANEL_W - 2*UI_BUBBLE_INSET_X);
 * text line heights come from the font at build time, not tokens. */
#define UI_CONV_HDR_H       34   /* chat name + divider line            */
#define UI_CONV_CARET_X      6   /* back-caret glyph, from the left     */
#define UI_CONV_CARET_Y     10   /* back-caret glyph, from the top      */
#define UI_CONV_NAME_X      24   /* chat name label, from the left      */
#define UI_CONV_NAME_Y       2   /* chat name label, from the top       */
#define UI_BUBBLE_INSET_X    6   /* screen margin on each side          */
#define UI_BUBBLE_PAD        6   /* inner padding of a bubble           */
#define UI_BUBBLE_RADIUS     2   /* nearly square: the UI's design      */
                                  /* language is radius 0 (rows, cards, */
                                  /* sidebar); bubbles keep a hint of  */
                                  /* softness                          */
#define UI_BUBBLE_GAP_SAME   3   /* between messages in one group       */
#define UI_BUBBLE_GAP_GROUP  8   /* between groups                      */
#define UI_TICK_W            3   /* delivery-mark width (px)            */
#define UI_TICK_H            6   /* delivery-mark height: twice the     */
                                  /* width, touching the bubble's      */
                                  /* bottom edge                       */
#define UI_TICK_GAP          2   /* gap between the two marks           */

/* ================================================================== */
/* Design tokens — font sizes (pixels)                                 */
/* ================================================================== */

#define UI_FONT_CHROME_PX    16   /* Silkscreen: status bar, soft keys  */
#define UI_FONT_CHROME_S_PX  13   /* Silkscreen: tabs, row meta values  */
#define UI_FONT_TITLE_PX     24   /* Silkscreen: page / category title  */
#define UI_FONT_BODY_L_PX    24   /* VT323: primary list text           */
#define UI_FONT_BODY_M_PX    16   /* Pixelify Sans 8bpp: preview/second.*/
#define UI_FONT_ICON_PX      22   /* Font Awesome: icons                */
#define UI_FONT_ICON_L_PX    26   /* §14.5: highlighted tray icon       */
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
/* Palette — the nine ROLES (ui-architecture.md §14.10)                 */
/* ================================================================== */

/* Painters ask for a ROLE, never a colour: "this label is primary text",
 * "this box is a frame", "this row is selected". The role -> colour
 * mapping lives in ONE data table in ui_theme.c, which is the only place
 * in the UI tree allowed to contain a colour literal. Adding a theme = one
 * more column of nine values; nothing else changes.
 *
 * Contrast rule (checked when a palette is added, at palette level):
 *   TEXT on GROUND / SURFACE, TEXT_DIM on GROUND / SURFACE,
 *   ON_HIGHLIGHT on HIGHLIGHT   — all >= 4.5:1. */
typedef enum {
    UI_ROLE_GROUND = 0,   /* page background                          */
    UI_ROLE_SURFACE,      /* rows, cells, panels (alternating band)    */
    UI_ROLE_BORDER,       /* frames, rails, dividers                   */
    UI_ROLE_TEXT,         /* primary text                              */
    UI_ROLE_TEXT_DIM,     /* meta values, timestamps, previews         */
    UI_ROLE_ACCENT,       /* icons, active tab, chevrons               */
    UI_ROLE_HIGHLIGHT,    /* selection fill                            */
    UI_ROLE_ACCENT_ALT,   /* secondary accent: the selection edge      */
    UI_ROLE_ON_HIGHLIGHT, /* anything drawn ON a highlight fill        */
    UI_ROLE_COUNT
} ui_role_t;

typedef struct {
    lv_color_t c[UI_ROLE_COUNT];
} ui_palette_t;

/* The named themes. Order is the Theme submenu's list order (spec §14.11). */
typedef enum {
    UI_THEME_DARK = 0,   /* charcoal-green ground, green highlight   */
    UI_THEME_LIGHT,      /* white ground, bright-orange highlight    */
    UI_THEME_AMBER,      /* warm sepia ground, gold accent           */
    UI_THEME_SLATE,      /* cold slate-blue ground, steel accent     */
    UI_THEME_MONO,       /* near-black ground, light-grey highlight  */
    UI_THEME_COUNT
} ui_theme_id_t;

/* Resolve the palette of the current theme. Call once on the UI thread
 * before building any page. */
int ui_theme_init(void);

/* The ONE colour accessor. Painters call ui_color(UI_ROLE_TEXT) etc. */
lv_color_t ui_color(ui_role_t role);

/* Switch theme by id. Resolves the new palette and then runs the shell's
 * registered apply pass (ui_theme_on_apply), which re-applies the styles of
 * every built object — so a switch is visible immediately, no restart and no
 * rebuild of the page tree. No-op when the theme is already active. */
void ui_theme_set(ui_theme_id_t id);

ui_theme_id_t ui_theme_get(void);
const char *ui_theme_name(ui_theme_id_t id);
const char *ui_role_name(ui_role_t role);

/* Register the ONE apply pass (the shell owns it: it knows every built
 * page). ui_theme_set() and ui_font_pref_next() both run it, so a colour or
 * a face/size change reaches every built object through the same path. */
void ui_appearance_on_apply(void (*cb)(void));

/* Run the registered apply pass now (the shell uses it after its own
 * appearance changes, e.g. re-laying out for the softkey bar). */
void ui_appearance_apply(void);

/* ================================================================== */
/* Font preferences — three roles, face + size (spec §14.12)           */
/* ================================================================== */

/* The three levels of abstraction the UI actually has. They are the ONLY
 * adjustable faces: the chrome face (status bar, soft keys) and the icon
 * face stay fixed — chrome is furniture, not text. */
typedef enum {
    UI_FONT_HEADER = 0,   /* page + category titles        */
    UI_FONT_TEXT,         /* primary list text, row labels */
    UI_FONT_SUBTEXT,      /* previews, meta values, values */
    UI_FONT_ROLE_COUNT
} ui_font_role_t;

/* Faces available to the three roles. All are OFL and load from the assets
 * font directory at runtime (no baked tables except the pixel-perfect
 * Pixelify 16 the UI was designed on — see font_pixelify.h). Removing a face
 * is not a preference: §14.12 keeps every candidate available so the trade
 * can be judged on the device. */
typedef enum {
    UI_FACE_VT323 = 0,
    UI_FACE_SILKSCREEN,
    UI_FACE_PIXELIFY,
    UI_FACE_JERSEY,
    UI_FACE_DOTGOTHIC,
    UI_FACE_COUNT
} ui_face_t;

typedef struct {
    uint8_t face;   /* ui_face_t */
    uint8_t px;
} ui_font_pref_t;

/* Current preference of a role (data only; the shell resolves it to a font). */
ui_font_pref_t ui_font_pref(ui_font_role_t role);

/* Advance the role to the next entry of its test ladder (face + size
 * together — the final picker will split the two; until then one CENTER is
 * one visible step). Runs the apply pass, so the change is immediate. */
void ui_font_pref_next(ui_font_role_t role);

/* The LVGL face behind a role (defined in the shell, which owns the font
 * objects). Renderers that draw text in a specific level call this. */
lv_font_t *ui_font_of(ui_font_role_t role);

/* Display strings: "Headers", "Text", "Subtext"; "VT323" / "SILK" / ... */
const char *ui_font_role_name(ui_font_role_t role);
const char *ui_face_name(ui_face_t face);    /* full name ("Silkscreen")  */
const char *ui_face_short(ui_face_t face);   /* meta column ("SILK")      */
const char *ui_face_file(ui_face_t face);    /* asset TTF, NULL = baked   */

/* Font accessors — declared here (the appearance API) but defined in the
 * shell, which owns the LVGL font objects and their lifecycle. The renderer
 * (ui_menu.c) resolves every face through these so it stays theme-agnostic. */
lv_font_t *ui_font_chrome(void);    /* Silkscreen 16 — status bar, soft keys */
lv_font_t *ui_font_chrome_s(void);  /* Silkscreen 13 — category tabs         */
lv_font_t *ui_font_title(void);     /* HEADER role   — page titles           */
lv_font_t *ui_font_body_l(void);    /* TEXT role     — primary list text     */
lv_font_t *ui_font_body_m(void);    /* SUBTEXT role  — previews/second line  */
lv_font_t *ui_font_icon(void);      /* Font Awesome solid 22                 */
lv_font_t *ui_font_icon_l(void);    /* Font Awesome solid 26 (§14.5 hi cell) */
lv_font_t *ui_font_icon_s(void);    /* Font Awesome solid 13 (carets)        */

#ifdef __cplusplus
}
#endif
