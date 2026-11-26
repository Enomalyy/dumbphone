/*
 * hal_sdl_host.h — host-only (Tier 1) extensions. NOT part of the HAL
 * contract; nothing above the HAL may include this.
 *
 * The SDL video backend owns the one and only SDL event pump (SDL requires
 * it on the main thread). It forwards input events to the HAL input queue
 * via hal_input_sdl_event(), so the UI thread never touches SDL.
 */
#pragma once

#include <stdbool.h>

#include <stdint.h>

#include <SDL2/SDL.h>

#include "hal/hal_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Drain SDL events; sets the quit flag on window close, forwards input. */
void hal_display_pump(void);

/* Requested presentation scale for the whole mock-up (<=0 = auto-fit).
 * Call before hal_display_init(). */
void hal_display_set_scale(float scale);

/* Highlight a physical key as held (drawn pressed by the bezel). */
void hal_display_set_key_pressed(int index, bool pressed);

/* Dump the composed window (bezel + panel) as a binary PPM. */
int  hal_display_write_window_ppm(const char *path);

/* Composed window geometry, in pixels. */
void hal_display_window_size(int *w, int *h);

/* Copy the composed window (ARGB8888, w*h uint32) into dst. Thread-safe:
 * the frame server reads frames while LVGL is drawing. */
int  hal_display_snapshot(uint32_t *dst);

/* Inject a named HAL key from outside the UI (the browser viewport). */
void hal_input_inject_key(hal_key_t key, bool down);

/* Inject a touch point from outside the UI (the shot harness's --swipe).
 * Coordinates are panel pixels (0..319 x 0..479). The first call with
 * down=true is a TOUCH_DOWN; further down=true calls while the touch is
 * active are TOUCH_MOVEs; down=false releases (TOUCH_UP). Same queue as
 * the mouse, so the UI cannot tell the source apart. */
void hal_input_inject_touch(uint16_t x, uint16_t y, bool down);

/* True after the window was closed. */
bool hal_display_quit_requested(void);

/* Tear the window down. */
void hal_display_shutdown(void);

/* Translate one SDL event (mouse/keyboard/wheel) into HAL input events and
 * push them onto the HAL input queue. Called only from the SDL pump. */
void hal_input_sdl_event(const SDL_Event *e);

#ifdef __cplusplus
}
#endif
