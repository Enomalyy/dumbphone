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

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"

#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "ui_calls.h"
#include "ui_chat_view.h"
#include "ui_heldpress.h"
#include "font_pixelify.h"
#include "ui_keymap.h"
#include "ui_menu.h"
#include "ui_pages.h"
#include "ui_port.h"
#include "ui_home.h"
#include "ui_messaging.h"
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

#define C_GROUND      ui_color(UI_ROLE_GROUND)
#define C_SURFACE     ui_color(UI_ROLE_SURFACE)
#define C_BORDER      ui_color(UI_ROLE_BORDER)
#define C_TEXT        ui_color(UI_ROLE_TEXT)
#define C_TEXT_DIM    ui_color(UI_ROLE_TEXT_DIM)
#define C_ACCENT      ui_color(UI_ROLE_ACCENT)
#define C_HIGHLIGHT   ui_color(UI_ROLE_HIGHLIGHT)
#define C_ACCENT_ALT  ui_color(UI_ROLE_ACCENT_ALT)
#define C_ON_HL       ui_color(UI_ROLE_ON_HIGHLIGHT)

/* Font Awesome 6 solid glyphs live in ui_icons.h (shared by every module). */

/* ------------------------------------------------------------------ */
/* Static state (no dynamic allocation in the hot path)                */
/* ------------------------------------------------------------------ */

static lv_font_t *f_chrome;    /* Silkscreen — status bar, soft keys     */
static lv_font_t *f_chrome_s;  /* Silkscreen small — tabs                */
static lv_font_t *f_title;     /* HEADER role  — titles                  */
static lv_font_t *f_body_l;    /* TEXT role    — primary list text       */
static lv_font_t *f_body_m;    /* SUBTEXT role — preview / secondary     */
/* Mutable shallow copy of the const baked descriptor (font_pixelify.h):
 * only `fallback` is written on it; bitmap/dsc tables stay shared. */
static lv_font_t body_m_copy;
static lv_font_t *f_icon;      /* Font Awesome solid                     */
static lv_font_t *f_icon_l;    /* Font Awesome solid, large (§14.5 hi)   */
static lv_font_t *f_icon_s;    /* Font Awesome solid, small (carets)     */

/* The clock seed a frozen (harness) run shows. One definition, used by the
 * status bar and the lock screen, so golden frames can never disagree. */
#define CLOCK_SEED_TIME "20:36"
#define CLOCK_SEED_DATE "Thu 26 Nov"

static bool clock_frozen = false;   /* harness: never tick (see set_clock_freeze) */
static ui_theme_id_t g_startup_theme = UI_THEME_DARK;  /* harness: --theme */
static bool g_startup_theme_set = false;

static lv_obj_t *scr_root;      /* the persistent root screen (the frame)  */
static lv_obj_t *content_area;  /* swaps screens; the frame around it is fixed */
static lv_obj_t *lbl_clock;

/* Screen stack. HOME is the base: END returns here from anywhere, and a
 * second END on it locks. The deepest real path today is HOME > MENU >
 * SETTINGS > sub-page (4); push_screen() keeps the stack honest past that. */
#define SCREEN_STACK_MAX 8
static ui_screen_id_t screen_stack[SCREEN_STACK_MAX] = { UI_SCREEN_HOME };
static int stack_top = 0;

/* Lock state machine (spec §1): a first END on HOME arms the lock, a
 * second END locks. Any other key — or leaving HOME — disarms it. */
static bool end_armed = false;

static bool quit_requested = false;

/* Cross-thread request slot: producers write here, the UI thread consumes it.
 * One slot is enough for the skeleton (last request wins). The lock and the
 * thread identity are the port's (ui_port.h): this file does not know what a
 * thread is on the device. */
static bool req_has_screen = false;
static ui_screen_id_t req_screen = UI_SCREEN_HOME;
static bool req_quit = false;
static bool req_has_stub = false;
static page_id_t req_stub_id = PAGE_NONE;

/* The display, its draw buffer, the tick source and the flush -> HAL region
 * blit are the port's (ui_port.h). Nothing in this file creates a display or
 * names a clock: that is what makes the UI buildable for the chip. */

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

/* Every label the frame paints, so the appearance pass can restyle it. */
#define SB_LABELS 5
static lv_obj_t *sb_lbl[SB_LABELS];
static int       sb_n;

static void build_status_bar(lv_obj_t *parent)
{
    /* Icons and text are separate labels: the icon font has no ASCII, the text
     * font has no Font Awesome glyphs. */
    sb_lbl[sb_n++] = make_label(parent, I_SIGNAL, f_icon, C_TEXT, 6, 3, 18);
    sb_lbl[sb_n++] = make_label(parent, "4G", f_chrome, C_TEXT, 26, 5, 30);

    sb_lbl[sb_n++] = make_label(parent, I_BATTERY, f_icon, C_TEXT, 186, 4, 22);
    /* One battery value, two readers: here and HOME's widget row. */
    static char batt[8];
    snprintf(batt, sizeof batt, "%d%%", ui_shell_battery_pct());
    sb_lbl[sb_n++] = make_label(parent, batt, f_chrome, C_TEXT, 210, 5, 40);
    lbl_clock = make_label(parent, CLOCK_SEED_TIME, f_chrome, C_TEXT, 254, 5, 60);
    sb_lbl[sb_n++] = lbl_clock;
    lv_obj_set_style_text_align(lbl_clock, LV_TEXT_ALIGN_RIGHT, 0);
}

static void clock_tick_cb(lv_timer_t *timer)
{
    (void)timer;
    if (clock_frozen) {
        return;   /* harness run: the seeded time stands still */
    }
    struct tm tm_now;
    ui_port_now(&tm_now);      /* the port owns the wall clock (ui_port.h) */
    char buf[8];
    strftime(buf, sizeof buf, "%H:%M", &tm_now);
    lv_label_set_text(lbl_clock, buf);
    /* The lock screen and HOME share the live date (ui_leaf.c / ui_home.c
     * own their labels). */
    char dbuf[16];
    strftime(dbuf, sizeof dbuf, "%a %d %b", &tm_now);
    ui_leaf_set_lock_time(buf, dbuf);
    ui_home_set_date(dbuf);
}

/* ------------------------------------------------------------------ */
/* Contextual keymaps (ui-architecture.md §1, §2, §4)                  */
/*                                                                     */
/* Controls map to ACTIONS, per page. Shoulders and d-pad arrows are    */
/* different controls: SHOULDER-L = options, SHOULDER-R = back,         */
/* D-PAD L/R = navigation, UP/DOWN = move selection, CENTER = activate, */
/* END = home from anywhere. Unbound slots are ACT_NOOP.                */
/* ------------------------------------------------------------------ */

/* The current page's contextual keys — one lookup in the page registry
 * (spec §16.3), not a switch over screens. A screen id that is not a page
 * (only possible for callers outside the harness) falls back to HOME's keys,
 * as the old default case did. */
static const keymap_t *page_keymap(void)
{
    const ui_page_t *pg = ui_page_for_screen(screen_stack[stack_top]);
    return pg ? pg->keymap() : ui_home_keymap();
}

static void go_home(void)
{
    end_armed = false;   /* leaving the END-on-HOME state */
    stack_top = 0;
    screen_stack[0] = UI_SCREEN_HOME;
    ui_shell_show_screen(UI_SCREEN_HOME);
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

/* Push a page onto the stack and show it.
 *
 * Invariant: the screen on display is always screen_stack[stack_top]. Key
 * routing reads the top of the stack, so the two must never disagree.
 *   - A page that is already on the stack is popped back to instead of
 *     stacked twice ("Settings" from a settings sub-page returns to the
 *     settings tree).
 *   - A full stack replaces its top entry and says so, rather than showing a
 *     screen the stack does not record. */
static void push_screen(ui_screen_id_t id)
{
    end_armed = false;
    int found = -1;
    for (int i = 0; i <= stack_top; i++) {
        if (screen_stack[i] == id) {
            found = i;
            break;
        }
    }
    if (found >= 0) {
        stack_top = found;
    } else if (stack_top < SCREEN_STACK_MAX - 1) {
        screen_stack[++stack_top] = id;
    } else {
        printf("ui_shell: screen stack full (%d), replacing its top\n",
               SCREEN_STACK_MAX);
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
    screen_stack[0] = UI_SCREEN_HOME;
    ui_shell_show_screen(UI_SCREEN_HOME);
}

static bool is_locked(void)
{
    return screen_stack[stack_top] == UI_SCREEN_LOCK;
}

/* One slot per registry row (ui_pages[] below): the built root object and,
 * for list pages, the renderer's page handle. Indexed by row position — the
 * ROW_* constants keep the table order honest, and the boot self-check
 * catches a drift. */
static lv_obj_t    *page_roots[UI_PAGE_COUNT];
static list_page_t *page_lists[UI_PAGE_COUNT];

/* Row positions in ui_pages[] (one row per screen — see pages_selfcheck). */
enum {
    ROW_CHATLIST = 0, ROW_HOME, ROW_MENU, ROW_DISPLAY, ROW_THEME,
    ROW_FONT, ROW_FONTDETAIL, ROW_SPEEDDIALS, ROW_HELDPRESS, ROW_CALLHIST,
    ROW_CONTACTS, ROW_STUB, ROW_LOCK, ROW_CONV,
};
_Static_assert(ROW_CONV + 1 == UI_SCREEN_COUNT, "one registry row per screen");

/* Forward decls: the input entry point (the soft-key bar re-enters it) and
 * the leaf-page openers, all defined further down. */
static void on_key(hal_key_t key);
static void update_softkeys(const keymap_t *km);
static void open_stub(page_id_t id);
static void open_dial(int idx);
static void open_page(page_id_t id);
static void relayout_all(void);

static void activate(void);

/* Touch on a row = select + activate (spec §1: touch is the universal
 * fallback for every key action; issue #13). The renderer does not know what
 * activation means — it hands back the page and the tapped index, and the
 * shell runs the SAME page-dependent activation CENTER would. The tap has
 * already selected the row (on_row_tap -> ui_list_select), so activate()
 * (which reads each page's sel) acts on exactly the tapped row. */
static void list_activate_cb(list_page_t *p, int idx)
{
    (void)idx;
    if (ui_sidebar_is_open()) {
        return;   /* the sidebar owns the pad while open: its rows have
                     their own tap path, page rows select only */
    }
    if (!lv_obj_is_visible(ui_list_root(p))) {
        return;   /* a hidden page's rows cannot be tapped */
    }
    activate();
}

/* CENTER: page-dependent meaning — the current row's activate callback
 * (spec §16.3). Called by CENTER, by the sidebar's execute, and by a row tap
 * (list_activate_cb); each callback reads its own page's selection, so all
 * three callers act on the same row. */
static void activate(void)
{
    const ui_page_t *pg = ui_page_for_screen(screen_stack[stack_top]);
    if (pg && pg->activate) {
        pg->activate();
    }
}

static void move_selection(int step)
{
    const ui_page_t *pg = ui_page_for_screen(screen_stack[stack_top]);
    list_page_t *p = pg ? page_lists[pg - ui_pages] : NULL;
    if (p) {
        ui_list_move_sel(p, step);
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
        update_softkeys(ui_sidebar_keymap());   /* legend follows the overlay */
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
    case ACT_CYCLE_CAT_NEXT: {
        const ui_page_t *pg = ui_page_for_screen(screen_stack[stack_top]);
        if (pg && pg->cycle) {
            pg->cycle(a == ACT_CYCLE_CAT_NEXT ? 1 : -1);
        }
        break;
    }
    case ACT_CALL_APP:
        /* CALL from HOME goes straight to the Calls page (spec §2): the
         * call-history list. The CALLS category stays reachable via the
         * main menu. */
        if (screen_stack[stack_top] == UI_SCREEN_HOME) {
            push_screen(UI_SCREEN_CALLHIST);
        }
        break;
    case ACT_NOOP:
    default:                                                  break;
    }
}

/* The softkey bar is a TOUCH EQUIVALENT of three physical keys (spec §1:
 * every element is tappable, and every tap has a key equivalent). Each cap
 * carries its hal_key_t in user_data and re-enters the same on_key() path as
 * a physical press, so its effect is whatever that key does in the current
 * context. Nothing here is decorative. */
static void on_softkey_tap(lv_event_t *e)
{
    if (lv_indev_get_press_moved(lv_event_get_indev(e))) {
        return;   /* it was a drag, not a tap (same rule as list rows) */
    }
    on_key((hal_key_t)(intptr_t)lv_event_get_user_data(e));
}

/* Contextual softkey legend (spec §12.2): three caps, one per mirrored
 * physical key — left under SHOULDER-L, centre under OK, right under
 * SHOULDER-R. Each cap shows the word for whatever its key does in the
 * CURRENT context (the page's keymap, or the sidebar's while it is open);
 * a cap whose action is unbound is hidden entirely — absent, not a blank
 * tappable ghost. Labels come from ui_keymap_label() as static strings and
 * are set with lv_label_set_text_static, so updates never allocate. */
static lv_obj_t *sk_cap[3];
static lv_obj_t *sk_lbl[3];
static lv_obj_t *sk_bar;

static void update_softkeys(const keymap_t *km)
{
    const action_t slots[3] = { km->shoulder_l, km->center, km->shoulder_r };
    for (int i = 0; i < 3; i++) {
        const char *txt = ui_keymap_label(slots[i]);
        if (txt) {
            lv_obj_clear_flag(sk_cap[i], LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text_static(sk_lbl[i], txt);
        } else {
            /* absent, not blank-but-tappable: hidden caps take no events */
            lv_obj_add_flag(sk_cap[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* HEADERS face at the cap's own size (defined with the font accessors). */
static lv_font_t *softkey_face(void);

static void build_softkey_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    sk_bar = bar;
    lv_obj_set_size(bar, PANEL_W, SOFTKEYS_H);
    lv_obj_set_pos(bar, 0, PANEL_H - SOFTKEYS_H);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(bar, C_GROUND, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);

    /* One cap per physical key it mirrors (the key is the cap's user_data). */
    static const hal_key_t cap_keys[3] = {
        HAL_KEY_SHOULDER_L, HAL_KEY_OK, HAL_KEY_SHOULDER_R,
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *box = lv_obj_create(bar);
        lv_obj_set_size(box, UI_SK_CAP_W, SOFTKEYS_H - 2 * UI_SK_CAP_TOP);
        lv_obj_set_pos(box, UI_SK_CAP_INSET + i * UI_SK_CAP_PITCH,
                       UI_SK_CAP_TOP);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(box, C_GROUND, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(box, C_BORDER, 0);
        lv_obj_set_style_border_width(box, 2, 0);
        lv_obj_set_style_radius(box, 0, 0);
        lv_obj_add_event_cb(box, on_softkey_tap, LV_EVENT_RELEASED,
                            (void *)(intptr_t)cap_keys[i]);
        sk_cap[i] = box;

        /* "OPTIONS" is the longest cap label: seeding it means every later
         * static-text swap fits the label's first layout. */
        sk_lbl[i] = lv_label_create(box);
        lv_label_set_text_static(sk_lbl[i], "OPTIONS");
        /* The caps take the HEADERS *face* (Eli, 27/11): pick Jersey for the
         * headers and the bar follows. The size stays the bar's own — a 32 px
         * header face cannot fit a 36 px bar without moving every frame, and
         * the bar is a fixed-height strip by design. */
        lv_obj_set_style_text_font(sk_lbl[i], softkey_face(), 0);
        lv_obj_set_style_text_color(sk_lbl[i], C_TEXT, 0);
        lv_obj_center(sk_lbl[i]);
    }
}


/* ------------------------------------------------------------------ */
/* The page registry (ui-architecture.md §16.3)                       */
/*                                                                    */
/* One row per page: the harness --screen name, both id enums, and the */
/* callbacks the shell needs to show / keymap / activate / move /      */
/* swipe / option a page. Every dispatch in this file is a loop over   */
/* the table — adding a page is writing its module and adding one row */
/* (spec §10). The renderer's list pool is sized from the table's     */
/* length, so a page not in the table cannot be reached and the pool */
/* cannot be under-counted.                                           */
/*                                                                    */
/* The table order matters: CHATLIST builds first, because the menu's  */
/* unread badge reads the chat list.                                  */
/* ------------------------------------------------------------------ */

#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* Build wrappers — each builds its page under the content area and,
 * for list pages, records the renderer's handle in the parallel state. */
static lv_obj_t *page_chatlist_build(void)
{
    list_page_t *p = ui_messaging_build(content_area);
    page_lists[ROW_CHATLIST] = p;
    return p ? ui_list_root(p) : NULL;
}

static lv_obj_t *page_home_build(void)
{
    return ui_home_build(content_area);
}

static lv_obj_t *page_menu_build(void)
{
    list_page_t *p = ui_mainmenu_build(content_area);
    page_lists[ROW_MENU] = p;
    return p ? ui_list_root(p) : NULL;
}

/* SETTINGS category > Display — theme / font / menu style / softkey bar
 * (spec §14). Rows carry their live value in `meta`; CENTER flips two-state
 * rows (the shell refreshes the rows via ui_list_refresh). */
static lv_obj_t *page_display_build(void)
{
    list_page_t *p = ui_list_build(content_area, ui_settings_display_page());
    page_lists[ROW_DISPLAY] = p;
    return p ? ui_list_root(p) : NULL;
}

/* Settings > Display > Theme — the named palettes (spec §14.11): one row
 * per theme, CENTER applies it through ui_settings_theme_activate(). */
static lv_obj_t *page_theme_build(void)
{
    list_page_t *p = ui_list_build(content_area, ui_settings_theme_page());
    page_lists[ROW_THEME] = p;
    return p ? ui_list_root(p) : NULL;
}

/* Settings > Display > Font — headers / text / subtext (spec §14.12). */
static lv_obj_t *page_font_build(void)
{
    list_page_t *p = ui_list_build(content_area, ui_settings_font_page());
    page_lists[ROW_FONT] = p;
    return p ? ui_list_root(p) : NULL;
}

/* Font > a role — Face and Size as two independent rings (issue #16). */
static lv_obj_t *page_fontdetail_build(void)
{
    list_page_t *p = ui_list_build(content_area,
                                   ui_settings_font_role_page());
    page_lists[ROW_FONTDETAIL] = p;
    return p ? ui_list_root(p) : NULL;
}

/* Speed-dial sub-page — a canonical list page over ui_speeddial's menu_t. */
static lv_obj_t *page_speeddial_build(void)
{
    list_page_t *p = ui_list_build(content_area, ui_speeddial_page());
    page_lists[ROW_SPEEDDIALS] = p;
    return p ? ui_list_root(p) : NULL;
}

/* Held-press sub-page — a canonical list page over ui_heldpress' menu_t. */
static lv_obj_t *page_heldpress_build(void)
{
    list_page_t *p = ui_list_build(content_area, ui_heldpress_page());
    page_lists[ROW_HELDPRESS] = p;
    return p ? ui_list_root(p) : NULL;
}

/* Call History — a canonical list page over ui_calls' menu_t (spec §2). */
static lv_obj_t *page_callhist_build(void)
{
    list_page_t *p = ui_list_build(content_area, ui_calls_history_page());
    page_lists[ROW_CALLHIST] = p;
    return p ? ui_list_root(p) : NULL;
}

/* Contacts — a canonical list page over ui_calls' menu_t (spec §2). */
static lv_obj_t *page_contacts_build(void)
{
    list_page_t *p = ui_list_build(content_area, ui_calls_contacts_page());
    page_lists[ROW_CONTACTS] = p;
    return p ? ui_list_root(p) : NULL;
}

/* The stub page's content lives in ui_leaf.c; the push is navigation, so it
 * stays in a keymap/activate callback. */
static lv_obj_t *page_stub_build(void)
{
    ui_leaf_build_stub(content_area);
    return ui_leaf_stub_root();
}

static lv_obj_t *page_lock_build(void)
{
    ui_leaf_build_lock(content_area);
    return ui_leaf_lock_root();
}

static lv_obj_t *page_conv_build(void)
{
    return ui_chatview_build(content_area);
}

/* Keymap wrappers — the list pages own their keymap inside their menu_t. */
static const keymap_t *page_display_keymap(void)   { return &ui_settings_display_page()->keys; }
static const keymap_t *page_theme_keymap(void)     { return &ui_settings_theme_page()->keys; }
static const keymap_t *page_font_keymap(void)      { return &ui_settings_font_page()->keys; }
static const keymap_t *page_fontdetail_keymap(void) { return &ui_settings_font_role_page()->keys; }
static const keymap_t *page_speeddial_keymap(void) { return &ui_speeddial_page()->keys; }
static const keymap_t *page_heldpress_keymap(void) { return &ui_heldpress_page()->keys; }
static const keymap_t *page_callhist_keymap(void)  { return &ui_calls_history_page()->keys; }
static const keymap_t *page_contacts_keymap(void)  { return &ui_calls_contacts_page()->keys; }
static const keymap_t *page_conv_keymap(void)      { return ui_chatview_keymap(); }

/* Activate callbacks — what CENTER / a row tap means on each page. Each
 * reads its own page's selection, so every caller (CENTER, the sidebar's
 * execute, a tap) acts on the same row. */
static void page_home_activate(void)
{
    push_screen(UI_SCREEN_MENU);
}

static void page_chatlist_activate(void)
{
    /* The selected chat row opens its CONVERSATION (spec §14.7): the model's
     * per-chat store was filled by the source when the list loaded. A
     * synthetic row (New Chat / Archive) or a missing selection keeps the
     * old stub behaviour — an honest placeholder until those stages land. */
    int idx = ui_messaging_selected_chat();
    const char *name = ui_list_selected_label(page_lists[ROW_CHATLIST]);
    if (idx < 0 || !name) {
        ui_leaf_set_stub_title(name ? name : "");
        push_screen(UI_SCREEN_STUB);
        return;
    }
    ui_chatview_open(idx, name);
    push_screen(UI_SCREEN_CONV);
}

static void page_menu_activate(void)
{
    page_id_t t = ui_list_selected_target(page_lists[ROW_MENU]);
    if (t == PAGE_NONE) {
        /* A value item of the SETTINGS category (Call Preference): toggle
         * in place through the settings handler, then show the new value
         * on the row's meta column. PAGE_NONE elsewhere is inert. */
        int row = ui_mainmenu_selected_setting();
        if (row >= 0) {
            ui_settings_activate((uint8_t)row);
            ui_mainmenu_refresh_meta();
            ui_list_refresh(page_lists[ROW_MENU]);
        }
        return;
    }
    open_page(t);   /* real target: Display / Speed Dials / Held Presses */
}

static void page_display_activate(void)
{
    list_page_t *p = page_lists[ROW_DISPLAY];
    page_id_t t = ui_list_selected_target(p);
    if (t == PAGE_NONE) {
        ui_setting_effect_t fx = ui_settings_display_activate(ui_list_sel(p));
        ui_list_refresh(p);
        if (fx == UI_SETTING_EFFECT_MENU_STYLE) {
            ui_mainmenu_sync_layout();   /* the menu view changed */
        } else if (fx == UI_SETTING_EFFECT_SOFTKEYS) {
            relayout_all();              /* content area changed  */
        }
    } else {
        open_page(t);
    }
}

static void page_theme_activate(void)
{
    list_page_t *p = page_lists[ROW_THEME];
    /* The palette applies immediately (no confirm — §14.11); the apply
     * pass already ran inside ui_theme_set(), so only the rows' ACTIVE
     * marker needs refreshing. */
    ui_settings_theme_activate(ui_list_sel(p));
    ui_list_refresh(p);
}

/* The Font page re-reads its values after a role page changed them. */
static void page_font_on_show(void)
{
    ui_settings_font_page();       /* refresh the meta strings */
    ui_list_refresh(page_lists[ROW_FONT]);
}

/* The role page re-reads its rows and title for the role just selected. */
static void page_fontdetail_on_show(void)
{
    ui_settings_font_role_page();  /* refresh rows + title */
    ui_list_refresh(page_lists[ROW_FONTDETAIL]);
}

static void page_font_activate(void)
{
    list_page_t *p = page_lists[ROW_FONT];
    /* The rows NAVIGATE now: the selected role opens its Face/Size page
     * (issue #16 — nothing cycles on this page any more). */
    ui_settings_font_open(ui_list_sel(p));
    open_page(PAGE_FONT_DETAIL);
}

static void page_fontdetail_activate(void)
{
    list_page_t *p = page_lists[ROW_FONTDETAIL];
    ui_settings_font_role_activate(ui_list_sel(p));
    ui_list_refresh(p);
}

static void page_speeddial_activate(void)
{
    list_page_t *p = page_lists[ROW_SPEEDDIALS];
    ui_speeddial_cycle(ui_list_sel(p));
    ui_list_refresh(p);
}

static void page_heldpress_activate(void)
{
    list_page_t *p = page_lists[ROW_HELDPRESS];
    ui_heldpress_cycle(ui_list_sel(p));
    ui_list_refresh(p);
}

/* Category-strip cycling (the d-pad L/R and the horizontal swipe both land
 * here). */
static void page_menu_cycle(int dir)     { ui_mainmenu_cycle_cat(dir); }
static void page_chatlist_cycle(int dir) { ui_messaging_cycle_cat(dir); }

/* The menu's layout (carousel vs grid) and its value metas are synced each
 * time the menu is shown — the old show_screen() special case, now a row
 * field. */
static void page_menu_on_show(void)
{
    ui_mainmenu_sync_layout();
    ui_mainmenu_refresh_meta();
}

/* THE table. Order: CHATLIST first (build dependency), then the rest in
 * screen-enum order. */
const ui_page_t ui_pages[] = {
    /* name         id               screen            build                keymap                activate                 cycle                  options              on_show             */
    { "chat",       PAGE_CHATLIST,   UI_SCREEN_CHATLIST, page_chatlist_build, ui_messaging_keymap,  page_chatlist_activate,   page_chatlist_cycle,    ui_opts_chatlist,     NULL },
    { "home",       PAGE_NONE,       UI_SCREEN_HOME,     page_home_build,     ui_home_keymap,       page_home_activate,     NULL,                 ui_opts_home,         NULL },
    { "menu",       PAGE_NONE,       UI_SCREEN_MENU,     page_menu_build,     ui_mainmenu_keymap,   page_menu_activate,     page_menu_cycle,      ui_opts_menu,         page_menu_on_show },
    { "display",    PAGE_DISPLAY,    UI_SCREEN_DISPLAY,  page_display_build,  page_display_keymap,  page_display_activate,  NULL,                 ui_opts_display,      NULL },
    { "theme",      PAGE_THEME,      UI_SCREEN_THEME,    page_theme_build,    page_theme_keymap,    page_theme_activate,    NULL,                 ui_opts_theme,        NULL },
    { "font",       PAGE_FONT,       UI_SCREEN_FONT,     page_font_build,     page_font_keymap,     page_font_activate,     NULL,                 ui_opts_font,         page_font_on_show },
    { "fontdetail", PAGE_FONT_DETAIL,UI_SCREEN_FONT_DETAIL, page_fontdetail_build, page_fontdetail_keymap, page_fontdetail_activate, NULL,     ui_opts_font,         page_fontdetail_on_show },
    { "speeddials", PAGE_SPEED_DIALS,UI_SCREEN_SPEEDDIALS,page_speeddial_build,page_speeddial_keymap,page_speeddial_activate,NULL,                ui_opts_speeddial,    NULL },
    { "heldpress",  PAGE_HELD_PRESS, UI_SCREEN_HELDPRESS,page_heldpress_build,page_heldpress_keymap,page_heldpress_activate,NULL,               ui_opts_heldpress,    NULL },
    { "callhist",   PAGE_CALL_HISTORY,UI_SCREEN_CALLHIST, page_callhist_build, page_callhist_keymap,  NULL,                   NULL,                 ui_opts_callhist,     NULL },
    { "contacts",   PAGE_CONTACTS,   UI_SCREEN_CONTACTS, page_contacts_build, page_contacts_keymap,  NULL,                   NULL,                 ui_opts_contacts,     NULL },
    { "stub",       PAGE_NONE,       UI_SCREEN_STUB,     page_stub_build,     ui_leaf_stub_keymap,  NULL,                   NULL,                 ui_opts_stub,         NULL },
    { "lock",       PAGE_NONE,       UI_SCREEN_LOCK,     page_lock_build,     ui_leaf_lock_keymap,  NULL,                   NULL,                 ui_opts_lock,         NULL },
    { "conv",       PAGE_NONE,       UI_SCREEN_CONV,     page_conv_build,     page_conv_keymap,     NULL,                   NULL,                 ui_opts_conv,         NULL },
};
const int ui_page_count = ARRAY_LEN(ui_pages);
_Static_assert(ARRAY_LEN(ui_pages) == UI_PAGE_COUNT,
               "UI_PAGE_COUNT must match the table");

/* Lookups — the three ways a caller names a page. */
const ui_page_t *ui_page_for_screen(ui_screen_id_t screen)
{
    for (int i = 0; i < ui_page_count; i++) {
        if (ui_pages[i].screen == screen) {
            return &ui_pages[i];
        }
    }
    return NULL;
}

const ui_page_t *ui_page_for_target(page_id_t id)
{
    if (id == PAGE_NONE) {
        return NULL;
    }
    for (int i = 0; i < ui_page_count; i++) {
        if (ui_pages[i].id == id) {
            return &ui_pages[i];
        }
    }
    return NULL;
}

const ui_page_t *ui_page_by_name(const char *name)
{
    if (!name) {
        return NULL;
    }
    for (int i = 0; i < ui_page_count; i++) {
        if (!strcmp(ui_pages[i].name, name)) {
            return &ui_pages[i];
        }
    }
    return NULL;
}

/* Boot-time validation of the table's invariants (spec §16.3): every screen
 * is exactly one page, names and targets are unique, and no row is missing
 * a required callback. A broken table would make a page unreachable — fail
 * loudly at boot instead of on first show. */
static bool pages_selfcheck(void)
{
    for (int i = 0; i < ui_page_count; i++) {
        const ui_page_t *a = &ui_pages[i];
        if (!a->name || !a->build || !a->keymap || !a->options) {
            printf("ui_shell: page row %d is missing a required field\n", i);
            return false;
        }
        for (int j = i + 1; j < ui_page_count; j++) {
            const ui_page_t *b = &ui_pages[j];
            if (b->screen == a->screen) {
                printf("ui_shell: screen %d claimed by two page rows\n",
                       (int)a->screen);
                return false;
            }
            if (b->id != PAGE_NONE && b->id == a->id) {
                printf("ui_shell: target %d claimed by two page rows\n",
                       (int)a->id);
                return false;
            }
            if (!strcmp(b->name, a->name)) {
                printf("ui_shell: page name '%s' used twice\n", a->name);
                return false;
            }
        }
    }
    for (int s = 0; s < UI_SCREEN_COUNT; s++) {
        if (!ui_page_for_screen((ui_screen_id_t)s)) {
            printf("ui_shell: screen %d has no page row\n", s);
            return false;
        }
    }
    return true;
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

/* Open the page a target names: the registry row whose id matches (spec
 * §16.3); a target with no row is stub territory. Which settings row to land
 * on is the caller's business (see the activate callbacks), not a side effect
 * of whatever the main menu happens to have selected. */
static void open_page(page_id_t id)
{
    const ui_page_t *pg = ui_page_for_target(id);
    if (pg) {
        push_screen(pg->screen);
    } else if (id != PAGE_NONE) {
        open_stub(id);
    }
}

/* ------------------------------------------------------------------ */
/* Options sidebar — the shell's half (spec §1: SHOULDER-L = options) */
/*                                                                    */
/* The panel, its row pool and its styling live in ui_sidebar.c; this  */
/* file owns what the panel cannot: executing the selected option's    */
/* ACTION (navigation, so it lives here) and the sidebar's key routing.*/
/* ------------------------------------------------------------------ */

/* Swipe intent: hand it to the visible page's own gesture callback (each
 * list page decides what a sideways swipe means — the chat list and the main
 * menu both cycle their category strip). */
static void page_swipe(int dir)
{
    if (ui_sidebar_is_open()) {
        return;
    }
    const ui_page_t *pg = ui_page_for_screen(screen_stack[stack_top]);
    list_page_t *p = pg ? page_lists[pg - ui_pages] : NULL;
    if (!p || !lv_obj_is_visible(ui_list_root(p))) {
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
    case OPT_ACTION_MAIN_MENU:
        push_screen(UI_SCREEN_MENU);   /* closes the sidebar */
        return;
    case OPT_ACTION_SETTINGS_CAT:      /* spec §14.13: settings live in the
                                         * menu's SETTINGS category — there
                                         * is no settings page to push */
        if (screen_stack[stack_top] != UI_SCREEN_MENU) {
            push_screen(UI_SCREEN_MENU);   /* closes the sidebar */
        } else {
            ui_sidebar_close();
        }
        ui_mainmenu_select_cat(MAINMENU_CAT_SETTINGS);
        update_softkeys(page_keymap());
        return;
    }
    ui_sidebar_close();   /* in-place action (view toggle): close after */
    update_softkeys(page_keymap());
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
        update_softkeys(page_keymap());   /* legend returns to the page */
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
    /* Hide every page root, show the one for `id` — a loop over the page
     * registry (spec §16.3), not a switch. A screen with no row shows
     * nothing (only possible for ids outside the harness). */
    const ui_page_t *pg = ui_page_for_screen(id);
    for (int i = 0; i < ui_page_count; i++) {
        if (page_roots[i]) {
            lv_obj_add_flag(page_roots[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (pg && page_roots[pg - ui_pages]) {
        lv_obj_remove_flag(page_roots[pg - ui_pages], LV_OBJ_FLAG_HIDDEN);
    }
    if (pg && pg->on_show) {
        pg->on_show();
    }

    /* The softkey legend follows the page (spec §12.2). */
    update_softkeys(pg ? pg->keymap() : ui_home_keymap());
}

/* What a completed key press means (ui_input.c measures it and hands the
 * duration here — spec §2: held presses = custom actions). A configurable
 * key held >= UI_LONG_PRESS_MS opens its configured page (ui_heldpress) and
 * suppresses the short-press action; every other key (unconfigured long
 * press, all short presses) behaves exactly as before. */
/* KEY_UP intent, in order: the lock gate first (spec §1: while LOCKED only
 * the keypad unlocks — every other key and every long press is inert), then
 * a configured long press, then the ordinary short press. */
/* §14.8: may this key start repeating right now? UP/DOWN only, never while
 * locked (the lock gate owns that state), and never for a key with a
 * configured held-press target — an explicit user binding wins over implicit
 * repeat (decisions §16.2). */
static bool can_repeat_key(hal_key_t key)
{
    if (key != HAL_KEY_UP && key != HAL_KEY_DOWN) {
        return false;
    }
    if (is_locked()) {
        return false;
    }
    return ui_heldpress_target(key) == PAGE_NONE;
}

/* §14.8: the repeat step — exactly what a short press of the key would do:
 * the shell's normal dispatch (the sidebar while it is open, this page's
 * keymap otherwise). */
static void on_key_step(hal_key_t key)
{
    on_key(key);
}

static void on_key_released(hal_key_t key, uint32_t held_ms)
{
    if (is_locked()) {
        if (ui_keymap_action(ui_leaf_lock_keymap(), key) == ACT_UNLOCK) {
            unlock_home();
        }
        return;
    }
    if (ui_input_fired_on_down(key)) {
        return;   /* §14.8: its step already ran on the press */
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
        screen_stack[stack_top] == UI_SCREEN_HOME) {
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
     * CENTER executes, UP/DOWN move). Everything else is a contextual pad
     * key: ask this page's keymap. */
    if (ui_sidebar_is_open()) {
        sidebar_on_key(key);
    } else {
        do_action(ui_keymap_action(page_keymap(), key), key);
    }

    /* An END that lands us on HOME arms the lock: spec §1's "END ×2 =
     * lock" must hold from any page, not only when already standing on
     * HOME (the first press is the one that brought us here). */
    if (key == HAL_KEY_END && screen_stack[stack_top] == UI_SCREEN_HOME)
        end_armed = true;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

/* The tick source and the face source are the port's (ui_port.h): a monotonic
 * clock and a filesystem path are simulator things, not UI things. */

/* Primary face loader: a missing TTF falls back to the built-in Montserrat
 * face so the simulator still starts on a tree without the assets. */
static lv_font_t *load_font(const char *name, int px, const lv_font_t *fallback)
{
    lv_font_t *f = ui_port_font(name, px);
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
    g_fb[i].hebrew = ui_port_font(UI_FONT_HEBREW_FILE, px);
    g_fb[i].emoji  = ui_port_font(UI_FONT_EMOJI_FILE, px);
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

/* ------------------------------------------------------------------ */
/* Appearance pass: fonts by role, then restyle every built object     */
/* ------------------------------------------------------------------ */

/* One font object per (face, size) actually asked for. tiny_ttf holds a
 * glyph cache per object, so re-creating on every press would churn; the
 * cache makes cycling back free. The pref ladders have 6 entries per role,
 * three roles, five faces — a long test session converges well inside this
 * bound, and the S3 build ships the default faces only until Settings moves
 * them. */
#define FONT_CACHE_MAX 24
static struct {
    lv_font_t *font;
    uint8_t    face;
    uint8_t    px;
} g_font_cache[FONT_CACHE_MAX];
static int g_font_cache_n = 0;

static const lv_font_t *role_builtin(ui_font_role_t role)
{
    switch (role) {
    case UI_FONT_HEADER:  return &lv_font_montserrat_20;
    case UI_FONT_TEXT:    return &lv_font_montserrat_18;
    case UI_FONT_SUBTEXT: return &lv_font_montserrat_14;
    default:              return &lv_font_montserrat_14;
    }
}

/* Resolve (face, px) to a chained, cached face. The baked Pixelify 16 is
 * the shipped subtext face and must stay pixel-exact, so that one size uses
 * the descriptor; every other size loads the static TTF. */
static lv_font_t *role_face(ui_font_role_t role, uint8_t face, uint8_t px)
{
    for (int i = 0; i < g_font_cache_n; i++) {
        if (g_font_cache[i].face == face && g_font_cache[i].px == px) {
            return g_font_cache[i].font;
        }
    }

    lv_font_t *f = NULL;
    if (face == UI_FACE_PIXELIFY && px == UI_FONT_BODY_M_PX) {
        f = &body_m_copy;   /* baked 8bpp descriptor, shared bitmaps */
    } else {
        const char *file = ui_face_file((ui_face_t)face);
        if (file) {
            f = load_font(file, px, role_builtin(role));
        }
    }
    if (!f) {
        return NULL;
    }
    if (f != &body_m_copy) {
        chain_fallbacks(f, role_builtin(role), px);
    }

    if (g_font_cache_n < FONT_CACHE_MAX) {
        g_font_cache[g_font_cache_n].font = f;
        g_font_cache[g_font_cache_n].face = face;
        g_font_cache[g_font_cache_n].px   = px;
        g_font_cache_n++;
    } else {
        printf("ui_shell: font cache full (%d) — face %s %d px not cached\n",
               FONT_CACHE_MAX, ui_face_short((ui_face_t)face), px);
    }
    return f;
}

/* Point the three role faces at the current preferences. Cheap when nothing
 * changed: the cache answers immediately. */
static void sync_role_fonts(void)
{
    static uint8_t have_face[UI_FONT_ROLE_COUNT] = { 0xFF, 0xFF, 0xFF };
    static uint8_t have_px[UI_FONT_ROLE_COUNT] = { 0, 0, 0 };

    for (int r = 0; r < UI_FONT_ROLE_COUNT; r++) {
        ui_font_pref_t p = ui_font_pref((ui_font_role_t)r);
        if (have_face[r] == p.face && have_px[r] == p.px) {
            continue;   /* already resolved */
        }
        lv_font_t *f = role_face((ui_font_role_t)r, p.face, p.px);
        if (!f) {
            printf("ui_shell: face %s %d px unavailable — keeping the old "
                   "face for role %s\n", ui_face_short((ui_face_t)p.face),
                   p.px, ui_font_role_name((ui_font_role_t)r));
            continue;
        }
        have_face[r] = p.face;
        have_px[r]   = p.px;
    }
    f_title  = role_face(UI_FONT_HEADER,  ui_font_pref(UI_FONT_HEADER).face,
                         ui_font_pref(UI_FONT_HEADER).px);
    f_body_l = role_face(UI_FONT_TEXT,    ui_font_pref(UI_FONT_TEXT).face,
                         ui_font_pref(UI_FONT_TEXT).px);
    f_body_m = role_face(UI_FONT_SUBTEXT, ui_font_pref(UI_FONT_SUBTEXT).face,
                         ui_font_pref(UI_FONT_SUBTEXT).px);
    if (!f_title)  { f_title  = (lv_font_t *)role_builtin(UI_FONT_HEADER); }
    if (!f_body_l) { f_body_l = (lv_font_t *)role_builtin(UI_FONT_TEXT); }
    if (!f_body_m) { f_body_m = (lv_font_t *)role_builtin(UI_FONT_SUBTEXT); }
}

/* THE apply pass (ui_theme.h): resolve the role faces, then make every built
 * object wear the current palette + faces. Called by ui_theme_set() and
 * ui_font_pref_next(), so a theme change and a font change take the same
 * road. Everything here is UI-thread only, like every style call. */
static void appearance_apply(void);

/* Appearance API (declared in ui_theme.h): the shell owns the font objects. */
lv_font_t *ui_font_chrome(void)   { return f_chrome; }
lv_font_t *ui_font_chrome_s(void) { return f_chrome_s; }
lv_font_t *ui_font_title(void)    { return f_title; }
lv_font_t *ui_font_body_l(void)   { return f_body_l; }
lv_font_t *ui_font_body_m(void)   { return f_body_m; }

/* Role -> face, for renderers that preview a level (the Font page rows). */
lv_font_t *ui_font_of(ui_font_role_t role)
{
    switch (role) {
    case UI_FONT_HEADER:  return f_title;
    case UI_FONT_SUBTEXT: return f_body_m;
    case UI_FONT_TEXT:
    default:              return f_body_l;
    }
}
/* The HEADERS face at the cap's own size: the bar follows the chosen family
 * without needing to grow. The size is the bar's, but a narrow panel's caps
 * are narrower than the word OPTIONS — so the size steps down just enough
 * to keep every cap label inside its box (issue #14). Falls back to the
 * chrome face if the face cannot be loaded at that size. */
static lv_font_t *softkey_face(void)
{
    ui_font_pref_t p = ui_font_pref(UI_FONT_HEADER);
    int px = UI_FONT_CHROME_PX;
    int fit = (UI_SK_CAP_W - 10) / 4;   /* "OPTIONS": 7 glyphs + inset */
    if (fit < px) {
        px = fit > 8 ? fit : 8;
    }
    lv_font_t *f = role_face(UI_FONT_HEADER, p.face, px);
    return f ? f : f_chrome;
}

lv_font_t *ui_font_icon(void)     { return f_icon; }
lv_font_t *ui_font_icon_l(void)   { return f_icon_l; }
lv_font_t *ui_font_icon_s(void)   { return f_icon_s; }

int ui_shell_init(void)
{
    /* The port owns lv_init, the tick source, the display and its draw buffer
     * (ui_port.h); this file owns the frame. */
    if (ui_port_init() != 0) {
        return -1;
    }

    hal_display_info_t info;
    hal_display_info(&info);
    if (info.width != PANEL_W || info.height != PANEL_H) {
        printf("ui_shell: panel %ux%u, expected %dx%d\n", info.width, info.height,
               PANEL_W, PANEL_H);
    }

    ui_input_attach_indev();
    /* ui_input.c owns the event queue and the gesture state; this file owns
     * what each intent means (lock gate, long press, page swipe). */
    ui_input_set_cbs(on_key_released, page_swipe);
    /* §14.8: held-down UP/DOWN accelerates through long lists. */
    ui_input_set_repeat_cbs(can_repeat_key, on_key_step);
    /* Snappier touch sampling than the 16 ms default: drags register in
     * fewer frames (and the harness's --swipe stays deterministic). */

    /* Palette from the theme layer; fonts loaded here on the UI thread (the
     * tiny_ttf glyph cache must be created in this LVGL context). The SIZES
     * are design tokens in ui_theme.h: changing one there resizes that face
     * everywhere ("make all fonts bigger" = one edit). */
    ui_theme_init();
    if (g_startup_theme_set) {
        ui_theme_set(g_startup_theme);
    }

    /* Chrome + icons are FIXED faces (furniture, not text): they are not in
     * the three adjustable roles. */
    f_chrome   = load_font("Silkscreen-Regular.ttf", UI_FONT_CHROME_PX, &lv_font_montserrat_14);
    f_chrome_s = load_font("Silkscreen-Regular.ttf", UI_FONT_CHROME_S_PX, &lv_font_montserrat_12);
    f_icon     = load_font("FA-Solid.ttf", UI_FONT_ICON_PX, &lv_font_montserrat_16);
    f_icon_l   = load_font("FA-Solid.ttf", UI_FONT_ICON_L_PX, &lv_font_montserrat_20);
    f_icon_s   = load_font("FA-Solid.ttf", UI_FONT_ICON_S_PX, &lv_font_montserrat_12);

    /* The three adjustable roles (HEADER / TEXT / SUBTEXT) come from the
     * font preferences. Their shipped defaults — Silkscreen 24, VT323 24,
     * baked Pixelify 16 — are the faces every golden frame was shot with.
     * The baked Pixelify descriptor is const rodata, so chain_fallbacks()
     * gets a mutable shallow copy: only `fallback` is ever written, the
     * bitmaps stay shared. */
    body_m_copy = lv_font_pixelify_m;
    /* The baked descriptor gets its data-script chain once here; role_face()
     * hands the same copy back for "Pixelify 16" and never re-chains it. */
    chain_fallbacks(&body_m_copy, &lv_font_montserrat_14, UI_FONT_BODY_M_PX);
    sync_role_fonts();

    /* Hebrew/emoji fall behind every face. ui_text_set() picks each data
     * string's base direction; the chain decides which face supplies a
     * glyph the primary pixel face does not have. (The three role faces got
     * their chain inside role_face().) */
    chain_fallbacks(f_chrome,   &lv_font_montserrat_14, UI_FONT_CHROME_PX);
    chain_fallbacks(f_chrome_s, &lv_font_montserrat_12, UI_FONT_CHROME_S_PX);
    chain_fallbacks(f_icon,     &lv_font_montserrat_16, UI_FONT_ICON_PX);
    chain_fallbacks(f_icon_l,   &lv_font_montserrat_20, UI_FONT_ICON_L_PX);
    chain_fallbacks(f_icon_s,   &lv_font_montserrat_12, UI_FONT_ICON_S_PX);

    /* Every colour/font change from here on goes through this one pass. */
    ui_appearance_on_apply(appearance_apply);

    /* The stable frame: pinned chrome + a content area that swaps screens. */
    lv_obj_t *root = lv_scr_act();
    scr_root = root;
    lv_obj_set_size(root, PANEL_W, PANEL_H);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root, C_GROUND, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);

    build_status_bar(root);

    content_area = lv_obj_create(root);
    lv_obj_set_size(content_area, PANEL_W, CONTENT_H);
    lv_obj_set_pos(content_area, 0, CONTENT_Y);
    lv_obj_remove_flag(content_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(content_area, C_GROUND, 0);
    lv_obj_set_style_bg_opa(content_area, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content_area, 0, 0);

    /* Build every page in table order (CHATLIST first — the menu's unread
     * badge reads the chat list). A page that failed to build must be
     * reported here, not crash the first time it is shown. */
    if (!pages_selfcheck()) {
        return -1;
    }
    for (int i = 0; i < ui_page_count; i++) {
        page_roots[i] = ui_pages[i].build();
    }
    for (int i = 0; i < ui_page_count; i++) {
        if (!page_roots[i]) {
            printf("ui_shell: page '%s' failed to build\n", ui_pages[i].name);
            return -1;
        }
    }

    /* Touch on a row = select + activate (spec §1, issue #13): one shared
     * callback for every list page; the shell decides what activation means. */
    for (int i = 0; i < ui_page_count; i++) {
        if (page_lists[i]) {
            ui_list_set_activate_cb(page_lists[i], list_activate_cb);
        }
    }

    /* The sidebar is created last so it draws above the content screens. */
    ui_sidebar_build(scr_root);
    ui_sidebar_set_activate_cb(sidebar_execute);

    build_softkey_bar(root);

    lv_timer_create(clock_tick_cb, 1000, NULL);

    ui_shell_show_screen(UI_SCREEN_HOME);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Appearance + layout passes (the shell owns both)                    */
/* ------------------------------------------------------------------ */

/* The frame around the content: root, status bar, soft-key bar. */
static void restyle_frame(void)
{
    lv_obj_set_style_bg_color(scr_root, C_GROUND, 0);
    lv_obj_set_style_bg_color(content_area, C_GROUND, 0);
    for (int i = 0; i < sb_n; i++) {
        lv_obj_set_style_text_color(sb_lbl[i], C_TEXT, 0);
    }
    if (sk_bar) {
        lv_obj_set_style_bg_color(sk_bar, C_GROUND, 0);
        for (int i = 0; i < 3; i++) {
            if (!sk_cap[i]) {
                continue;
            }
            lv_obj_set_style_bg_color(sk_cap[i], C_GROUND, 0);
            lv_obj_set_style_border_color(sk_cap[i], C_BORDER, 0);
            lv_obj_set_style_text_color(sk_lbl[i], C_TEXT, 0);
            lv_obj_set_style_text_font(sk_lbl[i], softkey_face(), 0);
        }
    }
}

/* ui_theme.h: the ONE apply pass. Resolve the role faces first (a font
 * change needs the new pointers before anything draws), then re-wear the
 * palette + faces: frame, every built list page, the sidebar, HOME and both
 * leaf screens. No page tree is rebuilt and the nav stack is untouched. */
static void appearance_apply(void)
{
    sync_role_fonts();


    restyle_frame();
    ui_menu_restyle();
    ui_sidebar_restyle();
    ui_home_restyle();
    ui_leaf_restyle();
    ui_chatview_restyle();
}

/* Soft-key bar shown/hidden (spec §14.9): the content area takes the bar's
 * height, then every screen is resized for the new height. */
static void relayout_all(void)
{
    int bar_h = ui_softkey_h();

    lv_obj_set_size(content_area, PANEL_W, CONTENT_H);
    if (sk_bar) {
        if (bar_h > 0) {
            lv_obj_set_size(sk_bar, PANEL_W, bar_h);
            lv_obj_set_pos(sk_bar, 0, PANEL_H - bar_h);
            lv_obj_remove_flag(sk_bar, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(sk_bar, LV_OBJ_FLAG_HIDDEN);
        }
    }
    ui_menu_relayout();
    ui_home_relayout();
    ui_leaf_relayout();
    ui_chatview_relayout();
    ui_sidebar_relayout();
}

void ui_shell_relayout(void)
{
    relayout_all();
}

void ui_shell_set_startup_theme(int theme_id)
{
    /* Recorded now, applied in ui_shell_init() right after ui_theme_init(),
     * i.e. before any page is built: a startup theme needs no restyle pass
     * and cannot race the UI thread. */
    if (theme_id >= 0 && theme_id < UI_THEME_COUNT) {
        g_startup_theme = (ui_theme_id_t)theme_id;
        g_startup_theme_set = true;
    }
}

void ui_shell_set_clock_freeze(bool frozen)
{
    /* The clock is the one piece of live state on the panel. Long harness
     * runs (swipes, key sequences, holds) take over a second, so a real
     * clock would land in the golden frame and change every minute. Shot
     * mode freezes it. Call before the UI thread starts. */
    clock_frozen = frozen;
}

bool ui_shell_clock_frozen(void)
{
    return clock_frozen;
}

void ui_shell_request_screen(ui_screen_id_t id)
{
    ui_port_lock();
    req_screen = id;
    req_has_screen = true;
    ui_port_unlock();
}

void ui_shell_request_quit(void)
{
    ui_port_lock();
    req_quit = true;
    ui_port_unlock();
}

void ui_shell_request_stub(page_id_t id)
{
    ui_port_lock();
    req_has_stub = true;
    req_stub_id = id;
    ui_port_unlock();
}

void ui_shell_run_iteration(void)
{
    /* Architectural guardrail: LVGL is not thread-safe. If anything but the
     * UI thread reaches this function, say so loudly in development. The port
     * knows which thread that is; this file only asks. */
    if (!ui_port_self_is_ui_thread()) {
        fprintf(stderr, "ui_shell: run_iteration() off the UI thread!\n");
        return;
    }

    /* Apply cross-thread requests first. */
    ui_port_lock();
    bool have_screen = req_has_screen;
    ui_screen_id_t want = req_screen;
    bool want_quit = req_quit;
    bool have_stub = req_has_stub;
    page_id_t stub_id = req_stub_id;
    req_has_screen = false;
    req_has_stub = false;
    ui_port_unlock();

    if (want_quit) {
        quit_requested = true;
    }
    if (have_stub) {
        open_stub(stub_id);
    }
    if (have_screen) {
        /* Keep the stack honest: the harness may request a screen directly,
         * but a later key press must still see where it really is. HOME is
         * always the base of the stack. */
        if (want == UI_SCREEN_HOME) {
            stack_top = 0;
            screen_stack[0] = UI_SCREEN_HOME;
        } else {
            screen_stack[0] = UI_SCREEN_HOME;
            screen_stack[1] = want;
            stack_top = 1;
        }
        ui_shell_show_screen(want);
        end_armed = false;   /* a direct screen request resets the state */
    }

    ui_input_poll();
    lv_timer_handler();
}

int ui_shell_battery_pct(void)
{
    /* The simulator has no battery HAL: one honest value, two readers —
     * the status bar and HOME's widget row. */
    return 76;
}

bool ui_shell_quit_requested(void)
{
    return quit_requested;
}
