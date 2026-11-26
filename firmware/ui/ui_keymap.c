/*
 * ui_keymap.c — resolve a physical key to the current page's action.
 */

#include "ui/ui_keymap.h"

action_t ui_keymap_action(const keymap_t *km, hal_key_t key)
{
    if (!km) {
        return ACT_NOOP;
    }
    switch (key) {
    case HAL_KEY_SHOULDER_L: return km->shoulder_l;
    case HAL_KEY_SHOULDER_R: return km->shoulder_r;
    case HAL_KEY_OK:         return km->center;
    case HAL_KEY_CALL:       return km->call;
    case HAL_KEY_END:        return km->end;
    case HAL_KEY_UP:         return km->up;
    case HAL_KEY_DOWN:       return km->down;
    case HAL_KEY_LEFT:       return km->left;
    case HAL_KEY_RIGHT:      return km->right;

    /* T9: slots are 1..9, 0, *, #. DIGIT_1..9 are contiguous by definition. */
    case HAL_KEY_DIGIT_1: case HAL_KEY_DIGIT_2: case HAL_KEY_DIGIT_3:
    case HAL_KEY_DIGIT_4: case HAL_KEY_DIGIT_5: case HAL_KEY_DIGIT_6:
    case HAL_KEY_DIGIT_7: case HAL_KEY_DIGIT_8: case HAL_KEY_DIGIT_9:
        return km->t9[key - HAL_KEY_DIGIT_1];
    case HAL_KEY_DIGIT_0: return km->t9[9];
    case HAL_KEY_STAR:    return km->t9[10];
    case HAL_KEY_HASH:    return km->t9[11];

    default:
        /* HOME/BACK/MENU/POWER/VOLUME are shell or desktop keys, not part of
         * the phone's contextual pad. */
        return ACT_NOOP;
    }
}
