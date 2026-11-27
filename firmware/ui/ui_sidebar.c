/*
 * ui_sidebar.c — the options sidebar overlay (spec §1: SHOULDER-L).
 *
 * Owns: the panel widget, the static row pool (no allocation in the
 * open/execute path), the option table pointer + selection state, and the
 * row styling — the row paint itself is the shared ui_row_style() from
 * ui_menu.c (audit §3.5), this file adds only the sidebar's text colours.
 * Exposes: ui_sidebar_build / open_for / close / is_open / move / sel /
 * count / keymap (ui_sidebar.h). Must not touch: executing an option's
 * action or the navigation stack — both live in ui_shell.c.
 */

#include <stdbool.h>

#include "lvgl.h"

#include "ui_icons.h"
#include "ui_keymap.h"
#include "ui_menu.h"     /* ui_row_style()       */
#include "ui_options.h"  /* opt_entry_t          */
#include "ui_pages.h"    /* the page registry    */
#include "ui_theme.h"
#include "ui_sidebar.h"

#define SIDEBAR_W      UI_SIDEBAR_W

/* Colours are ROLES (ui_theme.h); the theme table owns the values. */
#define C_GROUND      ui_color(UI_ROLE_GROUND)
#define C_BORDER      ui_color(UI_ROLE_BORDER)
#define C_TEXT        ui_color(UI_ROLE_TEXT)
#define C_ACCENT      ui_color(UI_ROLE_ACCENT)
#define C_ON_HL       ui_color(UI_ROLE_ON_HIGHLIGHT)
#define SIDEBAR_ROWS_Y UI_SIDEBAR_ROWS_Y
#define SIDEBAR_ROW_H  UI_SIDEBAR_ROW_H
#define OPT_MAX        UI_SIDEBAR_ROWS_MAX   /* more than any page's table */

static lv_obj_t *scr_sidebar;
static lv_obj_t *side_rows_box;  /* scrollable row area (spec §14.4) */
static lv_obj_t *side_title;   /* the "OPTIONS" header label */
static lv_obj_t *side_rows[OPT_MAX];
static lv_obj_t *side_row_icon[OPT_MAX];
static lv_obj_t *side_row_lbl[OPT_MAX];
static const opt_entry_t *g_side_opts = NULL;
static int g_side_n = 0;
static int g_side_sel = 0;
static bool sidebar_open = false;
static void (*activate_cb)(int idx) = NULL;

/* The sidebar's own contextual keymap while open (spec §1). */
static const keymap_t km_sidebar = {
    .shoulder_l = ACT_OPEN_OPTIONS,
    .shoulder_r = ACT_GO_BACK,
    .center     = ACT_ACTIVATE,
    .end        = ACT_GOTO_HOME,
    .up         = ACT_MOVE_SEL_UP,
    .down       = ACT_MOVE_SEL_DOWN,
};

/* A one-line label (the sidebar's own copy: the shell's helper is private
 * to ui_shell.c). */
static lv_obj_t *side_label(lv_obj_t *parent, const char *text,
                            lv_font_t *font, lv_color_t color,
                            int x, int y, int w)
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

/* Selection look: the shared row paint (band/border/fill) plus the text
 * colours that go with it — dark on the green selection. */
static void style_side_row(int idx)
{
    lv_obj_t *row = side_rows[idx];
    if (!row) {
        return;
    }
    bool sel = (idx == g_side_sel);

    ui_row_style(row, idx, sel);

    /* Ink on a selected row is ON_HIGHLIGHT (it used to be the page
     * GROUND — 2.7:1 on the dark highlight, the palette's one bad pair). */
    lv_obj_set_style_text_color(side_row_icon[idx], sel ? C_ON_HL : C_ACCENT, 0);
    lv_obj_set_style_text_color(side_row_lbl[idx], sel ? C_ON_HL : C_TEXT, 0);
}

/* Execute an option on RELEASE, not PRESS: a drag starting on a row must
 * not execute it (same rule as the list renderer's rows — see on_row_tap). */
static void on_side_row_tap(lv_event_t *e)
{
    if (lv_indev_get_press_moved(lv_event_get_indev(e))) {
        return;   /* it was a drag, not a tap */
    }
    lv_obj_t *t = lv_event_get_target(e);
    for (int i = 0; i < OPT_MAX; i++) {
        if (side_rows[i] == t) {
            if (activate_cb) {
                activate_cb(i);
            }
            return;
        }
    }
}

void ui_sidebar_build(lv_obj_t *parent)
{
    /* Last child of root: drawn above the content screens. It covers only
     * the content area — the pinned status/softkey bars stay clear. */
    scr_sidebar = lv_obj_create(parent);
    lv_obj_set_size(scr_sidebar, SIDEBAR_W, UI_CONTENT_H);
    lv_obj_set_pos(scr_sidebar, 0, UI_CONTENT_Y);
    lv_obj_remove_flag(scr_sidebar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr_sidebar, C_GROUND, 0);
    lv_obj_set_style_bg_opa(scr_sidebar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(scr_sidebar, C_BORDER, 0);
    lv_obj_set_style_border_width(scr_sidebar, 2, 0);
    lv_obj_set_style_radius(scr_sidebar, 0, 0);
    lv_obj_add_flag(scr_sidebar, LV_OBJ_FLAG_HIDDEN);

    side_title = side_label(scr_sidebar, "OPTIONS", ui_font_chrome(), C_TEXT,
                            10, 8, 90);

    /* The rows live in a scrollable box (spec §14.4) styled exactly like the
     * list pages' rows box: transparent, borderless, no scrollbar. Rows are
     * placed relative to it, so a table taller than the panel scrolls with
     * the same feel as every other surface — today no page's table is that
     * long, so it simply never scrolls. */
    side_rows_box = lv_obj_create(scr_sidebar);
    lv_obj_set_size(side_rows_box, SIDEBAR_W - 12,
                    (lv_coord_t)(UI_CONTENT_H - SIDEBAR_ROWS_Y));
    lv_obj_set_pos(side_rows_box, 6, SIDEBAR_ROWS_Y);
    lv_obj_set_scrollbar_mode(side_rows_box, LV_SCROLLBAR_MODE_OFF);  /* flat */
    lv_obj_set_style_bg_opa(side_rows_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(side_rows_box, 0, 0);

    for (int i = 0; i < OPT_MAX; i++) {
        lv_obj_t *row = lv_obj_create(side_rows_box);
        side_rows[i] = row;
        lv_obj_set_size(row, SIDEBAR_W - 12, SIDEBAR_ROW_H - 3);
        lv_obj_set_pos(row, 0, i * SIDEBAR_ROW_H);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 0, 0);
        /* Execute on RELEASE (see on_side_row_tap): a drag that starts on a
         * row must not execute it — same rule as the list renderer's rows. */
        lv_obj_add_event_cb(row, on_side_row_tap, LV_EVENT_RELEASED, NULL);

        side_row_icon[i] = side_label(row, "", ui_font_icon(), C_ACCENT,
                                      12, 6, 26);
        side_row_lbl[i]  = side_label(row, "", ui_font_body_l(), C_TEXT,
                                      46, 1, SIDEBAR_W - 46 - 8);
        lv_label_set_long_mode(side_row_lbl[i], LV_LABEL_LONG_MODE_DOTS);

        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Appearance apply pass: panel + every row re-wear the current palette. */
void ui_sidebar_restyle(void)
{
    if (!scr_sidebar) {
        return;
    }
    lv_obj_set_style_bg_color(scr_sidebar, C_GROUND, 0);
    lv_obj_set_style_border_color(scr_sidebar, C_BORDER, 0);
    if (side_title) {
        lv_obj_set_style_text_color(side_title, C_TEXT, 0);
    }
    for (int i = 0; i < OPT_MAX; i++) {
        if (!side_rows[i]) {
            continue;
        }
        style_side_row(i);
        lv_obj_set_style_text_font(side_row_lbl[i], ui_font_body_l(), 0);
    }
}

/* Re-layout after the content area changed height. */
void ui_sidebar_relayout(void)
{
    if (scr_sidebar) {
        lv_obj_set_size(scr_sidebar, SIDEBAR_W, UI_CONTENT_H);
        if (side_rows_box) {
            lv_obj_set_size(side_rows_box, SIDEBAR_W - 12,
                            (lv_coord_t)(UI_CONTENT_H - SIDEBAR_ROWS_Y));
        }
    }
}

void ui_sidebar_close(void)
{
    sidebar_open = false;
    if (scr_sidebar) {
        lv_obj_add_flag(scr_sidebar, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Populate the rows from `screen`'s page — its options accessor comes from
 * the page registry (spec §16.3), not a switch over screens — and show the
 * panel. */
void ui_sidebar_open_for(ui_screen_id_t screen)
{
    const ui_page_t *pg = ui_page_for_screen(screen);
    int n = 0;
    g_side_opts = (pg && pg->options) ? pg->options(&n) : NULL;
    if (n > OPT_MAX) {
        n = OPT_MAX;
    }
    g_side_n = n;
    g_side_sel = 0;

    for (int i = 0; i < OPT_MAX; i++) {
        lv_obj_t *row = side_rows[i];
        if (i < n) {
            const opt_entry_t *o = &g_side_opts[i];
            lv_label_set_text(side_row_icon[i], o->icon);
            lv_label_set_text(side_row_lbl[i], o->label);
            lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        }
    }
    for (int i = 0; i < n; i++) {
        style_side_row(i);
    }

    sidebar_open = true;
    lv_obj_remove_flag(scr_sidebar, LV_OBJ_FLAG_HIDDEN);
}

void ui_sidebar_move(int step)
{
    if (g_side_n == 0) {
        return;
    }
    int s = g_side_sel + step;
    if (s < 0) {
        s = 0;
    }
    if (s >= g_side_n) {
        s = g_side_n - 1;
    }
    if (s == g_side_sel) {
        return;
    }
    /* Update sel FIRST: style_side_row decides "selected" from
     * g_side_sel, so the old row must be styled after the pointer moves,
     * or it stays green and both rows end up highlighted (the same rule
     * ui_list_move_sel() in ui_menu.c follows — issue #10). */
    int old_sel = g_side_sel;
    g_side_sel = s;
    style_side_row(old_sel);
    style_side_row(g_side_sel);
    /* Keep the selection inside the visible part of the row area (a no-op
     * while every option fits — exactly like ui_menu.c's ensure_visible). */
    lv_obj_scroll_to_view(side_rows[g_side_sel], LV_ANIM_OFF);
}

bool ui_sidebar_is_open(void)
{
    return sidebar_open;
}

int ui_sidebar_sel(void)
{
    return g_side_sel;
}

const opt_entry_t *ui_sidebar_opt(int idx)
{
    if (idx < 0 || idx >= g_side_n || !g_side_opts) {
        return NULL;
    }
    return &g_side_opts[idx];
}

int ui_sidebar_count(void)
{
    return g_side_n;
}

const keymap_t *ui_sidebar_keymap(void)
{
    return &km_sidebar;
}

void ui_sidebar_set_activate_cb(void (*cb)(int idx))
{
    activate_cb = cb;
}
