/*
 * ui_menu.c — the generic list-page renderer behind menu_t.
 *
 * One code path renders every list-like page. The MAIN MENU is this renderer
 * with a populated category strip, laid out after the classic SMS-menu concept
 * (concept-art/03): an icon tray across the top (one icon per category, the
 * active one highlighted), a centered category title flanked by << >> chevrons
 * as the L/R cycling affordance, and a numbered item list below it with a
 * full-width highlight bar on the selected row. Plain pages are the same
 * renderer with `cats == NULL`.
 *
 * Appearance comes from the theme layer (ui_theme.h): charcoal background,
 * cyan active accent, green selected fill, thin sage card borders — flat,
 * whole-pixel, no gradients (concept-art/07 design language).
 */

#include "ui/ui_menu.h"
#include "ui/ui_pages.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"

/* Geometry — every value is a design token (ui_theme.h, the ONE place to
 * change any shared measurement). These aliases keep the call sites below
 * short; they carry no numbers of their own. */
#define PANEL_W       UI_PANEL_W
#define CONTENT_H     UI_CONTENT_H
#define TRAY_Y        UI_TRAY_Y
#define TITLE_Y       UI_TITLE_Y
#define ROWS_Y        UI_ROWS_Y     /* item rows start (with a strip)     */
#define ROW_H         UI_ROW_H
#define ROW_INSET_X   UI_ROW_INSET_X
#define ROW_INSET_W   UI_ROW_INSET_W
#define LABEL_X       UI_ROW_LABEL_X
#define SUB_X         UI_ROW_SUB_X
#define SUB_W         UI_ROW_SUB_W
#define META_X        UI_ROW_META_X
#define META_W        UI_ROW_META_W
#define BADGE_X       UI_ROW_BADGE_X
#define BADGE_W       UI_ROW_BADGE_W
#define RIGHT_PAD     UI_ROW_RIGHT_PAD
#define COL_GAP       UI_ROW_COL_GAP

/* Colours are ROLES (ui_theme.h); the theme table owns the values. */
#define C_GROUND      ui_color(UI_ROLE_GROUND)
#define C_SURFACE     ui_color(UI_ROLE_SURFACE)
#define C_BORDER      ui_color(UI_ROLE_BORDER)
#define C_TEXT        ui_color(UI_ROLE_TEXT)
#define C_TEXT_DIM    ui_color(UI_ROLE_TEXT_DIM)
#define C_ACCENT      ui_color(UI_ROLE_ACCENT)
#define C_HIGHLIGHT   ui_color(UI_ROLE_HIGHLIGHT)
#define C_ACCENT_ALT  ui_color(UI_ROLE_ACCENT_ALT)
#define C_ON_HL       ui_color(UI_ROLE_ON_HIGHLIGHT)

/* Grid layout (LAYOUT_GRID): the classic 3-wide app grid, one cell per
 * item in reading order. Both layouts render the same `items` data. */
#define GRID_COLS    UI_GRID_COLS
#define GRID_CELL_W  UI_GRID_CELL_W
#define GRID_ROW_H   UI_GRID_ROW_H

#define GLYPH_CARET_R u8"\uF0DA"   /* row selection caret          */
#define GLYPH_CHEV_L  u8"\uF100"   /* << chevrons-left             */
#define GLYPH_CHEV_R  u8"\uF101"   /* >> chevrons-right            */

/* The static pool of rendered list pages — sized from the page registry
 * (ui_pages.h), not counted by hand: a page that is not in the table cannot
 * be reached, so the pool can never be under-counted (spec §16.3). Non-list
 * pages (HOME, stub, lock) don't take slots; the spare capacity is deliberate
 * headroom. Pages are built once at startup and live for the process's
 * lifetime — this is the whole app's page budget. */
#define LIST_POOL UI_PAGE_COUNT

struct ui_list_page {
    menu_t   *m;
    lv_obj_t *root;
    lv_obj_t *cells[MENU_MAX_CATS];      /* icon-tray highlight boxes  */
    lv_obj_t *cell_icon[MENU_MAX_CATS];
    lv_obj_t *title_lbl;                 /* centered category title    */
    lv_obj_t *chev_l, *chev_r;           /* << >> cycling chevrons     */
    lv_obj_t *rows_box;                  /* scrollable row area        */
    ui_list_gesture_cb gesture_cb;       /* horizontal-swipe handler   */
    ui_list_activate_cb activate_cb;     /* tap-activate handler       */
    lv_obj_t *rows[MENU_MAX_ITEMS];
    lv_obj_t *row_caret[MENU_MAX_ITEMS];
    lv_obj_t *row_num[MENU_MAX_ITEMS];
    lv_obj_t *row_icon[MENU_MAX_ITEMS];
    lv_obj_t *row_lbl[MENU_MAX_ITEMS];
    lv_obj_t *row_sub[MENU_MAX_ITEMS];   /* optional second line       */
    lv_obj_t *row_meta[MENU_MAX_ITEMS];  /* optional right-hand value  */
    lv_obj_t *row_badge[MENU_MAX_ITEMS]; /* optional unread badge      */
};

static list_page_t g_pool[LIST_POOL];
static int         g_pool_n = 0;

/* ------------------------------------------------------------------ */
/* Where the rows start: the header slot is the title's own line height, so a
 * taller HEADERS face pushes the rows down instead of clipping the banner.
 * At the shipped preference this equals UI_ROWS_Y exactly, so no frame moves
 * (Eli, 27/11). */
static int rows_y(void)
{
    /* The banner sits in a slot that follows the HEADERS size: one pixel of
     * extra row space per pixel the preference is bigger than the shipped
     * size. At the default the arithmetic is exactly UI_ROWS_Y, so no frame
     * moves; at a 32 px header the rows step down by eight and the banner is
     * never clipped (Eli, 27/11). Using the preference rather than the face's
     * line height keeps it deterministic when a shot run starts with --font. */
    int px = ui_font_pref(UI_FONT_HEADER).px;
    int extra = px - UI_FONT_TITLE_PX;
    return ROWS_Y + (extra > 0 ? extra : 0);
}

/* Small label helpers                                                 */
/* ------------------------------------------------------------------ */

static lv_obj_t *mk_label(lv_obj_t *parent, const char *text, lv_font_t *font,
                          lv_color_t color, int x, int y, int w)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(lbl, x, y);
    lv_obj_set_width(lbl, w);
    return lbl;
}

static lv_obj_t *mk_center_label(lv_obj_t *parent, const char *text, lv_font_t *font,
                                 lv_color_t color, int y)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, y);
    return lbl;
}

/* A DATA-bearing one-line label: chat names, previews, contact names and
 * live values go through ui_text_set(), which also picks the base direction
 * (ui_text.h). Chrome labels keep mk_label(). */
static lv_obj_t *mk_data_line_label(lv_obj_t *parent, const char *text,
                                    lv_font_t *font, lv_color_t color,
                                    int x, int y, int w, int h)
{
    lv_obj_t *lbl = mk_label(parent, "", font, color, x, y, w);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(lbl, h);
    ui_text_set(lbl, text);
    /* An RTL base direction makes LVGL's AUTO alignment right-align the
     * paragraph, which would slide a Hebrew name out of its column and up
     * against the time. The direction still orders the runs; the column
     * grid stays left-anchored for every language. (meta overrides this
     * with RIGHT below.) */
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
    return lbl;
}

/* The face a row's label is drawn in: its own level when it names one (the
 * Font page), otherwise the page default, TEXT. */
static lv_font_t *item_label_font(uint8_t role)
{
    switch (role) {
    case MENU_FONT_HEADER:  return ui_font_of(UI_FONT_HEADER);
    case MENU_FONT_SUBTEXT: return ui_font_of(UI_FONT_SUBTEXT);
    case MENU_FONT_TEXT:    return ui_font_of(UI_FONT_TEXT);
    default:                return ui_font_body_l();
    }
}

/* ------------------------------------------------------------------ */
/* Row column sizing — measured, not reserved                          */
/* ------------------------------------------------------------------ */

/* Pixel width of `s` drawn on ONE line in `font`. Used to give a row's
 * label exactly the space the row's other columns leave it, in those
 * columns' own fonts (so a font change in Settings re-measures too). */
static int text_px(const char *s, const lv_font_t *font)
{
    lv_point_t sz = { 0, 0 };
    lv_text_get_size(&sz, s, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return sz.x;
}

/* How much of a `row_w`-wide row the label column may use.
 *
 * Rule: a row reserves only what it actually draws. No value and no badge
 * -> the label runs to the row's right pad. A value and/or a badge -> the
 * label stops COL_GAP short of the FIRST right-hand column, positioned from
 * that column's own measured width. The value column is still right-aligned
 * on META_X + META_W for every row (values stay in one column); what stops
 * is the over-reservation: a fixed 142 px label box truncated a 15-character
 * row label ("Call Prefer...") even when the row's value sat far to the
 * right of it. The ellipsis is now only for text that genuinely does not
 * fit beside what the row draws. */
static int label_width(const menu_item_t *it, int row_w)
{
    int right = row_w - RIGHT_PAD;

    if (it->meta && it->meta[0]) {
        int mx = META_X + META_W - text_px(it->meta, ui_font_chrome_s());
        if (mx < META_X) {
            mx = META_X;   /* over-long value: stays clipped at its box   */
        }
        if (mx < right) {
            right = mx;
        }
    }
    if (it->badge > 0) {
        char b[8];
        snprintf(b, sizeof b, "[%d]", it->badge);
        int bx = BADGE_X + BADGE_W - text_px(b, ui_font_chrome_s());
        if (bx < right) {
            right = bx;
        }
    }

    int w = right - COL_GAP - LABEL_X;
    return w > 16 ? w : 16;
}

/* ------------------------------------------------------------------ */
/* Styling — appearance resolved through the theme                     */
/* ------------------------------------------------------------------ */

static void style_cell(list_page_t *p, int idx)
{
    menu_t *m = p->m;
    bool sel = (idx == m->cat_sel);

    /* Active category: accent border box + accent icon (picture 7). */
    lv_obj_set_style_border_color(p->cells[idx], sel ? C_ACCENT : C_BORDER, 0);
    lv_obj_set_style_text_color(p->cell_icon[idx], sel ? C_ACCENT : C_TEXT_DIM, 0);
}

/* The ONE shared "paint a row" helper (declared in ui_menu.h): the base
 * look every list-style row wears — banded fill, green selection with its
 * bright edge, flat corners. The renderer and the options sidebar both go
 * through it, so the two can never drift apart. Text/icon colour is the
 * caller's business (the sidebar re-styles its own labels on selection). */
void ui_row_style(lv_obj_t *row, int idx, bool sel)
{
    /* Unselected rows read as flat cards: a GROUND/SURFACE band fill
     * (alternating) with a thin BORDER frame. Selected: HIGHLIGHT fill with
     * a 2 px ACCENT_ALT frame. */
    lv_color_t band = (idx % 2) ? C_SURFACE : C_GROUND;
    lv_obj_set_style_bg_color(row, sel ? C_HIGHLIGHT : band, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, sel ? C_ACCENT_ALT : C_BORDER, 0);
    lv_obj_set_style_border_width(row, sel ? 2 : 1, 0);
    lv_obj_set_style_radius(row, 0, 0);
}

static void style_row(list_page_t *p, int idx)
{
    menu_t *m = p->m;
    bool sel = (idx == m->sel);
    lv_obj_t *row = p->rows[idx];
    if (!row) {
        return;
    }

    ui_row_style(row, idx, sel);

    /* Ink follows the fill: everything drawn ON a selected row is
     * ON_HIGHLIGHT — primary text, the dim second line/meta and the accent
     * badge alike. That is what makes a selection legible in BOTH palettes
     * (black on the light theme's orange, off-white on the dark theme's
     * green); TEXT_DIM on a highlight was 2.5:1 in the dark palette. */
    lv_color_t ink     = sel ? C_ON_HL : C_TEXT;
    lv_color_t ink_dim = sel ? C_ON_HL : C_TEXT_DIM;
    lv_color_t ink_acc = sel ? C_ON_HL : C_ACCENT;

    if (p->row_lbl[idx])   lv_obj_set_style_text_color(p->row_lbl[idx], ink, 0);
    if (p->row_icon[idx])  lv_obj_set_style_text_color(p->row_icon[idx], ink, 0);
    if (p->row_num[idx])   lv_obj_set_style_text_color(p->row_num[idx], ink_dim, 0);
    if (p->row_sub[idx])   lv_obj_set_style_text_color(p->row_sub[idx], ink_dim, 0);
    if (p->row_meta[idx])  lv_obj_set_style_text_color(p->row_meta[idx], ink_dim, 0);
    if (p->row_badge[idx]) lv_obj_set_style_text_color(p->row_badge[idx], ink_acc, 0);

    lv_obj_t *caret = p->row_caret[idx];
    if (!caret) {
        return;
    }
    lv_obj_set_style_text_color(caret, ink_acc, 0);
    if (sel) {
        lv_obj_remove_flag(caret, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(caret, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ------------------------------------------------------------------ */
/* Row rebuild — recreates the item rows from the current `items`      */
/* ------------------------------------------------------------------ */

/* Tap a row: touch is the universal fallback for every key action
 * (spec §1). The page pointer rides in user_data; the index is found by
 * matching the target object against the row table (no allocation).
 *
 * Select on RELEASE, not PRESS: a drag that starts on a row and then
 * scrolls (spec §1) must not select it — during a scroll the rows move with
 * the finger, so at release the finger is often over the same row again and
 * RELEASED fires on it. press_moved distinguishes the two: it is set once
 * any indev read moves more than the 10 px scroll limit.
 *
 * A tap is a highlight AND a press (issue #13): select, then hand the page
 * to its activate callback — the shell's, which runs the same activation
 * CENTER would. The renderer does not know what activation means; and the
 * callback may rebuild or replace this page, so p and its rows are not
 * touched after it returns. */
static void on_row_tap(lv_event_t *e)
{
    if (lv_indev_get_press_moved(lv_event_get_indev(e))) {
        return;   /* it was a drag, not a tap */
    }
    list_page_t *p = lv_event_get_user_data(e);
    lv_obj_t *t = lv_event_get_target(e);
    for (int i = 0; i < MENU_MAX_ITEMS; i++) {
        if (p->rows[i] == t) {
            ui_list_select(p, i);
            if (p->activate_cb) {
                p->activate_cb(p, i);
            }
            return;
        }
    }
}

/* Keep the selected row inside the visible part of the scrollable row area.
 * A list shorter than the screen never scrolls, so this is a no-op there. */
static void ensure_visible(list_page_t *p)
{
    menu_t *m = p ? p->m : NULL;
    if (!p || !m || m->sel >= m->n_items || !p->rows[m->sel]) {
        return;
    }
    lv_obj_scroll_to_view(p->rows[m->sel], LV_ANIM_OFF);
}

static void rebuild_rows(list_page_t *p)
{
    menu_t *m = p->m;

    for (int i = 0; i < MENU_MAX_ITEMS; i++) {
        if (p->rows[i]) {
            lv_obj_del(p->rows[i]);   /* children (caret/num/icon/lbl) go too */
        }
        p->rows[i] = p->row_caret[i] = p->row_num[i] = NULL;
        p->row_icon[i] = p->row_lbl[i] = p->row_sub[i] = NULL;
        p->row_meta[i] = p->row_badge[i] = NULL;
    }

    /* The row arrays are MENU_MAX_ITEMS wide; a list longer than that
     * (bad data) drops its overflow rows instead of writing past them. */
    for (int i = 0; i < m->n_items && i < MENU_MAX_ITEMS; i++) {
        const menu_item_t *it = &m->items[i];
        if (!it->label) {
            continue;   /* empty slot: leave the row blank */
        }

        /* Geometry per layout: full-width rows, or one 3-wide grid cell in
         * reading order. The tap handler and selection styling below are
         * layout-agnostic — they work off p->rows[i] either way. */
        int x, y, w, h;
        if (m->layout == LAYOUT_GRID) {
            x = (i % GRID_COLS) * GRID_CELL_W + 4;
            y = (i / GRID_COLS) * GRID_ROW_H + 3;
            w = GRID_CELL_W - 8;
            h = GRID_ROW_H - 6;
        } else {
            x = ROW_INSET_X;
            y = i * ROW_H;
            w = PANEL_W - ROW_INSET_W;
            h = ROW_H - 3;
        }
        lv_obj_t *row = lv_obj_create(p->rows_box);
        p->rows[i] = row;
        lv_obj_set_size(row, w, h);
        lv_obj_set_pos(row, x, y);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        /* Select on release: drags that start on the row scroll instead
         * (spec §1) and never select it. */
        lv_obj_add_event_cb(row, on_row_tap, LV_EVENT_RELEASED, p);

        if (m->layout == LAYOUT_GRID) {
            /* Cell: icon centered top, label centered below (wraps to two
             * lines). No number/caret — the highlight box IS the selection. */
            lv_obj_t *ic = mk_label(row, it->icon, ui_font_icon(), C_TEXT,
                                    0, 6, w);
            lv_obj_set_style_text_align(ic, LV_TEXT_ALIGN_CENTER, 0);
            p->row_icon[i] = ic;

            lv_obj_t *lb = lv_label_create(row);
            lv_obj_set_style_text_font(lb, ui_font_body_m(), 0);
            lv_obj_set_style_text_color(lb, C_TEXT, 0);
            lv_obj_remove_flag(lb, LV_OBJ_FLAG_CLICKABLE);
            lv_label_set_long_mode(lb, LV_LABEL_LONG_MODE_WRAP);
            lv_obj_set_pos(lb, 4, 36);
            lv_obj_set_size(lb, w - 8, h - 36);
            lv_obj_set_style_text_align(lb, LV_TEXT_ALIGN_CENTER, 0);
            ui_text_set(lb, it->label);
            p->row_lbl[i] = lb;
        } else {
            char num[8];
            snprintf(num, sizeof num, "%d.", i + 1);
            p->row_caret[i] = mk_label(row, GLYPH_CARET_R, ui_font_icon_s(), C_ACCENT,
                                       1, 18, 12);
            p->row_num[i]   = mk_label(row, num, ui_font_body_l(), C_TEXT_DIM,
                                       8, 1, 22);
            p->row_icon[i]  = mk_label(row, it->icon, ui_font_icon(), C_TEXT,
                                       32, 6, 26);
            /* A row draws its label in its own role face when it names one
             * (the Font page: each row IS the level it sets), otherwise in
             * the page default (TEXT). The label box spans the row height so
             * a larger HEADER preview is not clipped; the text still sits at
             * the same y, and DOTS still keeps it to one line. */
            lv_font_t *lbl_font = item_label_font(it->font_role);
            /* One line, tall enough for the face in use: a bigger TEXT or a
             * HEADER-sized preview must not be clipped by the box, and the
             * shipped VT323 24 works out to the original 26 px. */
            int lbl_h = (int)lv_font_get_line_height(lbl_font) + 2;
            if (lbl_h < 26) {
                lbl_h = 26;
            }
            if (lbl_h > h - 2) {
                lbl_h = h - 2;
            }
            p->row_lbl[i]   = mk_data_line_label(row, it->label, lbl_font,
                                                 C_TEXT, LABEL_X, 1,
                                                 label_width(it, w), lbl_h);

            /* Optional row extras: second line, right-aligned meta (time),
             * unread badge. Coords match the chat-row layout (gen-08). */
            if (it->sub && it->sub[0]) {
                p->row_sub[i] = mk_data_line_label(row, it->sub, ui_font_body_m(),
                                                   C_TEXT_DIM, SUB_X, 26, SUB_W, 22);
            }
            if (it->meta && it->meta[0]) {
                /* Right-aligned value/time column, one line only. The column
                 * ends 12 px inside the row (UI_ROW_META_W): it used to run
                 * into the row border, which clipped the last glyph — every
                 * contact number lost its final digit and chat times lost the
                 * last pixel of the minutes. The explicit height below
                 * is what makes LVGL's DOTS mode one line; without it an
                 * over-long value wrapped to a second line instead. */
                lv_obj_t *t = mk_data_line_label(row, it->meta, ui_font_chrome_s(),
                                                 C_TEXT_DIM, META_X, 4, META_W, 20);
                lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_RIGHT, 0);
                p->row_meta[i] = t;
            }
            if (it->badge > 0) {
                char b[8];
                snprintf(b, sizeof b, "[%d]", it->badge);
                lv_obj_t *bd = mk_label(row, b, ui_font_chrome_s(), C_ACCENT,
                                        BADGE_X, 4, BADGE_W);
                lv_obj_set_style_text_align(bd, LV_TEXT_ALIGN_RIGHT, 0);
                p->row_badge[i] = bd;
            }
        }
    }

    for (int i = 0; i < m->n_items; i++) {
        style_row(p, i);
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

list_page_t *ui_list_build(lv_obj_t *parent, menu_t *m)
{
    if (g_pool_n >= LIST_POOL) {
        printf("ui_menu: page pool full (LIST_POOL=%d)\n", LIST_POOL);
        return NULL;
    }
    /* The row and tray arrays below are compile-time sizes. Clamp the page's
     * counts to them here, once, so nothing downstream (rebuild_rows,
     * style_cell, ui_list_set_category) can index past an array: an
     * over-wide category strip is silent memory corruption, not a visual
     * glitch. */
    if (m->n_cats > MENU_MAX_CATS) {
        printf("ui_menu: page '%s' has %u categories > MENU_MAX_CATS (%d) — "
               "raise it in ui_menu.h\n",
               m->title ? m->title : "?", (unsigned)m->n_cats, MENU_MAX_CATS);
        m->n_cats = MENU_MAX_CATS;
    }
    if (m->n_items > MENU_MAX_ITEMS) {
        printf("ui_menu: page '%s' has %u items > MENU_MAX_ITEMS (%d) — "
               "raise it in ui_menu.h\n",
               m->title ? m->title : "?", (unsigned)m->n_items, MENU_MAX_ITEMS);
        m->n_items = MENU_MAX_ITEMS;
    }
    if (m->cat_sel >= m->n_cats) {
        m->cat_sel = 0;
    }
    if (m->sel >= m->n_items) {
        m->sel = 0;
    }
    list_page_t *p = &g_pool[g_pool_n++];
    memset(p, 0, sizeof *p);
    p->m = m;

    p->root = lv_obj_create(parent);
    lv_obj_set_size(p->root, PANEL_W, CONTENT_H);
    lv_obj_set_pos(p->root, 0, 0);
    lv_obj_remove_flag(p->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(p->root, C_GROUND, 0);
    lv_obj_set_style_bg_opa(p->root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p->root, 0, 0);

    if (m->cats && m->n_cats > 0) {
        const int n = m->n_cats;
        const int cell_w = PANEL_W / n;   /* even columns across the width */

        /* --- icon tray: one icon per category, active highlighted --- */
        for (int i = 0; i < n; i++) {
            int cx = cell_w * i + cell_w / 2;
            lv_obj_t *box = lv_obj_create(p->root);
            p->cells[i] = box;
            lv_obj_set_size(box, UI_TRAY_CELL_W, UI_TRAY_CELL_H);
            lv_obj_set_pos(box, cx - UI_TRAY_CELL_W / 2, TRAY_Y);
            lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_color(box, C_GROUND, 0);
            lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(box, 2, 0);
            lv_obj_set_style_radius(box, 0, 0);
            p->cell_icon[i] = mk_label(box, m->cats[i].icon, ui_font_icon(),
                                       C_ACCENT, 4, 1, 20);
        }

        /* --- centered category title flanked by << >> chevrons --- */
        p->chev_l = mk_label(p->root, GLYPH_CHEV_L, ui_font_icon_s(), C_ACCENT,
                             4, TITLE_Y + 4, 16);
        p->chev_r = mk_label(p->root, GLYPH_CHEV_R, ui_font_icon_s(), C_ACCENT,
                             PANEL_W - 20, TITLE_Y + 4, 16);
        p->title_lbl = mk_center_label(p->root, m->cats[m->cat_sel].label,
                                       ui_font_title(), C_TEXT, TITLE_Y);

        for (int i = 0; i < n; i++) {
            style_cell(p, i);
        }
    } else if (m->title) {
        /* No category strip, but the page still has a name. Without it two
         * four-row settings pages are visually identical, so DISPLAY reads
         * as a second copy of SETTINGS rather than a page inside it
         * (Eli, 27/11). Same slot as a strip page's title. */
        p->title_lbl = mk_center_label(p->root, m->title, ui_font_title(),
                                       C_TEXT, TITLE_Y);
    }

    /* --- scrollable row area: long lists (e.g. the chat list) hold more
     * items than fit on screen; short lists simply never scroll. The box is
     * a native LVGL scroll target, so pointer drags (swipes) scroll it for
     * free once gestures are wired. */
    int y0 = rows_y();   /* strip or title: both live in the header slot */
    p->rows_box = lv_obj_create(p->root);
    lv_obj_set_size(p->rows_box, PANEL_W, CONTENT_H - y0);
    lv_obj_set_pos(p->rows_box, 0, y0);
    lv_obj_set_scrollbar_mode(p->rows_box, LV_SCROLLBAR_MODE_OFF);  /* flat */
    lv_obj_set_style_bg_opa(p->rows_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p->rows_box, 0, 0);
    /* Vertical drags scroll this box natively (LVGL scroll target). The
     * horizontal-swipe half of spec §1 is detected by the shell from the
     * HAL touch points — LVGL's own gesture never fires while a scroll
     * target is under the finger, so it is not used here. */

    rebuild_rows(p);
    return p;
}

void ui_list_set_category(list_page_t *p, uint8_t cat)
{
    menu_t *m = p ? p->m : NULL;
    if (!m || !m->cats || m->n_cats == 0) {
        return;
    }
    if (cat >= m->n_cats) {
        return;
    }
    m->cat_sel = cat;
    for (int i = 0; i < m->n_cats; i++) {
        style_cell(p, i);
    }
    if (p->title_lbl) {
        lv_label_set_text(p->title_lbl, m->cats[cat].label);
    }
    rebuild_rows(p);
    lv_obj_scroll_to_y(p->rows_box, 0, LV_ANIM_OFF);   /* new list: top */
}

void ui_list_move_sel(list_page_t *p, int step)
{
    menu_t *m = p ? p->m : NULL;
    if (!m || m->n_items == 0) {
        return;
    }
    int s = (int)m->sel + step;
    if (s < 0) {
        s = 0;
    }
    if (s >= m->n_items) {
        s = m->n_items - 1;
    }
    if ((uint8_t)s == m->sel) {
        return;
    }
    /* Update sel FIRST: style_row decides "selected" from m->sel, so the
     * old row must be styled after the pointer moves, or it stays green
     * and both rows end up highlighted. */
    uint8_t old_sel = m->sel;
    m->sel = (uint8_t)s;
    style_row(p, old_sel);
    style_row(p, m->sel);
    ensure_visible(p);
}

void ui_list_select(list_page_t *p, int idx)
{
    menu_t *m = p ? p->m : NULL;
    if (!m || m->n_items == 0) {
        return;
    }
    if (idx < 0 || idx >= m->n_items) {
        return;
    }
    if ((uint8_t)idx == m->sel) {
        return;
    }
    /* Update sel FIRST: style_row decides "selected" from m->sel, so the
     * old row must be styled after the pointer moves, or it stays green
     * and both rows end up highlighted. */
    uint8_t old_sel = m->sel;
    m->sel = (uint8_t)idx;
    style_row(p, old_sel);
    style_row(p, m->sel);
    ensure_visible(p);
}

/* Re-apply palette + faces to every built page. The row objects are rebuilt
 * (rebuild_rows recreates them from the current menu_t), so they pick up new
 * colours AND a new font size for free; the page furniture is restyled here.
 * This is the renderer's half of the appearance apply pass. */
void ui_menu_restyle(void)
{
    for (int i = 0; i < g_pool_n; i++) {
        list_page_t *p = &g_pool[i];
        if (!p->root) {
            continue;
        }
        lv_obj_set_style_bg_color(p->root, C_GROUND, 0);
        if (p->title_lbl) {
            lv_obj_set_style_text_color(p->title_lbl, C_TEXT, 0);
            /* The banner wears the HEADERS role, so a font change has to
             * reach it too — colours alone left every page title on the face
             * it was built with (Eli, 27/11). */
            lv_obj_set_style_text_font(p->title_lbl, ui_font_title(), 0);
        }
        if (p->chev_l) {
            lv_obj_set_style_text_color(p->chev_l, C_ACCENT, 0);
        }
        if (p->chev_r) {
            lv_obj_set_style_text_color(p->chev_r, C_ACCENT, 0);
        }
        for (int c = 0; c < p->m->n_cats; c++) {
            if (!p->cells[c]) {
                continue;
            }
            lv_obj_set_style_bg_color(p->cells[c], C_GROUND, 0);
            style_cell(p, c);
        }
        rebuild_rows(p);
    }
}

/* Re-layout every built page after the content area changed height (the
 * softkey bar toggle). Row offsets are relative to the rows box, so resizing
 * the box is the whole job; the page furniture is top-anchored. */
void ui_menu_relayout(void)
{
    for (int i = 0; i < g_pool_n; i++) {
        list_page_t *p = &g_pool[i];
        if (!p->root) {
            continue;
        }
        lv_obj_set_size(p->root, PANEL_W, CONTENT_H);
        if (p->rows_box) {
            int y0 = rows_y();
            lv_obj_set_pos(p->rows_box, 0, y0);
            lv_obj_set_size(p->rows_box, PANEL_W, CONTENT_H - y0);
        }
    }
}

void ui_list_refresh(list_page_t *p)
{
    if (!p) {
        return;
    }
    rebuild_rows(p);
    ensure_visible(p);
}

page_id_t ui_list_selected_target(const list_page_t *p)
{
    menu_t *m = p ? p->m : NULL;
    if (!m || m->n_items == 0 || m->sel >= m->n_items) {
        return PAGE_NONE;
    }
    return m->items[m->sel].target;
}

uint8_t ui_list_sel(const list_page_t *p) { return p ? p->m->sel : 0; }
uint8_t ui_list_cat(const list_page_t *p) { return p ? p->m->cat_sel : 0; }
lv_obj_t *ui_list_root(const list_page_t *p) { return p ? p->root : NULL; }

void ui_list_set_gesture_cb(list_page_t *p, ui_list_gesture_cb cb)
{
    if (p) {
        p->gesture_cb = cb;
    }
}

ui_list_gesture_cb ui_list_page_gesture_cb(const list_page_t *p)
{
    return p ? p->gesture_cb : NULL;
}

void ui_list_set_activate_cb(list_page_t *p, ui_list_activate_cb cb)
{
    if (p) {
        p->activate_cb = cb;
    }
}

const char *ui_list_selected_label(const list_page_t *p)
{
    menu_t *m = p ? p->m : NULL;
    if (!m || m->n_items == 0 || m->sel >= m->n_items) {
        return NULL;
    }
    return m->items[m->sel].label;
}
