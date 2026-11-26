/*
 * hal_sdl_display.c — Tier-1 (desktop) backend for hal/hal_display.h.
 *
 * The window shows a phone mock-up: the 320x480 RGB565 panel (the ONLY thing
 * the HAL knows about) surrounded by a bezel carrying the physical keys, see
 * bezel.c. Presentation is: compose bezel background -> scale the panel into
 * its recess -> re-draw any held key -> blit to the window.
 *
 * hal_display_framebuffer() keeps returning the pure panel buffer, so frame
 * dumps and any future viewport backend see device pixels only.
 */

#include <SDL2/SDL.h>

#include <stdint.h>
#include <string.h>

#include "bezel.h"
#include "hal/hal_display.h"
#include "hal_sdl_host.h"

#define FB_W    320
#define FB_H    480

static uint16_t frame[FB_W * FB_H];   /* canonical panel buffer, RGB565 */
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

int hal_display_init(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return -1;
    }

    bezel_init(requested_scale);

    int win_w = bezel_window_w();
    int win_h = bezel_window_h();

    window = SDL_CreateWindow("dumbphone — 320x480 panel + bezel",
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

    fb_surface = SDL_CreateRGBSurfaceFrom(frame, FB_W, FB_H, 16, FB_W * 2,
                                          0xF800, 0x07E0, 0x001F, 0);
    if (!fb_surface) {
        SDL_Log("SDL_CreateRGBSurfaceFrom failed: %s", SDL_GetError());
        return -1;
    }

    SDL_Log("desktop simulator: panel 320x480, bezel x%.2f -> window %dx%d",
            (double)bezel_scale(), win_w, win_h);
    return 0;
}

void hal_display_info(hal_display_info_t *out)
{
    out->width = FB_W;
    out->height = FB_H;
    out->bpp = 16;
}

/* Rebuild the window from the panel buffer + bezel. Caller holds gfx_lock. */
static int compose_and_present(void)
{
    /* 1. bezel + unused keys (cached) */
    SDL_BlitSurface(bezel_bg, NULL, compose, NULL);

    /* 2. the panel, scaled into its recess */
    int px, py, pw, ph;
    bezel_panel_rect(&px, &py, &pw, &ph);
    SDL_Rect dst = { px, py, pw, ph };
    if (SDL_BlitScaled(fb_surface, NULL, compose, &dst) != 0) {
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

    /* 4. present */
    SDL_Surface *ws = SDL_GetWindowSurface(window);
    if (ws) {
        SDL_BlitSurface(compose, NULL, ws, NULL);
        SDL_UpdateWindowSurface(window);
    }
    return 0;
}

int hal_display_blit(const void *pixels, size_t len_bytes)
{
    if (!window || !fb_surface || !compose || !gfx_lock || !pixels ||
        len_bytes < sizeof frame) {
        return -1;
    }
    int rc;
    SDL_LockMutex(gfx_lock);
    if (pixels != frame) {
        memcpy(frame, pixels, sizeof frame);
    }
    rc = compose_and_present();
    SDL_UnlockMutex(gfx_lock);
    return rc;
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
    compose_and_present();
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
