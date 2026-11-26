/*
 * bezel.h — the phone's PHYSICAL buttons, drawn around the panel by the
 * Tier-1 desktop simulator. Host-only: the ESP32-S3 target has real keys and
 * never includes this.
 *
 * The whole bezel is one data table (in bezel.c). Coordinates are "logical
 * units", the same space as the panel (320x480): the composition is
 * 408x789 units and is scaled as a whole when presented, so rearranging
 * buttons never touches drawing code.
 *
 * Classic T9 layout: side keys (vol/power), a circular d-pad with a centre
 * key, two shoulder keys flanking it, CALL/END below, then the 3x4 keypad.
 *
 * Presses are translated into ordinary hal_input key events by
 * hal_sdl_input.c, so a click here runs exactly the code the keyboard runs
 * (and, later, the code the real keys will run).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <SDL2/SDL.h>

#include "hal/hal_input.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BEZEL_STYLE_KEY = 0,   /* grey key: keypad, shoulders      */
    BEZEL_STYLE_SIDE,      /* thin grey: volume rocker         */
    BEZEL_STYLE_POWER,     /* thin grey: power                 */
    BEZEL_STYLE_CALL,      /* green: call                      */
    BEZEL_STYLE_END,       /* red: end                         */
} bezel_style_t;

typedef enum {
    BEZEL_SHAPE_RECT = 0,
    BEZEL_SHAPE_DISC,      /* centre key of the d-pad          */
    BEZEL_SHAPE_ARROW_UP,
    BEZEL_SHAPE_ARROW_DOWN,
    BEZEL_SHAPE_ARROW_LEFT,
    BEZEL_SHAPE_ARROW_RIGHT,
} bezel_shape_t;

typedef struct {
    const char *id;      /* stable name: --press <id>, logs  */
    const char *label;   /* main legend (NULL for arrows)    */
    const char *sub;     /* optional small second line (T9)  */
    hal_key_t   key;     /* the HAL event this key fires     */
    int16_t     x, y;    /* logical position                 */
    int16_t     w, h;    /* logical size / hit rect          */
    uint8_t     style;   /* bezel_style_t                    */
    uint8_t     shape;   /* bezel_shape_t                    */
} bezel_button_t;

/* Geometry. The panel size is a runtime value (ui/ui_geometry.c); it must
 * match the geometry set there. scale <= 0 selects the largest clean scale
 * that fits the display. */
void   bezel_init(int panel_w, int panel_h, float scale);
float  bezel_scale(void);
int    bezel_window_w(void);
int    bezel_window_h(void);

/* The runtime panel size in logical units (== ui_geom_w/h). */
void   bezel_panel_logical(int *w, int *h);

/* Where the panel sits, in window pixels. */
void   bezel_panel_rect(int *x, int *y, int *w, int *h);

/* Button table access (for hit-testing and automation). */
size_t bezel_button_count(void);
const bezel_button_t *bezel_button(size_t idx);

/* Hit test in logical units; -1 when the point is not on a button. */
int    bezel_hit(float lx, float ly);

/* Drawing (into an ARGB8888 surface the size of the window). */
void   bezel_draw_background(SDL_Surface *dst);
void   bezel_draw_button(SDL_Surface *dst, size_t idx, bool pressed);

#ifdef __cplusplus
}
#endif
