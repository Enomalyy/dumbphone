/*
 * ui_chat_view.c — the CONVERSATION page (spec §14.7, issue #5).
 *
 * The chat's messages as incoming/outgoing bubbles over a scrollable
 * column: consecutive same-side messages group together, a divider chip
 * marks gaps over UI_MSG_GAP_MIN minutes or a day change, and each group
 * carries its timestamp under its last bubble. Bubbles are this module's
 * own objects — NOT menu_t rows (see the header for why) — inside a
 * native LVGL scroll target, so the shared 10 px drag dead zone applies
 * like every other scrollable page (spec §14.4).
 *
 * Every data string goes through ui_text_set(): Hebrew/RTL bodies take
 * their per-paragraph direction from the text itself, mixed runs are
 * handled by LVGL's bidi, and emoji ride the existing Noto fallback
 * chain. There is no second text path here.
 */

#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#include "model/chat_model.h"
#include "model/messages.h"
#include "ui_icons.h"
#include "ui_keymap.h"
#include "ui_shell.h"
#include "ui_text.h"
#include "ui_theme.h"

/* Colours are ROLES (ui_theme.h); the theme table owns the values. */
#define C_SURFACE    ui_color(UI_ROLE_SURFACE)
#define C_BORDER     ui_color(UI_ROLE_BORDER)
#define C_TEXT       ui_color(UI_ROLE_TEXT)
#define C_TEXT_DIM   ui_color(UI_ROLE_TEXT_DIM)
#define C_HIGHLIGHT  ui_color(UI_ROLE_HIGHLIGHT)
#define C_ON_HL      ui_color(UI_ROLE_ON_HIGHLIGHT)

static lv_obj_t *g_root;
static lv_obj_t *g_caret;     /* back-caret glyph in the header */
static lv_obj_t *g_name;      /* chat name in the header        */
static lv_obj_t *g_hdr_line;  /* 1 px divider under the header  */
static lv_obj_t *g_scroll;    /* scroll container (native target) */
static lv_obj_t *g_flow;      /* bubble column; height = total   */

/* One object set per message slot, plus one divider slot per boundary and
 * the honest empty state. NULL when the open chat has fewer messages. */
static lv_obj_t *g_bubble[UI_MSG_MAX_PER_CHAT];
static lv_obj_t *g_body[UI_MSG_MAX_PER_CHAT];
static lv_obj_t *g_from[UI_MSG_MAX_PER_CHAT];
static lv_obj_t *g_time[UI_MSG_MAX_PER_CHAT];
static lv_obj_t *g_divider[UI_MSG_MAX_PER_CHAT];
/* Delivery marks, outgoing bubbles only (two slots per message). */
static lv_obj_t *g_tick[UI_MSG_MAX_PER_CHAT][2];
/* Pending bar: one rotating-bar glyph on an outgoing PENDING bubble. */
static lv_obj_t *g_pend[UI_MSG_MAX_PER_CHAT];
static lv_timer_t *g_pend_timer;
static int g_pend_frame = 0;
static lv_obj_t *g_empty;

static ui_chat_msgs_t g_cur;
static int  g_count = 0;
static bool g_open  = false;   /* a conversation with messages is shown */
static bool g_sms   = false;   /* non-WhatsApp chat: no WA treatment    */

/* The page's contextual keymap (spec §4): SHOULDER-R = back to the chat
 * list, END = home. UP/DOWN are unbound — scrollback is by drag (§14.4),
 * and compose is a later stage (spec §9). CENTER does nothing: there is no
 * activation on this page yet. */
static const keymap_t km_conv = {
    .shoulder_l = ACT_NOOP,
    .shoulder_r = ACT_GO_BACK,
    .center     = ACT_NOOP,
    .call       = ACT_NOOP,
    .end        = ACT_GOTO_HOME,
    .up         = ACT_NOOP,
    .down       = ACT_NOOP,
    .left       = ACT_NOOP,
    .right      = ACT_NOOP,
    .t9 = {
        ACT_NOOP, ACT_NOOP, ACT_NOOP,
        ACT_NOOP, ACT_NOOP, ACT_NOOP,
        ACT_NOOP, ACT_NOOP, ACT_NOOP,
        ACT_NOOP, ACT_NOOP, ACT_NOOP,
    },
};

lv_obj_t *ui_chatview_root(void)
{
    return g_root;
}

const keymap_t *ui_chatview_keymap(void)
{
    return &km_conv;
}

static void del_and_null(lv_obj_t **slot)
{
    if (*slot) {
        lv_obj_delete(*slot);
        *slot = NULL;
    }
}

/* Tear down the previous conversation's objects (opening a new chat). */
static void clear_chain(void)
{
    for (int i = 0; i < UI_MSG_MAX_PER_CHAT; i++) {
        del_and_null(&g_bubble[i]);
        del_and_null(&g_body[i]);
        del_and_null(&g_from[i]);
        del_and_null(&g_time[i]);
        del_and_null(&g_divider[i]);
        del_and_null(&g_tick[i][0]);
        del_and_null(&g_tick[i][1]);
        del_and_null(&g_pend[i]);
    }
    if (g_pend_timer) {
        lv_timer_delete(g_pend_timer);
        g_pend_timer = NULL;
    }
    g_pend_frame = 0;
    del_and_null(&g_empty);
}

static void scroll_to_bottom(void)
{
    int view_h = ui_content_h() - UI_CONV_HDR_H;
    int y = lv_obj_get_height(g_flow) - view_h;
    if (y < 0) {
        y = 0;
    }
    lv_obj_scroll_to_y(g_scroll, y, LV_ANIM_OFF);
}

/* The pending bar's rotation frames: one bar, four orientations. */
static const char *const g_pend_frames[4] = { "|", "/", "_", "\\" };

static void pend_tick(lv_timer_t *t)
{
    (void)t;
    g_pend_frame = (g_pend_frame + 1) % 4;
    for (int i = 0; i < UI_MSG_MAX_PER_CHAT; i++) {
        if (g_pend[i]) {
            ui_text_set(g_pend[i], g_pend_frames[g_pend_frame]);
        }
    }
}

/* Lay the open chat's messages out as a bubble chain inside g_flow. */
static void build_chain(void)
{
    lv_font_t *body_f = ui_font_body_m();
    int line_h = lv_font_get_line_height(body_f);
    int max_w  = UI_PANEL_W - 2 * UI_BUBBLE_INSET_X;

    int y = UI_BUBBLE_PAD;
    for (int i = 0; i < g_count; i++) {
        const ui_message_t *m = &g_cur.msgs[i];

        /* Group boundary: a new side, and — when the gap is meaningful —
         * a divider chip between the groups. */
        if (i > 0) {
            y += UI_BUBBLE_GAP_GROUP;
            if (ui_msg_needs_divider(&g_cur.msgs[i - 1], m)) {
                char tbuf[24];
                ui_msg_time_fmt(m->ts, tbuf, sizeof tbuf);
                char full[32];
                snprintf(full, sizeof full, "[%s]", tbuf);
                g_divider[i] = lv_label_create(g_flow);
                ui_text_set(g_divider[i], full);
                lv_obj_set_style_text_font(g_divider[i], body_f, 0);
                lv_obj_set_style_text_color(g_divider[i], C_TEXT_DIM, 0);
                lv_obj_set_width(g_divider[i], UI_PANEL_W);
                lv_obj_align(g_divider[i], LV_ALIGN_TOP_MID, 0, y);
                y += line_h;
            }
        } else if (m->mine == g_cur.msgs[i - 1].mine) {
            y += UI_BUBBLE_GAP_SAME;
        }

        /* Group senders ride above the body, incoming side only. */
        bool has_from = (m->from[0] != '\0') && !m->mine;
        int from_h = has_from ? line_h : 0;

        lv_point_t sz;
        lv_text_get_size(&sz, m->body, body_f, 0, 0,
                         max_w - 2 * UI_BUBBLE_PAD, LV_TEXT_FLAG_NONE);
        int bw = sz.x + 2 * UI_BUBBLE_PAD;
        if (bw < 48) {
            bw = 48;
        }
        if (bw > max_w) {
            bw = max_w;
        }
        int bh = sz.y + from_h + 2 * UI_BUBBLE_PAD;
        int x = m->mine ? UI_PANEL_W - UI_BUBBLE_INSET_X - bw : UI_BUBBLE_INSET_X;

        g_bubble[i] = lv_obj_create(g_flow);
        lv_obj_set_size(g_bubble[i], bw, bh);
        lv_obj_set_pos(g_bubble[i], x, y);
        lv_obj_clear_flag(g_bubble[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(g_bubble[i], UI_BUBBLE_RADIUS, 0);
        lv_obj_set_style_bg_color(g_bubble[i],
                                  m->mine ? C_HIGHLIGHT : C_SURFACE, 0);
        lv_obj_set_style_bg_opa(g_bubble[i], LV_OPA_COVER, 0);

        if (has_from) {
            g_from[i] = lv_label_create(g_bubble[i]);
            ui_text_set(g_from[i], m->from);
            lv_obj_set_style_text_font(g_from[i], body_f, 0);
            lv_obj_set_style_text_color(g_from[i], C_TEXT_DIM, 0);
            lv_obj_set_size(g_from[i], bw - 2 * UI_BUBBLE_PAD, line_h);
            lv_obj_align(g_from[i], LV_ALIGN_TOP_LEFT,
                         UI_BUBBLE_PAD, UI_BUBBLE_PAD);
        }

        g_body[i] = lv_label_create(g_bubble[i]);
        ui_text_set(g_body[i], m->body);
        lv_obj_set_style_text_font(g_body[i], body_f, 0);
        lv_obj_set_style_text_color(g_body[i],
                                    m->mine ? C_ON_HL : C_TEXT, 0);
        lv_obj_set_size(g_body[i], bw - 2 * UI_BUBBLE_PAD, sz.y);
        lv_label_set_long_mode(g_body[i], LV_LABEL_LONG_WRAP);
        /* TOP_RIGHT takes a NEGATIVE x offset to pull the right edge in by
         * the pad (positive would push it past the bubble). */
        lv_obj_align(g_body[i],
                     m->mine ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT,
                     m->mine ? -UI_BUBBLE_PAD : UI_BUBBLE_PAD,
                     UI_BUBBLE_PAD + from_h);

        /* Delivery marks (outgoing WA chats only): two UI_TICK_W x
         * UI_TICK_H marks on the bottom edge, touching it, right-aligned
         * inside the padding. WAHA ack scale: PENDING shows nothing,
         * SERVER one dim mark, DEVICE two dim marks, READ/PLAYED two
         * ON_HIGHLIGHT marks (PLAYED is rendered as READ — at this size there
         * is no room for a separate "played" glyph, and a played note
         * has at least been read). SMS chats get none of this. */
        if (m->mine && !g_sms) {
            int n_marks = (m->ack >= UI_MSG_ACK_SERVER) ? 1 : 0;
            if (m->ack >= UI_MSG_ACK_DEVICE) {
                n_marks = 2;
            }
            /* Delivery marks (spec §14.9): sent/delivered = TEXT_DIM,
             * read/played = ON_HIGHLIGHT — the role designed for text on a
             * C_HL bubble. ACCENT is the same orange as the light C_HL
             * bubble: on it, the marks were invisible (1.00:1). */
            lv_color_t tick_c =
                (m->ack >= UI_MSG_ACK_READ) ? C_ON_HL : C_TEXT_DIM;
            for (int t = 0; t < 2; t++) {
                g_tick[i][t] = lv_obj_create(g_bubble[i]);
                lv_obj_remove_style_all(g_tick[i][t]);
                lv_obj_set_size(g_tick[i][t], UI_TICK_W, UI_TICK_H);
                int tx = bw - UI_BUBBLE_PAD - (t + 1) * UI_TICK_W -
                         t * UI_TICK_GAP;
                /* Touch the bubble's bottom edge. */
                lv_obj_set_pos(g_tick[i][t], tx, bh - UI_TICK_H);
                lv_obj_set_style_bg_color(g_tick[i][t], tick_c, 0);
                lv_obj_set_style_bg_opa(g_tick[i][t], LV_OPA_COVER, 0);
                if (t >= n_marks) {
                    lv_obj_add_flag(g_tick[i][t], LV_OBJ_FLAG_HIDDEN);
                }
            }

            /* An unsent (PENDING) message shows a single rotating bar in
             * the mark slot: "|" "/" "_" "\"" cycling, one turn per
             * second. Harness runs pin it to the first frame so golden
             * frames are reproducible. */
            if (m->ack == UI_MSG_ACK_PENDING) {
                lv_point_t psz;
                lv_text_get_size(&psz, g_pend_frames[0], body_f, 0, 0,
                                 LV_COORD_MAX, LV_TEXT_FLAG_NONE);
                g_pend[i] = lv_label_create(g_bubble[i]);
                ui_text_set(g_pend[i], g_pend_frames[0]);
                lv_obj_set_style_text_font(g_pend[i], body_f, 0);
                lv_obj_set_style_text_color(g_pend[i], C_TEXT_DIM, 0);
                lv_obj_set_size(g_pend[i], psz.x, line_h);
                lv_obj_set_pos(g_pend[i], bw - UI_BUBBLE_PAD - psz.x,
                               bh - line_h);
            }
        }

        y += bh;

        /* The group's timestamp under its last bubble. */
        bool last_of_group = (i == g_count - 1) ||
                             (g_cur.msgs[i + 1].mine != m->mine);
        if (last_of_group) {
            char tbuf[24];
            ui_msg_time_fmt(m->ts, tbuf, sizeof tbuf);
            lv_point_t tsz;
            /* max_width must be COORD_MAX, not 0: a zero width makes
             * lv_text_get_next_line() return only the FIRST letter. */
            lv_text_get_size(&tsz, tbuf, body_f, 0, 0,
                             LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            g_time[i] = lv_label_create(g_flow);
            ui_text_set(g_time[i], tbuf);
            lv_obj_set_style_text_font(g_time[i], body_f, 0);
            lv_obj_set_style_text_color(g_time[i], C_TEXT_DIM, 0);
            lv_obj_set_pos(g_time[i],
                           m->mine ? x + bw - tsz.x - 2 : UI_BUBBLE_INSET_X, y);
            y += line_h;
        }
    }

    /* Drive the pending bars: one shared timer, 250 ms per frame. */
    for (int i = 0; i < g_count; i++) {
        if (g_pend[i] && !ui_shell_clock_frozen()) {
            g_pend_timer = lv_timer_create(pend_tick, 250, NULL);
            break;
        }
    }

    y += UI_BUBBLE_PAD;
    lv_obj_set_height(g_flow, y);
}

/* A chat with no messages: an honest centred line, not a fake thread. */
static void build_empty(void)
{
    g_empty = lv_label_create(g_flow);
    ui_text_set(g_empty, "No messages");
    lv_obj_set_style_text_font(g_empty, ui_font_body_m(), 0);
    lv_obj_set_style_text_color(g_empty, C_TEXT_DIM, 0);
    lv_obj_set_width(g_empty, UI_PANEL_W);
    lv_obj_align(g_empty, LV_ALIGN_TOP_MID, 0, 2 * UI_BUBBLE_PAD);
    lv_obj_set_height(g_flow,
                      2 * UI_BUBBLE_PAD + lv_font_get_line_height(ui_font_body_m()));
}

void ui_chatview_open(int chat_idx, const char *name)
{
    clear_chain();
    g_count = 0;
    g_open = false;
    g_sms = ui_chat_is_sms(chat_idx);

    ui_text_set(g_name, name ? name : "");

    if (ui_chat_messages_load(chat_idx, &g_cur) == 0) {
        g_open = true;
        g_count = g_cur.count;
        build_chain();
    } else {
        build_empty();
    }
    scroll_to_bottom();
}

void ui_chatview_restyle(void)
{
    if (!g_root) {
        return;
    }
    lv_font_t *body_f = ui_font_body_m();
    lv_obj_set_style_text_font(g_caret, ui_font_icon_s(), 0);
    lv_obj_set_style_text_color(g_caret, C_TEXT_DIM, 0);
    lv_obj_set_style_text_font(g_name, ui_font_title(), 0);
    lv_obj_set_style_text_color(g_name, C_TEXT, 0);
    lv_obj_set_style_bg_color(g_hdr_line, C_BORDER, 0);
    for (int i = 0; i < UI_MSG_MAX_PER_CHAT; i++) {
        const ui_message_t *m = (g_open && i < g_count) ? &g_cur.msgs[i] : NULL;
        if (g_bubble[i] && m) {
            lv_obj_set_style_bg_color(g_bubble[i],
                                      m->mine ? C_HIGHLIGHT : C_SURFACE, 0);
        }
        if (g_body[i] && m) {
            lv_obj_set_style_text_font(g_body[i], body_f, 0);
            lv_obj_set_style_text_color(g_body[i],
                                        m->mine ? C_ON_HL : C_TEXT, 0);
        }
        if (m && m->mine) {
            /* sent/delivered = TEXT_DIM, read/played = ON_HIGHLIGHT — the
             * role designed for text on a C_HL bubble (spec §14.9). */
            lv_color_t tick_c =
                (m->ack >= UI_MSG_ACK_READ) ? C_ON_HL : C_TEXT_DIM;
            for (int t = 0; t < 2; t++) {
                if (g_tick[i][t]) {
                    lv_obj_set_style_bg_color(g_tick[i][t], tick_c, 0);
                    lv_obj_set_style_bg_opa(g_tick[i][t], LV_OPA_COVER, 0);
                }
            }
        }
        if (g_pend[i]) {
            lv_obj_set_style_text_font(g_pend[i], body_f, 0);
            lv_obj_set_style_text_color(g_pend[i], C_TEXT_DIM, 0);
        }
        if (g_from[i]) {
            lv_obj_set_style_text_font(g_from[i], body_f, 0);
            lv_obj_set_style_text_color(g_from[i], C_TEXT_DIM, 0);
        }
        if (g_time[i]) {
            lv_obj_set_style_text_font(g_time[i], body_f, 0);
            lv_obj_set_style_text_color(g_time[i], C_TEXT_DIM, 0);
        }
        if (g_divider[i]) {
            lv_obj_set_style_text_font(g_divider[i], body_f, 0);
            lv_obj_set_style_text_color(g_divider[i], C_TEXT_DIM, 0);
        }
    }
    if (g_empty) {
        lv_obj_set_style_text_font(g_empty, body_f, 0);
        lv_obj_set_style_text_color(g_empty, C_TEXT_DIM, 0);
    }
}

void ui_chatview_relayout(void)
{
    if (!g_root) {
        return;
    }
    lv_obj_set_size(g_root, UI_PANEL_W, ui_content_h());
    lv_obj_set_size(g_scroll, UI_PANEL_W, ui_content_h() - UI_CONV_HDR_H);
    scroll_to_bottom();
}

lv_obj_t *ui_chatview_build(lv_obj_t *parent)
{
    g_root = lv_obj_create(parent);
    lv_obj_remove_style_all(g_root);
    lv_obj_set_size(g_root, UI_PANEL_W, UI_CONTENT_H);
    lv_obj_set_pos(g_root, 0, 0);
    lv_obj_clear_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_root, LV_OBJ_FLAG_HIDDEN);

    g_caret = lv_label_create(g_root);
    lv_label_set_text(g_caret, I_CARET_L);
    lv_obj_set_style_text_font(g_caret, ui_font_icon_s(), 0);
    lv_obj_set_style_text_color(g_caret, C_TEXT_DIM, 0);
    lv_obj_set_pos(g_caret, UI_CONV_CARET_X, UI_CONV_CARET_Y);

    g_name = lv_label_create(g_root);
    lv_obj_set_size(g_name, UI_PANEL_W - 4 * UI_BUBBLE_INSET_X,
                    UI_CONV_HDR_H - 6);
    lv_obj_align(g_name, LV_ALIGN_TOP_LEFT, UI_CONV_NAME_X, UI_CONV_NAME_Y);
    lv_obj_set_style_text_color(g_name, C_TEXT, 0);
    lv_obj_set_style_text_font(g_name, ui_font_title(), 0);
    lv_label_set_long_mode(g_name, LV_LABEL_LONG_DOT);
    ui_text_set(g_name, "");

    g_hdr_line = lv_obj_create(g_root);
    lv_obj_remove_style_all(g_hdr_line);
    lv_obj_set_size(g_hdr_line, UI_PANEL_W, 1);
    lv_obj_set_pos(g_hdr_line, 0, UI_CONV_HDR_H - 1);
    lv_obj_set_style_bg_color(g_hdr_line, C_BORDER, 0);
    lv_obj_set_style_bg_opa(g_hdr_line, LV_OPA_COVER, 0);

    g_scroll = lv_obj_create(g_root);
    lv_obj_remove_style_all(g_scroll);
    lv_obj_set_size(g_scroll, UI_PANEL_W, UI_CONTENT_H - UI_CONV_HDR_H);
    lv_obj_set_pos(g_scroll, 0, UI_CONV_HDR_H);
    lv_obj_set_scrollbar_mode(g_scroll, LV_SCROLLBAR_MODE_OFF);   /* flat */

    g_flow = lv_obj_create(g_scroll);
    lv_obj_remove_style_all(g_flow);
    lv_obj_set_width(g_flow, UI_PANEL_W);
    lv_obj_set_height(g_flow, 10);
    lv_obj_clear_flag(g_flow, LV_OBJ_FLAG_SCROLLABLE);

    return g_root;
}
