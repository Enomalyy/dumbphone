/*
 * ui_geometry.c — the runtime panel geometry (see ui_geometry.h).
 *
 * Derived metrics scale proportionally from the 320x480 baseline and are
 * clamped to a legible range. At exactly 320x480 every formula evaluates to
 * the old compile-time constant, so the default build is pixel-identical to
 * the pre-geometry code (the golden frames prove it).
 */

#include <stdio.h>

#include "model/settings.h"
#include "ui/ui_geometry.h"

#define GEOM_MIN_W   120
#define GEOM_MIN_H   160
#define GEOM_MAX     2560

static int g_w = 320;
static int g_h = 480;
/* The softkey-bar flag is model state (spec §16.4); the derived metrics
 * below read it through ui_geom_softkeys(). */

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void ui_geometry_set(int w, int h)
{
    if (w < GEOM_MIN_W || w > GEOM_MAX || h < GEOM_MIN_H || h > GEOM_MAX) {
        fprintf(stderr, "ui_geometry: %dx%d outside %d..%dx%d..%d — clamping\n",
                w, h, GEOM_MIN_W, GEOM_MIN_H, GEOM_MAX, GEOM_MAX);
    }
    g_w = clampi(w, GEOM_MIN_W, GEOM_MAX);
    g_h = clampi(h, GEOM_MIN_H, GEOM_MAX);
}

int ui_geom_w(void) { return g_w; }
int ui_geom_h(void) { return g_h; }

/* Proportional scale from the 320x480 baseline, rounded, clamped. */
static int scale_h(int base, int lo, int hi)
{
    return clampi((g_h * base + 240) / 480, lo, hi);
}

static int scale_w(int base, int lo, int hi)
{
    return clampi((g_w * base + 160) / 320, lo, hi);
}

int ui_status_h(void)
{
    return scale_h(26, 18, 44);
}

void ui_geometry_set_softkeys(bool shown)
{
    model_settings_set_softkeys(shown);
}

bool ui_geom_softkeys(void)
{
    return model_settings()->softkeys;
}

int ui_softkey_h(void)
{
    return ui_geom_softkeys() ? scale_h(36, 28, 54) : 0;
}

int ui_content_y(void)
{
    return ui_status_h();
}

int ui_content_h(void)
{
    return g_h - ui_status_h() - ui_softkey_h();
}

int ui_row_h(void)
{
    return scale_h(55, 40, 84);
}

int ui_sidebar_w(void)
{
    return scale_w(190, 150, 420);
}

int ui_sidebar_row_h(void)
{
    return scale_h(40, 32, 64);
}

/* Three soft-key caps share the bar: inset 6 at each edge and a 6 px gap
 * between neighbours. (320 - 2*6 - 2*6) / 3 == 98, the old UI_SK_CAP_W, and
 * the pitch is cap + gap == 104 — so the default frame is unchanged; every
 * other width keeps all three caps on the panel (issue #14). */
int ui_sk_cap_w(void)
{
    return (g_w - 2 * UI_SK_CAP_INSET - 2 * UI_SK_CAP_GAP) / 3;
}

int ui_sk_cap_pitch(void)
{
    return ui_sk_cap_w() + UI_SK_CAP_GAP;
}

/* Three cards across the panel: inset 11 on each side, gap 5 between.
 * (320 - 2*11 - 2*5) / 3 == 96, the old UI_HOME_CARD_W. */
int ui_home_card_w(void)
{
    return (g_w - 2 * 11 - 2 * 5) / 3;
}
