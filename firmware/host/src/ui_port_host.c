/*
 * ui_port_host.c — the Tier-1 simulator's side of the seam (ui/ui_port.h).
 *
 * Everything here is what ui_shell.c used to own: the display creation, the
 * tick source, the wall clock, the font source (a directory of TTFs through
 * LVGL's fs driver), the UI-thread guard and the request-slot lock. The
 * behaviour is deliberately unchanged — the golden frames are the proof: this
 * file moves code, it does not change it. The device side (hal/esp32s3/)
 * implements the same header against esp_lcd/esp_timer/NVS, and the UI in ui/
 * cannot tell the difference.
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>   /* atexit(), UI_FLUSH_STATS builds only */
#include <time.h>

#include "lvgl.h"

#include "hal/hal_display.h"
#include "ui/ui_port.h"

/* ------------------------------------------------------------------ */
/* State the port owns                                                 */
/* ------------------------------------------------------------------ */

static lv_display_t *port_display;
static uint8_t *draw_buf;
static uint16_t panel_w, panel_h;   /* the panel this target drives */

static pthread_t port_thread;                  /* the one thread allowed to touch LVGL */
static bool port_thread_set = false;
static pthread_mutex_t port_lock = PTHREAD_MUTEX_INITIALIZER;

/* Optional flush instrumentation (build with -DUI_FLUSH_STATS=1): counts the
 * bytes pushed through hal_display_blit*, the flush count, and the largest
 * single area; a summary prints at exit. Off by default — none of this
 * compiles into a normal build. */
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

/* LVGL tick source: monotonic clock. The device side of the seam points this
 * at esp_timer_get_time() / 1000. */
static uint32_t tick_get_cb(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* ------------------------------------------------------------------ */
/* Display glue: LVGL flush -> HAL region blit                        */
/* ------------------------------------------------------------------ */

static void display_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    /* Present ONLY the dirty area (audit §3.1): the full-frame shadow and the
     * browser viewport are the backend's business — hal_display_blit_area()
     * copies the region into the panel shadow before it presents it — so this
     * callback is a straight region hand-off. A backend with no shadow
     * (hal_display_framebuffer() == NULL) still displays. */
    int x = (int)area->x1;
    int y = (int)area->y1;
    int w = (int)(area->x2 - area->x1 + 1);
    int h = (int)(area->y2 - area->y1 + 1);

    /* LVGL never asks for an area outside the display, but a bad panel/driver
     * pairing must not become a wild write or a silent dropped frame. */
    if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
        x + w > (int)panel_w || y + h > (int)panel_h) {
        static bool area_warned;
        if (!area_warned) {
            area_warned = true;
            printf("ui_port: flush area (%d,%d %dx%d) outside the %dx%d panel "
                   "— skipped\n", x, y, w, h, panel_w, panel_h);
        }
        lv_display_flush_ready(disp);
        return;
    }

    /* Rows are handed over packed (w*2 bytes). LVGL's own buffer is packed
     * while LV_DRAW_BUF_STRIDE_ALIGN is 1 (the host lv_conf), but a device
     * lv_conf that pads rows for cache alignment would break that silently,
     * so read the real stride and fall back to row-by-row presentation. */
    lv_draw_buf_t *buf = lv_display_get_buf_active(disp);
    size_t stride = (buf && buf->header.stride) ? buf->header.stride
                                                : (size_t)w * 2;
    int rc;
    if (stride == (size_t)w * 2) {
        rc = hal_display_blit_area(px, (uint16_t)x, (uint16_t)y,
                                   (uint16_t)w, (uint16_t)h);
    } else {
        rc = 0;
        for (int ry = 0; ry < h; ry++) {
            rc |= hal_display_blit_area(px + (size_t)ry * stride,
                                        (uint16_t)x, (uint16_t)(y + ry),
                                        (uint16_t)w, 1);
        }
    }
    if (rc != 0) {
        static bool blit_warned;
        if (!blit_warned) {
            blit_warned = true;
            printf("ui_port: hal_display_blit_area rejected a %dx%d area at "
                   "(%d,%d)\n", w, h, x, y);
        }
    }
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
/* The seam                                                            */
/* ------------------------------------------------------------------ */

int ui_port_init(void)
{
    port_thread = pthread_self();
    port_thread_set = true;

#ifdef UI_FLUSH_STATS
    atexit(flush_stats_summary);   /* the UI thread is joined before exit */
#endif

    lv_init();
    lv_tick_set_cb(tick_get_cb);

    hal_display_info_t info;
    hal_display_info(&info);
    panel_w = info.width;
    panel_h = info.height;

    /* Full-frame RGB565 draw buffer (2 bytes/px). Heap: the panel size is a
     * runtime value (ui_geometry.c, spec §15), so it cannot be a static array.
     * Allocated once here; no allocation in the hot path. On the device this is
     * a partial buffer in PSRAM — the flush callback owns nothing but the region
     * LVGL hands it. */
    draw_buf = malloc((size_t)panel_w * panel_h * 2);
    if (!draw_buf) {
        printf("ui_port: draw buffer allocation failed\n");
        return -1;
    }

    port_display = lv_display_create(panel_w, panel_h);
    lv_display_set_color_format(port_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(port_display, draw_buf, NULL,
                           (size_t)panel_w * panel_h * 2,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(port_display, display_flush_cb);
    return 0;
}

lv_display_t *ui_port_display(void)
{
    return port_display;
}

/* Load a TTF from the font dir (absolute path first, then a CWD-relative
 * assets/fonts path for a build tree run from another directory). NULL when
 * unavailable. Called on the UI thread, so the fonts are created in this LVGL
 * instance before any screen renders. */
lv_font_t *ui_port_font(const char *name, int px)
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

void ui_port_now(struct tm *out)
{
    time_t now = time(NULL);
    localtime_r(&now, out);
}

bool ui_port_self_is_ui_thread(void)
{
    return !port_thread_set || pthread_equal(pthread_self(), port_thread);
}

void ui_port_lock(void)   { pthread_mutex_lock(&port_lock); }
void ui_port_unlock(void) { pthread_mutex_unlock(&port_lock); }
