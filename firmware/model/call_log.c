/*
 * call_log.c — static call history (mock until the telephony backbone).
 */
#include "model/call_log.h"

typedef struct {
    const char *name;
    const char *dir;    /* incoming | outgoing | missed        */
    const char *time;   /* short: fits the renderer's meta col  */
} call_entry_t;

static const call_entry_t g_calls[] = {
    { "Mom",       "incoming", "09:12"      },
    { "Dad",       "outgoing", "08:47"      },
    { "Work",      "missed",   "08:30"      },
    { "Netanel",   "incoming", "yest 21:04" },
    { "Doctor",    "outgoing", "yest 09:15" },
    { "Rina",      "missed",   "mon 18:22"  },
    { "School",    "incoming", "sun 10:05"  },
    { "Shmuel",    "outgoing", "fri 16:40"  },
};

int model_calls_count(void)
{
    return (int)(sizeof g_calls / sizeof g_calls[0]);
}

const char *model_call_name(int i)
{
    if (i < 0 || i >= model_calls_count()) {
        return "";
    }
    return g_calls[i].name;
}

const char *model_call_dir(int i)
{
    if (i < 0 || i >= model_calls_count()) {
        return "";
    }
    return g_calls[i].dir;
}

const char *model_call_time(int i)
{
    if (i < 0 || i >= model_calls_count()) {
        return "";
    }
    return g_calls[i].time;
}
