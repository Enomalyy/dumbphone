/*
 * ui_text.c — data-text direction policy (see ui_text.h for the contract).
 *
 * No tables, no allocation, no display access: a UTF-8 walk that counts
 * strong RTL letters against strong LTR letters. The ranges below are the
 * scripts that can realistically appear in this phone's chat data, not a
 * Unicode database. Anything else (digits, punctuation, emoji, symbols) is
 * neutral and does not vote.
 */

#include "ui/ui_text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Decode one UTF-8 code point. Malformed bytes consume one byte and yield
 * U+FFFD, so a bad string can never send the walk backwards or off the end. */
static uint32_t utf8_next(const char **cursor)
{
    const unsigned char *p = (const unsigned char *)*cursor;
    uint32_t cp;
    int extra;

    if (p[0] < 0x80) {
        *cursor += 1;
        return p[0];
    }
    if ((p[0] & 0xE0) == 0xC0) {
        extra = 1;
        cp = p[0] & 0x1Fu;
    } else if ((p[0] & 0xF0) == 0xE0) {
        extra = 2;
        cp = p[0] & 0x0Fu;
    } else if ((p[0] & 0xF8) == 0xF0) {
        extra = 3;
        cp = p[0] & 0x07u;
    } else {
        *cursor += 1;
        return 0xFFFD;
    }
    for (int i = 1; i <= extra; i++) {
        if (p[i] == 0 || (p[i] & 0xC0) != 0x80) {
            *cursor += 1;   /* truncated sequence: resync on the next byte */
            return 0xFFFD;
        }
        cp = (cp << 6) | (p[i] & 0x3Fu);
    }
    *cursor += extra + 1;
    return cp;
}

/* Strong right-to-left letters. Arabic-Indic digits are excluded: a number
 * written in Arabic-Indic digits must not make the paragraph RTL on its
 * own. Bidi controls (U+200E/F, U+061C) and combining marks are neutral. */
static bool is_rtl_letter(uint32_t cp)
{
    if ((cp >= 0x0660 && cp <= 0x0669) ||
        (cp >= 0x06F0 && cp <= 0x06F9)) {
        return false;
    }
    return (cp >= 0x0590 && cp <= 0x05FF) ||   /* Hebrew                */
           (cp >= 0x0600 && cp <= 0x06FF) ||   /* Arabic                */
           (cp >= 0x0700 && cp <= 0x074F) ||   /* Syriac                */
           (cp >= 0x0750 && cp <= 0x077F) ||   /* Arabic Supplement     */
           (cp >= 0x0780 && cp <= 0x07BF) ||   /* Thaana                */
           (cp >= 0x07C0 && cp <= 0x07FF) ||   /* NKo                   */
           (cp >= 0x0800 && cp <= 0x085F) ||   /* Samaritan + Mandaic   */
           (cp >= 0x08A0 && cp <= 0x08FF) ||   /* Arabic Extended-A     */
           (cp >= 0xFB1D && cp <= 0xFB4F) ||   /* Hebrew presentation   */
           (cp >= 0xFB50 && cp <= 0xFDFF) ||   /* Arabic presentation A */
           (cp >= 0xFE70 && cp <= 0xFEFF);     /* Arabic presentation B */
}

/* Strong left-to-right letters (ASCII plus the other scripts a mixed chat
 * name can carry). Latin-1's multiplication/division signs are excluded. */
static bool is_ltr_letter(uint32_t cp)
{
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) {
        return true;
    }
    return (cp >= 0x00C0 && cp <= 0x024F && cp != 0x00D7 && cp != 0x00F7) ||
           (cp >= 0x0250 && cp <= 0x02AF) ||   /* IPA                     */
           (cp >= 0x0370 && cp <= 0x052F) ||   /* Greek + Cyrillic        */
           (cp >= 0x0530 && cp <= 0x058F) ||   /* Armenian                */
           (cp >= 0x0900 && cp <= 0x0DFF) ||   /* Indic                   */
           (cp >= 0x0E00 && cp <= 0x0E7F) ||   /* Thai                    */
           (cp >= 0x10A0 && cp <= 0x10FF) ||   /* Georgian                */
           (cp >= 0x1E00 && cp <= 0x1FFF) ||   /* Latin/Greek Extended    */
           (cp >= 0x3040 && cp <= 0x30FF) ||   /* Kana                    */
           (cp >= 0x3400 && cp <= 0x9FFF) ||   /* CJK                     */
           (cp >= 0xAC00 && cp <= 0xD7AF) ||   /* Hangul                  */
           (cp >= 0xFF21 && cp <= 0xFF3A) ||   /* fullwidth A-Z           */
           (cp >= 0xFF41 && cp <= 0xFF5A);     /* fullwidth a-z           */
}

lv_base_dir_t ui_text_base_dir(const char *utf8)
{
    if (!utf8) {
        return LV_BASE_DIR_LTR;
    }

    int rtl = 0;
    int ltr = 0;
    const char *p = utf8;
    while (*p) {
        uint32_t cp = utf8_next(&p);
        if (is_rtl_letter(cp)) {
            rtl++;
        } else if (is_ltr_letter(cp)) {
            ltr++;
        }
    }

    /* A tie means "not Hebrew-dominant": English with a Hebrew name in it
     * stays an LTR paragraph, and the bidi pass still orders the run. */
    return rtl > ltr ? LV_BASE_DIR_RTL : LV_BASE_DIR_LTR;
}

void ui_text_set(lv_obj_t *label, const char *utf8)
{
    if (!label) {
        return;
    }
    if (!utf8) {
        utf8 = "";
    }
    lv_label_set_text(label, utf8);
    lv_obj_set_style_base_dir(label, ui_text_base_dir(utf8), 0);
}
