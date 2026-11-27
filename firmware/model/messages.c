/*
 * messages.c — the per-chat message store (see messages.h for the contract).
 *
 * Plain C, no UI/LVGL includes: the model layer never reaches into the
 * renderer (spec §16.4). The store is static state filled by the target's
 * source at load time; pages read copies out of it.
 */

#include "model/messages.h"

#include <string.h>

#include <stdio.h>

#include "model/chat_model.h"   /* UI_CHAT_MAX: one slot per chat */

static ui_chat_msgs_t g_store[UI_CHAT_MAX];

void model_messages_reset(void)
{
    memset(g_store, 0, sizeof g_store);
}

void model_messages_set(int chat_idx, const ui_message_t *msgs, int n)
{
    if (!msgs || chat_idx < 0 || chat_idx >= UI_CHAT_MAX) {
        return;
    }
    if (n > UI_MSG_MAX_PER_CHAT) {
        n = UI_MSG_MAX_PER_CHAT;
    }
    ui_chat_msgs_t *s = &g_store[chat_idx];
    memcpy(s->msgs, msgs, (size_t)n * sizeof *msgs);

    /* Oldest first: a stable insertion sort by ts, so equal stamps keep
     * their source order. n is small (capped at 24) — no fancy sort. */
    for (int i = 1; i < n; i++) {
        ui_message_t key = s->msgs[i];
        int j = i - 1;
        while (j >= 0 && s->msgs[j].ts > key.ts) {
            s->msgs[j + 1] = s->msgs[j];
            j--;
        }
        s->msgs[j + 1] = key;
    }
    s->count = n;
}

int ui_chat_messages_load(int chat_idx, ui_chat_msgs_t *out)
{
    if (!out || chat_idx < 0 || chat_idx >= UI_CHAT_MAX) {
        return -1;
    }
    const ui_chat_msgs_t *s = &g_store[chat_idx];
    if (s->count == 0) {
        return -1;
    }
    memcpy(out, s, sizeof *out);
    return 0;
}

void ui_msg_time_fmt(uint16_t ts, char *buf, size_t len)
{
    if (!buf || len == 0) {
        return;
    }
    int day = ts / 1440;
    int min = ts % 1440;
    if (day <= 0) {
        snprintf(buf, len, "%02d:%02d", min / 60, min % 60);
    } else if (day == 1) {
        snprintf(buf, len, "yest %02d:%02d", min / 60, min % 60);
    } else {
        snprintf(buf, len, "%d d %02d:%02d", day, min / 60, min % 60);
    }
}

bool ui_msg_needs_divider(const ui_message_t *a, const ui_message_t *b)
{
    if (!a || !b) {
        return false;
    }
    if (b->ts / 1440 > a->ts / 1440) {   /* the day changed */
        return true;
    }
    return b->ts - a->ts > UI_MSG_GAP_MIN;
}
