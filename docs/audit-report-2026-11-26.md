# Audit — the dumbphone user interface, 26 November 2026

**Who this is for:** Eliyahu, who asked for an honest outside check of the UI an overnight
10-leg relay built (commits `53b22a99..ce3a9d21`), judged against his quality bar of 26/11.

**What I did:** read every project document and every line of the UI code, ran the simulator
and drove it (keys, taps, swipes, long presses) to see what it actually does, fixed what was
small and safe, and wrote down what I chose not to touch.

**Bottom line first:** the project is in better shape than "an AI wrote it overnight" would
suggest. There is one page template, one renderer, one key/action vocabulary, and one appearance
layer — the architecture is coherent, and a future agent can add a page by following a checklist
(now written down, `ui-architecture.md` §10). I found and fixed nine real defects, two of them
visible on screen and one that silently undermines the project's testing. The complexity is not
out of control. The genuine risks are all on the **device side** — things the desktop simulator
hides — and they are now written down as a port budget (§11). Nothing I saw would make me tell
you to restart or re-architect.

Everything I fixed is verified: both builds are clean and the screenshots were regenerated from
the same commands, so what you see is what the code does.

---

## 1. What I checked

- **The record:** `AGENTS.md`, `ui-architecture.md` (the spec), `firmware-architecture.md`,
  `ESP32 phone.txt` (the mind map), `harness-setup.md`, `concept-art/`, and the relay packet
  `relay/menu-rebuild/` (charter, status, log).
- **The code:** all of `firmware/ui/` (9 modules, ~3,500 lines), `firmware/hal/`,
  `firmware/host/` (the desktop-only side), the `justfile` and the harness in `host/src/main.c`.
- **The behaviour:** built the simulator, then ran ~40 scripted sessions — every screen, both
  menu styles, the options sidebar, category cycling, taps, drags, long presses, the lock screen
  — and compared frames pixel by pixel, including a 61-second repeat to test reproducibility.
- **The bar itself:** the five criteria from your 26/11 note (one place for design knobs;
  readable and appendable; themes swappable; S3 performance budget; no complexity creep) plus
  "the docs must match the code".

*A quick glossary, since these words recur:* **menu_t** = the one "menu template" every list
page fills in; **renderer** = the one function that draws any menu_t; **keymap** = each page's
table of "what every key does here"; **theme/tokens** = the one file where colours, sizes and
spacing live; **golden frame** = a saved screenshot used to notice when the look changes.

---

## 2. Fixed now

Ten commits (nine fixes — the screenshot refresh needed two), each small and verified.
In plain language:

### 2.1 The three buttons at the bottom of the screen were fake — `36c3e540`
The bar says **CALLS · OPEN · OPTIONS**, but tapping any of the three only ever did "OPEN".
So tapping OPTIONS did not open the options panel, and tapping CALLS did not reach the Calls
page — despite the spec promising that everything on screen is tappable and that every tap has
a key equivalent. Each button now presses the key it is named after (CALLS = CALL key,
OPEN = OK key, OPTIONS = left shoulder key) on whatever page you are on. Verified by tapping
all three.

### 2.2 The options panel reacted to a touch *before* you finished it — `37d4867d`
Its rows fired on touch-down instead of touch-up. The code even had a guard "ignore this if the
finger is dragging", but the guard could never work, because the option had already run. So
dragging across the panel could accidentally run whatever row you touched. Now it acts when you
lift your finger, exactly like the list rows.

### 2.3 Design knobs were scattered — `3363ad91`
This is your biggest requirement: "make all menus smaller / all fonts bigger = one edit".
Before: the page size and row spacing were written separately in two files, the font sizes were
buried in the middle of the shell's start-up code, and the column positions were bare numbers
(`212`, `PANEL_W - 8 - 98`, …). Now `firmware/ui/ui_theme.h` is the single control panel: panel size,
status-bar and button-bar heights, row height, column positions, category-strip offsets, grid and
sidebar metrics, and the pixel size of all seven fonts. The two files that draw the UI only
*reference* those names.

I proved it is safe: 13 screens/state combinations are **pixel-identical** before and after this
change. (So when you change a token, whatever happens is exactly the change you asked for.)

### 2.4 Numbers in lists were being cut off — `7a10c35d`
Visible defect: every contact's phone number lost its last digit (`054987654` instead of
`0549876543`), chat and call times were sliced by the row's border, and the word "CAROUSEL" in
Settings touched the frame. Cause: the right-hand column ended *under* the row border. The column
now stops 12 px inside the row, so the last character is always whole. I also found that an
over-long value used to wrap onto a second line inside the row instead of being shortened; it now
shortens cleanly.

### 2.5 The screenshots changed every minute — `454ba543`
This one is invisible but important. The status-bar clock reads the real time, and any test run
longer than a second therefore baked the wall clock into the saved frame — run the same command a
minute later and the pixels differ. That quietly breaks the project's only regression tool: "this
screen is pixel-identical to yesterday" was only ever true for fast runs. Screenshot mode now
freezes the clock at 20:36; a running phone still shows real time. Verified: the same slow runs
(a swipe, a 700 ms hold) produced identical files 61 seconds apart.

### 2.6 Adding an eighth page would have crashed the phone — `be103ce8`
The renderer holds a fixed number of pages (7). Nothing said so, except a stale comment on one
page saying it was "the 4th and last". The next page added would have been created as "nothing"
and crashed the first time it was opened. Now the limit is documented as the page budget, it
prints a clear message if it runs out, and start-up checks every page instead of crashing.

### 2.7 The documentation described a program that no longer existed — `d0743044`
- `ui-architecture.md` (the spec every build leg reads) and `harness-setup.md` **were never
  committed** — a fresh clone had no spec at all. They are in the repository now.
- The spec's status section listed six things as "to build" that are all built; it is replaced
  with what exists and what is deliberately open.
- The spec never mentioned the options panel, the action-bar buttons, the settings sub-pages or
  the lock screen. Added.
- New **§10: the exact five-step checklist for adding a page** — the honest answer to "is a page
  really just data?" (it is data, plus five touch points, now listed).
- New **§11: the port budget** — the two costs the simulator hides (see §3.1 and §3.2).
- `AGENTS.md` and `firmware/README.md` were describing the very first skeleton (two screens,
  "shoulder-R opens the menu", "d-pad L/R not built yet"). Corrected, including the module list
  and the fact that `host/data/chats.json` is not in git.

### 2.8 The step-by-step guide is now reproducible — `0ed3e41c`, `144adf50`
The screenshot gallery showed the clipped digits and various wall-clock times. All interaction
frames were re-shot from the documented commands, so each picture matches its name and the
current code (I checked each one visually: the category frames show the right category, the
options frames show the right panel, the lock frame is locked, and so on). Reproducible now
because of 2.5.

### 2.9 One warning in the build log — `61a1c384`
"Zero warnings" was not quite true: the only warning came from LVGL itself (the downloaded
graphics library, pinned to a version, not our code). It is now silenced for that library only,
so a build from scratch prints **no warning and no error at all** — which means a real warning in
your own code can never hide in noise.

---

## 3. Recommended — worth doing, but bigger than an audit should do quietly

Ordered by how much they will hurt later.

**3.1 (High, for the device only) The screen is pushed to the panel as a whole picture, every time.**
LVGL itself redraws only what changed — but the shim between it and the hardware then copies the
whole screen and asks the display to push all 307 KB of it. On a desktop nobody notices; on the
ESP32 that is a full-screen write for a single blinking cursor, and the UI thread waits for it —
which is exactly the "region-based invalidation only, never block input" rule in your bar.
Fixing it means changing the tiny hardware interface (`hal/hal_display.h`) to accept a rectangle,
plus a smaller draw buffer in PSRAM. This is a real piece of design work, not a patch.

**3.2 (Medium) Lists are thrown away and rebuilt every time the category changes.**
Changing chat category (or flipping a setting) deletes roughly 200 small objects and creates them
again. On the desktop it is instant; on the chip it is steady memory churn, and churn is what
eventually fragments a small heap. The fix is a fixed pool of rows that get re-labelled and
hidden instead of destroyed. It touches one function (`rebuild_rows()` in `ui_menu.c`), so it is
a contained job.

**3.3 (Medium, and personal to you) Hebrew and emoji come out as empty boxes.**
The three fonts are Latin-only (Silkscreen, VT323, Font Awesome), and LVGL has no
right-to-left handling switched on. I demonstrated it: a chat named `אבא` draws as three empty
squares. Today you barely see it because long Hebrew names are truncated before the Hebrew part,
but most of your real chats are Hebrew, so this will bite as soon as real names are shown in
full. It also causes 338 "cache not allocated" messages in every run's log (harmless, but it is
what they are). Needs a Hebrew pixel font, right-to-left text support, and a policy for emoji.
A product decision more than a code fix.

**3.4 (Medium) One file has grown into a small city.**
`ui/ui_shell.c` is 1,658 lines and holds the frame, the input state machine, the chat-list data
adapter, the main-menu data, the build calls for every page, the options panel, the lock screen
and the leaf pages. It is readable — well commented, no clever tricks — but it is the file a
future agent is most likely to break, because five unrelated concerns share it. A split into
roughly four files (frame & chrome / input & navigation / home+menu data / options panel) would
make each one obvious. This is exactly the kind of move that is easy to get wrong without
testing every screen, so I left it.

**3.5 (Low–Medium) The options panel copies the list-row styling instead of sharing it.**
One function in the shell paints sidebar rows and another in the renderer paints list rows; they
look identical and must now be kept identical by hand. A single shared "paint a row" helper
removes a duplicate path — small, but it is the sort of duplicate that drifts.

**3.6 (Low) A theme can't be swapped while the program runs — yet.**
The structure is right: navigation code never mentions a colour. But colours are applied when a
page is built, and there is no "apply theme" pass, so a second theme needs the pages rebuilt.
The spec (§5) now says this honestly instead of implying it works.

**3.7 (Low) The simulated phone body repeats the colour values.**
The keys around the simulated screen are painted with the same colours as the screen, written out
again in `host/src/bezel.c`. It is desktop-only decoration, so it is low risk, but it is a second
place to edit when the palette changes.

**3.8 (Low) A fresh clone has an empty chat list.**
`host/data/chats.json` is not in git — the whole `host/data/` folder is ignored, because it is the
simulator's scratch storage and it holds your real WhatsApp chat names — so a new clone shows
"No chats". Now
documented in the README. A committed sample file with invented names would make the project
demo-able out of the box.

**3.9 (Low) The module is called `ui_menu.c` but its functions are `ui_list_*`.**
A future agent looking for "menu_build" will not find it. One line in the spec's checklist now
points this out; a rename or an alias would finish the job.

---

## 4. Watch items — fine today, know about them

- **The S3 build does not contain the UI.** `main/CMakeLists.txt` compiles one scaffold file, so
  "`just build-s3` is clean" is true but says nothing about whether this UI compiles for the chip.
  That is a known, recorded follow-up (wiring `ui/` + LVGL behind the screen driver), not an
  oversight — but do not read the clean S3 build as "the UI already runs on the device".
- **Out-of-scope follow-ups confirmed as recorded** in `relay/menu-rebuild/status.md` and
  untouched by me: PIN entry on the lock screen, real WAHA-backed content, settings persistence,
  S3 driver wiring. The relay packet is a historical record and I did not modify it.
- **Maps is five stub pages** ("coming soon") — the navigation works, as the charter allowed.
- **Opening a chat does nothing.** Chat rows select but do not open a conversation; same for call
  history and contacts rows. Expected at this stage, but the list *looks* like it should open.
- **Grid layout moves selection one cell at a time**, left-to-right, not up/down within a column.
  Documented in the relay status as a known sub-gap.
- **The buttons at the bottom never change label.** They always read CALLS/OPEN/OPTIONS; on the
  lock screen they are inert (correct) but still look pressable.
- **The category icons at the top of a menu are a little tight** — the active icon's edge touches
  its frame. Cosmetic; a one-token change to the tray cell size, but it would re-shoot every
  frame, so I left it.
- **Memory numbers to keep in mind:** the desktop draw buffer is a 307 KB static array, plus
  ~15 KB of chat tables. Fine on the desktop; on the chip that buffer must live in PSRAM.
- **Text that is not in the font is silently dropped** (no warning to the user, just boxes).
  Related to 3.3.

---

## 5. Evidence

- `just build-desktop` from scratch (`rm -rf build-host`): **0 warnings, 0 errors**.
- `just build-s3`: clean (`Project build complete`, no warnings).
- Design-token refactor: 13 golden states **pixel-identical** to before.
- Reproducibility: identical files from two identical slow runs 61 s apart.
- Behaviour: taps on all three action-bar buttons, a tap on an options row, both menu layouts, all
  five chat categories, the lock screen via END×2, long-press shoulder-L, drags — verified by
  comparing frames against the expected screen.
- The final `screenshots/chat.png` is pixel-identical to a freshly captured frame.

---

## 6. Bottom line

**Is it coherently maintainable long-term?** Yes — with the caveat that its maintainer is a
language model, so "maintainable" means "a fresh agent can find the rules". That now holds:
one page structure, one renderer, one keymap vocabulary, one control panel for appearance, and a
spec that states what exists, what is open, how to add a page, and what the device port must pay
for. The relay did not invent a second framework anywhere; every *list* page added last night went
through the same `menu_t` + renderer + keymap path (the lock screen and the "coming soon" leaf
are deliberately not lists, and say so).

**Is the complexity something to worry about?** Not the architecture — the trajectory is flat:
10 modules, ~3,500 lines, and the page modules are 40–140 lines each; the weight sits in one
shared renderer, as it should. What I
would watch is *file size* rather than *design*: `ui_shell.c` is becoming the place where every
new page is also wired up by hand, and the five-step page checklist is the symptom of that. If a
third or fourth UI feature lands there, split it (3.4) before it becomes untouchable.

**If you only do three things next:** fix the full-screen push (3.1) before the hardware arrives,
decide the Hebrew question (3.3) because it is your real data, and split `ui_shell.c` (3.4)
before the next big feature.

*Audit commits: `36c3e540`, `37d4867d`, `3363ad91`, `7a10c35d`, `454ba543`, `be103ce8`,
`d0743044`, `0ed3e41c`, `144adf50`, `61a1c384`.*
