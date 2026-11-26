/*
 * hal_input.h — input events (touch from GT911, plus physical keys).
 * Declarations only; backends come later (SDL2 events on host, GT911 over I2C
 * on target).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_KEY_HOME = 0,
    HAL_KEY_BACK,
    HAL_KEY_POWER,
    HAL_KEY_VOLUME_UP,
    HAL_KEY_VOLUME_DOWN,
    HAL_KEY_MENU,        /* soft-right key: opens the app menu */

    /* Classic T9 keypad (the hardware keys of Prototype #1). */
    HAL_KEY_UP,
    HAL_KEY_DOWN,
    HAL_KEY_LEFT,
    HAL_KEY_RIGHT,
    HAL_KEY_OK,          /* centre of the d-pad                */
    HAL_KEY_CALL,
    HAL_KEY_END,
    HAL_KEY_SHOULDER_L,  /* left shoulder, beside the d-pad    */
    HAL_KEY_SHOULDER_R,  /* right shoulder                     */
    HAL_KEY_DIGIT_0,
    HAL_KEY_DIGIT_1,     /* keep 0..9 contiguous: the input     */
    HAL_KEY_DIGIT_2,     /* backends map SDLK_0..9 arithmetically */
    HAL_KEY_DIGIT_3,
    HAL_KEY_DIGIT_4,
    HAL_KEY_DIGIT_5,
    HAL_KEY_DIGIT_6,
    HAL_KEY_DIGIT_7,
    HAL_KEY_DIGIT_8,
    HAL_KEY_DIGIT_9,
    HAL_KEY_STAR,
    HAL_KEY_HASH,

    HAL_KEY__COUNT
} hal_key_t;

typedef struct {
    uint16_t x;
    uint16_t y;
} hal_touch_point_t;

typedef enum {
    HAL_INPUT_NONE = 0,
    HAL_INPUT_TOUCH_DOWN,
    HAL_INPUT_TOUCH_MOVE,
    HAL_INPUT_TOUCH_UP,
    HAL_INPUT_KEY_DOWN,
    HAL_INPUT_KEY_UP,
} hal_input_type_t;

typedef struct {
    hal_input_type_t type;
    union {
        hal_touch_point_t touch;
        hal_key_t         key;
    } u;
} hal_input_event_t;

int  hal_input_init(void);

/* Non-blocking: returns true if an event was written to *out. */
bool hal_input_poll(hal_input_event_t *out);

#ifdef __cplusplus
}
#endif
