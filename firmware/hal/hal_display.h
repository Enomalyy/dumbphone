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

/* Push a w×h rectangle of packed RGB565 pixels (each row w*2 bytes, no
 * stride) to the panel at (x, y). The rectangle must lie inside the panel;
 * out-of-range rectangles are rejected. The host backend copies it into its
 * shadow panel buffer and presents just that region; an S3 port would DMA
 * only this window to the ST7796U, which is what makes partial updates
 * cheap on real hardware. */
int  hal_display_blit_area(const void *pixels, uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h);

/* Push a full frame of packed RGB565 pixels — hal_display_blit_area() over
 * the whole panel. Kept for callers that have no area to report. */
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
