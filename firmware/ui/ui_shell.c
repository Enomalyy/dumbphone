/*
 * ui_shell.c — barebones UI shell for the dumb phone (Tier-1 desktop simulator).
 *
 * Layout contract (the "stable-frame rule"): one persistent root screen holds
 *   - status bar pinned top    : signal / 4G ... battery % / time
 *   - soft-key action bar bottom: CALLS / OPEN / OPTIONS
 *   - a content container in between that swaps between screens.
 * The frame never moves; only the content area changes.
 *
 * Style (concept art gen-08 / gen-09): dark charcoal, flat fills only — no
 * gradients, no shadows — green selection with bright green border, cyan
 * accents, retro pixel fonts (Silkscreen for chrome, VT323 for text,
 * Font Awesome solid for icons).
 *
 * All LVGL work happens on the UI thread. Input crosses the HAL queue;
 * nothing here is called from any other thread. No dynamic allocation in
 * the hot path (draw buffer, rows, and stack are all static).
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"

#include "chat_model.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "ui_calls.h"
#include "ui_heldpress.h"
#include "ui_keymap.h"
#include "ui_menu.h"
#include "ui_home.h"
#include "ui_mainmenu.h"
#include "ui_options.h"
#include "ui_settings.h"
#include "ui_speeddial.h"
#include "ui_shell.h"
#include "ui_theme.h"
#include "ui_icons.h"

/* ------------------------------------------------------------------ */
/* Geometry — every value is a design token (ui_theme.h, the ONE place  */
/* to change any shared measurement); these aliases carry no numbers.  */
/* ------------------------------------------------------------------ */

#define PANEL_W      UI_PANEL_W
#define PANEL_H      UI_PANEL_H
#define STATUS_H     UI_STATUS_H
#define SOFTKEYS_H   UI_SOFTKEY_H
#define CONTENT_Y    UI_CONTENT_Y
#define CONTENT_H    UI_CONTENT_H

/* ------------------------------------------------------------------ */
/* Palette — resolved through the theme layer (ui-architecture.md §5).   */
/* The values live in ui_theme.c; these aliases keep call sites readable. */

#define C_BG        (ui_palette()->bg)
#define C_BG_ALT    (ui_palette()->bg_alt)
#define C_SEL       (ui_palette()->sel)
#define C_SEL_EDGE  (ui_palette()->sel_edge)
#define C_CYAN      (ui_palette()->cyan)
#define C_TEXT      (ui_palette()->text)
#define C_TEXT_DIM  (ui_palette()->text_dim)
#define C_BORDER    (ui_palette()->border)

/* Font Awesome 6 solid glyphs live in ui_icons.h (shared by every module). */

/* ------------------------------------------------------------------ */
/* Static state (no dynamic allocation in the hot path)                */
/* ------------------------------------------------------------------ */

/* Full-frame RGB565 draw buffer (2 bytes/px). Static: no allocation in the
 * hot path, and the panel format matches LV_COLOR_FORMAT_RGB565 exactly. */
static uint8_t draw_buf[PANEL_W * PANEL_H * 2];

static lv_font_t *f_chrome;    /* Silkscreen — status bar, soft keys     */
static lv_font_t *f_chrome_s;  /* Silkscreen small — tabs                */
static lv_font_t *f_title;     /* Silkscreen large — "MENU"              */
static lv_font_t *f_body_l;    /* VT323 — primary list text              */
static lv_font_t *f_body_m;    /* VT323 — preview / secondary text       */
static lv_font_t *f_icon;      /* Font Awesome solid                     */
static lv_font_t *f_icon_s;    /* Font Awesome solid, small (carets)     */

/* The clock seed a frozen (harness) run shows. One definition, used by the
 * status bar and the lock screen, so golden frames can never disagree. */
#define CLOCK_SEED_TIME "20:36"
#define CLOCK_SEED_DATE "Thu 26 Nov"

static bool clock_frozen = false;   /* harness: never tick (see set_clock_freeze) */

static lv_obj_t *scr_root;      /* the persistent root screen (the frame)  */
static lv_obj_t *content_area;  /* swaps screens; the frame around it is fixed */
static lv_obj_t *scr_menu;
static lv_obj_t *scr_lock;         /* lock screen (spec §1)                 */
static lv_obj_t *lock_clock_lbl;   /* the lock screen's large live clock    */
static lv_obj_t *lock_date_lbl;     /* the lock screen's date line           */
static lv_obj_t *lbl_clock;

/* Screen stack (depth 4 is plenty for a skeleton). */
static ui_screen_id_t screen_stack[4] = { UI_SCREEN_CHATLIST };
static int stack_top = 0;

/* Lock state machine (spec §1): a first END on HOME arms the lock, a
 * second END locks. Any other key — or leaving HOME — disarms it. */
static bool end_armed = false;

static bool quit_requested = false;
static pthread_t ui_thread_id;   /* the one and only thread allowed to touch LVGL */

/* Cross-thread request slot: producers write here, the UI thread consumes it.
 * One slot is enough for the skeleton (last request wins). */
static pthread_mutex_t req_lock = PTHREAD_MUTEX_INITIALIZER;
static bool req_has_screen = false;
static ui_screen_id_t req_screen = UI_SCREEN_CHATLIST;
static bool req_quit = false;
static bool req_has_stub = false;
static page_id_t req_stub_id = PAGE_NONE;
static bool ui_thread_set = false;

/* ------------------------------------------------------------------ */
/* Display glue: LVGL flush -> HAL full-frame blit                    */
/* ------------------------------------------------------------------ */

static void display_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    /* The render buffer lives behind hal_display; copy the dirty rows in,
     * then push the whole frame (320x480x2 = 307 KB — trivial on desktop). */
    const uint8_t *frame = hal_display_framebuffer();
    if (!frame) {
        lv_display_flush_ready(disp);
        return;
    }
    int w = (int)(area->x2 - area->x1 + 1);
    for (int y = (int)area->y1; y <= (int)area->y2; y++) {
        const uint8_t *src = px + (size_t)(y - area->y1) * w * 2;
        uint8_t *dst = (uint8_t *)frame + ((size_t)y * PANEL_W + area->x1) * 2;
        memcpy(dst, src, (size_t)w * 2);
    }
    hal_display_blit(frame, (size_t)PANEL_W * PANEL_H * 2);
    lv_display_flush_ready(disp);
}

/* ------------------------------------------------------------------ */
/* Input glue: HAL touch state -> LVGL indev                          */
/* ------------------------------------------------------------------ */

static struct {
    int16_t x, y;
    bool pressed;
} touch_state = { 0, 0, false };

static void indev_read_cb(lv_indev_t *indev, lv_indev_data_t *d)
{
    (void)indev;
    d->point.x = touch_state.x;
    d->point.y = touch_state.y;
    d->state = touch_state.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* ------------------------------------------------------------------ */
/* Style helpers                                                       */
/* ------------------------------------------------------------------ */

static void style_content_screen(lv_obj_t *scr)
{
    lv_obj_set_pos(scr, 0, 0);
    lv_obj_set_size(scr, PANEL_W, CONTENT_H);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, lv_font_t *font,
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

/* ------------------------------------------------------------------ */
/* Status bar (pinned top)                                             */
/* ------------------------------------------------------------------ */

static void build_status_bar(lv_obj_t *parent)
{
    /* Icons and text are separate labels: the icon font has no ASCII, the text
     * font has no Font Awesome glyphs. */
    make_label(parent, I_SIGNAL, f_icon, C_TEXT, 6, 3, 18);
    make_label(parent, "4G", f_chrome, C_TEXT, 26, 5, 30);

    make_label(parent, I_BATTERY, f_icon, C_TEXT, 186, 4, 22);
    make_label(parent, "76%", f_chrome, C_TEXT, 210, 5, 40);
    lbl_clock = make_label(parent, CLOCK_SEED_TIME, f_chrome, C_TEXT, 254, 5, 60);
    lv_obj_set_style_text_align(lbl_clock, LV_TEXT_ALIGN_RIGHT, 0);
}

static void clock_tick_cb(lv_timer_t *timer)
{
    (void)timer;
    if (clock_frozen) {
        return;   /* harness run: the seeded time stands still */
    }
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char buf[8];
    strftime(buf, sizeof buf, "%H:%M", &tm_now);
    lv_label_set_text(lbl_clock, buf);
    if (lock_clock_lbl) {   /* the lock screen shares the live clock */
        lv_label_set_text(lock_clock_lbl, buf);
        char dbuf[16];
        strftime(dbuf, sizeof dbuf, "%a %d %b", &tm_now);
        lv_label_set_text(lock_date_lbl, dbuf);
    }
}

/* ------------------------------------------------------------------ */
/* Soft-key action bar (pinned bottom)                                 */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Contextual keymaps (ui-architecture.md §1, §2, §4)                  */
/*                                                                     */
/* Controls map to ACTIONS, per page. Shoulders and d-pad arrows are    */
/* different controls: SHOULDER-L = options, SHOULDER-R = back,         */
/* D-PAD L/R = navigation, UP/DOWN = move selection, CENTER = activate, */
/* END = home from anywhere. Unbound slots are ACT_NOOP.                */
/* ------------------------------------------------------------------ */

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

/* Options sidebar overlay: its own contextual keymap while open (spec §1).
 * SHOULDER-L re-press and SHOULDER-R back both close it WITHOUT popping the
 * page behind; END closes and goes home; CENTER executes the selected
 * option; UP/DOWN move the selection. */
static const keymap_t km_sidebar = {
    .shoulder_l = ACT_OPEN_OPTIONS,
    .shoulder_r = ACT_GO_BACK,
    .center     = ACT_ACTIVATE,
    .end        = ACT_GOTO_HOME,
    .up         = ACT_MOVE_SEL_UP,
    .down       = ACT_MOVE_SEL_DOWN,
};

/* The options sidebar overlay (spec §1), defined further down; navigation
 * always closes it. */
static void sidebar_close(void);
static void sidebar_open_for(ui_screen_id_t screen);
static void sidebar_execute(int idx);

static const keymap_t *page_keymap(void)
{
    switch (screen_stack[stack_top]) {
    case UI_SCREEN_MENU:       return ui_mainmenu_keymap();
    case UI_SCREEN_SETTINGS:   return &ui_settings_page()->keys;
    case UI_SCREEN_SPEEDDIALS: return &ui_speeddial_page()->keys;
    case UI_SCREEN_HELDPRESS:  return &ui_heldpress_page()->keys;
    case UI_SCREEN_CALLHIST:   return &ui_calls_history_page()->keys;
    case UI_SCREEN_CONTACTS:   return &ui_calls_contacts_page()->keys;
    case UI_SCREEN_STUB:       return &km_stub;
    case UI_SCREEN_LOCK:       return &km_lock;
    default:                   return ui_home_keymap();   /* CHATLIST */
    }
}

static void go_home(void)
{
    end_armed = false;   /* leaving the END-on-HOME state */
    stack_top = 0;
    screen_stack[0] = UI_SCREEN_CHATLIST;
    ui_shell_show_screen(UI_SCREEN_CHATLIST);
    sidebar_close();
}

static void go_back(void)
{
    end_armed = false;
    if (stack_top > 0) {
        stack_top--;
        ui_shell_show_screen(screen_stack[stack_top]);
        sidebar_close();
    }
}

/* Push a page onto the stack and show it. */
static void push_screen(ui_screen_id_t id)
{
    end_armed = false;
    int top = (int)(sizeof screen_stack / sizeof screen_stack[0]) - 1;
    if (stack_top < top) {
        stack_top++;
        screen_stack[stack_top] = id;
    }
    ui_shell_show_screen(id);
    sidebar_close();
}

/* Lock the phone (spec §1): replace the whole stack with the LOCKED page.
 * Unlocking lands on HOME — per spec, not back to whatever was open. */
static void do_lock(void)
{
    end_armed = false;
    sidebar_close();
    stack_top = 0;
    screen_stack[0] = UI_SCREEN_LOCK;
    ui_shell_show_screen(UI_SCREEN_LOCK);
}

static void unlock_home(void)
{
    end_armed = false;
    stack_top = 0;
    screen_stack[0] = UI_SCREEN_CHATLIST;
    ui_shell_show_screen(UI_SCREEN_CHATLIST);
}

static bool is_locked(void)
{
    return screen_stack[stack_top] == UI_SCREEN_LOCK;
}

/* Forward decls: the main-menu carousel, settings tree, speed-dial sub-
 * page, held-press sub-page + stub page live further down; the list pages
 * are all canonical (menu_t + renderer). The chat list lives in ui_home.c. */
static list_page_t *g_menu = NULL;
static list_page_t *g_settings = NULL;
static list_page_t *g_speeddial = NULL;
static list_page_t *g_heldpress = NULL;
static list_page_t *g_callhist = NULL;
static list_page_t *g_contacts = NULL;

/* Forward decls: the input entry point (the soft-key bar re-enters it) and
 * the leaf-page openers, all defined further down. */
static void on_key(hal_key_t key);
static void open_stub(page_id_t id);
static void open_dial(int idx);
static void open_page(page_id_t id);

/* CENTER: page-dependent meaning. HOME -> MAIN MENU; MENU -> open item;
 * SETTINGS -> toggle the selected setting and redraw its value, or open the
 * sub-page a row targets (Speed Dials / Held Presses); SPEEDDIALS -> cycle
 * the selected slot; HELDPRESS -> cycle the selected key's preset.
 */
static void activate(void)
{
    if (screen_stack[stack_top] == UI_SCREEN_CHATLIST) {
        push_screen(UI_SCREEN_MENU);
    } else if (screen_stack[stack_top] == UI_SCREEN_MENU) {
        open_page(ui_list_selected_target(g_menu));
    } else if (screen_stack[stack_top] == UI_SCREEN_SETTINGS) {
        page_id_t t = ui_list_selected_target(g_settings);
        if (t == PAGE_NONE) {
            ui_settings_activate(ui_list_sel(g_settings));
            ui_list_refresh(g_settings);
        } else {
            open_page(t);   /* navigable row (a config sub-page) */
        }
    } else if (screen_stack[stack_top] == UI_SCREEN_SPEEDDIALS) {
        ui_speeddial_cycle(ui_list_sel(g_speeddial));
        ui_list_refresh(g_speeddial);
    } else if (screen_stack[stack_top] == UI_SCREEN_HELDPRESS) {
        ui_heldpress_cycle(ui_list_sel(g_heldpress));
        ui_list_refresh(g_heldpress);
    }
}

static void move_selection(int step)
{
    if (screen_stack[stack_top] == UI_SCREEN_CHATLIST) {
        ui_list_move_sel(ui_home_page(), step);
    } else if (screen_stack[stack_top] == UI_SCREEN_MENU) {
        ui_list_move_sel(g_menu, step);
    } else if (screen_stack[stack_top] == UI_SCREEN_SETTINGS) {
        ui_list_move_sel(g_settings, step);
    } else if (screen_stack[stack_top] == UI_SCREEN_SPEEDDIALS) {
        ui_list_move_sel(g_speeddial, step);
    } else if (screen_stack[stack_top] == UI_SCREEN_HELDPRESS) {
        ui_list_move_sel(g_heldpress, step);
    } else if (screen_stack[stack_top] == UI_SCREEN_CALLHIST) {
        ui_list_move_sel(g_callhist, step);
    } else if (screen_stack[stack_top] == UI_SCREEN_CONTACTS) {
        ui_list_move_sel(g_contacts, step);
    }
}

/* `key` is the physical key that produced the action: ACT_DIAL needs it to
 * find its speed-dial slot; every other action ignores it. */
static void do_action(action_t a, hal_key_t key)
{
    switch (a) {
    case ACT_GOTO_HOME:      go_home();            break;
    case ACT_GO_BACK:        go_back();            break;
    case ACT_MOVE_SEL_UP:    move_selection(-1);   break;
    case ACT_MOVE_SEL_DOWN:  move_selection(1);    break;
    case ACT_ACTIVATE:       activate();           break;
    case ACT_OPEN_OPTIONS:
        /* SHOULDER-L: the context-sensitive sidebar for THIS page. */
        sidebar_open_for(screen_stack[stack_top]);
        break;
    case ACT_DIAL:
        /* T9 keys 1..9 are contiguous in hal_key_t; the slot index is the
         * key minus DIGIT_1. Empty slots are inert. */
        if (key >= HAL_KEY_DIGIT_1 && key <= HAL_KEY_DIGIT_9) {
            int idx = (int)(key - HAL_KEY_DIGIT_1);
            if (ui_speeddial_set((uint8_t)idx)) {
                open_dial(idx);
            }
        }
        break;
    case ACT_CYCLE_CAT_PREV:
        if (screen_stack[stack_top] == UI_SCREEN_MENU) {
            ui_mainmenu_cycle_cat(-1);
        } else if (screen_stack[stack_top] == UI_SCREEN_CHATLIST) {
            ui_home_cycle_cat(-1);
        }
        break;
    case ACT_CYCLE_CAT_NEXT:
        if (screen_stack[stack_top] == UI_SCREEN_MENU) {
            ui_mainmenu_cycle_cat(1);
        } else if (screen_stack[stack_top] == UI_SCREEN_CHATLIST) {
            ui_home_cycle_cat(1);
        }
        break;
    case ACT_CALL_APP:
        /* CALL from HOME goes straight to the Calls page (spec §2): the
         * call-history list. The CALLS category stays reachable via the
         * main menu. */
        if (screen_stack[stack_top] == UI_SCREEN_CHATLIST) {
            push_screen(UI_SCREEN_CALLHIST);
        }
        break;
    case ACT_NOOP:
    default:                                                  break;
    }
}

/* The action bar is a TOUCH EQUIVALENT of three physical keys (spec §1:
 * every element is tappable, and every tap has a key equivalent). Each cap
 * carries its hal_key_t in user_data and re-enters the same on_key() path as
 * a physical press, so its effect is whatever that key does on the current
 * page: CALLS = CALL (straight to Calls from HOME), OPEN = CENTER/OK,
 * OPTIONS = SHOULDER-L (the options sidebar). Nothing here is decorative. */
static void on_softkey_tap(lv_event_t *e)
{
    if (lv_indev_get_press_moved(lv_event_get_indev(e))) {
        return;   /* it was a drag, not a tap (same rule as list rows) */
    }
    on_key((hal_key_t)(intptr_t)lv_event_get_user_data(e));
}

static void build_softkey_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, PANEL_W, SOFTKEYS_H);
    lv_obj_set_pos(bar, 0, PANEL_H - SOFTKEYS_H);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(bar, C_BG, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);

    /* One cap per physical key it mirrors (the key is the cap's user_data). */
    static const struct {
        const char *label;
        hal_key_t   key;
    } caps[3] = {
        { "CALLS",   HAL_KEY_CALL },
        { "OPEN",    HAL_KEY_OK },
        { "OPTIONS", HAL_KEY_SHOULDER_L },
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *box = lv_obj_create(bar);
        lv_obj_set_size(box, 98, SOFTKEYS_H - 10);
        lv_obj_set_pos(box, 6 + i * 104, 5);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(box, C_BG, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(box, C_BORDER, 0);
        lv_obj_set_style_border_width(box, 2, 0);
        lv_obj_set_style_radius(box, 0, 0);
        lv_obj_add_event_cb(box, on_softkey_tap, LV_EVENT_RELEASED,
                            (void *)(intptr_t)caps[i].key);

        lv_obj_t *lbl = lv_label_create(box);
        lv_label_set_text(lbl, caps[i].label);
        lv_obj_set_style_text_font(lbl, f_chrome, 0);
        lv_obj_set_style_text_color(lbl, C_TEXT, 0);
        lv_obj_center(lbl);
    }
}


/* ------------------------------------------------------------------ */
/* Settings tree — a canonical list page over ui_settings' menu_t.     */
/* Rows carry their live value in `meta`; CENTER flips two-state rows  */
/* (the shell refreshes the rows via ui_list_refresh).                */
/* ------------------------------------------------------------------ */

static void build_settings_screen(void)
{
    g_settings = ui_list_build(content_area, ui_settings_page());
}

/* Speed-dial sub-page — a canonical list page over ui_speeddial's menu_t. */
static void build_speeddial_screen(void)
{
    g_speeddial = ui_list_build(content_area, ui_speeddial_page());
}

/* Held-press sub-page — a canonical list page over ui_heldpress' menu_t. */
static void build_heldpress_screen(void)
{
    g_heldpress = ui_list_build(content_area, ui_heldpress_page());
}

/* Call History — a canonical list page over ui_calls' menu_t (spec §2). */
static void build_callhist_screen(void)
{
    g_callhist = ui_list_build(content_area, ui_calls_history_page());
}

/* Contacts — a canonical list page over ui_calls' menu_t (spec §2). */
static void build_contacts_screen(void)
{
    g_contacts = ui_list_build(content_area, ui_calls_contacts_page());
}

/* ------------------------------------------------------------------ */
/* Stub leaf page — title + "coming soon". Real content arrives in a    */
/* later leg; navigation already works end-to-end.                     */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Lock screen (spec §1) — bespoke like the stub page: a flat        */
/* readout, not a list. Lock glyph, large live clock, date line, and*/
/* the unlock hint. Locked by END×2 on HOME; any T9 keypad key       */
/* unlocks (PIN entry is explicitly later per spec).                */
/* ------------------------------------------------------------------ */

static void build_lock_screen(void)
{
    scr_lock = lv_obj_create(content_area);
    style_content_screen(scr_lock);
    lv_obj_add_flag(scr_lock, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *icon = make_label(scr_lock, I_LOCK, f_icon, C_CYAN, 0, 0,
                                PANEL_W);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -104);

    lock_clock_lbl = make_label(scr_lock, "", f_title, C_TEXT, 0, 0, PANEL_W);
    lv_obj_set_style_text_align(lock_clock_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lock_clock_lbl, LV_ALIGN_CENTER, 0, -52);

    lock_date_lbl = make_label(scr_lock, "", f_body_l, C_CYAN, 0, 0, PANEL_W);
    lv_obj_set_style_text_align(lock_date_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lock_date_lbl, LV_ALIGN_CENTER, 0, -4);

    lv_obj_t *hint = make_label(scr_lock, "KEYPAD TO UNLOCK", f_chrome_s,
                                C_TEXT_DIM, 0, 0, PANEL_W);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 52);

    /* Seed from the same constants as the status bar; the 1-second tick
     * refreshes both on a running phone, and a frozen (harness) run keeps
     * this value for good. */
    lv_label_set_text(lock_clock_lbl, CLOCK_SEED_TIME);
    lv_label_set_text(lock_date_lbl, CLOCK_SEED_DATE);
}

static lv_obj_t *scr_stub = NULL;
static lv_obj_t *stub_title_lbl = NULL;

static const char *stub_title(page_id_t id)
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

static void build_stub_screen(void)
{
    scr_stub = lv_obj_create(content_area);
    style_content_screen(scr_stub);
    lv_obj_add_flag(scr_stub, LV_OBJ_FLAG_HIDDEN);

    stub_title_lbl = lv_label_create(scr_stub);
    lv_label_set_text(stub_title_lbl, "");
    lv_obj_set_style_text_font(stub_title_lbl, f_title, 0);
    lv_obj_set_style_text_color(stub_title_lbl, C_TEXT, 0);
    lv_obj_remove_flag(stub_title_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(stub_title_lbl, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *sub = lv_label_create(scr_stub);
    lv_label_set_text(sub, "coming soon");
    lv_obj_set_style_text_font(sub, f_body_m, 0);
    lv_obj_set_style_text_color(sub, C_TEXT_DIM, 0);
    lv_obj_remove_flag(sub, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 8);
}

static void show_stub_title(const char *title)
{
    lv_label_set_text(stub_title_lbl, title);
    push_screen(UI_SCREEN_STUB);
}

static void open_stub(page_id_t id)
{
    show_stub_title(stub_title(id));
}

/* A speed dial's call screen: the stub titled with the contact name. The
 * real call/compose flow arrives later — the charter's bar is that the
 * wiring works. */
static void open_dial(int idx)
{
    show_stub_title(ui_speeddial_name((uint8_t)idx));
}

void ui_shell_show_stub(page_id_t id)
{
    open_stub(id);
}

/* CENTER on a main-menu item: Chats -> the chat list; Settings -> the
 * settings tree (landing on the row the menu item asked for — the rows are
 * in the same order as the SETTINGS category items); Calls pages -> their
 * real list pages (spec §2); everything else -> a stub page (real content
 * arrives in a later leg). */
static void open_page(page_id_t id)
{
    switch (id) {
    case PAGE_CHATLIST: push_screen(UI_SCREEN_CHATLIST); break;
    case PAGE_SETTINGS:
        push_screen(UI_SCREEN_SETTINGS);
        ui_list_select(g_settings, ui_list_sel(g_menu));
        break;
    case PAGE_SPEED_DIALS:  push_screen(UI_SCREEN_SPEEDDIALS);  break;
    case PAGE_HELD_PRESS:   push_screen(UI_SCREEN_HELDPRESS);   break;
    case PAGE_CALL_HISTORY: push_screen(UI_SCREEN_CALLHIST);    break;
    case PAGE_CONTACTS:     push_screen(UI_SCREEN_CONTACTS);    break;
    case PAGE_NONE:     break;
    default:            open_stub(id);                   break;
    }
}

/* ------------------------------------------------------------------ */
/* Options sidebar overlay (spec §1: SHOULDER-L = options, per page)  */
/*                                                                    */
/* A flat panel over the left part of the content area showing the    */
/* options relevant to the CURRENT page only (data from ui_options.c).*/
/* Opens on SHOULDER-L from any page; closes on re-press, back, END,  */
/* navigation, or after executing an option. Rows are a static pool:  */
/* no allocation in the open/execute path.                            */
/* ------------------------------------------------------------------ */

#define SIDEBAR_W      UI_SIDEBAR_W
#define SIDEBAR_ROWS_Y UI_SIDEBAR_ROWS_Y
#define SIDEBAR_ROW_H  UI_SIDEBAR_ROW_H
#define OPT_MAX        UI_SIDEBAR_ROWS_MAX   /* more than any page's table holds */

static lv_obj_t *scr_sidebar;
static lv_obj_t *side_rows[OPT_MAX];
static lv_obj_t *side_row_icon[OPT_MAX];
static lv_obj_t *side_row_lbl[OPT_MAX];
static const opt_entry_t *g_side_opts = NULL;
static int g_side_n = 0;
static int g_side_sel = 0;
static bool sidebar_open = false;

/* Same selection look as the list renderer: green fill + bright edge. */
static void style_side_row(int idx)
{
    lv_obj_t *row = side_rows[idx];
    if (!row) {
        return;
    }
    const ui_palette_t *pal = ui_palette();
    bool sel = (idx == g_side_sel);
    lv_color_t band = (idx % 2) ? pal->bg_alt : pal->bg;
    lv_obj_set_style_bg_color(row, sel ? pal->sel : band, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, sel ? pal->sel_edge : pal->text_dim, 0);
    lv_obj_set_style_border_width(row, sel ? 2 : 1, 0);
    /* Same text look as the list renderer: dark on the green selection. */
    lv_obj_set_style_text_color(side_row_icon[idx], sel ? pal->bg : pal->cyan, 0);
    lv_obj_set_style_text_color(side_row_lbl[idx], sel ? pal->bg : pal->text, 0);
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
            sidebar_execute(i);
            return;
        }
    }
}

static void build_sidebar(void)
{
    const ui_palette_t *pal = ui_palette();

    /* Last child of root: drawn above the content screens. It covers only
     * the content area — the pinned status/softkey bars stay clear. */
    scr_sidebar = lv_obj_create(scr_root);
    lv_obj_set_size(scr_sidebar, SIDEBAR_W, CONTENT_H);
    lv_obj_set_pos(scr_sidebar, 0, CONTENT_Y);
    lv_obj_remove_flag(scr_sidebar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr_sidebar, pal->bg, 0);
    lv_obj_set_style_bg_opa(scr_sidebar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(scr_sidebar, pal->border, 0);
    lv_obj_set_style_border_width(scr_sidebar, 2, 0);
    lv_obj_set_style_radius(scr_sidebar, 0, 0);
    lv_obj_add_flag(scr_sidebar, LV_OBJ_FLAG_HIDDEN);

    make_label(scr_sidebar, "OPTIONS", f_chrome, C_TEXT, 10, 8, 90);

    for (int i = 0; i < OPT_MAX; i++) {
        lv_obj_t *row = lv_obj_create(scr_sidebar);
        side_rows[i] = row;
        lv_obj_set_size(row, SIDEBAR_W - 12, SIDEBAR_ROW_H - 3);
        lv_obj_set_pos(row, 6, SIDEBAR_ROWS_Y + i * SIDEBAR_ROW_H);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 0, 0);
        /* Execute on RELEASE (see on_side_row_tap): a drag that starts on a
         * row must not execute it — same rule as the list renderer's rows. */
        lv_obj_add_event_cb(row, on_side_row_tap, LV_EVENT_RELEASED, NULL);

        side_row_icon[i] = make_label(row, "", f_icon, C_TEXT, 12, 6, 26);
        side_row_lbl[i]  = make_label(row, "", f_body_l, C_TEXT,
                                      46, 1, SIDEBAR_W - 46 - 8);
        lv_label_set_long_mode(side_row_lbl[i], LV_LABEL_LONG_MODE_DOTS);

        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Horizontal-swipe handling (spec §1): a drag that moves more than
 * SWIPE_MIN_PX sideways, dominant over vertical, fires the visible list
 * page's gesture callback once — swipe left = next, swipe right = previous.
 * Vertical drags scroll natively via LVGL and never fire it. Swipes are
 * inert while the options sidebar is open (it covers the row area). */
#define SWIPE_MIN_PX 50

static struct {
    bool   active;   /* a touch drag is in progress        */
    int16_t x0, y0;  /* down position, panel px           */
    bool   fired;    /* this drag already fired the swipe */
} swipe_state = { false, 0, 0, false };

static void page_swipe(int dir)
{
    if (sidebar_open) {
        return;
    }
    list_page_t *p = NULL;
    if (ui_home_page() && lv_obj_is_visible(ui_list_root(ui_home_page()))) {
        p = ui_home_page();
    } else if (lv_obj_is_visible(scr_menu)) {
        p = g_menu;
    }
    if (!p) {
        return;
    }
    ui_list_gesture_cb cb = ui_list_page_gesture_cb(p);
    if (cb) {
        cb(p, dir);
    }
}

static void sidebar_close(void)
{
    sidebar_open = false;
    if (scr_sidebar) {
        lv_obj_add_flag(scr_sidebar, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Populate the rows from `screen`'s options table and show the panel. */
static void sidebar_open_for(ui_screen_id_t screen)
{
    int n;
    g_side_opts = ui_options_for(screen, &n);
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

static void sidebar_move(int step)
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
    style_side_row(g_side_sel);
    g_side_sel = s;
    style_side_row(g_side_sel);
}

/* Execute option `idx`, then close. Navigation options push a screen, and
 * push_screen() closes the sidebar itself; in-place actions (the view
 * toggle) close it here. */
static void sidebar_execute(int idx)
{
    if (idx < 0 || idx >= g_side_n) {
        return;
    }
    const opt_entry_t *o = &g_side_opts[idx];
    switch (o->action) {
    case OPT_ACTION_PAGE:
        open_page(o->target);      /* push_screen() closes the sidebar */
        return;                    /* navigation already closed it     */
    case OPT_ACTION_MENU_CAROUSEL:
        ui_settings_set_menu_style(MENU_STYLE_CAROUSEL);
        ui_mainmenu_sync_layout();
        break;
    case OPT_ACTION_MENU_GRID:
        ui_settings_set_menu_style(MENU_STYLE_GRID);
        ui_mainmenu_sync_layout();
        break;
    case OPT_ACTION_MAIN_MENU:
        push_screen(UI_SCREEN_MENU);   /* closes the sidebar */
        return;
    }
    sidebar_close();   /* in-place action (view toggle): close after */
}

/* The sidebar's own key routing while it is open (km_sidebar). */
static void sidebar_on_key(hal_key_t key)
{
    switch (ui_keymap_action(&km_sidebar, key)) {
    case ACT_MOVE_SEL_UP:   sidebar_move(-1);  break;
    case ACT_MOVE_SEL_DOWN: sidebar_move(1);   break;
    case ACT_ACTIVATE:      sidebar_execute(g_side_sel); break;
    case ACT_OPEN_OPTIONS:  /* re-press closes */
    case ACT_GO_BACK:       /* back closes (does not pop the page) */
        sidebar_close();
        break;
    case ACT_GOTO_HOME:     go_home();   /* go_home() closes the sidebar */
        break;
    default:                break;
    }
}

/* ------------------------------------------------------------------ */
/* Screen manager (minimal stack)                                     */
/* ------------------------------------------------------------------ */

void ui_shell_show_screen(ui_screen_id_t id)
{
    if (ui_home_page()) {
        lv_obj_add_flag(ui_list_root(ui_home_page()), LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(scr_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(scr_stub, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(scr_lock, LV_OBJ_FLAG_HIDDEN);
    if (g_settings) {
        lv_obj_add_flag(ui_list_root(g_settings), LV_OBJ_FLAG_HIDDEN);
    }
    if (g_speeddial) {
        lv_obj_add_flag(ui_list_root(g_speeddial), LV_OBJ_FLAG_HIDDEN);
    }
    if (g_heldpress) {
        lv_obj_add_flag(ui_list_root(g_heldpress), LV_OBJ_FLAG_HIDDEN);
    }
    if (g_callhist) {
        lv_obj_add_flag(ui_list_root(g_callhist), LV_OBJ_FLAG_HIDDEN);
    }
    if (g_contacts) {
        lv_obj_add_flag(ui_list_root(g_contacts), LV_OBJ_FLAG_HIDDEN);
    }
    switch (id) {
    case UI_SCREEN_CHATLIST:
        if (ui_home_page() && ui_list_root(ui_home_page())) {
            lv_obj_remove_flag(ui_list_root(ui_home_page()),
                               LV_OBJ_FLAG_HIDDEN);
        }
        break;
    case UI_SCREEN_MENU:
        lv_obj_remove_flag(scr_menu, LV_OBJ_FLAG_HIDDEN);
        ui_mainmenu_sync_layout();
        break;
    case UI_SCREEN_SETTINGS:
        lv_obj_remove_flag(ui_list_root(g_settings), LV_OBJ_FLAG_HIDDEN);
        break;
    case UI_SCREEN_SPEEDDIALS:
        lv_obj_remove_flag(ui_list_root(g_speeddial), LV_OBJ_FLAG_HIDDEN);
        break;
    case UI_SCREEN_HELDPRESS:
        lv_obj_remove_flag(ui_list_root(g_heldpress), LV_OBJ_FLAG_HIDDEN);
        break;
    case UI_SCREEN_CALLHIST:
        lv_obj_remove_flag(ui_list_root(g_callhist), LV_OBJ_FLAG_HIDDEN);
        break;
    case UI_SCREEN_CONTACTS:
        lv_obj_remove_flag(ui_list_root(g_contacts), LV_OBJ_FLAG_HIDDEN);
        break;
    case UI_SCREEN_STUB:
        lv_obj_remove_flag(scr_stub, LV_OBJ_FLAG_HIDDEN);
        break;
    case UI_SCREEN_LOCK:
        lv_obj_remove_flag(scr_lock, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

/* Long-press detection (spec §2: held presses = custom actions). A press
 * completes on KEY_UP, so its duration is only known then; the action fires
 * on release. A configurable key held >= LONG_PRESS_MS opens its configured
 * page (ui_heldpress) and suppresses the short-press action; every other key
 * (unconfigured long press, all short presses) behaves exactly as before.
 * Static per-key state: no allocation in the input path. */
#define LONG_PRESS_MS 500
static uint32_t key_down_ms[HAL_KEY__COUNT];
static bool     key_is_down[HAL_KEY__COUNT];

static void on_key(hal_key_t key)
{
    /* Keys that exist only on the desktop (no such control on the T9 body):
     * they act directly instead of going through a page keymap. */
    switch (key) {
    case HAL_KEY_HOME: do_action(ACT_GOTO_HOME, key); return;
    case HAL_KEY_BACK: do_action(ACT_GO_BACK, key);   return;
    case HAL_KEY_MENU: do_action(ACT_ACTIVATE, key);  return;
    case HAL_KEY_POWER:
    case HAL_KEY_VOLUME_UP:
    case HAL_KEY_VOLUME_DOWN:
        /* Reserved: power menu / sleep, volume. */
        return;
    default:
        break;
    }

    /* Lock state machine (spec §1): a first END on HOME arms; a second
     * END locks. Any other key disarms, so locking needs two ENDs in a
     * row on HOME. (While LOCKED the input path never reaches here —
     * see run_iteration's KEY_UP gate.) */
    if (key == HAL_KEY_END && !sidebar_open &&
        screen_stack[stack_top] == UI_SCREEN_CHATLIST) {
        if (end_armed) {
            do_lock();
            return;
        }
        end_armed = true;   /* first END on HOME: already home — arm only */
        return;
    }
    end_armed = false;

    /* The options sidebar intercepts every pad key while open: its own
     * contextual keymap decides (re-press/back close, END closes + home,
     * CENTER executes, UP/DOWN move). */
    if (sidebar_open) {
        sidebar_on_key(key);
        return;
    }

    /* Everything else is a contextual pad key: ask this page's keymap. */
    do_action(ui_keymap_action(page_keymap(), key), key);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

/* LVGL tick source: monotonic clock. The S3 port points this at
 * esp_timer_get_time() / 1000 instead. */
static uint32_t tick_get_cb(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* Load a TTF from the font dir, falling back to a CWD-relative path and then
 * to a built-in Montserrat face. Called from ui_shell_init(), i.e. on the UI
 * thread, so the fonts are created in this LVGL instance before any screen
 * renders. The shell owns the font objects (ui_theme.h declares the accessors). */
static lv_font_t *load_font(const char *name, int px, const lv_font_t *fallback)
{
    char path[512];
    snprintf(path, sizeof path, "A:" FONT_DIR "/%s", name);
    lv_font_t *f = lv_tiny_ttf_create_file(path, px);
    if (!f) {
        snprintf(path, sizeof path, "A:assets/fonts/%s", name);
        f = lv_tiny_ttf_create_file(path, px);
    }
    if (!f) {
        printf("ui_shell: font %s missing, using built-in fallback\n", name);
        return (lv_font_t *)fallback;
    }
    return f;
}

/* Appearance API (declared in ui_theme.h): the shell owns the font objects. */
lv_font_t *ui_font_chrome(void)   { return f_chrome; }
lv_font_t *ui_font_chrome_s(void) { return f_chrome_s; }
lv_font_t *ui_font_title(void)    { return f_title; }
lv_font_t *ui_font_body_l(void)   { return f_body_l; }
lv_font_t *ui_font_body_m(void)   { return f_body_m; }
lv_font_t *ui_font_icon(void)     { return f_icon; }
lv_font_t *ui_font_icon_s(void)   { return f_icon_s; }

int ui_shell_init(void)
{
    ui_thread_id = pthread_self();
    ui_thread_set = true;

    lv_init();
    lv_tick_set_cb(tick_get_cb);

    hal_display_info_t info;
    hal_display_info(&info);
    if (info.width != PANEL_W || info.height != PANEL_H) {
        printf("ui_shell: panel %ux%u, expected %dx%d\n", info.width, info.height,
               PANEL_W, PANEL_H);
    }

    lv_display_t *disp = lv_display_create(PANEL_W, PANEL_H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof draw_buf,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, display_flush_cb);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, indev_read_cb);
    /* Snappier touch sampling than the 16 ms default: drags register in
     * fewer frames (and the harness's --swipe stays deterministic). */
    lv_timer_set_period(lv_indev_get_read_timer(indev), 10);

    /* Palette from the theme layer; fonts loaded here on the UI thread (the
     * tiny_ttf glyph cache must be created in this LVGL context). The SIZES
     * are design tokens in ui_theme.h: changing one there resizes that face
     * everywhere ("make all fonts bigger" = one edit). */
    ui_theme_init();
    f_chrome   = load_font("Silkscreen-Regular.ttf", UI_FONT_CHROME_PX, &lv_font_montserrat_14);
    f_chrome_s = load_font("Silkscreen-Regular.ttf", UI_FONT_CHROME_S_PX, &lv_font_montserrat_12);
    f_title    = load_font("Silkscreen-Regular.ttf", UI_FONT_TITLE_PX, &lv_font_montserrat_20);
    f_body_l   = load_font("VT323-Regular.ttf", UI_FONT_BODY_L_PX, &lv_font_montserrat_18);
    f_body_m   = load_font("VT323-Regular.ttf", UI_FONT_BODY_M_PX, &lv_font_montserrat_14);
    f_icon     = load_font("FA-Solid.ttf", UI_FONT_ICON_PX, &lv_font_montserrat_16);
    f_icon_s   = load_font("FA-Solid.ttf", UI_FONT_ICON_S_PX, &lv_font_montserrat_12);

    /* The stable frame: pinned chrome + a content area that swaps screens. */
    lv_obj_t *root = lv_scr_act();
    scr_root = root;
    lv_obj_set_size(root, PANEL_W, PANEL_H);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root, C_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);

    build_status_bar(root);

    content_area = lv_obj_create(root);
    lv_obj_set_size(content_area, PANEL_W, CONTENT_H);
    lv_obj_set_pos(content_area, 0, CONTENT_Y);
    lv_obj_remove_flag(content_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(content_area, C_BG, 0);
    lv_obj_set_style_bg_opa(content_area, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content_area, 0, 0);

    ui_home_build(content_area);   /* HOME chat list (ui_home.c)       */
    g_menu = ui_mainmenu_build(content_area);   /* main menu (ui_mainmenu.c) */
    scr_menu = ui_list_root(g_menu);   /* the renderer's root object */
    build_settings_screen();
    build_speeddial_screen();
    build_heldpress_screen();
    build_callhist_screen();
    build_contacts_screen();
    build_stub_screen();
    build_lock_screen();

    /* The renderer's page pool is a compile-time size: a page that failed to
     * build must be reported here, not crash the first time it is shown. */
    if (!ui_home_page() || !g_menu || !g_settings || !g_speeddial ||
        !g_heldpress || !g_callhist || !g_contacts) {
        printf("ui_shell: page pool exhausted — raise LIST_POOL in ui_menu.c\n");
        return -1;
    }

    /* The sidebar is created last so it draws above the content screens. */
    build_sidebar();

    build_softkey_bar(root);

    lv_timer_create(clock_tick_cb, 1000, NULL);

    ui_shell_show_screen(UI_SCREEN_CHATLIST);
    return 0;
}

void ui_shell_set_clock_freeze(bool frozen)
{
    /* The clock is the one piece of live state on the panel. Long harness
     * runs (swipes, key sequences, holds) take over a second, so a real
     * clock would land in the golden frame and change every minute. Shot
     * mode freezes it. Call before the UI thread starts. */
    clock_frozen = frozen;
}

void ui_shell_request_screen(ui_screen_id_t id)
{
    pthread_mutex_lock(&req_lock);
    req_screen = id;
    req_has_screen = true;
    pthread_mutex_unlock(&req_lock);
}

void ui_shell_request_quit(void)
{
    pthread_mutex_lock(&req_lock);
    req_quit = true;
    pthread_mutex_unlock(&req_lock);
}

void ui_shell_request_stub(page_id_t id)
{
    pthread_mutex_lock(&req_lock);
    req_has_stub = true;
    req_stub_id = id;
    pthread_mutex_unlock(&req_lock);
}

void ui_shell_run_iteration(void)
{
    /* Architectural guardrail: LVGL is not thread-safe. If anything but the
     * UI thread reaches this function, say so loudly in development. */
    if (ui_thread_set && !pthread_equal(pthread_self(), ui_thread_id)) {
        fprintf(stderr, "ui_shell: run_iteration() off the UI thread!\n");
        return;
    }

    /* Apply cross-thread requests first. */
    pthread_mutex_lock(&req_lock);
    bool have_screen = req_has_screen;
    ui_screen_id_t want = req_screen;
    bool want_quit = req_quit;
    bool have_stub = req_has_stub;
    page_id_t stub_id = req_stub_id;
    req_has_screen = false;
    req_has_stub = false;
    pthread_mutex_unlock(&req_lock);

    if (want_quit) {
        quit_requested = true;
    }
    if (have_stub) {
        open_stub(stub_id);
    }
    if (have_screen) {
        /* Keep the stack honest: the harness may request a screen directly,
         * but a later key press must still see where it really is. */
        if (want == UI_SCREEN_CHATLIST) {
            stack_top = 0;
            screen_stack[0] = UI_SCREEN_CHATLIST;
        } else {
            screen_stack[0] = UI_SCREEN_CHATLIST;
            screen_stack[1] = want;
            stack_top = 1;
        }
        ui_shell_show_screen(want);
        end_armed = false;   /* a direct screen request resets the state */
    }

    hal_input_event_t ev;
    while (hal_input_poll(&ev)) {
        switch (ev.type) {
        case HAL_INPUT_TOUCH_DOWN:
            touch_state.pressed = true;
            touch_state.x = ev.u.touch.x;
            touch_state.y = ev.u.touch.y;
            swipe_state.active = true;
            swipe_state.x0 = ev.u.touch.x;
            swipe_state.y0 = ev.u.touch.y;
            swipe_state.fired = false;
            break;
        case HAL_INPUT_TOUCH_MOVE:
            touch_state.x = ev.u.touch.x;
            touch_state.y = ev.u.touch.y;
            if (swipe_state.active && !swipe_state.fired) {
                int dx = ev.u.touch.x - swipe_state.x0;
                int dy = ev.u.touch.y - swipe_state.y0;
                /* A sideways-dominant drag past the threshold is a swipe
                 * (spec §1); it fires once per drag. */
                if ((dx > SWIPE_MIN_PX || dx < -SWIPE_MIN_PX) &&
                    (dy > -abs(dx) && dy < abs(dx))) {
                    swipe_state.fired = true;
                    page_swipe(dx < 0 ? 1 : -1);   /* left=next, right=prev */
                }
            }
            break;
        case HAL_INPUT_TOUCH_UP:
            touch_state.pressed = false;
            swipe_state.active = false;
            break;
        case HAL_INPUT_KEY_DOWN:
            key_down_ms[ev.u.key] = tick_get_cb();
            key_is_down[ev.u.key] = true;
            break;
        case HAL_INPUT_KEY_UP: {
            uint32_t dur = key_is_down[ev.u.key]
                               ? tick_get_cb() - key_down_ms[ev.u.key]
                               : 0;   /* stray UP without a DOWN */
            key_is_down[ev.u.key] = false;
            if (is_locked()) {
                /* While locked only the keypad unlocks (spec §1): every
                 * other key — and every long press — is inert. */
                if (ui_keymap_action(&km_lock, ev.u.key) == ACT_UNLOCK) {
                    unlock_home();
                }
                break;
            }
            page_id_t hp = ui_heldpress_target(ev.u.key);
            if (dur >= LONG_PRESS_MS && hp != PAGE_NONE) {
                open_page(hp);   /* configured long press: the custom action */
            } else {
                on_key(ev.u.key);   /* short press, or unconfigured key      */
            }
            break;
        }
        case HAL_INPUT_NONE:
        default:
            break;
        }
    }
    lv_timer_handler();
}

bool ui_shell_quit_requested(void)
{
    return quit_requested;
}
