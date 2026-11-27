/*
 * ui_options.h — per-page option data for the SHOULDER-L sidebar overlay.
 *
 * Spec §1: SHOULDER-L = options on every page. The sidebar (built by the
 * shell, ui_shell.c) shows only the options relevant to the CURRENT page:
 * each page has a small table of opt_entry_t rows in ui_options.c, exposed
 * through one accessor per page (ui_opts_*) that the page registry
 * (ui_pages.h) points at. An entry is an ACTION, not a page — executing it
 * does something real (open a page, switch the main-menu view), then the
 * sidebar closes.
 *
 * Data only: no LVGL, no SDL. The shell renders and routes the sidebar;
 * adding an option = filling in a row here.
 */
#pragma once

#include <stdint.h>

#include "ui_menu.h"   /* page_id_t */

#ifdef __cplusplus
extern "C" {
#endif

/* What executing an option does. */
typedef enum {
    OPT_ACTION_PAGE = 0,       /* open `target` (a page_id_t)         */
    OPT_ACTION_MAIN_MENU,      /* go to the main menu                 */
} opt_action_t;

/* One row of the options sidebar. `label` must fit one line in the
 * sidebar's label column (VT323 24, ~136 px). */
typedef struct {
    const char   *label;
    const char   *icon;      /* Font Awesome glyph string (ui_icons.h) */
    opt_action_t  action;
    page_id_t     target;    /* for OPT_ACTION_PAGE                    */
} opt_entry_t;

/* One accessor per page — the page registry (ui_pages.h) points each row's
 * options field at its page's accessor. `*n` receives the row count; a
 * NULL table means no options (the lock screen). UI thread or before it
 * starts. */
const opt_entry_t *ui_opts_home(int *n);
const opt_entry_t *ui_opts_chatlist(int *n);
const opt_entry_t *ui_opts_menu(int *n);
const opt_entry_t *ui_opts_settings(int *n);
const opt_entry_t *ui_opts_display(int *n);
const opt_entry_t *ui_opts_font(int *n);
const opt_entry_t *ui_opts_speeddial(int *n);
const opt_entry_t *ui_opts_heldpress(int *n);
const opt_entry_t *ui_opts_callhist(int *n);
const opt_entry_t *ui_opts_contacts(int *n);
const opt_entry_t *ui_opts_stub(int *n);
const opt_entry_t *ui_opts_lock(int *n);

#ifdef __cplusplus
}
#endif
