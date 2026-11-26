/*
 * ui_shell.h — the phone UI shell (LVGL), target-agnostic.
 *
 * Runs entirely inside the UI task/thread. Everything crosses a queue:
 * input arrives as hal_input_event_t (see hal/hal_input.h) and is drained by
 * ui_shell_run_iteration(); no other thread ever touches LVGL.
 */
#pragma once

#include <stdbool.h>

#include "hal/hal_input.h"
#include "ui_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The screens of the shell. CHATLIST is also HOME (the unified WhatsApp
 * surface doubles as the home screen). MENU is the main-menu category
 * carousel. SETTINGS is the settings tree with working toggles.
 * SPEEDDIALS is the speed-dial config sub-page (T9 1..9 names). HELDPRESS
 * is the held-press key config sub-page (long-press remap, spec §2).
 * CALLHIST / CONTACTS are the Calls category's real pages (spec §2): the
 * recent-calls list and the contacts list. STUB is a generic leaf page for
 * menu items whose real content arrives in a later leg.
 */
typedef enum {
    UI_SCREEN_CHATLIST = 0,  /* home: WhatsApp-style chat list (concept gen-08) */
    UI_SCREEN_MENU,          /* main-menu category carousel (concept gen-09/03) */
    UI_SCREEN_SETTINGS,      /* settings tree with working toggles              */
    UI_SCREEN_SPEEDDIALS,    /* speed-dial config sub-page (T9 1..9 names)       */
    UI_SCREEN_HELDPRESS,     /* held-press key config sub-page (spec §2)         */
    UI_SCREEN_CALLHIST,      /* Calls: recent calls list (spec §2)               */
    UI_SCREEN_CONTACTS,      /* Calls: contacts list (spec §2)                   */
    UI_SCREEN_STUB,          /* generic leaf page (title + "coming soon")       */
    UI_SCREEN_LOCK,          /* lock screen: keypad unlocks (spec §1)           */
} ui_screen_id_t;

/* Initialize LVGL, fonts, and both screens. Must run on the UI thread, after
 * hal_display_init(). Returns 0 on success. */
int  ui_shell_init(void);

/* Freeze the status-bar / lock clock at its seed value (20:36). Harness
 * only: keeps golden frames reproducible for runs longer than a second.
 * Call before the UI thread starts (shot mode does this for you). */
void ui_shell_set_clock_freeze(bool frozen);

/* Swap the content area to a screen. Must be called on the UI thread. */
void ui_shell_show_screen(ui_screen_id_t id);

/* One iteration: drain pending HAL input events, then tick LVGL.
 * Call in a loop from the UI thread. */
void ui_shell_run_iteration(void);

/* True once the POWER key asked to shut down (desktop: quit). */
bool ui_shell_quit_requested(void);

/* Thread-safe requests (any thread -> UI thread, applied on the next
 * iteration). The harness uses these to drive screens headlessly; the
 * device would use the same path for events from background services. */
void ui_shell_request_screen(ui_screen_id_t id);
void ui_shell_request_quit(void);
void ui_shell_request_stub(page_id_t id);

/* Show the generic leaf (stub) page titled for a given target page id.
 * UI thread. Used by the harness to capture a representative leaf page. */
void ui_shell_show_stub(page_id_t id);

#ifdef __cplusplus
}
#endif
