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
 * after lv_init() and before the UI runs. */
void ui_input_attach_indev(void);

#ifdef __cplusplus
}
#endif
