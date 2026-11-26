/*
 * ui_leaf.c — the bespoke leaf screens: LOCKED and the stub page.
 *
 * Owns: both screens' widgets (the lock screen's glyph/clock/date/hint and
 * the stub's title + "coming soon" line) plus their contextual keymaps.
 * Exposes: build/lock_root/set_lock_time/lock_keymap and
 * build/stub_root/set_stub_title/stub_keymap (ui_leaf.h). Must not touch:
 * the navigation stack (unlocking and pushing the stub are shell
 * decisions), the status bar's own clock, or the list renderer.
 */

#include <stddef.h>

#include "lvgl.h"

#include "ui_icons.h"
#include "ui_keymap.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_leaf.h"

/* Colours are ROLES (ui_theme.h); the theme table owns the values. */
#define C_GROUND      ui_color(UI_ROLE_GROUND)
#define C_TEXT        ui_color(UI_ROLE_TEXT)
#define C_TEXT_DIM    ui_color(UI_ROLE_TEXT_DIM)
#define C_ACCENT      ui_color(UI_ROLE_ACCENT)

/* Seed values: the same constants the status bar starts from. The shell's
 * 1-second tick refreshes both on a running phone; a frozen (harness) run
 * keeps these for good. */
#define LEAF_CLOCK_SEED_TIME "20:36"
#define LEAF_CLOCK_SEED_DATE "Thu 26 Nov"

/* Stub leaf page: options with shoulder-L, back with shoulder-R, home
 * with END; nothing else. */
static const keymap_t km_stub = {
    .shoulder_l = ACT_OPEN_OPTIONS,
    .shoulder_r = ACT_GO_BACK,
    .end        = ACT_GOTO_HOME,
};

/* LOCKED (spec §1): the keypad unlocks — any T9 key; every other control
 * (d-pad, shoulders, CALL, END, CENTER) is inert while locked. */
static const keymap_t km_lock = {
    .shoulder_l = ACT_NOOP,
    .shoulder_r = ACT_NOOP,
    .center     = ACT_NOOP,
    .call       = ACT_NOOP,
    .end        = ACT_NOOP,
    .up         = ACT_NOOP,
    .down       = ACT_NOOP,
    .left       = ACT_NOOP,
    .right      = ACT_NOOP,
    .t9         = { ACT_UNLOCK, ACT_UNLOCK, ACT_UNLOCK, ACT_UNLOCK,
                    ACT_UNLOCK, ACT_UNLOCK, ACT_UNLOCK, ACT_UNLOCK,
                    ACT_UNLOCK, ACT_UNLOCK, ACT_UNLOCK, ACT_UNLOCK },
};

static lv_obj_t *scr_lock;
static lv_obj_t *lock_icon;        /* the dim lock glyph                 */
static lv_obj_t *lock_clock_lbl;   /* the lock screen's large live clock */
static lv_obj_t *lock_date_lbl;    /* the lock screen's date line        */
static lv_obj_t *lock_hint_lbl;    /* "KEYPAD TO UNLOCK"                 */

static lv_obj_t *scr_stub;
static lv_obj_t *stub_title_lbl;
static lv_obj_t *stub_sub_lbl;

/* A one-line label, left-aligned at (x, y) with a fixed width. */
static lv_obj_t *leaf_label(lv_obj_t *parent, const char *text,
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

/* Every content screen is the same flat card: full content area, no
 * scroll, no border, no padding. */
static void style_content_screen(lv_obj_t *scr)
{
    lv_obj_set_pos(scr, 0, 0);
    lv_obj_set_size(scr, UI_PANEL_W, UI_CONTENT_H);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, C_GROUND, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
}

/* ------------------------------------------------------------------ */
/* Lock screen (spec §1) — a flat readout, not a list: lock glyph,     */
/* large live clock, date line, and the unlock hint. Locked by END×2   */
/* on HOME; any T9 keypad key unlocks (PIN entry is explicitly later). */
/* ------------------------------------------------------------------ */

void ui_leaf_build_lock(lv_obj_t *parent)
{
    scr_lock = lv_obj_create(parent);
    style_content_screen(scr_lock);
    lv_obj_add_flag(scr_lock, LV_OBJ_FLAG_HIDDEN);

    lock_icon = leaf_label(scr_lock, I_LOCK, ui_font_icon(), C_ACCENT,
                           0, 0, UI_PANEL_W);
    lv_obj_set_style_text_align(lock_icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lock_icon, LV_ALIGN_CENTER, 0, -104);

    lock_clock_lbl = leaf_label(scr_lock, "", ui_font_title(), C_TEXT,
                                0, 0, UI_PANEL_W);
    lv_obj_set_style_text_align(lock_clock_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lock_clock_lbl, LV_ALIGN_CENTER, 0, -52);

    lock_date_lbl = leaf_label(scr_lock, "", ui_font_body_l(), C_ACCENT,
                               0, 0, UI_PANEL_W);
    lv_obj_set_style_text_align(lock_date_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lock_date_lbl, LV_ALIGN_CENTER, 0, -4);

    lock_hint_lbl = leaf_label(scr_lock, "KEYPAD TO UNLOCK",
                               ui_font_chrome_s(), C_TEXT_DIM,
                               0, 0, UI_PANEL_W);
    lv_obj_set_style_text_align(lock_hint_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lock_hint_lbl, LV_ALIGN_CENTER, 0, 52);

    lv_label_set_text(lock_clock_lbl, LEAF_CLOCK_SEED_TIME);
    lv_label_set_text(lock_date_lbl, LEAF_CLOCK_SEED_DATE);
}

/* Appearance apply pass: both leaf screens re-wear the palette + faces. */
void ui_leaf_restyle(void)
{
    if (scr_lock) {
        lv_obj_set_style_bg_color(scr_lock, C_GROUND, 0);
        lv_obj_set_style_text_color(lock_icon, C_ACCENT, 0);
        lv_obj_set_style_text_font(lock_clock_lbl, ui_font_title(), 0);
        lv_obj_set_style_text_color(lock_clock_lbl, C_TEXT, 0);
        lv_obj_set_style_text_font(lock_date_lbl, ui_font_body_l(), 0);
        lv_obj_set_style_text_color(lock_date_lbl, C_ACCENT, 0);
        lv_obj_set_style_text_color(lock_hint_lbl, C_TEXT_DIM, 0);
    }
    if (scr_stub) {
        lv_obj_set_style_bg_color(scr_stub, C_GROUND, 0);
        lv_obj_set_style_text_font(stub_title_lbl, ui_font_title(), 0);
        lv_obj_set_style_text_color(stub_title_lbl, C_TEXT, 0);
        lv_obj_set_style_text_font(stub_sub_lbl, ui_font_body_m(), 0);
        lv_obj_set_style_text_color(stub_sub_lbl, C_TEXT_DIM, 0);
    }
}

/* Re-layout after the content area changed height (softkey bar toggle). */
void ui_leaf_relayout(void)
{
    if (scr_lock) {
        lv_obj_set_size(scr_lock, UI_PANEL_W, UI_CONTENT_H);
    }
    if (scr_stub) {
        lv_obj_set_size(scr_stub, UI_PANEL_W, UI_CONTENT_H);
    }
}

lv_obj_t *ui_leaf_lock_root(void)
{
    return scr_lock;
}

void ui_leaf_set_lock_time(const char *hhmm, const char *date)
{
    if (lock_clock_lbl && hhmm) {
        lv_label_set_text(lock_clock_lbl, hhmm);
    }
    if (lock_date_lbl && date) {
        lv_label_set_text(lock_date_lbl, date);
    }
}

const keymap_t *ui_leaf_lock_keymap(void)
{
    return &km_lock;
}

/* ------------------------------------------------------------------ */
/* Stub leaf page — title + "coming soon". Real content arrives in a   */
/* later leg; navigation already works end-to-end.                     */
/* ------------------------------------------------------------------ */

void ui_leaf_build_stub(lv_obj_t *parent)
{
    scr_stub = lv_obj_create(parent);
    style_content_screen(scr_stub);
    lv_obj_add_flag(scr_stub, LV_OBJ_FLAG_HIDDEN);

    stub_title_lbl = lv_label_create(scr_stub);
    lv_label_set_text(stub_title_lbl, "");
    lv_obj_set_style_text_font(stub_title_lbl, ui_font_title(), 0);
    lv_obj_set_style_text_color(stub_title_lbl, C_TEXT, 0);
    lv_obj_remove_flag(stub_title_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(stub_title_lbl, LV_ALIGN_CENTER, 0, -40);

    stub_sub_lbl = lv_label_create(scr_stub);
    lv_label_set_text(stub_sub_lbl, "coming soon");
    lv_obj_set_style_text_font(stub_sub_lbl, ui_font_body_m(), 0);
    lv_obj_set_style_text_color(stub_sub_lbl, C_TEXT_DIM, 0);
    lv_obj_remove_flag(stub_sub_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(stub_sub_lbl, LV_ALIGN_CENTER, 0, 8);
}

lv_obj_t *ui_leaf_stub_root(void)
{
    return scr_stub;
}

void ui_leaf_set_stub_title(const char *title)
{
    if (stub_title_lbl) {
        /* A speed-dial contact name can be Hebrew: data text, not chrome. */
        ui_text_set(stub_title_lbl, title);
    }
}

const char *ui_leaf_stub_title_for(page_id_t id)
{
    switch (id) {
    case PAGE_NEW_MESSAGE:  return "New Message";
    case PAGE_CALL_HISTORY: return "Call History";
    case PAGE_NEW_CALL:     return "New Call";
    case PAGE_CONTACTS:     return "Contacts";
    case PAGE_MAP_SEARCH:   return "Search";
    case PAGE_TRANSIT_PAY:  return "Transit Payments";
    case PAGE_ROUTES:       return "Routes";
    case PAGE_LINES:        return "Lines";
    case PAGE_STATIONS:     return "Stations";
    case PAGE_SETTINGS:     return "Settings";
    default:                return "Coming Soon";
    }
}

const keymap_t *ui_leaf_stub_keymap(void)
{
    return &km_stub;
}
