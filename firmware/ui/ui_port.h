/*
 * ui_port.h — the target seam (spec §16.2, issue #17).
 *
 * The UI may ask its environment for exactly these things, and nothing else.
 * Before this seam ui_shell.c owned them itself: it created the LVGL display,
 * read the clock with clock_gettime(), took the wall clock through localtime_r,
 * loaded TTFs from a directory path, and asserted its own thread with pthread —
 * five assumptions only the simulator had ever tested. Each target supplies one
 * implementation:
 *
 *   host/src/ui_port_host.c    the Tier-1 simulator (SDL, a filesystem, pthreads)
 *   hal/esp32s3/ui_port_esp32.c  the device image (esp_lcd, esp_timer, NVS, GPIO)
 *
 * Storage and input are deliberately NOT here: they are already HAL contracts
 * (hal/hal_storage.h, hal/hal_input.h) and the UI reads through them. This
 * header covers the five things §16.2 listed plus the lock the cross-thread
 * request slot uses, which is the same guardrail.
 *
 * The UI must have exactly one text path: a face is whatever ui_port_font()
 * hands back, whether it came from a file or from flash.
 */
#pragma once

#include <stdbool.h>
#include <time.h>

#include "lvgl.h"

/* Wire LVGL to the target: lv_init, the tick source, the display, the draw
 * buffer (PSRAM on the device) and the region flush into hal_display_blit_area().
 * The target is expected to have brought the HAL backends up first. */
int ui_port_init(void);

/* The display the port created — the UI never creates one. */
lv_display_t *ui_port_display(void);

/* One face at one size. Host: tiny_ttf reading host/assets/fonts. Device: the
 * same face read straight out of the copy embedded in the image. NULL when the
 * face is unavailable — the caller's fallback policy is the UI's, not the port's. */
lv_font_t *ui_port_font(const char *name, int px);

/* Wall clock for the status bar and the lock screen. */
void ui_port_now(struct tm *out);

/* LVGL is not thread-safe: only the UI thread/task may touch it (§16.2). */
bool ui_port_self_is_ui_thread(void);

/* The cross-thread request slot: producers write, the UI thread consumes. */
void ui_port_lock(void);
void ui_port_unlock(void);
