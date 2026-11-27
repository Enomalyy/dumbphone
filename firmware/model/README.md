# model/ — the data model layer (spec §16.4)

Where phone data lives. The UI **reads** it; the WAHA relay will **write** it
(spec §9). No page module owns a copy of its data any more:

| File | Owns |
|---|---|
| `chat_model.h/.c` | chat types, the target-specific source seam (`ui_chat_source_load`), and the per-category slice builder (spec §2) — a pure function over the list |
| `contacts.h/.c` | the contact list (static mock until the WAHA backbone) |
| `call_log.h/.c` | the call history (static mock until the telephony backbone) |
| `settings.h/.c` | every user-configurable value: menu style, call preference, theme, the three font roles, softkey bar, nine speed dials, sixteen held-press bindings — plus persistence through `hal_storage` |

## Rules

- **Dependency direction:** model may include `hal/` (it persists through
  `hal_storage`) and plain C only. It never includes `ui/` or LVGL. The UI
  includes model headers and casts at the boundary where a model value maps
  to a UI enum (theme id, menu style, font face — numerically identical by
  design).
- **Mutators persist.** Every setter in `settings.c` writes through
  `hal_storage` immediately: a change on the device is durable. Load happens
  once at startup (`model_settings_load()` in `host/src/main.c`, after
  `hal_storage_init()`), before the UI thread starts.
- **Persistence format:** one text file, `settings`, in the USER partition
  (host: `data/user/settings`), versioned first line, one `key=value` per
  line, parsed leniently — unknown keys ignored, missing keys keep defaults,
  a bad version re-seeds everything.

## Where it compiles

Today the **host build** lists the `.c` files in `host/CMakeLists.txt` (both
`dumbphone-desktop` and `test-host`). The S3 build does not compile them yet —
that is R5's job: `hal`, `ui` and `model` become ESP-IDF components whose
`CMakeLists.txt` also works as a plain CMake library for the host (spec
§16.2/§16.5), so this directory carries over unchanged.

## The gate and persistence

`golden.sh` (firmware root) wipes `data/user/` before every scene: the gate tests the
factory-seeded boot, and one scene's toggles must never leak into the next
frame. Interactive runs keep their state in `data/user/settings`.
