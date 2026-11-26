# dumbphone firmware

Two build paths share this tree, deliberately independent:

| Target | Toolchain | Commands |
|---|---|---|
| **ESP32-S3** (real device image) | ESP-IDF v5.5, target `esp32s3` | `just build-s3`, `just run-qemu`, `just flash-s3` |
| **Linux desktop** (Tier-1 simulator) | plain CMake + SDL2 + LVGL v9.4.0 | `just build-desktop`, `just run-desktop`, `just shot-desktop` |

Both are wired through `just` so the harness never needs an IDE. Build workflow and the
three-tier virtualization plan: `../harness-setup.md`. What the OS becomes: `../firmware-architecture.md`.

## Layout

```
main/main.c          ESP-IDF app entry (scaffold: boot + heartbeat task)
hal/                 the HAL contract — headers only, no backends
  hal_display.h        panel: info / blit(full RGB565 frame) / backlight / framebuffer()
  hal_input.h          touch + named keys (home, back, power, vol+/-) — never raw keycodes
  hal_net.h            WiFi sockets (stub)
  hal_storage.h        filesystem per partition (host: plain directory)
  hal_audio.h          PCM out / capture (stub)
ui/                  the phone shell (LVGL) — target-agnostic, one thread owns it
  ui_shell.c           frame (status/action bars), screen stack, input state machine,
                       the HOME chat list + main menu data, options sidebar, lock screen
  ui_menu.h/.c         menu_t + THE generic list/grid renderer every page uses
  ui_keymap.h/.c       action vocabulary + per-page keymaps (contextual bindings)
  ui_theme.h/.c        palette + ALL design tokens (font sizes, panel/frame/menu metrics)
  ui_options.h/.c      per-page option tables behind the SHOULDER-L sidebar
  ui_settings.h/.c     settings tree (working toggles)
  ui_speeddial.h/.c    HOME speed dials (T9 1-9) + their sub-page
  ui_heldpress.h/.c    long-press key config + its sub-page
  ui_calls.h/.c        Call History + Contacts pages
  ui_icons.h           Font Awesome glyph strings, named once
  chat_model.h         what the chat list renders; the source is per-target
host/                Tier-1 desktop target ONLY (nothing here is for the S3)
  src/hal_sdl_display.c  window = bezel + the 320x480 RGB565 panel scaled into it
  src/hal_sdl_input.c    mouse->touch, key caps & keyboard->named HAL keys
  src/bezel.c            the PHYSICAL keys around the panel: one data table
  src/chat_source_json.c chat rows from data/chats.json (the desktop source)
  data/chats.json        real WAHA chats for simulator testing
  src/frame_server.c     browser viewport: HTTP + WebSocket, JPEG frames, input
  src/hal_stubs.c        net/audio stubs + host-directory storage
  src/main.c             main thread = SDL pump ("Core 0"); UI thread = LVGL ("Core 1")
  lv_conf.h              LVGL config for the simulator (RGB565, tiny_ttf, no theme)
vendor/lvgl          pinned LVGL (v9.4.0), fetched by `just vendor` — not committed
```

## The rules this skeleton enforces

- **HAL only.** UI code calls `hal_*`, never SDL. Porting to the S3 = filling in the HAL backends.
- **One thread owns LVGL.** `ui_shell_run_iteration()` asserts it runs on the UI thread;
  everything else crosses a queue (`hal_input_poll`, `ui_shell_request_*`).
- **Stable frame.** Status bar and soft-key bar are pinned on a persistent root; only the
  content area swaps screens.
- **No allocation in the hot path.** Draw buffer, rows, and the screen stack are static.
- **Pinned versions.** LVGL `v9.4.0` (`just vendor`), ESP-IDF v5.5.

## Desktop simulator

The window is a phone mock-up: the 320x480 portrait RGB565 panel (the only thing the HAL and
`shot-desktop` see) inside a bezel carrying the **physical keys of a classic T9 phone** — VOL+ /
VOL- on the left edge and PWR on the right, then a circular d-pad (four arrows + OK) with shoulder
keys L/R flanking it, CALL (green) and END (red) below, and the 3x4 keypad (1-9, *, 0, #) with
letter clusters at the bottom.

The keys are one data table in `host/src/bezel.c` (`id`, `label`, optional second line,
`hal_key_t`, x, y, w, h, style, shape); rearranging them is a table edit, never a drawing change.
Every cap fires a named HAL key exactly as the keyboard does — hit test -> `hal_input` event ->
`ui_shell` — so "what a press does" lives in one place and the real GPIO keys will reuse it
unchanged. The UI has no idea a mouse exists.

Wired today: UP/DOWN move the list selection, END and shoulder-L go home, shoulder-R opens the
menu. OK, CALL, LEFT/RIGHT, the digits and * / # already deliver their named events and are
reserved for the app layer and the T9 composer.

```bash
just run-desktop                      # window (auto-fits the display: 2x, else 1.5x)
just shot-desktop chat                # PANEL only, 320x480 device pixels (golden diffs)
just shot-window chat digit-5         # composed mock-up, holding the 5 key
# button ids: vol-up vol-down power up down left right ok
#             shoulder-l shoulder-r call end digit-0..digit-9 star hash
```

### Contextual keymaps

Keys are not hard-wired to screens. Each page declares what every control does
(`ui/ui_keymap.h`), from the vocabulary in `../ui-architecture.md` §4:

| Control | Default |
|---|---|
| SHOULDER-L | options (`ACT_OPEN_OPTIONS`) |
| SHOULDER-R | back to the previous page (`ACT_GO_BACK`) |
| d-pad LEFT/RIGHT | navigation — cycle the page's category strip (`ACT_CYCLE_CAT_*`) |
| d-pad UP/DOWN | move selection |
| CENTER (OK) | activate — on HOME this opens the MAIN MENU |
| END | home, from anywhere; a second END on HOME locks the phone |
| T9 1-9 | speed dials on HOME (unbound elsewhere) |
| long press | the configured custom action for that key (Settings ➜ Held Presses) |

Shoulders and d-pad arrows are **different controls**. Slots a page does not use are
`ACT_NOOP`. Adding a page is a keymap plus data (the full checklist is
`../ui-architecture.md` §10), not new routing code. The three caps on the action bar are the
touch equivalents of CALL, OK and SHOULDER-L on the current page.

### Chat list data

The list renders `host/data/chats.json` — real WAHA chats (name, last snippet, time, `mine`,
`unread`, `group`, `family`, `favorite`, and `archived_count`). That directory is git-ignored
(host storage scratch + personal data), so **a fresh clone shows "No chats" until you put a
file there**; the path is baked in by `host/CMakeLists.txt` (`CHAT_DATA_PATH`). Rows are generated: newest chats, then an Archived row
carrying the count, then New Chat. `mine` renders as a `You:` prefix, long names and snippets
ellipsize, and `[image]` / `[voice note]` snippets render as sent — the renderer has no
special cases. `ui/chat_model.h` is the interface; the S3 build will implement it from storage.

### Browser viewport (phone/tablet)

`just run-desktop` also starts a small HTTP/WebSocket server so the simulator can be driven from
another device on the tailnet — no extra dependencies beyond libjpeg:

```
http://100.110.199.62:8099/          # printed at startup; --server-bind / --server-port override
```

The page is a canvas showing the **composed window** (panel + bezel, so the hardware keys are
tappable), `image-rendering: pixelated`, scaled to the viewport. Frames are JPEG at 18 fps — but
only when the screen actually changes, so an idle phone costs almost nothing. Taps arrive as
`{"tap":true,"x":N,"y":N}` in window coordinates and are pushed into SDL's event queue, so they run
the *same* hit test as a mouse click; `{"key":"digit-5"}` presses a named key (a bezel id, or
home/back/menu) and lights the cap up while held.

`--no-server` disables it (the headless `--shot`/`--shot-win` paths never start it).
If the tailnet address is not available, pass `--server-bind 0.0.0.0`.

The keyboard mirrors every physical key: arrows = d-pad, `Enter`/`Space` = OK, `C` = call,
`E` = end, `Q`/`[` = shoulder-L, `W`/`]` = shoulder-R, `0`-`9` = keypad, `Shift+8` = `*`,
`Shift+3` = `#`; legacy `H` home, `B` back, `M` menu, `P` = power (a phone key, no-op for now),
`+`/`-` or wheel volume. `Esc` or the window's close button quits the simulator. Left click = touch or key cap, right click = back. `--scale N` overrides the presentation
scale; the panel buffer is always 320x480 regardless.

Dependencies on this box live in `~/dev-deps` (user-space headers/libs extracted from RPMs);
override with `DEPS_PREFIX=/path just build-desktop`.

## Status

Phase 1 scaffold (S3 boots under QEMU) + a complete navigable UI in the Tier-1 simulator:
HOME chat list with a five-way category strip, 4-category main menu (carousel + grid),
settings tree with working toggles and two config sub-pages, Calls pages, options sidebar,
touch taps/drags, lock screen. Every page is a `menu_t` on one renderer; all shared
measurements and font sizes are design tokens in `ui/ui_theme.h`.

What is built, what is open, and the exact steps for adding a page:
`../ui-architecture.md` §8 and §10. Next real milestone: wire `ui/` + LVGL into the S3 build
behind the ST7796U/GT911 drivers (the port's performance budget is §11).
