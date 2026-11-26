/*
 * ui_options.c — the per-page options tables behind the SHOULDER-L sidebar.
 *
 * Pages are data (spec §4): each screen id maps to a small const table of
 * opt_entry_t rows. Every listed option is an action that WORKS in the
 * current UI state — opening a real page, or switching the main-menu view
 * (the two renderings of the same menu_t data). Nothing here is inert: the
 * sidebar shows only options relevant to the page it overlays.
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

const opt_entry_t *ui_options_for(ui_screen_id_t screen, int *n)
{
    switch (screen) {
    case UI_SCREEN_HOME:       *n = 3; return opts_home;
    case UI_SCREEN_CHATLIST:   *n = 3; return opts_messaging;
    case UI_SCREEN_MENU:       *n = 3; return opts_menu;
    case UI_SCREEN_SETTINGS:   *n = 2; return opts_settings;
    case UI_SCREEN_DISPLAY:    *n = 2; return opts_display;
    case UI_SCREEN_FONT:       *n = 2; return opts_font;
    case UI_SCREEN_SPEEDDIALS: *n = 1; return opts_speeddial;
    case UI_SCREEN_HELDPRESS:  *n = 1; return opts_heldpress;
    case UI_SCREEN_CALLHIST:   *n = 2; return opts_callhist;
    case UI_SCREEN_CONTACTS:   *n = 1; return opts_contacts;
    case UI_SCREEN_STUB:       *n = 1; return opts_stub;
    case UI_SCREEN_LOCK:       *n = 0; return NULL;        /* locked: no options */
    default:                   *n = 0; return NULL;        /* unknown screen */
    }
}
