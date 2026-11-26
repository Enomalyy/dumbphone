/*
 * chat_model.h — what the chat list renders, and where it comes from.
 *
 * The shell owns the rendering; the rows are data. The source is
 * target-specific: the desktop simulator reads host/data/chats.json, the S3
 * will read the relay's cache from storage. ui/ never touches a file directly.
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

#ifdef __cplusplus
}
#endif
