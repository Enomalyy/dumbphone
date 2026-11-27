# model/ — the data layer (spec §16.4)

Owns chats, contacts, call log and settings (`chat_model`, `contacts`, `call_log`, `settings`). The UI reads it; pages are readers only — no page module keeps a copy of its data.

- **Dependency direction:** model includes `hal/` and plain C only; never `ui/` or LVGL. The UI casts at the boundary (enums numerically identical by design).
- **Persistence:** every settings setter writes through `hal_storage` immediately; load once at startup (`model_settings_load()` in `host/src/main.c`). Format: one text file `settings` in the user partition — versioned first line, `key=value` per line, parsed leniently.
- **Fixture format:** `../fixtures/chats.sample.json` (schema in its `_comment`; loader `host/src/chat_source_json.c`). The simulator reads `host/data/chats.json` (real data, git-ignored) first, then the fixture.

Full details: `README.md` here.
