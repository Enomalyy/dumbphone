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

#ifdef __cplusplus
extern "C" {
#endif

/* Set the panel geometry in device pixels. Out-of-range values are clamped
 * to a sane window and reported on stderr. Must precede display/UI init. */
void ui_geometry_set(int w, int h);

/* Current panel size (device pixels). */
int ui_geom_w(void);
int ui_geom_h(void);

/* Derived layout metrics — computed from the panel size above: */
int ui_status_h(void);    /* pinned status bar height (top)          */
int ui_softkey_h(void);   /* pinned softkey bar height (bottom)      */
int ui_content_y(void);   /* content area top edge                   */
int ui_content_h(void);   /* content area height                     */
int ui_row_h(void);       /* list row height (menu renderer)         */
int ui_sidebar_w(void);   /* options sidebar panel width             */
int ui_sidebar_row_h(void); /* options sidebar row height            */
int ui_home_card_w(void); /* one HOME widget card width              */

#ifdef __cplusplus
}
#endif
