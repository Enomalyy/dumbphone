/*
 * call_log.h — the phone's call history (spec §16.4).
 *
 * Model data: static for now, in the spirit of chats.json. The WAHA /
 * telephony backbone will write it later; the accessors below are the seam
 * the UI reads through, so the page never owns a copy.
 */
#pragma once

#define MODEL_CALLS_MAX 16

int model_calls_count(void);
const char *model_call_name(int i);
const char *model_call_dir(int i);   /* "incoming" | "outgoing" | "missed" */
const char *model_call_time(int i);
