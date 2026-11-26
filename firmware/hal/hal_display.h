/*
 * hal_display.h — display panel interface (Prototype #1: 3.5" ST7796U + GT911,
 * see ../datasheets/). Declarations only; backends come in a later phase:
 *   - s3 backend: esp_lcd + esp_lcd_qemu_rgb (QEMU virtual framebuffer)
 *   - host backend: LVGL SDL2 window (Tier 1 native)
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t width;   /* pixels */
    uint16_t height;  /* pixels */
    uint8_t  bpp;     /* bits per pixel (16 for RGB565) */
} hal_display_info_t;

/* Bring the panel up (bus, reset, init sequence, backlight off). */
int  hal_display_init(void);

/* Geometry of the active panel. */
void hal_display_info(hal_display_info_t *out);

/* Push a full frame of packed RGB565 pixels. */
int  hal_display_blit(const void *pixels, size_t len_bytes);

/* Backlight / panel power. */
int  hal_display_set_backlight(uint8_t percent);

/* Pointer to the current full-frame buffer (width*height*2 bytes, packed
 * RGB565). Valid until the next blit; lets a frame-grab/screenshot backend
 * read the panel without touching the UI. */
const void *hal_display_framebuffer(void);

#ifdef __cplusplus
}
#endif
