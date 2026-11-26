/*
 * ui_icons.h — Font Awesome 6 solid glyph strings (spec §7: whole-pixel icons).
 *
 * Icons are drawn as bitmap-font glyphs from the baked-in FA-Solid face; each
 * is a handful of whole-pixel primitives in one palette colour. The constants
 * live here so every module (shell, renderer, pages) names the same codepoints
 * without duplicating the table. Add a glyph once, here, and use it anywhere.
 */
#pragma once

#define I_USER       u8"\uF007"   /* user                */
#define I_USERS      u8"\uF0C0"   /* users               */
#define I_ARCHIVE    u8"\uF187"   /* box-archive         */
#define I_ENVELOPE   u8"\uF0E0"   /* envelope            */
#define I_PHONE      u8"\uF095"   /* phone               */
#define I_COMMENT    u8"\uF075"   /* comment (WhatsApp)  */
#define I_MAP_PIN    u8"\uF276"   /* map-pin             */
#define I_JOURNAL    u8"\uF02D"   /* book                */
#define I_GEAR       u8"\uF013"   /* gear                */
#define I_SIGNAL     u8"\uF012"   /* signal bars         */
#define I_BATTERY    u8"\uF240"   /* battery-full        */
#define I_PLANE      u8"\uF072"   /* plane               */
#define I_BOLT       u8"\uF0E7"   /* bolt                */
#define I_CARET_R    u8"\uF0DA"   /* caret-right (select) */
#define I_CARET_L    u8"\uF0D9"   /* caret-left           */
#define I_ELLIPSIS   u8"\uF141"   /* ellipsis             */
#define I_INBOX      u8"\uF02C"   /* inbox (ALL category) */
#define I_HEART      u8"\uF004"   /* heart (FAVORITES)    */
#define I_TH         u8"\uF00A"   /* th-large (grid view) */
#define I_LOCK       u8"\uF023"   /* lock (lock screen)   */
/* Appearance rows (Settings > Display / Font). Named here like every other
 * glyph so a page never carries a bare codepoint. */
#define I_PALETTE    u8"\uF53F"   /* palette (appearance) */
#define I_ADJUST     u8"\uF042"   /* circle-half-stroke (dark/light) */
#define I_FONT       u8"\uF031"   /* font                 */
#define I_HEADING    u8"\uF1DC"   /* heading              */
#define I_PARAGRAPH  u8"\uF1DD"   /* paragraph            */
