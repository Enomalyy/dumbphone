/*
 * ui_geometry.h — the ONE runtime source of truth for panel size.
 *
 * The panel's width/height are a RUNTIME value, not compile-time constants:
 * the harness picks a resolution (preset list or free-form WxH) before init
 * and every layer — LVGL display + buffers, status/softkey bars, content
 * area, row heights, sidebar, HOME widgets, bezel mock, browser viewport,
 * shot mode — derives its geometry from here.
 *
 * Every derived metric is an IDENTITY at the default 320x480: running with
 * the default geometry produces exactly the layout (and golden frames) the
 * code had before this module existed.
 *
 * ui_geometry_set() must be called before hal_display_init() and before any
 * UI is built; it is never changed afterwards (the phone does not resize).
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Set the panel geometry in device pixels. Out-of-range values are clamped
 * to a sane window and reported on stderr. Must precede display/UI init. */
void ui_geometry_set(int w, int h);

/* Current panel size (device pixels). */
int ui_geom_w(void);
int ui_geom_h(void);

/* Show or hide the soft-key action bar. Hiding it gives its height back to
 * the content area (spec §14.9): every page then re-lays out through the
 * shell's relayout pass. Takes effect at the next relayout; the default is
 * SHOWN and the identity geometry is unchanged either way. */
void ui_geometry_set_softkeys(bool shown);
bool ui_geom_softkeys(void);

/* Soft-key caps (spec §12.2): three across the bar, one per mirrored
 * physical key. Insets and the inter-cap gap are fixed; the cap width is
 * derived so all three stay on any panel (issue #14). */
#define UI_SK_CAP_INSET    6   /* first cap's inset from the bar edge  */
#define UI_SK_CAP_GAP      6   /* gap between neighbouring caps        */
#define UI_SK_CAP_TOP      5   /* cap top inset within the bar         */

/* Derived layout metrics — computed from the panel size above: */
int ui_status_h(void);    /* pinned status bar height (top)          */
int ui_softkey_h(void);   /* softkey bar height (bottom)             */
int ui_content_y(void);   /* content area top edge                   */
int ui_content_h(void);   /* content area height                     */
int ui_row_h(void);       /* list row height (menu renderer)         */
int ui_sidebar_w(void);   /* options sidebar panel width             */
int ui_sk_cap_w(void);    /* one soft-key cap width (three across)   */
int ui_sk_cap_pitch(void);/* cap left edge to the next cap's left    */
int ui_sidebar_row_h(void); /* options sidebar row height            */
int ui_home_card_w(void); /* one HOME widget card width              */

#ifdef __cplusplus
}
#endif
