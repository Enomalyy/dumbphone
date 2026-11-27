/*
 * ui_options.c — the per-page options tables behind the SHOULDER-L sidebar.
 *
 * Pages are data (spec §4): each page owns a small const table of
 * opt_entry_t rows, exposed through one accessor per page. Every listed
 * option is an action that WORKS in the current UI state — opening a real
 * page, or switching the main-menu view (the two renderings of the same
 * menu_t data). Nothing here is inert: the sidebar shows only options
 * relevant to the page it overlays. The page registry (ui_shell.c) wires
 * each page's `options` field to its accessor (spec §16.3).
 */

#include <stddef.h>

#include "ui/ui_options.h"
#include "ui/ui_icons.h"

/* HOME: a compose entry + jumps into the Calls pages. */
static const opt_entry_t opts_home[] = {
    { "New Chat",     I_ENVELOPE, OPT_ACTION_PAGE, PAGE_NEW_MESSAGE },
    { "Call History", I_PHONE,    OPT_ACTION_PAGE, PAGE_CALL_HISTORY },
    { "Contacts",     I_USERS,    OPT_ACTION_PAGE, PAGE_CONTACTS },
};

/* Messaging (the chat list): the same compose entry + Calls jumps. */
static const opt_entry_t opts_messaging[] = {
    { "New Chat",     I_ENVELOPE, OPT_ACTION_PAGE, PAGE_NEW_MESSAGE },
    { "Call History", I_PHONE,    OPT_ACTION_PAGE, PAGE_CALL_HISTORY },
    { "Contacts",     I_USERS,    OPT_ACTION_PAGE, PAGE_CONTACTS },
};

/* Main menu: the view toggle (both options re-render the same data) and a
 * jump to the settings tree. */
static const opt_entry_t opts_menu[] = {
    { "View: Carousel", I_TH,       OPT_ACTION_MENU_CAROUSEL, PAGE_NONE },
    { "View: Grid",     I_GEAR,     OPT_ACTION_MENU_GRID,     PAGE_NONE },
    { "Settings",       I_ELLIPSIS, OPT_ACTION_PAGE,          PAGE_SETTINGS },
};

/* Settings tree: its two config sub-pages. */
static const opt_entry_t opts_settings[] = {
    { "Speed Dials",  I_USER, OPT_ACTION_PAGE, PAGE_SPEED_DIALS },
    { "Held Presses", I_BOLT, OPT_ACTION_PAGE, PAGE_HELD_PRESS },
};

/* Display page: the three font roles are one level deeper; the settings
 * tree and the main menu are one back. */
static const opt_entry_t opts_display[] = {
    { "Font Roles", I_TH,        OPT_ACTION_PAGE, PAGE_FONT },
    { "Settings",   I_GEAR,      OPT_ACTION_PAGE, PAGE_SETTINGS },
};
static const opt_entry_t opts_font[] = {
    { "Display",  I_TH,   OPT_ACTION_PAGE, PAGE_DISPLAY },
    { "Settings", I_GEAR, OPT_ACTION_PAGE, PAGE_SETTINGS },
};

/* Config sub-pages: back to the settings tree they belong to. */
static const opt_entry_t opts_speeddial[] = {
    { "Settings", I_GEAR, OPT_ACTION_PAGE, PAGE_SETTINGS },
};
static const opt_entry_t opts_heldpress[] = {
    { "Settings", I_GEAR, OPT_ACTION_PAGE, PAGE_SETTINGS },
};

/* Call history: a compose entry + the contacts list. */
static const opt_entry_t opts_callhist[] = {
    { "New Call",   I_BOLT,  OPT_ACTION_PAGE, PAGE_NEW_CALL },
    { "Contacts",   I_USERS, OPT_ACTION_PAGE, PAGE_CONTACTS },
};

/* Contacts: back to the call history. */
static const opt_entry_t opts_contacts[] = {
    { "Call History", I_PHONE, OPT_ACTION_PAGE, PAGE_CALL_HISTORY },
};

/* Stub leaf: back to the main menu. */
static const opt_entry_t opts_stub[] = {
    { "Main Menu", I_ELLIPSIS, OPT_ACTION_MAIN_MENU, PAGE_NONE },
};

/* One accessor per page — the lock screen has no options: locked is a
 * state, not a page with actions. */
const opt_entry_t *ui_opts_home(int *n)      { *n = 3; return opts_home; }
const opt_entry_t *ui_opts_chatlist(int *n)  { *n = 3; return opts_messaging; }
const opt_entry_t *ui_opts_menu(int *n)      { *n = 3; return opts_menu; }
const opt_entry_t *ui_opts_settings(int *n)  { *n = 2; return opts_settings; }
const opt_entry_t *ui_opts_display(int *n)   { *n = 2; return opts_display; }
const opt_entry_t *ui_opts_font(int *n)      { *n = 2; return opts_font; }
const opt_entry_t *ui_opts_speeddial(int *n) { *n = 1; return opts_speeddial; }
const opt_entry_t *ui_opts_heldpress(int *n) { *n = 1; return opts_heldpress; }
const opt_entry_t *ui_opts_callhist(int *n)  { *n = 2; return opts_callhist; }
const opt_entry_t *ui_opts_contacts(int *n)  { *n = 1; return opts_contacts; }
const opt_entry_t *ui_opts_stub(int *n)      { *n = 1; return opts_stub; }
const opt_entry_t *ui_opts_lock(int *n)      { *n = 0; return NULL; }
