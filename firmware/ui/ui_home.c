/*
 * ui_home.c — HOME screen (spec §2): wallpaper + honest widgets.
 *
 * HOME is its own screen, reached from LOCK and returned to by END from
 * anywhere; CENTER opens the MAIN MENU from here. It is deliberately NOT a
 * menu_t page: a flat pixel-art background motif (one dim phone glyph — no
 * photos, no gradients), one row of working widgets fed only by data the
 * shell already has (date from the status-bar clock, unread total from the
 * chat source, battery from the shell's single value — the status bar's
 * clock is not duplicated here), and a key affordance line.
 *
 * Owns: the home root object + widgets, its keymap. Exposes:
 * ui_home_build / ui_home_root / ui_home_keymap / ui_home_set_date
 * (ui_home.h). Must not touch: the navigation stack, the status/softkey
 * bars, any other screen.
 */

#include <stdio.h>

#include "lvgl.h"

#include "ui_icons.h"
#include "ui_keymap.h"
#include "ui_messaging.h"  /* ui_messaging_unread_total() */
#include "ui_shell.h"      /* ui_shell_battery_pct()      */
#include "ui_theme.h"

/* Colours are ROLES (ui_theme.h); the theme table owns the values. */
#define C_GROUND      ui_color(UI_ROLE_GROUND)
#define C_BORDER      ui_color(UI_ROLE_BORDER)
#define C_TEXT        ui_color(UI_ROLE_TEXT)
#define C_TEXT_DIM    ui_color(UI_ROLE_TEXT_DIM)

static lv_obj_t *g_root;
static lv_obj_t *g_date_val;    /* widget value labels (updated at runtime) */
static lv_obj_t *g_unread_val;

/* Every painted piece of HOME, so the appearance pass can restyle it. */
#define HOME_CARDS 3
static lv_obj_t *g_card[HOME_CARDS];
static lv_obj_t *g_card_lbl[HOME_CARDS];
static lv_obj_t *g_card_val[HOME_CARDS];
static int       g_card_n;
static lv_obj_t *g_motif;
static lv_obj_t *g_hint;

/* HOME's contextual keymap (spec §2/§4): CENTER opens the MAIN MENU,
 * T9 1-9 are speed dials, CALL goes straight to Calls. Nothing behind
 * SHOULDER-R — there is no previous page from HOME. */
static const keymap_t km_home = {
    .shoulder_l = ACT_OPEN_OPTIONS,
    .shoulder_r = ACT_NOOP,
    .center     = ACT_ACTIVATE,   /* opens the MAIN MENU (shell) */
    .call       = ACT_CALL_APP,   /* straight to Call History    */
    .end        = ACT_NOOP,       /* already home; 2nd END locks (shell state machine) */
    .t9         = { ACT_DIAL, ACT_DIAL, ACT_DIAL, ACT_DIAL, ACT_DIAL,
                    ACT_DIAL, ACT_DIAL, ACT_DIAL, ACT_DIAL,
                    ACT_NOOP, ACT_NOOP, ACT_NOOP },
};

/* One widget card: a flat bordered box with a small label over a value.
 * The value label is returned so the caller can update it at runtime. */
static lv_obj_t *widget_card(lv_obj_t *parent, int x, const char *label,
                             const char *value)
{
    int slot = g_card_n < HOME_CARDS ? g_card_n++ : 0;
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, UI_HOME_CARD_W, UI_HOME_WIDGET_H);
    lv_obj_set_pos(card, x, UI_HOME_WIDGET_Y);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, C_GROUND, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, C_BORDER, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 0, 0);
    g_card[slot] = card;

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, ui_font_chrome_s(), 0);
    lv_obj_set_style_text_color(lbl, C_TEXT_DIM, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 6);
    g_card_lbl[slot] = lbl;

    lv_obj_t *val = lv_label_create(card);
    lv_label_set_text(val, value);
    lv_obj_set_style_text_font(val, ui_font_body_m(), 0);
    lv_obj_set_style_text_color(val, C_TEXT, 0);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_remove_flag(val, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(val, LV_ALIGN_TOP_MID, 0, 28);
    g_card_val[slot] = val;

    return val;
}

lv_obj_t *ui_home_build(lv_obj_t *parent)
{
    char buf[16];

    g_root = lv_obj_create(parent);
    lv_obj_set_size(g_root, UI_PANEL_W, UI_CONTENT_H);
    lv_obj_set_pos(g_root, 0, 0);
    lv_obj_remove_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);
    /* The wallpaper base is the flat panel bg — no gradient, no image. */
    lv_obj_set_style_bg_color(g_root, C_GROUND, 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);

    /* Wallpaper motif: one dim phone glyph, centred between the widget row
     * and the hint line. A font glyph, not a picture — cheap to draw and
     * it reads as the pixel-art wallpaper of this concept. */
    lv_obj_t *motif = lv_label_create(g_root);
    g_motif = motif;
    lv_label_set_text(motif, I_PHONE);
    lv_obj_set_style_text_font(motif, ui_font_icon(), 0);
    lv_obj_set_style_text_color(motif, C_TEXT_DIM, 0);
    lv_obj_remove_flag(motif, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(motif, LV_ALIGN_TOP_MID, 0, UI_HOME_MOTIF_Y);

    /* The widget row: three honest widgets. Values come from data the
     * shell already has — nothing invented, and the status bar's clock is
     * deliberately not duplicated (the date here is a DATE, not a time). */
    g_date_val = widget_card(g_root, UI_HOME_WIDGET_INSET, "DATE",
                             "Thu 26 Nov");   /* seed; the clock tick updates it */

    snprintf(buf, sizeof buf, "%d", ui_messaging_unread_total());
    g_unread_val = widget_card(g_root,
                               UI_HOME_WIDGET_INSET + UI_HOME_CARD_W + UI_HOME_WIDGET_GAP,
                               "UNREAD", buf);

    snprintf(buf, sizeof buf, "%d%%", ui_shell_battery_pct());
    widget_card(g_root,
                UI_HOME_WIDGET_INSET + 2 * (UI_HOME_CARD_W + UI_HOME_WIDGET_GAP),
                "BATTERY", buf);

    /* Key affordance line: what the two non-obvious keys do from here. */
    lv_obj_t *hint = lv_label_create(g_root);
    g_hint = hint;
    lv_label_set_text(hint, "OK: MENU  ·  END: LOCK");
    lv_obj_set_style_text_font(hint, ui_font_chrome_s(), 0);
    lv_obj_set_style_text_color(hint, C_TEXT_DIM, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_remove_flag(hint, LV_OBJ_FLAG_CLICKABLE);
    /* Pinned 46 px above the content area's bottom edge (identity at the
     * default panel), so hiding the softkey bar moves it down with it. */
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, UI_CONTENT_H - UI_HOME_HINT_BOTTOM);

    return g_root;
}

/* Appearance apply pass: wallpaper, cards, labels and the value text. */
void ui_home_restyle(void)
{
    if (!g_root) {
        return;
    }
    lv_obj_set_style_bg_color(g_root, C_GROUND, 0);
    for (int i = 0; i < g_card_n; i++) {
        lv_obj_set_style_bg_color(g_card[i], C_GROUND, 0);
        lv_obj_set_style_border_color(g_card[i], C_BORDER, 0);
        lv_obj_set_style_text_color(g_card_lbl[i], C_TEXT_DIM, 0);
        lv_obj_set_style_text_color(g_card_val[i], C_TEXT, 0);
        lv_obj_set_style_text_font(g_card_val[i], ui_font_body_m(), 0);
    }
    if (g_motif) {
        lv_obj_set_style_text_color(g_motif, C_TEXT_DIM, 0);
    }
    if (g_hint) {
        lv_obj_set_style_text_color(g_hint, C_TEXT_DIM, 0);
    }
}

/* Re-layout after the softkey bar appears/disappears: the content area grew
 * or shrank by the bar's height, so the hint line follows its bottom edge. */
void ui_home_relayout(void)
{
    if (!g_root) {
        return;
    }
    lv_obj_set_size(g_root, UI_PANEL_W, UI_CONTENT_H);
    if (g_hint) {
        lv_obj_align(g_hint, LV_ALIGN_TOP_MID, 0,
                     UI_CONTENT_H - UI_HOME_HINT_BOTTOM);
    }
}

lv_obj_t *ui_home_root(void)
{
    return g_root;
}

const keymap_t *ui_home_keymap(void)
{
    return &km_home;
}

void ui_home_set_date(const char *date)
{
    if (g_date_val) {
        lv_label_set_text(g_date_val, date);
    }
}
