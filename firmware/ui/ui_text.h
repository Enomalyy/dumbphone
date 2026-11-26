/*
 * ui_text.h — the ONE entry point for data-bearing label text.
 *
 * CHROME strings (soft-key caps, status bar, page/category titles, settings
 * labels) are English and call lv_label_set_text() directly. Anything that
 * comes from chat/contact data — chat names, message previews, contact
 * names, stub titles, live values — goes through ui_text_set(): it sets the
 * text AND the label's base direction.
 *
 * Direction policy: RTL when the string's strong Hebrew/Arabic characters
 * outnumber its strong LTR ones, LTR otherwise. That is a per-PARAGRAPH
 * choice only. Mixed strings ("Tehillim ... בנימין") keep their internal
 * runs, which LVGL's bidi pass orders correctly once LV_USE_BIDI is on
 * (host/lv_conf.h). Digits, punctuation, emoji and whitespace are neutral
 * and do not vote.
 *
 * This module owns the decision, not the glyphs: the shell loads a Hebrew
 * and a monochrome-emoji fallback per text size and chains them behind the
 * pixel faces, so only missing code points ever leave Silkscreen/VT323
 * (ui_architecture.md §5, fonts README).
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Set `utf8` on `label` and pick its base direction. NULL text is treated
 * as empty (LTR). Safe to call on a reused label; it re-decides each time. */
void ui_text_set(lv_obj_t *label, const char *utf8);

/* The direction rule on its own: LV_BASE_DIR_RTL when Hebrew/Arabic letters
 * are the majority of the string's strong letters, LV_BASE_DIR_LTR
 * otherwise (including neutral-only strings). Exposed so the rule can be
 * tested without a display. */
lv_base_dir_t ui_text_base_dir(const char *utf8);

#ifdef __cplusplus
}
#endif
