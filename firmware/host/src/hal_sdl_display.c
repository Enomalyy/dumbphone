/*
 * hal_sdl_display.c — Tier-1 (desktop) backend for hal/hal_display.h.
 *
 * The window shows a phone mock-up: the runtime-sized RGB565 panel (the ONLY
 * thing the HAL knows about — its size comes from ui/ui_geometry.c via the
 * bezel) surrounded by a bezel carrying the physical keys, see bezel.c. Presentation is: compose bezel background -> scale the panel into
 * its recess -> re-draw any held key -> blit to the window.
 *
 * hal_display_framebuffer() keeps returning the pure panel buffer, so frame
 * dumps and any future viewport backend see device pixels only.
 */

#include <SDL2/SDL.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bezel.h"
#include "hal/hal_display.h"
#include "hal_sdl_host.h"
#include "ui/ui_geometry.h"

static uint16_t *frame;               /* canonical panel buffer, RGB565 */
static int s_fb_w, s_fb_h;            /* runtime panel size (device px)  */
static SDL_Window *window;
static SDL_Surface *fb_surface;       /* wraps `frame`, zero-copy */
static SDL_Surface *compose;          /* window-sized ARGB8888 composition */
static SDL_Surface *bezel_bg;         /* cached bezel + unpressed keys */
static float requested_scale;         /* <=0: auto-fit */
static bool quit_requested = false;

/* Which physical keys are currently held (drawn pressed). Must cover the
 * whole bezel table, or the draw loop reads past the end. */
#define MAX_BEZEL_KEYS 64
static bool key_held[MAX_BEZEL_KEYS];

/* Compose+present runs from two threads: LVGL flushes on the UI thread, key
 * press/release redraws arrive on the main (event) thread. One lock keeps the
 * surfaces consistent. */
static SDL_mutex *gfx_lock;

static int compose_and_present(const SDL_Rect *panel_area);

int hal_display_init(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return -1;
    }

    /* The panel size is a runtime value — ui_geometry.c is the single
     * source of truth (spec §15 / issue #1). */
    s_fb_w = ui_geom_w();
    s_fb_h = ui_geom_h();
    frame = malloc((size_t)s_fb_w * s_fb_h * sizeof(uint16_t));
    if (!frame) {
        SDL_Log("panel framebuffer allocation failed");
        return -1;
    }

    bezel_init(s_fb_w, s_fb_h, requested_scale);

    int win_w = bezel_window_w();
    int win_h = bezel_window_h();

    char title[96];
    snprintf(title, sizeof title, "dumbphone — %dx%d panel + bezel",
             s_fb_w, s_fb_h);
    window = SDL_CreateWindow(title,
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              win_w, win_h, SDL_WINDOW_SHOWN);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return -1;
    }

    compose = SDL_CreateRGBSurfaceWithFormat(0, win_w, win_h, 32,
                                             SDL_PIXELFORMAT_ARGB8888);
    bezel_bg = SDL_CreateRGBSurfaceWithFormat(0, win_w, win_h, 32,
                                              SDL_PIXELFORMAT_ARGB8888);
    if (!compose || !bezel_bg) {
        SDL_Log("surface allocation failed: %s", SDL_GetError());
        return -1;
    }
    gfx_lock = SDL_CreateMutex();
    if (!gfx_lock) {
        SDL_Log("mutex allocation failed: %s", SDL_GetError());
        return -1;
    }
    bezel_draw_background(bezel_bg);

    fb_surface = SDL_CreateRGBSurfaceFrom(frame, s_fb_w, s_fb_h, 16,
                                          s_fb_w * 2, 0xF800, 0x07E0, 0x001F, 0);
    if (!fb_surface) {
        SDL_Log("SDL_CreateRGBSurfaceFrom failed: %s", SDL_GetError());
        return -1;
    }

    SDL_Log("desktop simulator: panel %dx%d, bezel x%.2f -> window %dx%d",
            s_fb_w, s_fb_h, (double)bezel_scale(), win_w, win_h);

    /* Prime the composition. hal_display_blit_area() only re-composes the
     * dirty recess region (that is the whole point of the region blit), so
     * without this first full pass the bezel and the window surface would
     * stay uninitialised until the first key press. */
    return compose_and_present(NULL);
}

void hal_display_info(hal_display_info_t *out)
{
    out->width = s_fb_w;
    out->height = s_fb_h;
    out->bpp = 16;
}

/* Rebuild the window from the panel buffer + bezel. Caller holds gfx_lock.
 * `panel_area` (panel pixels) selects which part of the recess to
 * re-compose; NULL re-composes the whole window, bezel included. */
static int compose_and_present(const SDL_Rect *panel_area)
{
    if (!panel_area) {
        /* 1. bezel + unused keys (cached) */
        SDL_BlitSurface(bezel_bg, NULL, compose, NULL);
    }

    /* 2. the panel (or its dirty part), scaled into its recess. Both dst
     * edges are scaled from the SRC edges first and only then offset by the
     * recess origin: subtracting the already-offset dst.x instead shrank
     * every area by the bezel margin (the full frame came out 276x454 in a
     * 320x480 recess — a visibly compressed panel). Because adjacent areas
     * share the same boundary formula, they still tile the recess exactly. */
    int px, py, pw, ph;
    bezel_panel_rect(&px, &py, &pw, &ph);
    const SDL_Rect *src = panel_area ? panel_area
                                     : &(const SDL_Rect){0, 0, s_fb_w, s_fb_h};
    const int sx0 = (int)(((uint64_t)src->x * pw) / s_fb_w);
    const int sy0 = (int)(((uint64_t)src->y * ph) / s_fb_h);
    const int sx1 = (int)(((uint64_t)(src->x + src->w) * pw) / s_fb_w);
    const int sy1 = (int)(((uint64_t)(src->y + src->h) * ph) / s_fb_h);
    SDL_Rect dst;
    dst.x = px + sx0;
    dst.y = py + sy0;
    dst.w = sx1 - sx0;
    dst.h = sy1 - sy0;
    if (SDL_BlitScaled(fb_surface, src, compose, &dst) != 0) {
        return -1;
    }

    /* 3. held keys on top */
    size_t nkeys = bezel_button_count();
    if (nkeys > MAX_BEZEL_KEYS) {
        nkeys = MAX_BEZEL_KEYS;
    }
    for (size_t i = 0; i < nkeys; i++) {
        if (key_held[i]) {
            bezel_draw_button(compose, i, true);
        }
    }

    /* 4. present — the whole window for a full re-compose, only the
     * updated recess region for a partial one. */
    SDL_Surface *ws = SDL_GetWindowSurface(window);
    if (ws) {
        if (panel_area) {
            SDL_BlitSurface(compose, &dst, ws, &dst);
            SDL_UpdateWindowSurfaceRects(window, &dst, 1);
        } else {
            SDL_BlitSurface(compose, NULL, ws, NULL);
            SDL_UpdateWindowSurface(window);
        }
    }
    return 0;
}

int hal_display_blit_area(const void *pixels, uint16_t x, uint16_t y,
                          uint16_t w, uint16_t h)
{
    if (!window || !fb_surface || !compose || !gfx_lock || !pixels) {
        return -1;
    }
    if (w == 0 || h == 0 || x + w > s_fb_w || y + h > s_fb_h) {
        return -1;
    }
    int rc;
    SDL_LockMutex(gfx_lock);
    const uint8_t *src = (const uint8_t *)pixels;
    for (uint16_t ry = 0; ry < h; ry++) {
        memcpy(&frame[(size_t)(y + ry) * s_fb_w + x], src + (size_t)ry * w * 2,
               (size_t)w * 2);
    }
    SDL_Rect area = { x, y, w, h };
    rc = compose_and_present(&area);
    SDL_UnlockMutex(gfx_lock);
    return rc;
}

int hal_display_blit(const void *pixels, size_t len_bytes)
{
    if (len_bytes < (size_t)s_fb_w * s_fb_h * sizeof(uint16_t)) {
        return -1;
    }
    return hal_display_blit_area(pixels, 0, 0, s_fb_w, s_fb_h);
}

int hal_display_set_backlight(uint8_t percent)
{
    (void)percent;   /* desktop has no backlight */
    return 0;
}

const void *hal_display_framebuffer(void)
{
    return frame;
}

/* ---- desktop-only ------------------------------------------------- */

void hal_display_set_scale(float scale)
{
    requested_scale = scale;
}

void hal_display_set_key_pressed(int index, bool pressed)
{
    if (index < 0 || index >= (int)(sizeof key_held / sizeof key_held[0])) {
        return;
    }
    SDL_LockMutex(gfx_lock);
    key_held[index] = pressed;
    /* Redraw now: LVGL may not flush for a second, and a held key has to
     * look held immediately. */
    compose_and_present(NULL);
    SDL_UnlockMutex(gfx_lock);
}

void hal_display_pump(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            quit_requested = true;
        } else {
            hal_input_sdl_event(&e);
        }
    }
}

bool hal_display_quit_requested(void)
{
    return quit_requested;
}

void hal_display_shutdown(void)
{
    free(frame);
    frame = NULL;
    if (fb_surface) {
        SDL_FreeSurface(fb_surface);
        fb_surface = NULL;
    }
    if (bezel_bg) {
        SDL_FreeSurface(bezel_bg);
        bezel_bg = NULL;
    }
    if (compose) {
        SDL_FreeSurface(compose);
        compose = NULL;
    }
    if (gfx_lock) {
        SDL_DestroyMutex(gfx_lock);
        gfx_lock = NULL;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = NULL;
    }
    SDL_Quit();
}

void hal_display_window_size(int *w, int *h)
{
    if (w) *w = compose ? compose->w : 0;
    if (h) *h = compose ? compose->h : 0;
}

int hal_display_snapshot(uint32_t *dst)
{
    if (!compose || !gfx_lock || !dst) {
        return -1;
    }
    SDL_LockMutex(gfx_lock);
    for (int y = 0; y < compose->h; y++) {
        memcpy(dst + (size_t)y * (size_t)compose->w,
               (const uint8_t *)compose->pixels + (size_t)y * (size_t)compose->pitch,
               (size_t)compose->w * 4);
    }
    SDL_UnlockMutex(gfx_lock);
    return 0;
}

/* Dump the composed window (bezel + panel) as a binary PPM. */
int hal_display_write_window_ppm(const char *path)
{
    if (!compose || !path) {
        return -1;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        return -1;
    }
    fprintf(f, "P6\n%d %d\n255\n", compose->w, compose->h);
    for (int y = 0; y < compose->h; y++) {
        const Uint32 *row = (const Uint32 *)((const Uint8 *)compose->pixels +
                                             (size_t)y * compose->pitch);
        for (int x = 0; x < compose->w; x++) {
            Uint8 r, g, b;
            SDL_GetRGB(row[x], compose->format, &r, &g, &b);
            fwrite(&r, 1, 1, f);
            fwrite(&g, 1, 1, f);
            fwrite(&b, 1, 1, f);
        }
    }
    fclose(f);
    return 0;
}
