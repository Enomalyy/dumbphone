/*
 * main.c — Tier-1 (Linux desktop) entry point for the dumb-phone shell.
 *
 * Mirrors the on-device split:
 *   - main thread  = "Core 0": SDL video + the single SDL event pump, which
 *                    translates input into HAL events and pushes them onto
 *                    the HAL queue. It never touches LVGL.
 *   - UI thread    = "Core 1": owns LVGL exclusively and drains the queue.
 *
 * Desktop keys:  H = home, B/Backspace = back, M = menu, P/Esc = power (quit),
 *                +/- or mouse wheel = volume, left click = touch or key cap,
 *                right click = back. The bezel keys in the window fire the
 *                same named events as those keys.
 *
 * Browser viewport: a small HTTP/WebSocket server serves the composed window
 * as JPEG frames and accepts taps/named keys back (see frame_server.c).
 * Default bind is 127.0.0.1 (override: DUMBPHONE_SERVER_BIND or
 * --server-bind); --no-server disables it.
 *
 * Harness mode (no window, deterministic frame dumps; the clock is frozen
 * at 20:36 so a frame is reproducible regardless of when it was taken):
 *   dumbphone-desktop --shot out.ppm        panel only (device pixels)
 *   dumbphone-desktop --shot-win out.ppm    composed mock-up (bezel + panel)
 *   options: --screen <name>     one of the page registry's names (ui_pages.h):
 *            home chat menu settings display font speeddials heldpress
 *            callhist contacts stub lock
 *            --press <button-id>
 *            --cycle left|right N  --key <name> [N]  --activate N
 *            --swipe up|down|left|right [dist]  --tap x y  --hold <ms>
 *            --menu-style carousel|grid  --scale N  --frames N
 *
 * Panel geometry (spec §15 / issue #1): the panel size is a RUNTIME value,
 * one source of truth in ui/ui_geometry.c. Pick it with:
 *   dumbphone-desktop --geom WxH            e.g. --geom 480x800, --geom 240x320
 * Presets: 320x480 (default), 240x320, 240x400, 320x240, 480x800, 480x640,
 * 1280x720 — plus any free-form WxH. Everything (LVGL buffers, bars, rows,
 * bezel, viewport, shots) derives from it; the default is pixel-identical to
 * the pre-geometry build.
 */

#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bezel.h"
#ifndef __EMSCRIPTEN__
#include "frame_server.h"
#endif
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "ui/ui_geometry.h"
#include "ui/ui_pages.h"
#include "ui/ui_settings.h"
#include "ui/ui_shell.h"
#include "ui/ui_theme.h"

#include "hal_sdl_host.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void *ui_thread(void *arg)
{
    (void)arg;
    printf("[ui] thread up, initializing shell\n");
    if (ui_shell_init() != 0) {
        fprintf(stderr, "[ui] ui_shell_init failed\n");
        return NULL;
    }
    printf("[ui] shell ready\n");
    fflush(stdout);

    while (!ui_shell_quit_requested()) {
        ui_shell_run_iteration();   /* drain HAL input queue + lv_timer_handler */
        sleep_ms(5);
    }
    return NULL;
}

/* Write the pure panel framebuffer (RGB565) as a binary PPM (RGB888). */
static int write_ppm(const char *path)
{
    const uint16_t *fb = hal_display_framebuffer();
    if (!fb) {
        return -1;
    }
    hal_display_info_t info;
    hal_display_info(&info);
    FILE *f = fopen(path, "wb");
    if (!f) {
        return -1;
    }
    fprintf(f, "P6\n%d %d\n255\n", info.width, info.height);
    for (int i = 0; i < info.width * info.height; i++) {
        uint16_t p = fb[i];
        unsigned char rgb[3] = {
            (unsigned char)(((p >> 11) & 0x1F) * 255 / 31),
            (unsigned char)(((p >> 5) & 0x3F) * 255 / 63),
            (unsigned char)((p & 0x1F) * 255 / 31),
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

/* Synthesize a click on a physical key, at its centre in window pixels.
 * Goes through the real path: SDL event -> hit test -> HAL key -> UI. */
static bool press_key_by_id(const char *id)
{
    for (size_t i = 0; i < bezel_button_count(); i++) {
        const bezel_button_t *b = bezel_button(i);
        if (strcmp(b->id, id) != 0) {
            continue;
        }
        SDL_Event ev;
        memset(&ev, 0, sizeof ev);
        ev.type = SDL_MOUSEBUTTONDOWN;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.x = (int)lroundf(((float)b->x + b->w / 2.0f) * bezel_scale());
        ev.button.y = (int)lroundf(((float)b->y + b->h / 2.0f) * bezel_scale());
        SDL_PushEvent(&ev);
        return true;
    }
    return false;
}

/* Map a named HAL key (the bezel's button-id vocabulary) to its hal_key_t.
 * Returns 0 on success. */
static int key_from_name(const char *name, hal_key_t *out)
{
    static const struct {
        const char *n;
        hal_key_t k;
    } names[] = {
        { "up", HAL_KEY_UP },         { "down", HAL_KEY_DOWN },
        { "left", HAL_KEY_LEFT },     { "right", HAL_KEY_RIGHT },
        { "ok", HAL_KEY_OK },         { "call", HAL_KEY_CALL },
        { "end", HAL_KEY_END },       { "shoulder-l", HAL_KEY_SHOULDER_L },
        { "shoulder-r", HAL_KEY_SHOULDER_R },
        { "digit-0", HAL_KEY_DIGIT_0 }, { "digit-1", HAL_KEY_DIGIT_1 },
        { "digit-2", HAL_KEY_DIGIT_2 }, { "digit-3", HAL_KEY_DIGIT_3 },
        { "digit-4", HAL_KEY_DIGIT_4 }, { "digit-5", HAL_KEY_DIGIT_5 },
        { "digit-6", HAL_KEY_DIGIT_6 }, { "digit-7", HAL_KEY_DIGIT_7 },
        { "digit-8", HAL_KEY_DIGIT_8 }, { "digit-9", HAL_KEY_DIGIT_9 },
        { "star", HAL_KEY_STAR },     { "hash", HAL_KEY_HASH },
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (strcmp(name, names[i].n) == 0) {
            *out = names[i].k;
            return 0;
        }
    }
    return -1;
}

static void settle(int frames)
{
    for (int i = 0; i < frames; i++) {
        hal_display_pump();
        sleep_ms(10);
    }
}

/* Named keys to inject in shot mode: repeated --key builds a small sequence
 * so one run can land on states two keys produce (e.g. SHOULDER-L opens the
 * options sidebar, then OK executes its selected option). */
typedef struct {
    const char *name;
    int         n;   /* full presses of this key */
} key_seq_t;

/* 8 named keys: enough for any realistic scene (open a page, move,
 * activate twice). A full array used to SILENTLY drop the extra --key
 * args, which shipped scenes that landed one state short. */
static key_seq_t g_key_seq[8];
static int       g_key_seq_n = 0;

/* Swipes to inject in shot mode: repeated --swipe builds a sequence so one
 * run can land on states drags produce (a scrolled list, a cycled category).
 * Each swipe goes through the real HAL touch path (DOWN, MOVE*, UP). */
typedef struct {
    const char *dir;    /* "up" | "down" | "left" | "right" */
    int         dist;   /* drag distance in panel px (default 120) */
} swipe_seq_t;

static swipe_seq_t g_swipe_seq[4];
static int         g_swipe_seq_n = 0;

/* Taps to inject in shot mode: repeated --tap x y builds a sequence of
 * point touches (DOWN, brief hold, UP at the same panel pixel). */
typedef struct {
    int x, y;   /* panel pixels */
} tap_seq_t;

static tap_seq_t g_tap_seq[4];
static int       g_tap_seq_n = 0;

/* One deterministic swipe through the HAL touch path: DOWN, N slow MOVE
 * steps, UP. Steps are spaced far enough apart that LVGL's scroll-throw
 * velocity history decays to zero before release (no fling), so the frame
 * is reproducible. The drag ends inside the row area of every screen. */
static void inject_swipe(const char *dir, int dist)
{
    int x0 = 160, y0 = 350, x1 = 160, y1 = 150;
    if (!strcmp(dir, "up")) {
        y1 = y0 - dist;
    } else if (!strcmp(dir, "down")) {
        y0 = 150;
        y1 = y0 + dist;
    } else if (!strcmp(dir, "left")) {
        x0 = 250;
        x1 = x0 - dist;
        y0 = y1 = 250;
    } else {   /* right */
        x0 = 70;
        x1 = x0 + dist;
        y0 = y1 = 250;
    }

    hal_input_inject_touch((uint16_t)x0, (uint16_t)y0, true);
    settle(15);
    const int steps = 8;
    for (int i = 1; i <= steps; i++) {
        int x = x0 + (x1 - x0) * i / steps;
        int y = y0 + (y1 - y0) * i / steps;
        hal_input_inject_touch((uint16_t)x, (uint16_t)y, true);
        settle(15);
    }
    hal_input_inject_touch((uint16_t)x1, (uint16_t)y1, false);
    settle(30);
}

/* One deterministic tap through the HAL touch path: DOWN, a short hold,
 * UP at the same panel pixel. No movement, so LVGL's press_moved stays
 * clear and the row under the point is selected/executed on release. */
static void inject_tap(int x, int y)
{
    hal_input_inject_touch((uint16_t)x, (uint16_t)y, true);
    settle(8);
    hal_input_inject_touch((uint16_t)x, (uint16_t)y, false);
    settle(20);
}

int main(int argc, char **argv)
{
    const char *shot_path = NULL;
    const char *shot_win_path = NULL;
    const char *screen = NULL;
    const char *press = NULL;
    const char *cycle_dir = NULL;   /* "left" | "right"           */
    int cycle_n = 0;                /* how many presses to inject  */
    const char *key_name = NULL;     /* a named HAL key (bezel ids) */
    int activate_n = 0;             /* full CENTER presses to inject */
    int per_key_settle = 12;        /* frames after each injected key  */
    const char *swipe_dir = NULL;    /* "up" | "down" | "left" | "right" */
    int tap_x = 0, tap_y = 0;        /* a point touch (panel px)          */
    int hold_ms = 0;                /* how long --key presses stay down */
    const char *menu_style = NULL;   /* "carousel" | "grid"         */
    const char *theme_name = NULL;    /* "dark" | "light"            */
    const char *font_cycle = NULL;    /* "header|text|subtext[:N]"   */
    const char *geom = NULL;          /* "WxH" panel geometry          */
    /* Loopback by default: the viewport is a dev tool and should not be
     * reachable from other machines unless asked. To drive it from a phone
     * on your LAN/tailnet, set DUMBPHONE_SERVER_BIND or pass --server-bind
     * (an address on this machine, or 0.0.0.0 for all interfaces). */
    const char *bind_ip = getenv("DUMBPHONE_SERVER_BIND");
    if (!bind_ip || !bind_ip[0]) {
        bind_ip = "127.0.0.1";
    }
    float scale = 0.0f;          /* 0 = auto-fit */
    int frames = 40;
    int server_port = 8099;
    int server_fps = 18;
    bool server_enabled = true;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
            shot_path = argv[++i];
        } else if (!strcmp(argv[i], "--shot-win") && i + 1 < argc) {
            shot_win_path = argv[++i];
        } else if (!strcmp(argv[i], "--screen") && i + 1 < argc) {
            screen = argv[++i];
        } else if (!strcmp(argv[i], "--press") && i + 1 < argc) {
            press = argv[++i];
        } else if (!strcmp(argv[i], "--cycle") && i + 2 < argc) {
            cycle_dir = argv[++i];
            cycle_n = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--key") && i + 1 < argc) {
            key_name = argv[++i];
            int n = 1;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {
                n = atoi(argv[++i]);
            }
            if (g_key_seq_n < (int)(sizeof g_key_seq / sizeof g_key_seq[0])) {
                g_key_seq[g_key_seq_n].name = key_name;
                g_key_seq[g_key_seq_n].n = n;
                g_key_seq_n++;
            }
        } else if (!strcmp(argv[i], "--activate") && i + 1 < argc) {
            activate_n = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--settle") && i + 1 < argc) {
            /* Frames to pump after each injected key/cycle. The default
             * (12) predates animation: scenes that must land past a
             * moving-carousel slide pass --settle 30, and the mid-slide
             * scene passes --settle 8 to freeze inside the settle window. */
            per_key_settle = atoi(argv[++i]);
            if (per_key_settle < 1) {
                per_key_settle = 1;
            }
        } else if (!strcmp(argv[i], "--swipe") && i + 1 < argc) {
            swipe_dir = argv[++i];
            int dist = 120;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {
                dist = atoi(argv[++i]);
            }
            if (g_swipe_seq_n < (int)(sizeof g_swipe_seq / sizeof g_swipe_seq[0])) {
                g_swipe_seq[g_swipe_seq_n].dir = swipe_dir;
                g_swipe_seq[g_swipe_seq_n].dist = dist;
                g_swipe_seq_n++;
            }
        } else if (!strcmp(argv[i], "--tap") && i + 2 < argc) {
            tap_x = atoi(argv[++i]);
            tap_y = atoi(argv[++i]);
            if (g_tap_seq_n < (int)(sizeof g_tap_seq / sizeof g_tap_seq[0])) {
                g_tap_seq[g_tap_seq_n].x = tap_x;
                g_tap_seq[g_tap_seq_n].y = tap_y;
                g_tap_seq_n++;
            }
        } else if (!strcmp(argv[i], "--hold") && i + 1 < argc) {
            hold_ms = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--menu-style") && i + 1 < argc) {
            menu_style = argv[++i];
        } else if (!strcmp(argv[i], "--theme") && i + 1 < argc) {
            theme_name = argv[++i];
        } else if (!strcmp(argv[i], "--font") && i + 1 < argc) {
            font_cycle = argv[++i];
        } else if ((!strcmp(argv[i], "--geom") || !strcmp(argv[i], "--geometry"))
                   && i + 1 < argc) {
            geom = argv[++i];
        } else if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
            scale = (float)atof(argv[++i]);
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            frames = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--no-server")) {
            server_enabled = false;
        } else if (!strcmp(argv[i], "--server-bind") && i + 1 < argc) {
            bind_ip = argv[++i];
        } else if (!strcmp(argv[i], "--server-port") && i + 1 < argc) {
            server_port = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--server-fps") && i + 1 < argc) {
            server_fps = atoi(argv[++i]);
        } else {
            fprintf(stderr,
                    "usage: %s [--shot out.ppm] [--shot-win out.ppm] "
                    "[--screen <page name from the page registry>] "
                    "[--press <button-id>] [--cycle left|right N] "
                    "[--key <name> [N] ...] [--activate N] "
                    "[--swipe up|down|left|right [dist] ...] [--tap x y ...] "
                    "[--hold <ms>] [--settle N] "
                    "[--menu-style carousel|grid] "
                    "[--geom WxH] [--scale N] [--frames N] "
                    "[--no-server] [--server-bind IP] [--server-port N] "
                    "[--server-fps N]\n", argv[0]);
            return 2;
        }
    }

    printf("[main] dumbphone desktop simulator (Tier 1, LVGL + SDL2)\n");

    /* Panel geometry — the single runtime source of truth. Must be set
     * before hal_display_init() (buffers, bezel and window size from it). */
    if (geom) {
        int gw = 0, gh = 0;
        if (sscanf(geom, "%dx%d", &gw, &gh) != 2 || gw <= 0 || gh <= 0) {
            fprintf(stderr, "[main] bad --geom '%s' (want WxH, e.g. 480x800)\n",
                    geom);
            return 2;
        }
        ui_geometry_set(gw, gh);
    }
    printf("[main] panel geometry: %dx%d\n", ui_geom_w(), ui_geom_h());

    if ((shot_path || shot_win_path) && !getenv("SDL_VIDEODRIVER")) {
        /* Headless: software surfaces still work, no window appears. */
        setenv("SDL_VIDEODRIVER", "dummy", 1);
    }

#ifdef __EMSCRIPTEN__
    /* The web page scales the canvas with CSS; keep the logical window at
     * its native size so input mapping and the page's fit math are
     * deterministic (408x789 at scale 1.0). */
    scale = 1.0f;
#endif
    hal_display_set_scale(scale);

    if (hal_display_init() != 0) {
        fprintf(stderr, "[main] hal_display_init failed\n");
        return 1;
    }
    if (hal_input_init() != 0) {
        fprintf(stderr, "[main] hal_input_init failed\n");
        return 1;
    }

    extern int hal_storage_init(void);
    hal_storage_init();

    /* Deterministic menu view for shots: set before the UI thread starts so
     * build_main_menu() reads it. (pthread_create is the happens-before.) */
    if (menu_style) {
        /* One of carousel | grid | moving (spec §14.3's three styles). */
        uint8_t st = MENU_STYLE_CAROUSEL;
        if (!strcmp(menu_style, "grid")) {
            st = MENU_STYLE_GRID;
        } else if (!strcmp(menu_style, "moving")) {
            st = MENU_STYLE_MOVING;
        }
        ui_settings_set_menu_style(st);
    }

    /* Deterministic appearance for shots: the theme is applied inside
     * ui_shell_init() (before pages are built), and the font ladder is
     * advanced here — also before the UI exists, so no restyle is needed. */
    if (theme_name) {
        ui_shell_set_startup_theme(!strcmp(theme_name, "light")
                                    ? UI_THEME_LIGHT : UI_THEME_DARK);
    }
    if (font_cycle) {
        int n = 1;
        char name[16] = { 0 };
        if (sscanf(font_cycle, "%15[^:]:%d", name, &n) < 1) {
            fprintf(stderr, "[main] bad --font '%s'\n", font_cycle);
            return 2;
        }
        ui_font_role_t role = !strcmp(name, "header") ? UI_FONT_HEADER
                            : !strcmp(name, "text")   ? UI_FONT_TEXT
                            : !strcmp(name, "subtext") ? UI_FONT_SUBTEXT
                            : (ui_font_role_t)-1;
        if ((int)role < 0) {
            fprintf(stderr, "[main] --font wants header|text|subtext[:N]\n");
            return 2;
        }
        for (int k = 0; k < n; k++) {
            ui_font_pref_next(role);
        }
    }

    /* Shot mode freezes the clock at its seed value: any run that injects
     * input (swipes, holds, several keys) takes more than a second, and a
     * live clock would put the wall time into the golden frame and change
     * it every minute. Golden frames must be reproducible. */
    if (shot_path || shot_win_path) {
        ui_shell_set_clock_freeze(true);
    }

#ifdef __EMSCRIPTEN__
    /* WebAssembly build (web/build.sh): GitHub Pages cannot set COOP/COEP
     * headers, so SharedArrayBuffer is unavailable and -pthread is off the
     * table. main() therefore only initialises the shell here (desktop does
     * it on the UI thread); the page's own requestAnimationFrame loop
     * (web/index.html) then drives one web_frame_pump() per frame
     * (web_hooks.c), which drains SDL's DOM events (mouse/keyboard ->
     * hal_input_sdl_event, the same path as the desktop pump) and runs the
     * SAME ui_shell_run_iteration() the desktop UI thread does. ui/ is
     * untouched; only this entry point changes shape.
     *
     * (Emscripten 7's built-in rAF wrappers — emscripten_set_main_loop /
     * emscripten_request_animation_frame_loop — registered their C callback
     * from here but never fired it in our verification environment, while a
     * plain page-side rAF + ccall runs at a steady 60fps.) */
    if (ui_shell_init() != 0) {
        fprintf(stderr, "[ui] ui_shell_init failed\n");
        return 1;
    }
    return 0;
#endif

    pthread_t ui;
    if (pthread_create(&ui, NULL, ui_thread, NULL) != 0) {
        fprintf(stderr, "[main] could not start UI thread\n");
        return 1;
    }

    if (shot_path || shot_win_path) {
        settle(frames);

        if (screen) {
            /* The page registry (spec §16.3) is the single source of the
             * --screen names: one lookup, no strcmp chain. "stub" stays a
             * named special case — it pushes a representative leaf TITLED
             * "Call History", not a bare screen swap. */
            const ui_page_t *pg = ui_page_by_name(screen);
            if (!pg) {
                fprintf(stderr, "[main] unknown --screen '%s'\n", screen);
                return 2;
            }
            if (pg->screen == UI_SCREEN_STUB) {
                ui_shell_request_stub(PAGE_CALL_HISTORY);
            } else {
                ui_shell_request_screen(pg->screen);
            }
            settle(10);
        }
        if (cycle_n > 0 && cycle_dir) {
            /* Inject N full d-pad presses (down+up pairs through the named
             * HAL path) so shots can land on any carousel category. */
            hal_key_t k = !strcmp(cycle_dir, "left") ? HAL_KEY_LEFT
                                                     : HAL_KEY_RIGHT;
            for (int i = 0; i < cycle_n; i++) {
                hal_input_inject_key(k, true);
                hal_input_inject_key(k, false);
                settle(per_key_settle);
            }
        }
        if (activate_n > 0) {
            /* Inject N full CENTER presses (down+up pairs through the named
             * HAL path) so shots can land on a toggled state. */
            for (int i = 0; i < activate_n; i++) {
                hal_input_inject_key(HAL_KEY_OK, true);
                hal_input_inject_key(HAL_KEY_OK, false);
                settle(per_key_settle);
            }
        }
        for (int ks = 0; ks < g_key_seq_n; ks++) {
            /* Inject N full presses of each named HAL key in sequence
             * (down+up pairs through the named HAL path) so shots can land
             * on states one or more keys produce (e.g. a T9 speed dial from
             * HOME, a menu category cycle after --activate landed on MENU,
             * or SHOULDER-L + OK: open the options sidebar then execute its
             * selected option). */
            hal_key_t k;
            if (key_from_name(g_key_seq[ks].name, &k) != 0) {
                fprintf(stderr, "[main] no such key: %s\n", g_key_seq[ks].name);
                return 1;
            }
            for (int i = 0; i < g_key_seq[ks].n; i++) {
                hal_input_inject_key(k, true);
                if (hold_ms > 0) {
                    sleep_ms(hold_ms);   /* a long press: held >= LONG_PRESS_MS */
                }
                hal_input_inject_key(k, false);
                settle(per_key_settle);
            }
        }
        for (int sw = 0; sw < g_swipe_seq_n; sw++) {
            /* Inject each swipe in order (through the HAL touch path) so
             * shots can land on states drags produce: a scrolled list, or a
             * category cycled by a horizontal swipe (spec §1). */
            inject_swipe(g_swipe_seq[sw].dir, g_swipe_seq[sw].dist);
        }
        for (int tp = 0; tp < g_tap_seq_n; tp++) {
            /* Inject each tap in order (through the HAL touch path) so
             * shots can land on states point touches produce: a row
             * selected by tapping it. */
            inject_tap(g_tap_seq[tp].x, g_tap_seq[tp].y);
        }
        if (press && press[0] && strcmp(press, "none") != 0) {
            if (!press_key_by_id(press)) {
                fprintf(stderr, "[main] no such button: %s\n", press);
                return 1;
            }
            settle(15);   /* key stays held: the dump shows it depressed */
        }

        if (shot_path) {
            if (write_ppm(shot_path) != 0) {
                fprintf(stderr, "[main] could not write %s\n", shot_path);
                return 1;
            }
            printf("[main] wrote %s (panel)\n", shot_path);
        }
        if (shot_win_path) {
            if (hal_display_write_window_ppm(shot_win_path) != 0) {
                fprintf(stderr, "[main] could not write %s\n", shot_win_path);
                return 1;
            }
            printf("[main] wrote %s (window)\n", shot_win_path);
        }

        ui_shell_request_quit();
        pthread_join(ui, NULL);
        hal_display_shutdown();
        return 0;
    }

#ifndef __EMSCRIPTEN__
    if (server_enabled) {
        int port = frame_server_start(bind_ip, server_port, server_fps);
        if (port > 0) {
            printf("[main] browser viewport: http://%s:%d/  (%d fps)\n",
                   bind_ip, port, server_fps);
            printf("[main]   disable with --no-server\n");
            fflush(stdout);
        } else {
            fprintf(stderr, "[main] browser viewport disabled (bind failed)\n");
        }
    }

    /* Core 0: pump SDL until the window closes or the power key quits. */
    while (!hal_display_quit_requested() && !ui_shell_quit_requested()) {
        hal_display_pump();
        sleep_ms(8);
    }

    printf("[main] shutting down\n");
    frame_server_stop();
    ui_shell_request_quit();
    pthread_join(ui, NULL);
    hal_display_shutdown();
#endif
    return 0;
}
