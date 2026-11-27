/*
 * messages.h — the per-chat message store (spec §14.7, built on §16.4).
 *
 * Messages belong to a chat: one slot per chat index (into ui_chat_list_t),
 * filled by the target's source (host: the JSON fixture parser) and read by
 * the conversation page. The model owns the storage and the pure helpers
 * over it; the UI renders, the relay will write later (spec §9).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_MSG_MAX_PER_CHAT 24   /* per-chat message cap               */
#define UI_MSG_BODY_LEN     256  /* long messages wrap, not truncate   */
#define UI_MSG_FROM_LEN     16   /* group sender name ("" in 1:1)      */

/* Delivery state on the WAHA WAMessageAck scale (outgoing only):
 * PENDING = nothing shown, SERVER = one mark, DEVICE = two marks,
 * READ/PLAYED = two accent marks. WAHA's ERROR (-1) has no value here —
 * a failed send is known locally by the sender and never reaches the
 * peer, so the store only carries 0..4. */
#define UI_MSG_ACK_PENDING 0
#define UI_MSG_ACK_SERVER  1
#define UI_MSG_ACK_DEVICE  2
#define UI_MSG_ACK_READ    3
#define UI_MSG_ACK_PLAYED  4

/* A divider belongs between two consecutive messages when the gap exceeds
 * this many minutes (or they fall on different days). */
#define UI_MSG_GAP_MIN 10

typedef struct {
    bool     mine;   /* true = outgoing (right bubble)          */
    uint16_t ts;     /* day*1440 + hh*60 + mm; larger = newer   */
    uint8_t  ack;    /* WAHA WAMessageAck scale, outgoing only  */
    char     from[UI_MSG_FROM_LEN];  /* group sender, "" in 1:1 */
    char     body[UI_MSG_BODY_LEN];
} ui_message_t;

typedef struct {
    ui_message_t msgs[UI_MSG_MAX_PER_CHAT];
    int          count;   /* oldest first                       */
} ui_chat_msgs_t;

/* Target-source seam (host: the JSON parser). reset() clears every slot
 * for a fresh load; set() fills one chat's slot and orders it oldest
 * first. Both run before any page reads the store. */
void model_messages_reset(void);
void model_messages_set(int chat_idx, const ui_message_t *msgs, int n);

/* Copy chat `chat_idx`'s messages into out (oldest first). Returns 0 on
 * success, <0 when the index is out of range or the chat has no
 * messages (the page then shows its honest empty state). */
int ui_chat_messages_load(int chat_idx, ui_chat_msgs_t *out);

/* Format ts for display: "HH:MM" today, "yest HH:MM" yesterday,
 * "N d HH:MM" older. buf must hold at least 12 characters. */
void ui_msg_time_fmt(uint16_t ts, char *buf, size_t len);

/* True when a divider belongs between consecutive messages a (older) and
 * b (newer): the gap exceeds UI_MSG_GAP_MIN minutes or the day changes. */
bool ui_msg_needs_divider(const ui_message_t *a, const ui_message_t *b);

#ifdef __cplusplus
}
#endif
