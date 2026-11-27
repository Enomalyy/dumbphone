/*
 * chat_model.h — what the chat list renders, and where it comes from.
 *
 * The model layer is where phone data lives (spec §16.4): the UI reads it,
 * and the WAHA relay will write it later (spec §9). The shell owns the
 * rendering; the rows are data. The source is target-specific: the desktop
 * simulator reads host/data/chats.json, the S3 will read the relay's cache
 * from storage. ui/ never touches a file directly.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_CHAT_MAX        32
#define UI_CHAT_NAME_LEN   64
#define UI_CHAT_TEXT_LEN   128

typedef struct {
    char     name[UI_CHAT_NAME_LEN];
    char     last[UI_CHAT_TEXT_LEN];   /* snippet as sent (may be "[image]") */
    char     time[8];                  /* "HH:MM"                            */
    uint16_t unread;
    bool     mine;                     /* last message was ours -> "You:"    */
    bool     group;
    bool     family;                   /* member of the FAMILY category      */
    bool     favorite;                 /* pinned to FAVORITES                */
} ui_chat_t;

typedef struct {
    ui_chat_t chats[UI_CHAT_MAX];
    int       count;
    int       archived_count;
} ui_chat_list_t;

/* Fill *out from this target's chat source. Returns 0 on success, <0 when
 * there is no data (the shell then renders an empty state). */
int ui_chat_source_load(ui_chat_list_t *out);

/* Point the source at an explicit file (the shot harness' --data flag):
 * it takes priority over the real-data/sample chain, so golden scenes are
 * deterministic on any machine. NULL restores the default chain. */
void ui_chat_source_set_path(const char *path);

/* Per-chat transport, set by the target source as it loads the list:
 * true for non-WhatsApp chats (SMS). Such chats get no WA treatment in
 * the conversation view — no delivery marks, no pending bar. A fresh load
 * clears the flags; out-of-range indexes are false. */
void model_chats_reset_sms(void);
void model_chat_set_sms(int chat_idx, bool sms);
bool ui_chat_is_sms(int chat_idx);

/* ---- per-category slice (spec §2) -------------------------------------- */

typedef enum {
    MODEL_CHAT_CAT_ALL = 0,
    MODEL_CHAT_CAT_FAMILY,
    MODEL_CHAT_CAT_UNREAD,
    MODEL_CHAT_CAT_FAVORITES,
    MODEL_CHAT_CAT_ARCHIVED,
    MODEL_CHAT_CAT_COUNT
} model_chat_cat_t;

/* A row in a category slice. Chat rows point into the caller's list;
 * synthetic rows carry no chat pointer. */
typedef enum {
    MODEL_CHAT_ROW_CHAT = 0,   /* .chat is valid            */
    MODEL_CHAT_ROW_NEW_CHAT,   /* trailing "New Chat" (ALL) */
    MODEL_CHAT_ROW_ARCHIVED,   /* summary, .archived_count  */
    MODEL_CHAT_ROW_NO_DATA     /* source unavailable        */
} model_chat_row_kind_t;

typedef struct {
    const ui_chat_t *chat;
    uint8_t kind;              /* model_chat_row_kind_t */
    int archived_count;        /* valid for MODEL_CHAT_ROW_ARCHIVED */
} model_chat_row_t;

/* Resolve `list` into the rows of category `cat`, in list order (ALL /
 * FAMILY / UNREAD / FAVORITES), capped at `max`. ALL appends one New Chat
 * row after the chats whenever the slice has room; ARCHIVED yields one
 * summary row when archived_count is non-zero. A NULL list yields a single
 * NO_DATA row — the honest empty state, not a fabricated demo list. */
int model_chats_slice(const ui_chat_list_t *list, model_chat_cat_t cat,
                      model_chat_row_t *rows, int max);

/* Total unread across all chats (0 for a NULL list). Feeds HOME's widget. */
int model_chats_unread_total(const ui_chat_list_t *list);

#ifdef __cplusplus
}
#endif
