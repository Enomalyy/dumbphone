/*
 * ui_options.c — the per-page options tables behind the SHOULDER-L sidebar.
 *
 * Pages are data (spec §4): each page owns a small const table of
 * opt_entry_t rows, exposed through one accessor per page. Every listed
 * option is an action that WORKS in the current UI state — opening a real
 * page. Nothing here is inert: the sidebar shows only options
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

/* Main menu: its SETTINGS category is where settings live (spec §14.13) —
 * the entry switches the menu to it. The main-menu view is chosen by
 * Display's one cycling Menu Style row (spec §14.3) — the sidebar no longer
 * carries per-style entries. */
static const opt_entry_t opts_menu[] = {
    { "Settings", I_ELLIPSIS, OPT_ACTION_SETTINGS_CAT, PAGE_NONE },
};

/* Display page: the Theme submenu and the three font roles are one level
 * deeper; the menu's SETTINGS category is one back. */
static const opt_entry_t opts_display[] = {
    { "Themes",     I_PALETTE,   OPT_ACTION_PAGE, PAGE_THEME },
    { "Font Roles", I_TH,        OPT_ACTION_PAGE, PAGE_FONT },
    { "Settings",   I_GEAR,      OPT_ACTION_SETTINGS_CAT, PAGE_NONE },
};

/* Theme page: back to Display; the menu's SETTINGS category is one back. */
static const opt_entry_t opts_theme[] = {
    { "Display",  I_PALETTE, OPT_ACTION_PAGE, PAGE_DISPLAY },
    { "Settings", I_GEAR,    OPT_ACTION_SETTINGS_CAT, PAGE_NONE },
};
static const opt_entry_t opts_font[] = {
    { "Display",  I_TH,   OPT_ACTION_PAGE, PAGE_DISPLAY },
    { "Settings", I_GEAR, OPT_ACTION_SETTINGS_CAT, PAGE_NONE },
};

/* Config sub-pages: back to the menu's SETTINGS category. */
static const opt_entry_t opts_speeddial[] = {
    { "Settings", I_GEAR, OPT_ACTION_SETTINGS_CAT, PAGE_NONE },
};
static const opt_entry_t opts_heldpress[] = {
    { "Settings", I_GEAR, OPT_ACTION_SETTINGS_CAT, PAGE_NONE },
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
const opt_entry_t *ui_opts_menu(int *n)      { *n = 1; return opts_menu; }
const opt_entry_t *ui_opts_display(int *n)   { *n = 3; return opts_display; }
const opt_entry_t *ui_opts_theme(int *n)     { *n = 2; return opts_theme; }
const opt_entry_t *ui_opts_font(int *n)      { *n = 2; return opts_font; }
const opt_entry_t *ui_opts_speeddial(int *n) { *n = 1; return opts_speeddial; }
const opt_entry_t *ui_opts_heldpress(int *n) { *n = 1; return opts_heldpress; }
const opt_entry_t *ui_opts_callhist(int *n)  { *n = 2; return opts_callhist; }
const opt_entry_t *ui_opts_contacts(int *n)  { *n = 1; return opts_contacts; }
const opt_entry_t *ui_opts_stub(int *n)      { *n = 1; return opts_stub; }
const opt_entry_t *ui_opts_lock(int *n)      { *n = 0; return NULL; }
const opt_entry_t *ui_opts_conv(int *n)      { *n = 0; return NULL; }
