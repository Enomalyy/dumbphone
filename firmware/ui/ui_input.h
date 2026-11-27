/*
 * ui_input.h — HAL input plumbing: drain the event queue, recognise gestures.
 *
 * This is the boundary between raw device events and the UI's meaning layer.
 * It owns: draining hal_input_poll(), the touch/swipe state machine (a
 * sideways-dominant drag past the threshold fires one swipe), and the per-key
 * press timestamps (so a release can report how long the key was held).
 *
 * It does NOT own what anything MEANS: which key does what, whether the
 * phone is locked, which page is on top — the shell decides, and it receives
 * one callback per recognised intent.
 */
#pragma once

#include <stdint.h>

#include "hal/hal_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Long-press threshold (spec §2: held presses = custom actions). The press
 * completes on KEY_UP, so the recogniser reports the measured duration and
 * lets the shell compare it against this. */
#define UI_LONG_PRESS_MS 500

/* A swipe must move this far sideways to count (spec §1). */
#define UI_SWIPE_MIN_PX 50

/* Vertical drag must move this far before scrolling starts (spec §14.4):
 * one shared dead zone for EVERY scroll target, so every page feels the
 * same. Set on the indev itself, not per widget. Same value as the
 * horizontal swipe threshold on purpose. */
#define UI_SCROLL_DEADZONE_PX 10

/* Key repeat for held-down UP/DOWN (spec §14.8): the first step fires on the
 * key-down itself; repeats then start after a dwell and fire at an interval
 * that halves per repeat down to a floor. Release stops motion instantly.
 */
#define UI_KEY_REPEAT_DWELL_MS 400
#define UI_KEY_REPEAT_START_MS 200
#define UI_KEY_REPEAT_MIN_MS   40

/* The next repeat interval after one has fired: halve, clamped at the floor.
 * Pure — unit-tested in test/test_host.c. */
static inline uint32_t ui_key_repeat_next_interval(uint32_t cur)
{
    uint32_t half = cur / 2;
    return (half < UI_KEY_REPEAT_MIN_MS) ? UI_KEY_REPEAT_MIN_MS : half;
}

/* A completed key press: `key` was released after `held_ms`. */
typedef void (*ui_input_key_cb)(hal_key_t key, uint32_t held_ms);

/* A horizontal swipe: `dir` is +1 for left (next), -1 for right (previous). */
typedef void (*ui_input_swipe_cb)(int dir);

/* Install the intent handlers. Called once, before the UI thread starts
 * polling. Unset handlers are simply skipped. */
void ui_input_set_cbs(ui_input_key_cb on_key, ui_input_swipe_cb on_swipe);

/* Drain everything hal_input_poll() has queued, updating the gesture state
 * and issuing one callback per completed intent. UI thread only. */
void ui_input_poll(void);

/* Register the LVGL pointer device driven by the HAL's touch state: this
 * module owns the port, so taps/drags reach the widgets too. Call once,
 * after lv_init() and before the UI runs. Sets the shared §14.4 scroll dead
 * zone on the indev itself, so every scroll target under it feels the same. */
void ui_input_attach_indev(void);

/* Repeat engine (spec §14.8). `can_repeat` decides per key whether a press
 * may start repeating (the shell checks lock state and held-press config —
 * an explicit user binding wins over implicit repeat); `on_step` is the
 * action for the first step (fired on the key-down itself) and every repeat
 * tick. Both NULL = no key repeat at all. */
typedef bool (*ui_input_can_repeat_cb)(hal_key_t key);
typedef void (*ui_input_step_cb)(hal_key_t key);
void ui_input_set_repeat_cbs(ui_input_can_repeat_cb can_repeat,
                             ui_input_step_cb on_step);

/* True while a press of `key` already fired its step on the down event, so
 * the release handler does not run it twice. */
bool ui_input_fired_on_down(hal_key_t key);

#ifdef __cplusplus
}
#endif
