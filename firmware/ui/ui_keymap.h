/*
 * ui_keymap.h — contextual key bindings (ui-architecture.md §1, §2, §4).
 *
 * Every control maps to an ACTION, and every page declares its own keymap.
 * Shoulder keys and d-pad arrows are separate controls:
 *
 *   SHOULDER-L = options      SHOULDER-R = back      (overridable per page)
 *   D-PAD L/R  = navigation   D-PAD U/D    = move selection
 *   CENTER     = activate     END          = home (from anywhere)
 *
 * A page renders and routes generically; adding a page is data, not code.
 */
#pragma once

#include <stdbool.h>

#include "hal/hal_input.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ACT_NOOP = 0,        /* nothing bound on this page            */
    ACT_ACTIVATE,        /* CENTER: open / choose                 */
    ACT_OPEN_OPTIONS,    /* SHOULDER-L default                    */
    ACT_GO_BACK,         /* SHOULDER-R default: pop the page      */
    ACT_CYCLE_CAT_PREV,  /* D-PAD LEFT: previous category         */
    ACT_CYCLE_CAT_NEXT,  /* D-PAD RIGHT: next category            */
    ACT_MOVE_SEL_UP,
    ACT_MOVE_SEL_DOWN,
    ACT_NEW_ITEM,
    ACT_GOTO_HOME,       /* END: always pops to HOME              */
    ACT_CALL_APP,
    ACT_LOCK,            /* second END on HOME (shell state)      */
    ACT_UNLOCK,          /* keypad press on the LOCKED page       */
    ACT_DIAL,
    ACT_COMPOSE_T9,
} action_t;

typedef struct {
    action_t shoulder_l, shoulder_r;
    action_t center, call, end;
    action_t up, down, left, right;
    action_t t9[12];     /* 1..9, 0, *, #                         */
} keymap_t;

/* The action a physical key performs under `km` (NOOP when unbound). */
action_t ui_keymap_action(const keymap_t *km, hal_key_t key);

/* Short softkey-cap label for an action (spec §12.2): NULL means the
 * action never surfaces as a cap — an unbound key leaves no tappable
 * ghost. Static strings: the shell sets them with lv_label_set_text_static,
 * so cap updates never allocate. */
const char *ui_keymap_label(action_t a);

#ifdef __cplusplus
}
#endif
