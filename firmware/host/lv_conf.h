/**
 * lv_conf.h — LVGL v9.4 configuration for the Tier-1 desktop simulator only.
 *
 * The ESP32-S3 build will get its own lv_conf (smaller buffer pool, PSRAM
 * allocator, Montserrat-only fonts) when the display driver lands. Keeping
 * this file host-local means the desktop can use TTF rendering and a big
 * static frame buffer without affecting the target.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* ---- color / geometry -------------------------------------------- */
#define LV_COLOR_DEPTH          16          /* RGB565, same as the panel */

/* ---- memory ------------------------------------------------------ */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_MEM_SIZE             (256 * 1024)

/* ---- tick / refresh ---------------------------------------------- */
#define LV_DEF_REFR_PERIOD      16          /* ~60 Hz */
#define LV_DPI_DEF              130

/* ---- threading: LVGL is single-threaded; the shell serializes access -- */
#define LV_USE_OS               LV_OS_NONE

/* ---- logging ------------------------------------------------------ */
#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           1

/* ---- font engine -------------------------------------------------- */
#define LV_USE_TINY_TTF         1           /* stb_truetype, bundled */
#define LV_TINY_TTF_FILE_SUPPORT 1

/* ---- bidirectional text -------------------------------------------
 * Required for Hebrew chat data: the label draw pass reorders each
 * paragraph using the label's base direction (ui_text_set() picks it).
 * Arabic JOINING stays off on purpose: the real WhatsApp corpus contains
 * no Arabic code points and no Arabic font is bundled, so the shaping
 * table would be dead flash on the S3. Flip this only together with an
 * Arabic fallback face. */
#define LV_USE_BIDI             1
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/* ---- built-in fallback fonts -------------------------------------- */
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_18   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

/* ---- themes: disabled on purpose -----------------------------------
 * The shell draws every object explicitly (flat charcoal, 0 radius, no
 * shadow). No theme means no gradients, no rounded cards, no surprises. */
#define LV_USE_THEME_DEFAULT    0

/* ---- misc --------------------------------------------------------- */
#define LV_USE_FLOAT            1

/* stdio filesystem driver: tiny_ttf loads TTFs through lv_fs. Letter 'A' is
 * arbitrary; paths are passed as "A:/abs/path.ttf". */
#define LV_USE_FS_STDIO         1
#if LV_USE_FS_STDIO
    #define LV_FS_STDIO_LETTER  'A'
    #define LV_FS_STDIO_PATH    ""
    #define LV_FS_STDIO_CACHE_SIZE 0
#endif
#define LV_USE_VECTOR_GRAPHIC   0

#endif /* LV_CONF_H */
