/*
 * contacts.h — the phone's contact list (spec §16.4).
 *
 * Model data: static for now, in the spirit of chats.json. The WAHA
 * backbone (spec §9) will write it later; the accessors below are the seam
 * the UI reads through, so the page never owns a copy.
 */
#pragma once

#define MODEL_CONTACTS_MAX 32

int model_contacts_count(void);
const char *model_contact_name(int i);
const char *model_contact_number(int i);
