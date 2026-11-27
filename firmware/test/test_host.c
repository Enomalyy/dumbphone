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
#include <string.h>

#include "hal/hal_storage.h"
#include "model/chat_model.h"
#include "model/settings.h"
#include "ui/ui_geometry.h"
#include "ui/ui_input.h"
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

/* ------------------------------------------------------------------ */
/* ui_key_repeat_next_interval — the §14.8 halving ladder: 200 -> 100  */
/* -> 50 -> 40, and the floor holds from there on.                     */
/* ------------------------------------------------------------------ */

static void test_key_repeat(void)
{
    uint32_t iv = UI_KEY_REPEAT_START_MS;
    CHECK(iv == 200, "repeat: start interval is 200 ms");
    iv = ui_key_repeat_next_interval(iv);
    CHECK(iv == 100, "repeat: 200 -> 100");
    iv = ui_key_repeat_next_interval(iv);
    CHECK(iv == 50, "repeat: 100 -> 50");
    iv = ui_key_repeat_next_interval(iv);
    CHECK(iv == UI_KEY_REPEAT_MIN_MS, "repeat: 50 -> floor 40");
    iv = ui_key_repeat_next_interval(iv);
    CHECK(iv == UI_KEY_REPEAT_MIN_MS, "repeat: floor holds at 40");
}

/* ------------------------------------------------------------------ */
/* model_chats_slice — the per-category slice builder (spec §16.4).   */
/* The UI's chat list reads its rows from here; these checks pin the  */
/* contract: order, membership, caps, synthetic rows, empty state.    */
/* ------------------------------------------------------------------ */

static void test_model_chats(void)
{
    ui_chat_list_t list = {
        .count = 4,
        .archived_count = 5,
        .chats = {
            [0] = { .name = "A", .unread = 2, .family = true },
            [1] = { .name = "B", .favorite = true },
            [2] = { .name = "C", .unread = 1, .family = true,
                    .favorite = true },
            [3] = { .name = "D" },
        },
    };

    model_chat_row_t rows[UI_CHAT_MAX + 2];

    /* ALL: every chat in order, then the trailing New Chat row. */
    int n = model_chats_slice(&list, MODEL_CHAT_CAT_ALL, rows, UI_CHAT_MAX + 2);
    CHECK(n == 5, "slice ALL: 4 chats + New Chat");
    CHECK(rows[0].kind == MODEL_CHAT_ROW_CHAT && rows[0].chat == &list.chats[0],
          "slice ALL: row 0 is chat A");
    CHECK(rows[3].kind == MODEL_CHAT_ROW_CHAT && rows[3].chat == &list.chats[3],
          "slice ALL: row 3 is chat D");
    CHECK(rows[4].kind == MODEL_CHAT_ROW_NEW_CHAT && rows[4].chat == NULL,
          "slice ALL: trailing New Chat row");

    /* FAMILY / UNREAD / FAVORITES: membership by flag, list order kept. */
    n = model_chats_slice(&list, MODEL_CHAT_CAT_FAMILY, rows, UI_CHAT_MAX + 2);
    CHECK(n == 2 && rows[0].chat == &list.chats[0] && rows[1].chat == &list.chats[2],
          "slice FAMILY: A and C");

    n = model_chats_slice(&list, MODEL_CHAT_CAT_UNREAD, rows, UI_CHAT_MAX + 2);
    CHECK(n == 2 && rows[0].chat == &list.chats[0] && rows[1].chat == &list.chats[2],
          "slice UNREAD: A and C");

    n = model_chats_slice(&list, MODEL_CHAT_CAT_FAVORITES, rows, UI_CHAT_MAX + 2);
    CHECK(n == 2 && rows[0].chat == &list.chats[1] && rows[1].chat == &list.chats[2],
          "slice FAVORITES: B and C");

    /* ARCHIVED: one summary row carrying the count. */
    n = model_chats_slice(&list, MODEL_CHAT_CAT_ARCHIVED, rows, UI_CHAT_MAX + 2);
    CHECK(n == 1 && rows[0].kind == MODEL_CHAT_ROW_ARCHIVED,
          "slice ARCHIVED: one summary row");
    CHECK(rows[0].archived_count == 5, "slice ARCHIVED: count carried");

    /* The cap drops the overflow (and the New Chat row when full). */
    n = model_chats_slice(&list, MODEL_CHAT_CAT_ALL, rows, 3);
    CHECK(n == 3 && rows[2].kind == MODEL_CHAT_ROW_CHAT && rows[2].chat == &list.chats[2],
          "slice ALL cap=3: A B C, no New Chat");

    /* Empty list (source loaded, zero chats): ALL is just New Chat; */
    /* ARCHIVED has no summary row.                                 */
    ui_chat_list_t empty = { .count = 0, .archived_count = 0 };
    n = model_chats_slice(&empty, MODEL_CHAT_CAT_ALL, rows, UI_CHAT_MAX + 2);
    CHECK(n == 1 && rows[0].kind == MODEL_CHAT_ROW_NEW_CHAT,
          "slice ALL empty list: New Chat only");
    n = model_chats_slice(&empty, MODEL_CHAT_CAT_ARCHIVED, rows, UI_CHAT_MAX + 2);
    CHECK(n == 0, "slice ARCHIVED empty list: no rows");

    /* NULL list (source unavailable): one honest NO_DATA row. */
    n = model_chats_slice(NULL, MODEL_CHAT_CAT_ALL, rows, UI_CHAT_MAX + 2);
    CHECK(n == 1 && rows[0].kind == MODEL_CHAT_ROW_NO_DATA,
          "slice ALL null list: NO_DATA row");
    n = model_chats_slice(NULL, MODEL_CHAT_CAT_UNREAD, rows, UI_CHAT_MAX + 2);
    CHECK(n == 1 && rows[0].kind == MODEL_CHAT_ROW_NO_DATA,
          "slice UNREAD null list: NO_DATA row");

    /* Unread total feeds HOME's widget. */
    CHECK(model_chats_unread_total(&list) == 3, "unread total: 2+0+1+0 = 3");
    CHECK(model_chats_unread_total(NULL) == 0, "unread total: null -> 0");
}

/* ------------------------------------------------------------------ */
/* model settings — the round-trip through hal_storage (spec §16.4).  */
/* Mutate, save, wipe memory, load: every value must come back. The    */
/* parser's leniency is pinned too: unknown keys ignored, missing     */
/* keys keep factory values, bad versions void the file.              */
/* ------------------------------------------------------------------ */

static void test_model_settings(void)
{
    /* The test process starts at the factory defaults. */
    model_settings_reset();
    const model_settings_t *s = model_settings();
    CHECK(s->softkeys == true, "settings: factory softkeys on");
    CHECK(s->menu_style == MODEL_MENU_CAROUSEL, "settings: factory menu carousel");
    CHECK(strcmp(s->speeddial[0], "Mom") == 0, "settings: factory dial 0 Mom");

    /* Mutate the live state (each mutator also saves). */
    model_settings_set_menu_style(MODEL_MENU_GRID);
    model_settings_set_theme(MODEL_THEME_LIGHT);
    model_settings_set_softkeys(false);
    model_settings_set_font(1, 3, 20);
    model_settings_set_speeddial(2, "Work");
    model_settings_cycle_heldpress(1);   /* slot 1: none -> new message */

    CHECK(model_settings_save() == 0, "settings: save ok");

    /* Wipe the in-memory state and restore it from storage. */
    model_settings_reset();
    CHECK(model_settings()->menu_style == MODEL_MENU_CAROUSEL,
          "settings: reset back to factory");
    CHECK(model_settings_load() == 0, "settings: load ok");
    s = model_settings();
    CHECK(s->menu_style == MODEL_MENU_GRID, "settings: menu style round-trip");
    CHECK(s->theme == MODEL_THEME_LIGHT, "settings: theme round-trip");
    CHECK(s->softkeys == false, "settings: softkeys round-trip");
    CHECK(s->fonts[1].face == 3 && s->fonts[1].px == 20,
          "settings: font round-trip");
    CHECK(strcmp(s->speeddial[2], "Work") == 0, "settings: speeddial round-trip");
    CHECK(s->heldpress[1] == MODEL_HP_NEW_MESSAGE, "settings: heldpress round-trip");

    /* Lenient parsing: unknown keys ignored, out-of-range values rejected,
     * missing keys keep their factory value. */
    const char *lenient =
        "1\n"
        "menu_style=2\n"
        "bogus_key=whatever\n"
        "theme=99\n"          /* out of range: ignored              */
        "font_text=4:16\n";
    CHECK(hal_storage_write_file(HAL_STORAGE_USER, "settings", lenient,
                                 strlen(lenient)) == 0,
          "settings: wrote lenient file");
    model_settings_reset();
    model_settings_load();
    s = model_settings();
    CHECK(s->menu_style == MODEL_MENU_MOVING, "settings: lenient menu style parsed");
    CHECK(s->theme == MODEL_THEME_DARK, "settings: out-of-range theme ignored");
    CHECK(s->fonts[1].face == 4 && s->fonts[1].px == 16,
          "settings: lenient font parsed");
    CHECK(strcmp(s->speeddial[0], "Mom") == 0, "settings: missing keys keep factory");

    /* A bad version voids the whole file. */
    const char *badver = "2\nmenu_style=1\n";
    hal_storage_write_file(HAL_STORAGE_USER, "settings", badver,
                           strlen(badver));
    model_settings_reset();
    model_settings_load();
    CHECK(model_settings()->menu_style == MODEL_MENU_CAROUSEL,
          "settings: bad version re-seeds factory");

    /* Leave no trace: the live app must boot with the factory state. */
    hal_storage_remove(HAL_STORAGE_USER, "settings");
    model_settings_reset();
}

int main(void)
{
    test_ui_text();
    test_ui_geometry();
    test_key_repeat();
    test_model_chats();
    test_model_settings();

    printf("test-host: %d passed, %d failed\n", n_pass, n_fail);
    if (n_fail > 0) {
        return 1;
    }
    printf("PASS\n");
    return 0;
}
