/*
 * ui_input.c — HAL input plumbing: drain the event queue, recognise gestures.
 *
 * Owns: the hal_input_poll() drain loop, the touch/swipe state machine, and
 * the per-key press timestamps. Exposes: ui_input_set_cbs / ui_input_poll
 * (ui_input.h). Must not touch: keymaps, the screen stack, the lock state,
 * or any page — the shell's callbacks decide what each intent means.
 *
 * Static state only: nothing here allocates.
 */

#include <stdbool.h>
#include <stdlib.h>   /* abs() */

#include "lvgl.h"

#include "hal/hal_input.h"
#include "ui_input.h"

static ui_input_key_cb key_cb = NULL;
static ui_input_swipe_cb swipe_cb = NULL;

/* The HAL's touch state, as LVGL's pointer device sees it. */
static struct {
    int16_t x, y;
    bool pressed;
} touch_state = { 0, 0, false };

/* A touch drag in progress: where it started and whether it already fired. */
static struct {
    bool   active;   /* a touch drag is in progress        */
    int16_t x0, y0;  /* down position, panel px            */
    bool   fired;    /* this drag already fired the swipe  */
} swipe_state = { false, 0, 0, false };

static uint32_t key_down_ms[HAL_KEY__COUNT];
static bool     key_is_down[HAL_KEY__COUNT];

/* Repeat engine state per key (spec §14.8). */
static ui_input_can_repeat_cb can_repeat = NULL;
static ui_input_step_cb       step_cb    = NULL;
static struct {
    bool       active;      /* this press may repeat                */
    uint32_t   next_ms;     /* deadline of the next repeat tick     */
    uint32_t   interval_ms; /* current repeat interval              */
} key_rep[HAL_KEY__COUNT];

/* Set when a press fired its step on the down event (spec §14.8); read by
 * the release handler so it does not run the step twice, cleared only
 * after that handler has run. */
static bool fired_on_down[HAL_KEY__COUNT];

static void indev_read_cb(lv_indev_t *indev, lv_indev_data_t *d)
{
    (void)indev;
    d->point.x = touch_state.x;
    d->point.y = touch_state.y;
    d->state = touch_state.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void ui_input_attach_indev(void)
{
    lv_indev_t *indev = lv_indev_create();
    if (!indev) {
        return;
    }
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    /* One shared drag dead zone for every scroll target (spec §14.4): a
     * vertical drag must move UI_SCROLL_DEADZONE_PX before scrolling starts,
     * on every page. Set here — on the indev itself — so no widget can
     * drift from the contract. */
    lv_indev_set_scroll_limit(indev, UI_SCROLL_DEADZONE_PX);
    lv_indev_set_read_cb(indev, indev_read_cb);
    /* Read faster than lv_timer_handler runs so drags stay responsive. */
    lv_timer_set_period(lv_indev_get_read_timer(indev), 10);
}

void ui_input_set_cbs(ui_input_key_cb on_key, ui_input_swipe_cb on_swipe)
{
    key_cb = on_key;
    swipe_cb = on_swipe;
}

void ui_input_poll(void)
{
    hal_input_event_t ev;
    while (hal_input_poll(&ev)) {
        /* A backend that reports a key id past HAL_KEY__COUNT would index
         * the press tables out of bounds; every real backend maps its
         * hardware onto the enum, so this only catches a porting mistake. */
        if ((ev.type == HAL_INPUT_KEY_DOWN || ev.type == HAL_INPUT_KEY_UP) &&
            (unsigned)ev.u.key >= (unsigned)HAL_KEY__COUNT) {
            continue;
        }
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
                if ((dx > UI_SWIPE_MIN_PX || dx < -UI_SWIPE_MIN_PX) &&
                    (dy > -abs(dx) && dy < abs(dx))) {
                    swipe_state.fired = true;
                    if (swipe_cb) {
                        swipe_cb(dx < 0 ? 1 : -1);   /* left=next, right=prev */
                    }
                }
            }
            break;

        case HAL_INPUT_TOUCH_UP:
            touch_state.pressed = false;
            swipe_state.active = false;
            break;

        case HAL_INPUT_KEY_DOWN: {
            hal_key_t k = ev.u.key;
            uint32_t now = lv_tick_get();
            key_down_ms[k] = now;
            key_is_down[k] = true;
            key_rep[k].active = false;
            fired_on_down[k] = false;
            if (can_repeat && step_cb && can_repeat(k)) {
                /* §14.8: the first step fires on the press itself — input
                 * latency, not animation, is what makes a held key feel
                 * responsive. Repeats then start after the dwell. */
                key_rep[k].active = true;
                fired_on_down[k] = true;
                key_rep[k].interval_ms = UI_KEY_REPEAT_START_MS;
                key_rep[k].next_ms = now + UI_KEY_REPEAT_DWELL_MS +
                                            UI_KEY_REPEAT_START_MS;
                step_cb(k);
            }
            break;
        }

        case HAL_INPUT_KEY_UP: {
            /* lv_tick_get() is the shell's monotonic tick source (it installs
             * this very function via lv_tick_set_cb), so press durations are
             * measured on the UI's own clock. */
            hal_key_t k = ev.u.key;
            uint32_t dur = key_is_down[k]
                               ? lv_tick_get() - key_down_ms[k]
                               : 0;   /* stray UP without a DOWN */
            key_is_down[k] = false;
            /* §14.8: release stops the repeat motion instantly. */
            key_rep[k].active = false;
            if (key_cb) {
                key_cb(k, dur);
            }
            /* Cleared only after the release handler ran — it consults
             * ui_input_fired_on_down() while it is executing. */
            fired_on_down[k] = false;
            break;
        }

        case HAL_INPUT_NONE:
        default:
            break;
        }
    }

    /* Repeat engine (spec §14.8): a held key fires its step on every
     * deadline, the interval halving per tick down to the floor. Runs after
     * the drain so a KEY_UP queued in the same batch has already stopped it.
     */
    if (step_cb) {
        uint32_t now = lv_tick_get();
        for (int k = 0; k < HAL_KEY__COUNT; k++) {
            if (!key_is_down[k] || !key_rep[k].active) {
                continue;
            }
            while (now >= key_rep[k].next_ms) {
                step_cb(k);
                key_rep[k].interval_ms = ui_key_repeat_next_interval(
                                             key_rep[k].interval_ms);
                key_rep[k].next_ms += key_rep[k].interval_ms;
            }
        }
    }
}

void ui_input_set_repeat_cbs(ui_input_can_repeat_cb can_repeat_cb,
                             ui_input_step_cb on_step)
{
    can_repeat = can_repeat_cb;
    step_cb = on_step;
}

bool ui_input_fired_on_down(hal_key_t key)
{
    return (unsigned)key < (unsigned)HAL_KEY__COUNT && fired_on_down[key];
}
