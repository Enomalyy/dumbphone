/*
 * chat_source_json.c — desktop chat source: host/data/chats.json.
 *
 * A tiny tolerant scanner rather than a JSON library: the file is ours, the
 * schema is fixed, and this keeps the simulator dependency-free. Unknown keys
 * (like "_comment") are simply ignored.
 *
 * Schema:
 *   { "chats": [ {"name","last","time","mine","unread","group",
 *                 "family","favorite"}, ... ],
 *     "archived_count": N }
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui/chat_model.h"

#ifndef CHAT_DATA_PATH
#define CHAT_DATA_PATH "data/chats.json"
#endif

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    return p;
}

/* Position of "key" : <value>, or NULL. */
static const char *find_key(const char *obj, const char *key)
{
    char pat[40];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(obj, pat);
    if (!p) {
        return NULL;
    }
    p = skip_ws(p + strlen(pat));
    if (*p != ':') {
        return NULL;
    }
    return skip_ws(p + 1);
}

static bool parse_string(const char *obj, const char *key, char *out, size_t out_len)
{
    const char *p = find_key(obj, key);
    if (!p || *p != '"') {
        return false;
    }
    p++;
    size_t i = 0;
    while (*p && *p != '"') {
        char c = *p;
        if (c == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n':  c = '\n'; p++; break;
            case 't':  c = '\t'; p++; break;
            case 'r':  c = '\r'; p++; break;
            case '"':  c = '"';  p++; break;
            case '\\': c = '\\'; p++; break;
            case 'u':  c = '?';  p += (p[1] && p[2] && p[3] && p[4]) ? 5 : 1; break;
            default:   c = *p;   p++; break;
            }
        } else {
            p++;
        }
        if (i + 1 < out_len) {
            out[i++] = c;
        }
    }
    out[i] = '\0';
    return true;
}

static bool parse_int(const char *obj, const char *key, int *out)
{
    const char *p = find_key(obj, key);
    if (!p) {
        return false;
    }
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) {
        return false;
    }
    *out = (int)v;
    return true;
}

static bool parse_bool(const char *obj, const char *key, bool *out)
{
    const char *p = find_key(obj, key);
    if (!p) {
        return false;
    }
    if (!strncmp(p, "true", 4))  { *out = true;  return true; }
    if (!strncmp(p, "false", 5)) { *out = false; return true; }
    return false;
}

/* End of the {...} / [...] block starting at p, honouring strings. */
static const char *block_end(const char *p, char open, char close)
{
    int depth = 0;
    bool in_str = false;
    for (; *p; p++) {
        char c = *p;
        if (in_str) {
            if (c == '\\' && p[1]) {
                p++;
            } else if (c == '"') {
                in_str = false;
            }
            continue;
        }
        if (c == '"') {
            in_str = true;
        } else if (c == open) {
            depth++;
        } else if (c == close) {
            if (--depth == 0) {
                return p;
            }
        }
    }
    return NULL;
}

static int load_file(const char *path, char **out)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 512 * 1024) {
        fclose(f);
        return -1;
    }
    char *buf = malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    *out = buf;
    return 0;
}

int ui_chat_source_load(ui_chat_list_t *out)
{
    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof *out);

    char *text = NULL;
    if (load_file(CHAT_DATA_PATH, &text) != 0) {
        fprintf(stderr, "[chats] cannot read %s\n", CHAT_DATA_PATH);
        return -1;
    }

    int archived = 0;
    if (parse_int(text, "archived_count", &archived)) {
        out->archived_count = archived;
    }

    const char *arr = find_key(text, "chats");
    if (!arr || *arr != '[') {
        free(text);
        return -1;
    }
    arr++;

    const char *p = arr;
    while (out->count < UI_CHAT_MAX) {
        p = skip_ws(p);
        if (*p == ']' || *p == '\0') {
            break;
        }
        if (*p != '{') {
            break;
        }
        const char *end = block_end(p, '{', '}');
        if (!end) {
            break;
        }

        /* Work on a bounded copy so the key lookups cannot run past this
         * object into its neighbours. */
        char obj[768];
        size_t len = (size_t)(end - p) + 1;
        if (len >= sizeof obj) {
            len = sizeof obj - 1;
        }
        memcpy(obj, p, len);
        obj[len] = '\0';

        ui_chat_t *c = &out->chats[out->count];
        if (!parse_string(obj, "name", c->name, sizeof c->name) || !c->name[0]) {
            p = end + 1;
            continue;                       /* nameless rows are useless */
        }
        parse_string(obj, "last", c->last, sizeof c->last);
        parse_string(obj, "time", c->time, sizeof c->time);
        parse_bool(obj, "mine", &c->mine);
        parse_bool(obj, "group", &c->group);
        parse_bool(obj, "family", &c->family);
        parse_bool(obj, "favorite", &c->favorite);
        int unread = 0;
        if (parse_int(obj, "unread", &unread) && unread > 0) {
            c->unread = (uint16_t)unread;
        }
        out->count++;

        p = end + 1;
        if (*p == ',') {
            p++;
        }
    }

    free(text);
    return out->count > 0 ? 0 : -1;
}
