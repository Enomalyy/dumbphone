/*
 * chat_source_json.c — desktop chat source: host/data/chats.json, falling
 * back to the committed fixtures/chats.sample.json.
 *
 * A tiny tolerant scanner rather than a JSON library: the file is ours, the
 * schema is fixed, and this keeps the simulator dependency-free. Unknown keys
 * (like "_comment") are simply ignored.
 *
 * Schema:
 *   { "chats": [ {"name","last","time","mine","unread","group",
 *                 "family","favorite",
 *                 "messages": [ {"mine","day","time","from","text"}, ... ]},
 *                 ... ],
 *     "archived_count": N }
 *
 * The per-chat "messages" arrays (spec §14.7) feed the model's message
 * store: day 0 is today, ts = day*1440 + hh*60 + mm.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "model/chat_model.h"
#include "model/messages.h"

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

/* Append a Unicode codepoint as UTF-8; returns the new index. */
static size_t append_utf8(char *out, size_t i, size_t out_len, uint32_t cp)
{
    if (cp < 0x80) {
        if (i + 1 < out_len) out[i++] = (char)cp;
    } else if (cp < 0x800) {
        if (i + 2 < out_len) {
            out[i++] = (char)(0xC0 | (cp >> 6));
            out[i++] = (char)(0x80 | (cp & 0x3F));
        }
    } else if (cp < 0x10000) {
        if (i + 3 < out_len) {
            out[i++] = (char)(0xE0 | (cp >> 12));
            out[i++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[i++] = (char)(0x80 | (cp & 0x3F));
        }
    } else {
        if (i + 4 < out_len) {
            out[i++] = (char)(0xF0 | (cp >> 18));
            out[i++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            out[i++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[i++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    return i;
}

/* Four hex digits at p into *out; 0 on success. */
static int hex4(const char *p, uint32_t *out)
{
    uint32_t v = 0;
    for (int k = 0; k < 4; k++) {
        char c = p[k];
        v <<= 4;
        if (c >= '0' && c <= '9')      v |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
        else return -1;
    }
    *out = v;
    return 0;
}

/* Position of "key" : <value> inside the span [s, e), or NULL. The span
 * keeps a key lookup from running past its own object into a neighbour. */
static const char *find_key_span(const char *s, const char *e, const char *key)
{
    char pat[40];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    size_t plen = strlen(pat);
    for (const char *p = s; p + plen <= e; p++) {
        if (memcmp(p, pat, plen) == 0) {
            const char *q = skip_ws(p + plen);
            if (q < e && *q == ':') {
                return skip_ws(q + 1);
            }
        }
    }
    return NULL;
}

static bool parse_string_span(const char *s, const char *e, const char *key,
                              char *out, size_t out_len)
{
    const char *p = find_key_span(s, e, key);
    if (!p || p >= e || *p != '"') {
        return false;
    }
    p++;
    size_t i = 0;
    while (p < e && *p && *p != '"') {
        char c = *p;
        if (c == '\\' && p + 1 < e) {
            p++;
            switch (*p) {
            case 'n':  c = '\n'; p++; break;
            case 't':  c = '\t'; p++; break;
            case 'r':  c = '\r'; p++; break;
            case '"':  c = '"';  p++; break;
            case '\\': c = '\\'; p++; break;
            case 'u': {
                uint32_t cp = 0;
                if (p + 4 < e && hex4(p + 1, &cp) == 0) {
                    p += 5; /* past \uXXXX */
                    /* UTF-16 surrogate pair -> one codepoint. */
                    if (cp >= 0xD800 && cp <= 0xDBFF && p + 5 < e &&
                        p[0] == '\\' && p[1] == 'u') {
                        uint32_t lo = 0;
                        if (hex4(p + 2, &lo) == 0 && lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                            p += 6;
                        }
                    }
                    i = append_utf8(out, i, out_len, cp);
                    continue; /* already written */
                }
                c = '?';
                p += (p + 4 < e) ? 5 : 1;
                break;
            }
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

static bool parse_int_span(const char *s, const char *e, const char *key, int *out)
{
    const char *p = find_key_span(s, e, key);
    if (!p || p >= e) {
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

static bool parse_bool_span(const char *s, const char *e, const char *key, bool *out)
{
    const char *p = find_key_span(s, e, key);
    if (!p || p >= e) {
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

/* One message object: {mine, day, time "HH:MM", ack, from, text}.
 * Missing fields keep their zero values (an undated message sorts to the
 * top of the chat; a missing ack is PENDING). */
static void parse_message(const char *p, const char *e, ui_message_t *m)
{
    memset(m, 0, sizeof *m);
    parse_bool_span(p, e, "mine", &m->mine);
    int ack = 0;
    if (parse_int_span(p, e, "ack", &ack)) {
        m->ack = (uint8_t)(ack < 0 ? 0 : (ack > 4 ? 4 : ack));
    }
    int day = 0;
    if (parse_int_span(p, e, "day", &day) && day < 0) {
        day = 0;
    }
    char tbuf[8] = "";
    int hh = 0, mm = 0;
    if (parse_string_span(p, e, "time", tbuf, sizeof tbuf)) {
        sscanf(tbuf, "%d:%d", &hh, &mm);
    }
    parse_string_span(p, e, "from", m->from, sizeof m->from);
    parse_string_span(p, e, "text", m->body, sizeof m->body);
    m->ts = (uint16_t)(day * 1440 + hh * 60 + mm);
}

/* The optional per-chat "messages" array inside [p, e) -> the model store. */
static void parse_messages(const char *p, const char *e, int chat_idx)
{
    const char *arr = find_key_span(p, e, "messages");
    if (!arr || arr >= e || *arr != '[') {
        return;
    }
    arr++;
    ui_message_t msgs[UI_MSG_MAX_PER_CHAT];
    int n = 0;
    const char *q = arr;
    while (n < UI_MSG_MAX_PER_CHAT) {
        q = skip_ws(q);
        if (q >= e || *q == ']' || *q == '\0') {
            break;
        }
        if (*q != '{') {
            break;
        }
        const char *mend = block_end(q, '{', '}');
        if (!mend || mend > e) {
            break;
        }
        parse_message(q, mend, &msgs[n]);
        n++;
        q = mend + 1;
        if (*q == ',') {
            q++;
        }
    }
    model_messages_set(chat_idx, msgs, n);
}

/* Explicit data file (the shot harness' --data flag): takes priority over
 * the real-data/sample chain so golden scenes are deterministic. */
static const char *g_explicit_path = NULL;

void ui_chat_source_set_path(const char *path)
{
    g_explicit_path = path;
}

int ui_chat_source_load(ui_chat_list_t *out)
{
    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof *out);
    model_messages_reset();   /* a fresh load owns the message store too */
    model_chats_reset_sms();  /* ...and the per-chat transport flags       */

    /* Explicit path first (deterministic shots), then real data
     * (host/data/chats.json, git-ignored), then the committed sample with
     * invented names, so a fresh clone is never empty. */
    char *text = NULL;
    if (g_explicit_path && load_file(g_explicit_path, &text) == 0) {
        /* explicit source loaded */
    } else if (load_file(CHAT_DATA_PATH, &text) != 0) {
        if (load_file(CHAT_SAMPLE_PATH, &text) != 0) {
            fprintf(stderr, "[chats] cannot read %s or %s\n",
                    CHAT_DATA_PATH, CHAT_SAMPLE_PATH);
            return -1;
        }
        fprintf(stderr, "[chats] %s not found, using the sample %s\n",
                CHAT_DATA_PATH, CHAT_SAMPLE_PATH);
    }

    int archived = 0;
    if (parse_int_span(text, text + strlen(text), "archived_count", &archived)) {
        out->archived_count = archived;
    }

    const char *arr = find_key_span(text, text + strlen(text), "chats");
    if (!arr || *arr != '[') {
        free(text);
        return -1;
    }
    arr++;
    const char *text_end = text + strlen(text);

    const char *p = arr;
    while (out->count < UI_CHAT_MAX) {
        p = skip_ws(p);
        if (p >= text_end || *p == ']' || *p == '\0') {
            break;
        }
        if (*p != '{') {
            break;
        }
        const char *end = block_end(p, '{', '}');
        if (!end) {
            break;
        }

        ui_chat_t *c = &out->chats[out->count];
        if (!parse_string_span(p, end, "name", c->name, sizeof c->name)
                || !c->name[0]) {
            p = end + 1;
            continue;                       /* nameless rows are useless */
        }
        parse_string_span(p, end, "last", c->last, sizeof c->last);
        parse_string_span(p, end, "time", c->time, sizeof c->time);
        parse_bool_span(p, end, "mine", &c->mine);
        parse_bool_span(p, end, "group", &c->group);
        parse_bool_span(p, end, "family", &c->family);
        parse_bool_span(p, end, "favorite", &c->favorite);
        bool sms = false;
        if (parse_bool_span(p, end, "sms", &sms)) {
            model_chat_set_sms(out->count, sms);
        }
        int unread = 0;
        if (parse_int_span(p, end, "unread", &unread) && unread > 0) {
            c->unread = (uint16_t)unread;
        }
        parse_messages(p, end, out->count);
        out->count++;

        p = end + 1;
        if (*p == ',') {
            p++;
        }
    }

    free(text);
    return out->count > 0 ? 0 : -1;
}

