# firmware — two independent build paths

ESP-IDF image for the ESP32-S3 and the Tier-1 Linux desktop simulator (SDL2 + LVGL). Separate build dirs, one CLI: this folder's `justfile`. Never drive the build from an IDE. The UI lives in `ui/`; the design authority is `../ui-architecture.md` — **§10 is the add-a-page procedure; read it before building any page.**

## Build targets (run from this folder)
| Target | What it proves |
|---|---|
| `build-s3` | device image compiles for esp32s3 (no UI in this build yet — R5, spec §16.5) |
| `run-qemu` | the image boots under xtensa QEMU, UART streamed |
| `flash-s3` | flashes a tethered board over USB (`ESPPORT` overrides the port) |
| `build-desktop` | simulator compiles; runs `vendor` + `check-colours` first |
| `run-desktop` | bezelled window + browser viewport on 127.0.0.1:8099 (`--no-server` off; `DUMBPHONE_SERVER_BIND=0.0.0.0` for the tablet) |
| `shot-desktop <screen>` | headless panel frame, clock frozen — golden-frame input; `--theme`/`--font`/`--menu-style` pin appearance deterministically |
| `shot-window <screen> [press]` | composed bezel+panel mock-up, optionally holding a key |
| `geom=WxH …` | any target at another panel geometry (spec §15); non-default writes `<name>-WxH.png` |
| `golden-check` / `golden-update` | compare committed frames to `scenes.txt` recipes / the only frame-rewrite path |
| `test-host` | display-free unit tests (`test/test_host.c`) |
| `check` | the one-command gate: build + goldens + tests |
| `picker` | panel-geometry picker page on :8096 (see `../tools/AGENTS.md`) |
| `publish-web` | build + deploy the web demo (see `../tools/AGENTS.md`) |

**Gate caveat:** never run `check` or shots with a live `dumbphone-desktop` up — two instances share `data/` and port 8099.

## Source map
- `ui/` — the whole UI: one `menu_t` renderer, page modules, theme tokens, keymaps, page registry. Read `ui/AGENTS.md`.
- `model/` — data layer (chats, contacts, call log, settings); pages are readers only. Read `model/AGENTS.md`.
- `host/` — SDL2 HAL backends, `bezel.c` (physical-key table), `frame_server.c` (browser viewport over HTTP/WebSocket), fixture loading, stubs.
- `hal/` — contract headers (display, input, audio, modem, storage, net); no S3 backends yet (R5).
- `vendor/lvgl` — LVGL pinned v9.4.0; `just vendor` fetches it on a fresh clone.
- `main/` — ESP-IDF entry: boot + one FreeRTOS task (no UI yet).
- `test/` — host unit tests · `fixtures/` — sample chat JSON for fresh clones · `data/` — runtime user storage, wiped by the gate.
- `scenes.txt` + `golden.sh` — golden-frame recipes and comparison engine.

## Rules
- Colour: palette roles only — literals live in `ui/ui_theme.c` and `host/src/bezel.c`; `check-colours` (inside `build-desktop`) fails the build on any other literal.
- All shared metrics, fonts and sizes are tokens in `ui/ui_theme.h`; geometry derives from `ui/ui_geometry.c` (spec §15).
- A pixel-moving change names its frames in the commit; goldens stay AE=0 otherwise.
