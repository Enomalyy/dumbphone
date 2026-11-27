/*
 * chat_model.c — the per-category slice builder (spec §2, §16.4).
 *
 * Pure logic over a ui_chat_list_t: no files, no LVGL, no globals. The UI
 * maps each row to its menu_item_t (icons, "You:" prefixes, labels are
 * presentation and stay there); the relay will write the list it maintains
 * (spec §9) through the same types.
 */
#include <stddef.h>

#include "model/chat_model.h"

int model_chats_slice(const ui_chat_list_t *list, model_chat_cat_t cat,
                      model_chat_row_t *rows, int max)
{
    if (max <= 0) {
        return 0;
    }
    if (!list) {
        rows[0].chat = NULL;
        rows[0].kind = MODEL_CHAT_ROW_NO_DATA;
        rows[0].archived_count = 0;
        return 1;
    }

    int n = 0;

    if (cat == MODEL_CHAT_CAT_ARCHIVED) {
        if (list->archived_count > 0) {
            rows[n].chat = NULL;
            rows[n].kind = MODEL_CHAT_ROW_ARCHIVED;
            rows[n].archived_count = list->archived_count;
            n++;
        }
        return n;
    }

    for (int i = 0; i < list->count && n < max; i++) {
        const ui_chat_t *c = &list->chats[i];
        if (cat == MODEL_CHAT_CAT_FAMILY && !c->family) continue;
        if (cat == MODEL_CHAT_CAT_UNREAD && c->unread == 0) continue;
        if (cat == MODEL_CHAT_CAT_FAVORITES && !c->favorite) continue;
        rows[n].chat = c;
        rows[n].kind = MODEL_CHAT_ROW_CHAT;
        rows[n].archived_count = 0;
        n++;
    }

    /* ALL carries the trailing "New Chat" row whenever the slice has room. */
    if (cat == MODEL_CHAT_CAT_ALL && n < max) {
        rows[n].chat = NULL;
        rows[n].kind = MODEL_CHAT_ROW_NEW_CHAT;
        rows[n].archived_count = 0;
        n++;
    }
    return n;
}

int model_chats_unread_total(const ui_chat_list_t *list)
{
    if (!list) {
        return 0;
    }
    int total = 0;
    for (int i = 0; i < list->count; i++) {
        total += list->chats[i].unread;
    }
    return total;
}
