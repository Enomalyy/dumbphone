/*
 * test_host.c — first host unit tests (spec §16.1, issue #7).
 *
 * Two modules, both display-free by design:
 *   - ui_text_base_dir(): the data-text direction rule on its own.
 *   - ui_geometry: every derived metric at the four sizes that matter
 *     (the identity 320x480 plus the three most-used non-defaults),
 *     including the clamps and the softkey-hidden case.
 *
 * No framework: main() + checks + a PASS/FAIL summary, exit code 0 iff all
 * pass. `just test-host` builds and runs it; `just check` includes it in the
 * local gate.
 */

#include <stdio.h>

#include "ui/ui_geometry.h"
#include "ui/ui_text.h"

static int n_pass = 0;
static int n_fail = 0;

#define CHECK(cond, name) do {                                   \
    if (cond) {                                                  \
        n_pass++;                                                \
    } else {                                                     \
        n_fail++;                                                \
        printf("FAIL %s (line %d)\n", name, __LINE__);           \
    }                                                            \
} while (0)

/* ------------------------------------------------------------------ */
/* ui_text_base_dir — RTL when strong Hebrew/Arabic letters outnumber  */
/* strong LTR ones; neutral (digits, punctuation, emoji) never votes.  */
/* ------------------------------------------------------------------ */

static void test_ui_text(void)
{
    CHECK(ui_text_base_dir(NULL) == LV_BASE_DIR_LTR, "text: NULL -> LTR");
    CHECK(ui_text_base_dir("") == LV_BASE_DIR_LTR, "text: empty -> LTR");
    CHECK(ui_text_base_dir("hello world") == LV_BASE_DIR_LTR, "text: latin -> LTR");

    /* shalom (Hebrew) */
    CHECK(ui_text_base_dir("\xd7\xa9\xd7\x9c\xd7\x9e\xd7\x95\xd7\x9d") == LV_BASE_DIR_RTL,
          "text: hebrew -> RTL");
    /* salam (Arabic) */
    CHECK(ui_text_base_dir("\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85") == LV_BASE_DIR_RTL,
          "text: arabic -> RTL");

    /* Mixed: LTR majority stays an LTR paragraph. */
    CHECK(ui_text_base_dir("a b c \xd7\x9d") == LV_BASE_DIR_LTR,
          "text: 3 ltr + 1 rtl -> LTR");
    CHECK(ui_text_base_dir("a\xd7\x9d\xd7\xaa") == LV_BASE_DIR_RTL,
          "text: 1 ltr + 2 rtl -> RTL");
    /* A tie is not Hebrew-dominant. */
    CHECK(ui_text_base_dir("a\xd7\x9d") == LV_BASE_DIR_LTR,
          "text: 1 ltr + 1 rtl (tie) -> LTR");

    /* Neutrals never vote. */
    CHECK(ui_text_base_dir("12345 !?..") == LV_BASE_DIR_LTR,
          "text: digits+punct only -> LTR");
    /* Arabic-Indic digits are excluded from the RTL set. */
    CHECK(ui_text_base_dir("\xd9\xa1\xd9\xa2\xd9\xa3") == LV_BASE_DIR_LTR,
          "text: arabic-indic digits -> LTR");
    /* Malformed bytes decode to U+FFFD (neutral), never crash or loop. */
    CHECK(ui_text_base_dir("\xff\xfe") == LV_BASE_DIR_LTR,
          "text: malformed bytes -> LTR");

    /* Greek is a strong LTR script here. */
    CHECK(ui_text_base_dir("\xce\xb1\xd9\xa1") == LV_BASE_DIR_LTR,
          "text: greek + arabic-indic digit -> LTR");
}

/* ------------------------------------------------------------------ */
/* ui_geometry — derived metrics at the sizes that matter.             */
/* ------------------------------------------------------------------ */

static void test_ui_geometry(void)
{
    /* 320x480 — the identity: every formula equals the old constant. */
    ui_geometry_set(320, 480);
    CHECK(ui_geom_w() == 320 && ui_geom_h() == 480, "geom 320x480: panel size");
    CHECK(ui_status_h() == 26, "geom 320x480: status_h");
    CHECK(ui_softkey_h() == 36, "geom 320x480: softkey_h");
    CHECK(ui_content_y() == 26, "geom 320x480: content_y");
    CHECK(ui_content_h() == 418, "geom 320x480: content_h");
    CHECK(ui_row_h() == 55, "geom 320x480: row_h");
    CHECK(ui_sidebar_w() == 190, "geom 320x480: sidebar_w");
    CHECK(ui_sidebar_row_h() == 40, "geom 320x480: sidebar_row_h");
    CHECK(ui_home_card_w() == 96, "geom 320x480: home_card_w");

    /* 240x320 — small panel: the clamps bite (status 18, softkey 28,
     * row 40, sidebar_w 150, sidebar_row 32). */
    ui_geometry_set(240, 320);
    CHECK(ui_geom_w() == 240 && ui_geom_h() == 320, "geom 240x320: panel size");
    CHECK(ui_status_h() == 18, "geom 240x320: status_h");
    CHECK(ui_softkey_h() == 28, "geom 240x320: softkey_h");
    CHECK(ui_content_y() == 18, "geom 240x320: content_y");
    CHECK(ui_content_h() == 274, "geom 240x320: content_h");
    CHECK(ui_row_h() == 40, "geom 240x320: row_h");
    CHECK(ui_sidebar_w() == 150, "geom 240x320: sidebar_w");
    CHECK(ui_sidebar_row_h() == 32, "geom 240x320: sidebar_row_h");
    CHECK(ui_home_card_w() == 69, "geom 240x320: home_card_w");

    /* 480x800 — tall tablet: softkey and row hit their upper clamps. */
    ui_geometry_set(480, 800);
    CHECK(ui_geom_w() == 480 && ui_geom_h() == 800, "geom 480x800: panel size");
    CHECK(ui_status_h() == 43, "geom 480x800: status_h");
    CHECK(ui_softkey_h() == 54, "geom 480x800: softkey_h");
    CHECK(ui_content_y() == 43, "geom 480x800: content_y");
    CHECK(ui_content_h() == 703, "geom 480x800: content_h");
    CHECK(ui_row_h() == 84, "geom 480x800: row_h");
    CHECK(ui_sidebar_w() == 285, "geom 480x800: sidebar_w");
    CHECK(ui_sidebar_row_h() == 64, "geom 480x800: sidebar_row_h");
    CHECK(ui_home_card_w() == 149, "geom 480x800: home_card_w");

    /* 1280x720 — wide tablet: sidebar_w hits its 420 upper clamp. */
    ui_geometry_set(1280, 720);
    CHECK(ui_geom_w() == 1280 && ui_geom_h() == 720, "geom 1280x720: panel size");
    CHECK(ui_status_h() == 39, "geom 1280x720: status_h");
    CHECK(ui_softkey_h() == 54, "geom 1280x720: softkey_h");
    CHECK(ui_content_y() == 39, "geom 1280x720: content_y");
    CHECK(ui_content_h() == 627, "geom 1280x720: content_h");
    CHECK(ui_row_h() == 83, "geom 1280x720: row_h");
    CHECK(ui_sidebar_w() == 420, "geom 1280x720: sidebar_w");
    CHECK(ui_sidebar_row_h() == 60, "geom 1280x720: sidebar_row_h");
    CHECK(ui_home_card_w() == 416, "geom 1280x720: home_card_w");

    /* Softkeys hidden: the bar gives its height back to the content. */
    ui_geometry_set(320, 480);
    ui_geometry_set_softkeys(false);
    CHECK(ui_geom_softkeys() == false, "geom softkeys: flag off");
    CHECK(ui_softkey_h() == 0, "geom softkeys hidden: softkey_h");
    CHECK(ui_content_h() == 454, "geom softkeys hidden: content_h");
    ui_geometry_set_softkeys(true);
    CHECK(ui_geom_softkeys() == true, "geom softkeys: flag on");

    /* Out-of-range sizes are clamped to the sane window. */
    ui_geometry_set(50, 50);
    CHECK(ui_geom_w() == 120 && ui_geom_h() == 160, "geom clamp: 50x50 -> 120x160");
}

int main(void)
{
    test_ui_text();
    test_ui_geometry();

    printf("test-host: %d passed, %d failed\n", n_pass, n_fail);
    if (n_fail > 0) {
        return 1;
    }
    printf("PASS\n");
    return 0;
}
