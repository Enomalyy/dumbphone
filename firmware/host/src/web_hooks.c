/*
 * web_hooks.c — WebAssembly test hooks (web build only).
 *
 * Compiled ONLY by web/build.sh — never by the desktop or S3 builds. Exposes
 * tiny C entry points so the page's ?test=1 harness can drive and verify the
 * real UI from JavaScript: find a named bezel key's centre, hash the panel
 * framebuffer to prove frames change, count pumped frames, and inject a named
 * key through the HAL queue. No UI logic lives here — it calls into the same
 * bezel table and HAL the desktop build uses.
 */

#include <emscripten.h>
#include <string.h>

#include "bezel.h"
#include "hal/hal_display.h"
#include "hal_sdl_host.h"   /* hal_input_inject_key() */
#include "ui/ui_shell.h"

/* Centre of a named bezel key in logical window units (pre-scale).
 * Returns 0 on success, -1 if the id is unknown. */
EMSCRIPTEN_KEEPALIVE
int web_key_center(const char *id, int *cx, int *cy)
{
    for (size_t i = 0; i < bezel_button_count(); i++) {
        const bezel_button_t *b = bezel_button(i);
        if (strcmp(b->id, id) == 0) {
            *cx = b->x + b->w / 2;
            *cy = b->y + b->h / 2;
            return 0;
        }
    }
    return -1;
}

/* FNV-1a over a stride sample of the RGB565 panel (runtime size from
 * ui_geometry.c). Cheap, and any visible change in the UI moves the hash. */
EMSCRIPTEN_KEEPALIVE
unsigned int web_frame_hash(void)
{
    const uint16_t *fb = hal_display_framebuffer();
    if (!fb) {
        return 0;
    }
    hal_display_info_t info;
    hal_display_info(&info);
    unsigned int h = 2166136261u;
    for (int i = 0; i < info.width * info.height; i += 7) {
        h ^= fb[i];
        h *= 16777619u;
    }
    return h;
}

/* The composed window size in window pixels (the canvas's native size).
 * The page uses this to fit the canvas and map taps — it must not hardcode
 * a panel geometry (spec §15 / issue #1). */
EMSCRIPTEN_KEEPALIVE
int web_window_size(int *w, int *h)
{
    hal_display_window_size(w, h);
    return 0;
}

/* Framebook: main.c's rAF loop notes every pumped frame, so the harness can
 * prove the loop (and therefore hal_display_pump + ui_shell_run_iteration)
 * is actually running. */
static int g_frames = 0;

EMSCRIPTEN_KEEPALIVE
void web_note_frame(void)
{
    g_frames++;
}

EMSCRIPTEN_KEEPALIVE
int web_frame_count(void)
{
    return g_frames;
}

/* One pump iteration: drain SDL's DOM events, then run the SAME
 * ui_shell_run_iteration() the desktop UI thread runs. Returns non-zero
 * while the app should keep running.
 *
 * Driven by the page's OWN requestAnimationFrame loop (web/index.html)
 * via ccall. Emscripten 7's built-in rAF wrappers
 * (emscripten_request_animation_frame_loop / emscripten_set_main_loop)
 * registered their C callback from main() but never fired it in our
 * verification environment, while a plain page-side rAF + ccall runs at a
 * steady 60fps — same effect, more explicit. */
EMSCRIPTEN_KEEPALIVE
int web_frame_pump(void)
{
    web_note_frame();
    hal_display_pump();
    ui_shell_run_iteration();
    return !(ui_shell_quit_requested() || hal_display_quit_requested());
}

/* Inject a full press (down+up) of a named bezel key through the SAME HAL
 * queue the DOM path feeds — isolates C-side input handling from DOM event
 * delivery. Returns 0 on success, -1 for an unknown name. */
EMSCRIPTEN_KEEPALIVE
int web_press_key(const char *name)
{
    static const struct {
        const char *n;
        hal_key_t k;
    } names[] = {
        { "up", HAL_KEY_UP },         { "down", HAL_KEY_DOWN },
        { "left", HAL_KEY_LEFT },     { "right", HAL_KEY_RIGHT },
        { "ok", HAL_KEY_OK },         { "call", HAL_KEY_CALL },
        { "end", HAL_KEY_END },
        { "shoulder-l", HAL_KEY_SHOULDER_L },
        { "shoulder-r", HAL_KEY_SHOULDER_R },
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (strcmp(name, names[i].n) == 0) {
            hal_input_inject_key(names[i].k, true);
            hal_input_inject_key(names[i].k, false);
            return 0;
        }
    }
    return -1;
}
