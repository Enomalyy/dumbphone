/*
 * hal_sdl_input.c — Tier-1 (desktop) backend for hal/hal_input.h.
 *
 * Two sources, ONE output: named HAL events on a mutex ring.
 *   - clicks inside the panel recess -> touch (scaled to panel pixels)
 *   - clicks on the bezel keys       -> the same HAL_KEY_* a keyboard emits
 * so "what a press does" is decided only by the UI shell's key handler.
 *
 * Keyboard: H home, B/Backspace back, M menu, P/Esc power, +/- volume.
 * Mouse: left = touch or key cap, right = back, wheel = volume.
 */

#include <SDL2/SDL.h>

#include <stdbool.h>
#include <stdint.h>

#include "bezel.h"
#include "hal/hal_input.h"
#include "hal_sdl_host.h"

#define RING_SIZE 64

static hal_input_event_t ring[RING_SIZE];
static volatile int ring_head;   /* producer (SDL thread) */
static volatile int ring_tail;   /* consumer (UI thread)  */
static SDL_mutex *ring_mutex;
static bool initialized = false;

/* Interaction state owned by the SDL (main) thread. */
static int held_key = -1;        /* bezel key index currently held, -1 none */
static bool touch_active = false;

int hal_input_init(void)
{
    ring_mutex = SDL_CreateMutex();
    if (!ring_mutex) {
        return -1;
    }
    ring_head = ring_tail = 0;
    initialized = true;
    return 0;
}

static void push(const hal_input_event_t *ev)
{
    if (!initialized) {
        return;
    }
    SDL_LockMutex(ring_mutex);
    int next = (ring_head + 1) % RING_SIZE;
    if (next != ring_tail) {   /* drop on overflow: never block the pump */
        ring[ring_head] = *ev;
        ring_head = next;
    }
    SDL_UnlockMutex(ring_mutex);
}

bool hal_input_poll(hal_input_event_t *out)
{
    if (!initialized) {
        return false;
    }
    bool got = false;
    SDL_LockMutex(ring_mutex);
    if (ring_tail != ring_head) {
        *out = ring[ring_tail];
        ring_tail = (ring_tail + 1) % RING_SIZE;
        got = true;
    }
    SDL_UnlockMutex(ring_mutex);
    return got;
}

/* ---- event helpers ------------------------------------------------ */

static void push_key(bool down, hal_key_t key)
{
    hal_input_event_t ev = { .type = down ? HAL_INPUT_KEY_DOWN : HAL_INPUT_KEY_UP };
    ev.u.key = key;
    push(&ev);
}

static void push_touch(hal_input_type_t type, int wx, int wy)
{
    int px, py, pw, ph;
    bezel_panel_rect(&px, &py, &pw, &ph);
    float scale = bezel_scale();

    int x = (int)((float)(wx - px) / scale);
    int y = (int)((float)(wy - py) / scale);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > 319) x = 319;
    if (y > 479) y = 479;

    hal_input_event_t ev = { .type = type };
    ev.u.touch.x = (uint16_t)x;
    ev.u.touch.y = (uint16_t)y;
    push(&ev);
}

static bool in_panel(int wx, int wy)
{
    int px, py, pw, ph;
    bezel_panel_rect(&px, &py, &pw, &ph);
    return wx >= px && wx < px + pw && wy >= py && wy < py + ph;
}

static int bezel_at(int wx, int wy)
{
    float scale = bezel_scale();
    return bezel_hit((float)wx / scale, (float)wy / scale);
}

/* Named key injected from outside (browser viewport). Same queue as the
 * keyboard and the bezel, so the UI cannot tell the source apart. */
void hal_input_inject_key(hal_key_t key, bool down)
{
    push_key(down, key);
}

/* Touch injection state: the harness sends DOWN, MOVE*, UP — mirroring how
 * the mouse path tracks touch_active. */
static bool inject_touch_active = false;

/* Touch point injected from outside the UI (the shot harness's --swipe):
 * panel pixels straight in, no window scaling. First down=true is a
 * TOUCH_DOWN, further down=true while active are TOUCH_MOVEs, down=false
 * releases with TOUCH_UP. Same queue as the mouse, so the UI cannot tell
 * the source apart. */
void hal_input_inject_touch(uint16_t x, uint16_t y, bool down)
{
    hal_input_type_t t;
    if (!down) {
        t = HAL_INPUT_TOUCH_UP;
        inject_touch_active = false;
    } else if (inject_touch_active) {
        t = HAL_INPUT_TOUCH_MOVE;
    } else {
        t = HAL_INPUT_TOUCH_DOWN;
        inject_touch_active = true;
    }
    hal_input_event_t ev = { .type = t };
    ev.u.touch.x = x;
    ev.u.touch.y = y;
    push(&ev);
}

/* Presses a bezel key: same named event the keyboard produces. */
static void press_bezel_key(int idx)
{
    const bezel_button_t *b = bezel_button(idx);
    if (!b) {
        return;
    }
    held_key = idx;
    hal_display_set_key_pressed(idx, true);
    push_key(true, b->key);
}

static void release_bezel_key(void)
{
    if (held_key < 0) {
        return;
    }
    const bezel_button_t *b = bezel_button((size_t)held_key);
    if (b) {
        push_key(false, b->key);
    }
    hal_display_set_key_pressed(held_key, false);
    held_key = -1;
}

/* ---- SDL event translation --------------------------------------- */

/* Keyboard mirrors every physical key, so behaviour can be driven either way:
 *   arrows = d-pad, Enter/Space = OK, C = call, E = end,
 *   Q/[ = shoulder-L, W/] = shoulder-R, 0-9 = keypad, Shift+8 = *, Shift+3 = #.
 * Legacy: H home, B/Backspace back, M menu, P/Esc power, +/- volume. */
static void on_keyboard(const SDL_KeyboardEvent *k)
{
    if (k->repeat) {
        return;
    }
    bool down = (k->type == SDL_KEYDOWN);
    bool shift = (k->keysym.mod & KMOD_SHIFT) != 0;
    SDL_Keycode sym = k->keysym.sym;

    /* Shifted number row produces the * and # keys on a US layout. */
    if (shift && sym == SDLK_8) { push_key(down, HAL_KEY_STAR); return; }
    if (shift && sym == SDLK_3) { push_key(down, HAL_KEY_HASH); return; }

    /* Keypad digits: the enum keeps DIGIT_0..DIGIT_9 contiguous. */
    if (sym >= SDLK_0 && sym <= SDLK_9) {
        push_key(down, (hal_key_t)(HAL_KEY_DIGIT_0 + (sym - SDLK_0)));
        return;
    }

    switch (sym) {
    case SDLK_UP:        push_key(down, HAL_KEY_UP);           break;
    case SDLK_DOWN:      push_key(down, HAL_KEY_DOWN);         break;
    case SDLK_LEFT:      push_key(down, HAL_KEY_LEFT);         break;
    case SDLK_RIGHT:     push_key(down, HAL_KEY_RIGHT);        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:     push_key(down, HAL_KEY_OK);           break;
    case SDLK_c:         push_key(down, HAL_KEY_CALL);         break;
    case SDLK_e:         push_key(down, HAL_KEY_END);          break;
    case SDLK_q:
    case SDLK_LEFTBRACKET:  push_key(down, HAL_KEY_SHOULDER_L); break;
    case SDLK_w:
    case SDLK_RIGHTBRACKET: push_key(down, HAL_KEY_SHOULDER_R); break;
    case SDLK_ASTERISK:
    case SDLK_KP_MULTIPLY:  push_key(down, HAL_KEY_STAR);      break;
    case SDLK_HASH:
    case SDLK_KP_DIVIDE:    push_key(down, HAL_KEY_HASH);      break;

    /* legacy desktop shortcuts */
    case SDLK_ESCAPE:
        /* Desktop "close the simulator". The POWER key no longer quits (it is
         * the phone's key, reserved for sleep/power menu), so Esc and the
         * window's close button are the way out. */
        if (down) {
            SDL_Event q;
            memset(&q, 0, sizeof q);
            q.type = SDL_QUIT;
            SDL_PushEvent(&q);
        }
        break;
    case SDLK_p:         push_key(down, HAL_KEY_POWER);        break;
    case SDLK_b:
    case SDLK_BACKSPACE: push_key(down, HAL_KEY_BACK);         break;
    case SDLK_h:
    case SDLK_HOME:      push_key(down, HAL_KEY_HOME);         break;
    case SDLK_m:         push_key(down, HAL_KEY_MENU);         break;
    case SDLK_PLUS:
    case SDLK_EQUALS:    push_key(down, HAL_KEY_VOLUME_UP);    break;
    case SDLK_MINUS:     push_key(down, HAL_KEY_VOLUME_DOWN);  break;
    default: break;
    }
}

void hal_input_sdl_event(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_MOUSEBUTTONDOWN: {
        if (e->button.button == SDL_BUTTON_RIGHT) {
            push_key(true, HAL_KEY_BACK);     /* desktop: right-click = back */
            push_key(false, HAL_KEY_BACK);
            return;
        }
        if (e->button.button != SDL_BUTTON_LEFT) {
            return;
        }
        int key = bezel_at(e->button.x, e->button.y);
        if (key >= 0) {
            press_bezel_key(key);
        } else if (in_panel(e->button.x, e->button.y)) {
            touch_active = true;
            push_touch(HAL_INPUT_TOUCH_DOWN, e->button.x, e->button.y);
        }
        break;
    }

    case SDL_MOUSEBUTTONUP:
        if (e->button.button != SDL_BUTTON_LEFT) {
            return;
        }
        /* A held key always releases, even if the pointer drifted off it. */
        release_bezel_key();
        if (touch_active) {
            touch_active = false;
            push_touch(HAL_INPUT_TOUCH_UP, e->button.x, e->button.y);
        }
        break;

    case SDL_MOUSEMOTION:
        if (held_key >= 0 && !(e->motion.state & SDL_BUTTON_LMASK)) {
            release_bezel_key();              /* button lost, no release event */
        }
        if (touch_active && (e->motion.state & SDL_BUTTON_LMASK)) {
            push_touch(HAL_INPUT_TOUCH_MOVE, e->motion.x, e->motion.y);
        }
        break;

    case SDL_MOUSEWHEEL: {
        /* Volume keys are the wheel's named-event equivalent. */
        int n = e->wheel.y;
        hal_key_t k = n > 0 ? HAL_KEY_VOLUME_UP : HAL_KEY_VOLUME_DOWN;
        for (int i = 0; i < (n < 0 ? -n : n) && i < 5; i++) {
            push_key(true, k);
            push_key(false, k);
        }
        break;
    }

    case SDL_KEYDOWN:
    case SDL_KEYUP:
        on_keyboard(&e->key);
        break;

    default:
        break;
    }
}
