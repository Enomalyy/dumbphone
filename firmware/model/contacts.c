/*
 * contacts.c — static contact list (mock until the WAHA backbone, §9).
 */
#include "model/contacts.h"

typedef struct {
    const char *name;
    const char *num;    /* short: fits the renderer's meta col  */
} contact_entry_t;

/* Numbers are hyphen-free: the renderer's meta column is 104 px (UI_ROW_META_W), and a
 * hyphen is a word-break opportunity for LVGL text layout — an 11-char
 * "052-1234567" wrapped to two lines. Ten digits fit one line (verified). */
static const contact_entry_t g_contacts[] = {
    { "Mom",       "0521234567" },
    { "Dad",       "0549876543" },
    { "Netanel",   "0501112223" },
    { "Work",      "026234567"  },
    { "Doctor",    "0587778889" },
    { "School",    "025301234"  },
    { "Rina",      "0534445556" },
    { "Shmuel",    "0592223334" },
};

int model_contacts_count(void)
{
    return (int)(sizeof g_contacts / sizeof g_contacts[0]);
}

const char *model_contact_name(int i)
{
    if (i < 0 || i >= model_contacts_count()) {
        return "";
    }
    return g_contacts[i].name;
}

const char *model_contact_number(int i)
{
    if (i < 0 || i >= model_contacts_count()) {
        return "";
    }
    return g_contacts[i].num;
}
