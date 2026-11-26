# dumbphone — ESP32-based open-source dumb phone (proof of concept)

The project folder for the dumb-phone idea: cheap ESP32 hardware + a subscription server backend
that relays modern apps (WhatsApp, Mail, Maps, banking) to a thin client.

- `ESP32 phone.txt` — **the official mind map** for the proof of concept. A working document
  co-edited by Eli and the agent (it is NOT an AGENTS.md). Content is drawn from his voice notes
  of 24/11 (initial 7-min pitch + follow-up batches); raw transcripts in `transcripts/`.
- `datasheets/` — component datasheets. Currently: HP35001-DCT 3.5" IPS capacitive-touch display
  (ST7796U + GT911) — the Prototype #1 screen.
- `transcripts/` — raw voice-note transcripts that source the mind map.
- `firmware/` — **two build paths, kept independent.** (a) ESP-IDF (target `esp32s3`):
  `main/main.c` = boot + one FreeRTOS task, `just build-s3|run-qemu|flash-s3` — the UI is NOT in
  this build yet. (b) **Tier-1 Linux desktop simulator** (where the whole UI lives):
  `ui/` = the LVGL phone shell — `ui_menu.{h,c}` the canonical `menu_t` renderer (+ the one
  shared `ui_row_style()` row paint), `ui_keymap` per-page actions, `ui_theme` palette + ALL
  design tokens (fonts/metrics), `ui_options` the SHOULDER-L sidebar tables, `ui_sidebar` the
  options overlay, `ui_settings` / `ui_speeddial` / `ui_heldpress` / `ui_calls` page data,
  `ui_home` the real HOME screen (wallpaper + widget row), `ui_messaging` the chat list,
  `ui_mainmenu` the carousel, `ui_leaf` lock + stub pages,
  `ui_input` the HAL event pump, `ui_shell.c` the frame + screen stack + key dispatcher;
  `host/` = SDL2 backends for `hal_display`/`hal_input` plus stubs, `just
  build-desktop|run-desktop|shot-desktop|shot-window|picker`. Panel size is runtime (`--geom WxH`,
  `just geom=WxH shot-desktop`, spec §15); `tools/geom-picker.py` (`just picker`, :8096) is the
  tap-a-size page for a phone or tablet — it restarts the simulator at that geometry. `host/src/bezel.c` holds the simulated
  PHYSICAL keys as one data table around the panel — classic T9: vol/power side keys, circular
  d-pad + OK, L/R shoulders, CALL/END, and the 3x4 keypad (1-9, *, 0, #). A cap click fires the
  same named HAL key the keyboard does; UP/DOWN move the list selection, CENTER opens the main
  menu from HOME, END goes home (×2 = lock), SHOULDER-L opens the options sidebar, SHOULDER-R
  goes back. `host/src/frame_server.c` serves the same window over
  HTTP/WebSocket (canvas + JPEG) so it can be driven from a phone on the tailnet —
  `just run-desktop` prints the URL; `--no-server` disables it. `vendor/lvgl` = LVGL pinned v9.4.0 via `just vendor`.
  `hal/` holds the contract (display, input, audio, modem, storage, net) with no S3 backends yet.
  `just build-s3` and the desktop build never touch each other.
- `firmware-architecture.md` — distilled plan for the on-device OS shell: ESP-IDF/FreeRTOS + LVGL,
  event bus, Core 0/Core 1 split, HAL, desktop SDL simulator loop, AI-coding guardrails.
- `ui-architecture.md` — **THE SPEC**: navigation model & key semantics (hardened from Eli's
  voice notes 25/11–26/11; diagram in `ui-map.png`): stack nav (END=home, END×2=lock), main-menu
  category carousel (Messaging/Calls/Maps/Settings via L/R) + grid alternative, unified WhatsApp
  surface, contextual per-page keymaps (left=options / right=back defaults), generic `menu_t`
  "fill in the blanks" page object, the theme/design-token layer (§5), **§8 status (what is
  built), §10 the exact checklist for adding a page, §11 the S3 port budget**. Read it before
  building any new page.
- `harness-setup.md` — local test harness: CLI-first ESP-IDF workflow, three-tier virtualization
  (native SDL2 / QEMU / Wokwi + tethered board), test-surface design (UART JSON-RPC, panic hooks,
  framebuffer snapshots).
- **Hardware ordered (24/11):** Waveshare ESP32-S3-Touch-LCD-3.5 (N16R8, 16 MB flash / 8 MB PSRAM)
  board from AliExpress — **order 1124283904159970** (Waveshare ESP32-S3 3.5in Touch, "3.5 With
  Camera-Case"), confirmed 26/11; delivery window **9–18/12**. Screen = HP35001-DCT 3.5" IPS (ST7796U +
  GT911), datasheet in `datasheets/`.

Status (26/11): concept phase + **Phase 1 harness** (ESP-IDF v5.5.6, QEMU boots the scaffold).
  **Tier-1 simulator has a complete, navigable UI** (overnight 10-leg relay `relay/menu-rebuild/`,
  commits `53b22a99..ce3a9d21`): a dedicated HOME screen (flat wallpaper + DATE/UNREAD/BATTERY
  widgets), the Messaging chat list from real WAHA data with a 5-way category strip, main menu as
  a 4-category carousel + grid alternative, Settings tree with working toggles, Speed Dials +
  Held Presses config, Calls (Call History, Contacts), SHOULDER-L options sidebar, touch
  taps/swipes, lock screen, and a contextual softkey legend for the physical keys. Navigation:
  `LOCK ↦ HOME ↦ menu ↦ category ↦ page` — END returns to HOME from anywhere, END×2 on HOME locks.
  Every list page is a `menu_t` on one renderer; all shared metrics and
  font sizes are tokens in `ui/ui_theme.h`. Spec §12 (HOME split + softkey legend) is BUILT.
  **Stage 0 (spec §15, issue #1) is BUILT:** panel geometry is a runtime value —
  `ui/ui_geometry.c` is the single source of truth (identity at 320×480, so the
  default goldens are unchanged); `ui_theme.h`'s geometry tokens expand to it; the
  bezel re-lays itself from it; LVGL buffers, the window and the browser viewport
  follow it. Pick a size with `just geom=480x800 shot-desktop chat` (presets:
  320×480, 240×320, 240×400, 320×240, 480×800, 480×640, 1280×720 + free-form) or
  `./build-host/dumbphone-desktop --geom WxH`; non-default shots write `<name>-WxH.png`.
  Run it: `just run-desktop` (window + browser viewport on 127.0.0.1:8099 — reach it from the
  tablet with `DUMBPHONE_SERVER_BIND=0.0.0.0 just run-desktop`) · `just shot-desktop <screen>`
  (golden frame, clock frozen) · `just build-s3`.
  Next: the out-of-scope follow-ups recorded in `relay/menu-rebuild/status.md` — PIN entry, real
  WAHA-backed content (spec §9), settings persistence, and wiring `ui/` + LVGL into the S3 build
  behind the ST7796U/GT911 drivers — plus the S3 budget items in `ui-architecture.md` §11.

## Public repo (26/11)

`https://github.com/Enomalyy/dumbphone` — public, `main`. **Never push the home-dir monorepo**
(the git root is `/home/eliyahu`: 5 GB, private WhatsApp memory, school files, agent config).
Publish from a clean staging tree instead:

```sh
P=~/dumbphone-publish            # the existing staging clone (own fresh history, f13c1ae)
rsync -a --delete \
  --exclude build/ --exclude build-host/ --exclude vendor/ \
  --exclude sdkconfig --exclude sdkconfig.old --exclude host/data/ \
  ~/dumbphone/firmware/ "$P/firmware/"
cp ~/dumbphone/*.md "$P/docs/" && cp ~/dumbphone/screenshots/*.png "$P/screenshots/"
cd "$P" && git add -A && git commit -m "…" && git push
```

Excluded on purpose: `transcripts/` (personal voice notes), `relay/` (internal process logs),
`datasheets/` (third-party manufacturer PDFs), `host/data/chats.json` (real WAHA chats).
Auth: `gh` lives at `~/.local/bin/gh` (no sudo). **Known limitation:** the device-flow token we
obtained lacks `read:org`, so `gh auth login --with-token` refuses it — use
`GH_TOKEN=$(cat ~/.config/gh-token) git push` with the raw token instead.
Screenshots contain real chat names — Eli was told; sanitise if he asks.

## Work tracking — read this before starting anything

The plan lives in **three places, and they must agree**:

1. **`ROADMAP.md`** — the staged plan in one table (Stage 0, A, B, C, D), each row pointing at its
   spec section and its GitHub issue.
2. **`ui-architecture.md`** — the design authority. §13–§15 hold the queued work in detail; §10 is the
   checklist for adding a page. If code and spec disagree, the spec is right until Eli says otherwise.
3. **GitHub issues** — the live status: `https://github.com/Enomalyy/dumbphone/issues`. One issue per
   stage, labelled `stage-0` … `stage-d`.

Not one of the three: **`reorg-plan.md`** is an outside structural proposal (steps R1–R5 placed
around the stages: golden-check CI, a port seam inside Stage 0, a page registry, a model layer, the
UI in the S3 build). It is not the plan until a step has an issue. Read it before reworking the
screen stack, page routing or the build, so the same ground isn't redone two ways.

**Conventions:**
- Start from an issue, not from a chat message. Read the issue and its spec section, then work.
- New work that isn't in the roadmap gets an issue first (or it will be forgotten the moment the
  session ends). Ask Bowie to file it if you can't.
- Commit messages say what changed and why. **Never attribute a decision to Eliyahu that he did not
  make** — one session invented an "Eli's final call" to justify a shortcut, and it cost a rebuild.
  If the requirement can't be met, stop and report that.
- The publish hook mirrors every `dumbphone/` commit to the public repo, so commits are public
  immediately. Write them accordingly.
