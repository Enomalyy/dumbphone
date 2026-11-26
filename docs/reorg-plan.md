# Structure proposal — on top of `fb9b676`, 26 November 2026

An outside structural review of the public repo, plus a proposed order for the structural work,
**fitted around the existing [`ROADMAP.md`](ROADMAP.md) stages rather than competing with them**.
To avoid name clashes with "Stage 0, A–D", the steps here are called **R1–R5**.

**Status:** a proposal. Nothing here is in the roadmap or the issues until Eli decides it is. The
small fixes in §2 come with a PR. The steps in §3 are suggestions with no work started.

---

## 0. For agents picking this up

- **The roadmap and the issues come first** (see `AGENTS.md` → Work tracking). Don't start an
  R-step unless it has been turned into an issue or folded into a stage.
- **This public repo is a publish mirror.** The private tree is rsync'd over it (`--delete`), so a
  change merged here only survives if it is also applied in the private tree. The PR that carries
  §2 is meant to be ported, not just merged.
- **Two links in the mirror are not bugs.** `firmware/README.md` links like `../ui-architecture.md`
  are correct in the private layout, where the docs sit at the root. References to
  `ESP32 phone.txt`, `relay/`, `datasheets/` and `transcripts/` point at things deliberately left
  out of the mirror. Don't "fix" either.
- **Verify the §2 PR before building on it.** It was written on a machine without LVGL or SDL, and
  was only compile-checked against a stand-in LVGL header. Before relying on it:
  1. Run `just build-desktop`. It must finish with 0 warnings.
  2. Re-shoot the frames from `screenshots/README.md` and confirm they're AE=0. No frame should
     change: none of them goes through the two navigation paths that were fixed.
  3. Delete the generated `firmware/sdkconfig`, then run `just build-s3` and `just run-qemu`.
     It must still boot with PSRAM on.

---

## 1. Already resolved upstream (since the first review)

Credit where due. These were open in the first pass and are done in `fb9b676`:

- The `ui_shell.c` split into `ui_input` / `ui_leaf` / `ui_messaging` / `ui_home` / `ui_sidebar`
  / `ui_text`. `ui_sidebar` is compiled, and `ui_row_style()` really is shared.
- Region-aware flush: `hal_display_blit_area()` is in, and the backend owns the shadow
  framebuffer (spec §11).
- Hebrew, RTL and emoji through `ui_text_set()` and a font fallback chain.
- `MENU_MAX_ITEMS` / `MENU_MAX_CATS` clamp with a message instead of corrupting memory.
- An honest six-step page checklist (spec §10), golden-frame recipes (`screenshots/README.md`),
  `AGENTS.md` work-tracking rules, and `ROADMAP.md` with issues #1–#5.

---

## 2. Small fixes — carried by the accompanying PR

All of these are verified against `fb9b676`:

| Problem on `fb9b676` | Fix in the PR |
|---|---|
| **Navigation stack desync.** `screen_stack[4]`: `push_screen()` silently skips the push when full but still *shows* the screen. Repro: HOME → OK → (SETTINGS category) → OK on "Speed Dials" → OK → SHOULDER-L → "Settings" → OK. Settings is visible, but UP/DOWN/OK drive the hidden Speed Dials page. | The stack is 8 deep. A page already on the stack is popped back to, never stacked twice. A full stack replaces its top and logs it. Invariant: the visible screen is always the top of the stack. |
| **Index coupling.** `open_page(PAGE_SETTINGS)` selects `ui_list_sel(g_menu)`, which assumes the menu's SETTINGS items and the settings rows share an order. From the options sidebar it uses whatever the menu had selected, in any category. | `cat_settings_rows[]` in `ui_mainmenu.c` names the settings row for each item (`_Static_assert` keeps it the same length). Only `activate()` on the main menu applies it, through `ui_mainmenu_selected_setting()`. The category item counts use `ARRAY_LEN`. |
| `sdkconfig.defaults` sets **8 MB flash and no PSRAM** on an N16R8 board. Every PSRAM plan in §11 assumes PSRAM is on. | 16 MB flash, `CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_MODE_OCT=y`. |
| The justfile hardcodes `$HOME/dumbphone/firmware/env.sh` and `nproc`. | `justfile_directory()` and `cmake --build --parallel`. `env.sh` honours `IDF_PATH` / `IDF_PYTHON_ENV_PATH` and keeps the current defaults. |
| The viewport binds to a hardcoded tailnet IP by default. That address only exists on one machine, and it's public in a public repo. | The default is `127.0.0.1`. Set `DUMBPHONE_SERVER_BIND` (or pass `--server-bind`) to expose it, e.g. to the tailnet. |
| A fresh clone shows "No chats", because the only fixture is personal data and git-ignored. | `firmware/fixtures/chats.sample.json` (invented names) is used when `host/data/chats.json` is absent. |
| Windows checkouts get CRLF in `env.sh` and the justfile, which bash under WSL rejects. | `.gitattributes`: LF for `*.sh` and `justfile`. |

**Behaviour change to know:** with the "pop back to it" rule, the options sidebar's "Settings"
entry (and "Call History" / "Contacts" on each other's pages) now *returns* to that page if it is
already open, instead of stacking a second copy.

---

## 3. Structural steps — proposed order around the roadmap

Each step is sized to land as its own PR. Every step keeps the golden frames AE=0, unless the PR
names the frames that change.

### R1 — Safety net. *Before Stage 0.*
Stage 0 touches every layout constant, so it is the riskiest change on the roadmap to make by
eye.
- Turn the `screenshots/README.md` table into a `scenes.txt` (frame name → harness flags).
  `just golden-check` renders every scene and compares it against the committed PNGs.
  `just golden-update` rewrites them. Include the window frames, which review 26b §7.1 says are
  the only check on the compose path.
- Wire `just test-host`. The first test is `ui_text`'s direction rule (review 26b §6 calls it the
  cheapest first test).
- CI: `build-desktop` under `SDL_VIDEODRIVER=dummy`, then `golden-check`, then `idf.py build` in
  the `espressif/idf:v5.5` container.

### R2 — Port seam. *Fold into Stage 0.*
Stage 0 (spec §15) rewrites exactly the code that should leave `ui/`. `ui_shell.c` still owns
display creation, draw buffers, the tick (`clock_gettime`), the clock (`localtime_r`), the
`pthread` UI-thread guard, and font loading by path (`FONT_DIR`, which only the host CMake
defines). Since Stage 0 makes geometry runtime anyway, have the target supply it through one
small `ui_port.h` (geometry, tick, local time, font load, UI-thread assert). The host implements
it in `host/src/`, the S3 later in `main/`. Then `ui/` compiles for the chip unchanged.

### R3 — Page registry. *After Stage 0, before Stage A.*
Stages A (Settings → Display) and D (the conversation page) add pages. Each new page currently
costs the six touch points in spec §10, five of them `switch`/`if` chains in `ui_shell.c`, plus
two id enums (`page_id_t`, `ui_screen_id_t`) and a hand-counted `LIST_POOL`. The fix is one
`ui_page_t` descriptor per page (id, name, build, keymap, activate, cycle, options, root) in one
table. Show/keymap/activate/move/swipe/options and the harness `--screen` names become loops over
it, and §10 becomes "write the module, add one table row".

### R4 — Model layer. *Before or as the first part of Stage D.*
Stage D needs a real message model. Do it as a `model/` layer the UI reads and the WAHA relay
will later write: chats, messages, contacts, call log, settings. Today the mock data and settings
state live inside the page modules (`ui_calls.c`, `ui_speeddial.c`, `ui_settings.c`, and the slice
builder in `ui_messaging.c`). `chat_model.h` is already the right pattern, generalised. Settings
persist through `hal_storage`.

### R5 — UI in the S3 build. *When the board arrives (9–18/12).*
`main/CMakeLists.txt` still compiles only the scaffold. Make `hal`, `ui` (and `model`) ESP-IDF
components whose `CMakeLists.txt` also works as a plain CMake library for the host. Then
`build-s3` proves the UI compiles for the chip, and the host source list stops being a
hand-maintained second copy (review 26b §7.3). Also: pin LVGL once (a submodule or IDF component)
for both builds, and move the draw buffer to PSRAM (spec §11).

**Not proposed:** moving docs or screenshots around in the mirror. The publish script decides
that layout, so any move has to start in the private tree.

---

## 4. Decisions only Eli can make

1. **License.** The repo has none, so nobody can legally reuse or contribute to it.
   MIT/Apache-2.0 (most reusable) or GPL-3.0 (forks stay open).
2. **Outside contributions to the mirror.** Port PRs by hand (what this one assumes), accept
   patches as issues instead, or make GitHub the source of truth for `firmware/`.
3. **Which of R1–R5 become issues**, and where they sit relative to Stages 0–D.
4. **Supported dev platforms.** Linux only (WSL2 on Windows), or native Windows/macOS too.
