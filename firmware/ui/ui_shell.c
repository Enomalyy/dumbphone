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
#include "ui_input.h"
#include "ui_mainmenu.h"
#include "ui_leaf.h"
#include "ui_options.h"
#include "ui_sidebar.h"
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
/* Display glue: LVGL flush -> HAL region blit                        */
/* ------------------------------------------------------------------ */

/* Optional flush instrumentation (build with -DUI_FLUSH_STATS=1): counts
 * the bytes pushed through hal_display_blit*, the flush count, and the
 * largest single area; a summary prints at exit. Off by default — none of
 * this compiles into a normal build. */
#ifdef UI_FLUSH_STATS
static uint64_t g_flush_bytes;
static uint32_t g_flush_count;
static uint16_t g_flush_max_w, g_flush_max_h;

static void flush_stats_summary(void)
{
    printf("[flush] %u flushes, %llu bytes pushed via hal_display_blit*, "
           "largest area %ux%u px\n",
           g_flush_count, (unsigned long long)g_flush_bytes,
           g_flush_max_w, g_flush_max_h);
}
#endif

static void display_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    /* Keep the shadow framebuffer current (frame dumps and the browser
     * viewport read it), then present ONLY the dirty area. LVGL's draw
     * buffer rows are packed exactly as hal_display_blit_area expects:
     * w*2 bytes per row, no stride — so the region goes straight from the
     * render buffer to the panel, no full-frame readback. */
    const uint8_t *frame = hal_display_framebuffer();
    if (!frame) {
        lv_display_flush_ready(disp);
        return;
    }
    int x = (int)area->x1;
    int y = (int)area->y1;
    int w = (int)(area->x2 - area->x1 + 1);
    int h = (int)(area->y2 - area->y1 + 1);
    for (int ry = 0; ry < h; ry++) {
        memcpy((uint8_t *)frame + ((size_t)(y + ry) * PANEL_W + x) * 2,
               px + (size_t)ry * w * 2, (size_t)w * 2);
    }
    hal_display_blit_area(px, (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h);
#ifdef UI_FLUSH_STATS
    {
        uint32_t area_px = (uint32_t)w * (uint32_t)h;
        g_flush_bytes += (uint64_t)w * h * 2;
        g_flush_count++;
        if (area_px > (uint32_t)g_flush_max_w * g_flush_max_h) {
            g_flush_max_w = (uint16_t)w;
            g_flush_max_h = (uint16_t)h;
        }
    }
#endif
    lv_display_flush_ready(disp);
}

/* ------------------------------------------------------------------ */
/* Style helpers                                                       */
/* ------------------------------------------------------------------ */

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
    /* The lock screen shares the live clock (ui_leaf.c owns the labels). */
    char dbuf[16];
    strftime(dbuf, sizeof dbuf, "%a %d %b", &tm_now);
    ui_leaf_set_lock_time(buf, dbuf);
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

static const keymap_t *page_keymap(void)
{
    switch (screen_stack[stack_top]) {
    case UI_SCREEN_MENU:       return ui_mainmenu_keymap();
    case UI_SCREEN_SETTINGS:   return &ui_settings_page()->keys;
    case UI_SCREEN_SPEEDDIALS: return &ui_speeddial_page()->keys;
    case UI_SCREEN_HELDPRESS:  return &ui_heldpress_page()->keys;
    case UI_SCREEN_CALLHIST:   return &ui_calls_history_page()->keys;
    case UI_SCREEN_CONTACTS:   return &ui_calls_contacts_page()->keys;
    case UI_SCREEN_STUB:       return ui_leaf_stub_keymap();
    case UI_SCREEN_LOCK:       return ui_leaf_lock_keymap();
    default:                   return ui_home_keymap();   /* CHATLIST */
    }
}

static void go_home(void)
{
    end_armed = false;   /* leaving the END-on-HOME state */
    stack_top = 0;
    screen_stack[0] = UI_SCREEN_CHATLIST;
    ui_shell_show_screen(UI_SCREEN_CHATLIST);
    ui_sidebar_close();
}

static void go_back(void)
{
    end_armed = false;
    if (stack_top > 0) {
        stack_top--;
        ui_shell_show_screen(screen_stack[stack_top]);
        ui_sidebar_close();
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
    ui_sidebar_close();
}

/* Lock the phone (spec §1): replace the whole stack with the LOCKED page.
 * Unlocking lands on HOME — per spec, not back to whatever was open. */
static void do_lock(void)
{
    end_armed = false;
    ui_sidebar_close();
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
        ui_sidebar_open_for(screen_stack[stack_top]);
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

/* The stub page's content lives in ui_leaf.c; the push is navigation, so it
 * stays here. */
static void show_stub_title(const char *title)
{
    ui_leaf_set_stub_title(title);
    push_screen(UI_SCREEN_STUB);
}

static void open_stub(page_id_t id)
{
    show_stub_title(ui_leaf_stub_title_for(id));
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

/* Swipe intent: hand it to the visible page's own gesture callback (each
 * list page decides what a sideways swipe means — the chat list and the main
 * menu both cycle their category strip). */
static void page_swipe(int dir)
{
    if (ui_sidebar_is_open()) {
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

/* Execute option `idx`, then close. Navigation options push a screen, and
 * push_screen() closes the sidebar itself; in-place actions (the view
 * toggle) close it here. */
static void sidebar_execute(int idx)
{
    if (idx < 0 || idx >= ui_sidebar_count()) {
        return;
    }
    const opt_entry_t *o = ui_sidebar_opt(idx);
    if (!o) {
        return;
    }
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
    ui_sidebar_close();   /* in-place action (view toggle): close after */
}

/* The sidebar's own key routing while it is open (km_sidebar). */
static void sidebar_on_key(hal_key_t key)
{
    switch (ui_keymap_action(ui_sidebar_keymap(), key)) {
    case ACT_MOVE_SEL_UP:   ui_sidebar_move(-1);  break;
    case ACT_MOVE_SEL_DOWN: ui_sidebar_move(1);   break;
    case ACT_ACTIVATE:      sidebar_execute(ui_sidebar_sel()); break;
    case ACT_OPEN_OPTIONS:  /* re-press closes */
    case ACT_GO_BACK:       /* back closes (does not pop the page) */
        ui_sidebar_close();
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
    lv_obj_add_flag(ui_leaf_stub_root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_leaf_lock_root(), LV_OBJ_FLAG_HIDDEN);
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
        lv_obj_remove_flag(ui_leaf_stub_root(), LV_OBJ_FLAG_HIDDEN);
        break;
    case UI_SCREEN_LOCK:
        lv_obj_remove_flag(ui_leaf_lock_root(), LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

/* What a completed key press means (ui_input.c measures it and hands the
 * duration here — spec §2: held presses = custom actions). A configurable
 * key held >= UI_LONG_PRESS_MS opens its configured page (ui_heldpress) and
 * suppresses the short-press action; every other key (unconfigured long
 * press, all short presses) behaves exactly as before. */
/* KEY_UP intent, in order: the lock gate first (spec §1: while LOCKED only
 * the keypad unlocks — every other key and every long press is inert), then
 * a configured long press, then the ordinary short press. */
static void on_key_released(hal_key_t key, uint32_t held_ms)
{
    if (is_locked()) {
        if (ui_keymap_action(ui_leaf_lock_keymap(), key) == ACT_UNLOCK) {
            unlock_home();
        }
        return;
    }
    page_id_t hp = ui_heldpress_target(key);
    if (held_ms >= UI_LONG_PRESS_MS && hp != PAGE_NONE) {
        open_page(hp);   /* configured long press: the custom action */
    } else {
        on_key(key);     /* short press, or unconfigured key         */
    }
}

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
    if (key == HAL_KEY_END && !ui_sidebar_is_open() &&
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
    if (ui_sidebar_is_open()) {
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

/* Load a TTF from the font dir (absolute path first, then a CWD-relative
 * assets/fonts path for a build tree run from another directory). NULL when
 * unavailable. Called from ui_shell_init(), i.e. on the UI thread, so the
 * fonts are created in this LVGL instance before any screen renders. */
static lv_font_t *load_ttf(const char *name, int px)
{
    char path[512];
    snprintf(path, sizeof path, "A:" FONT_DIR "/%s", name);
    lv_font_t *f = lv_tiny_ttf_create_file(path, px);
    if (!f) {
        snprintf(path, sizeof path, "A:assets/fonts/%s", name);
        f = lv_tiny_ttf_create_file(path, px);
    }
    return f;
}

/* Primary face loader: a missing TTF falls back to the built-in Montserrat
 * face so the simulator still starts on a tree without the assets. */
static lv_font_t *load_font(const char *name, int px, const lv_font_t *fallback)
{
    lv_font_t *f = load_ttf(name, px);
    if (!f) {
        printf("ui_shell: font %s (%d px) missing, using built-in fallback\n",
               name, px);
        return (lv_font_t *)fallback;
    }
    return f;
}

/* Data-script fallback chain (ui-architecture.md §5): one Hebrew and one
 * emoji face per TEXT SIZE. LVGL resolves a glyph at the face's own size, so
 * a single 24 px Hebrew face cannot serve a 13 px label; this table loads
 * each size once and shares it between the faces of that size. Only code
 * points the primary face reports missing ever reach these. */
#define FB_MAX 8
static struct {
    int        px;
    lv_font_t *hebrew;
    lv_font_t *emoji;
} g_fb[FB_MAX];
static int g_fb_n = 0;

static lv_font_t *fallback_face(int px, bool emoji)
{
    for (int i = 0; i < g_fb_n; i++) {
        if (g_fb[i].px == px) {
            return emoji ? g_fb[i].emoji : g_fb[i].hebrew;
        }
    }
    if (g_fb_n >= FB_MAX) {
        return NULL;
    }
    int i = g_fb_n++;
    g_fb[i].px = px;
    g_fb[i].hebrew = load_ttf(UI_FONT_HEBREW_FILE, px);
    g_fb[i].emoji  = load_ttf(UI_FONT_EMOJI_FILE, px);
    if (!g_fb[i].hebrew && !g_fb[i].emoji) {
        printf("ui_shell: no fallback faces at %d px\n", px);
    }
    return emoji ? g_fb[i].emoji : g_fb[i].hebrew;
}

/* Attach the chain to a primary face. `builtin` is the Montserrat pointer
 * load_font() may have returned; it lives in rodata and must never be
 * written to, so a missing TTF simply leaves that face unchained. */
static void chain_fallbacks(lv_font_t *base, const lv_font_t *builtin, int px)
{
    if (!base || base == builtin) {
        return;
    }
    lv_font_t *hebrew = fallback_face(px, false);
    lv_font_t *emoji  = fallback_face(px, true);
    if (hebrew && emoji) {
        hebrew->fallback = emoji;   /* outer fallback: emoji anywhere */
    }
    lv_font_t *outer = hebrew ? hebrew : emoji;
    if (outer) {
        ((lv_font_t *)base)->fallback = outer;
    }
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

#ifdef UI_FLUSH_STATS
    atexit(flush_stats_summary);   /* the UI thread is joined before exit */
#endif

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

    ui_input_attach_indev();
    /* ui_input.c owns the event queue and the gesture state; this file owns
     * what each intent means (lock gate, long press, page swipe). */
    ui_input_set_cbs(on_key_released, page_swipe);
    /* Snappier touch sampling than the 16 ms default: drags register in
     * fewer frames (and the harness's --swipe stays deterministic). */

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

    /* Hebrew/emoji fall behind every face. ui_text_set() picks each data
     * string's base direction; the chain decides which face supplies a
     * glyph the primary pixel face does not have. */
    chain_fallbacks(f_chrome,   &lv_font_montserrat_14, UI_FONT_CHROME_PX);
    chain_fallbacks(f_chrome_s, &lv_font_montserrat_12, UI_FONT_CHROME_S_PX);
    chain_fallbacks(f_title,    &lv_font_montserrat_20, UI_FONT_TITLE_PX);
    chain_fallbacks(f_body_l,   &lv_font_montserrat_18, UI_FONT_BODY_L_PX);
    chain_fallbacks(f_body_m,   &lv_font_montserrat_14, UI_FONT_BODY_M_PX);
    chain_fallbacks(f_icon,     &lv_font_montserrat_16, UI_FONT_ICON_PX);
    chain_fallbacks(f_icon_s,   &lv_font_montserrat_12, UI_FONT_ICON_S_PX);

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
    ui_leaf_build_stub(content_area);   /* stub leaf (ui_leaf.c)   */
    ui_leaf_build_lock(content_area);   /* lock screen (ui_leaf.c) */

    /* The renderer's page pool is a compile-time size: a page that failed to
     * build must be reported here, not crash the first time it is shown. */
    if (!ui_home_page() || !g_menu || !g_settings || !g_speeddial ||
        !g_heldpress || !g_callhist || !g_contacts) {
        printf("ui_shell: page pool exhausted — raise LIST_POOL in ui_menu.c\n");
        return -1;
    }

    /* The sidebar is created last so it draws above the content screens. */
    ui_sidebar_build(scr_root);
    ui_sidebar_set_activate_cb(sidebar_execute);

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

    ui_input_poll();
    lv_timer_handler();
}

bool ui_shell_quit_requested(void)
{
    return quit_requested;
}
