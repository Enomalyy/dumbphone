# Review — the three 26/11 changes, second pass

**Who this is for:** Eliyahu. A follow-up to `audit-report-2026-11-26.md`: the three changes built
after that audit — the `ui_shell.c` split, the region-aware display flush, and Hebrew/RTL/emoji —
read adversarially, fixed where they were wrong, and checked for the drift a fresh session would
trip over.

**What I did:** read every line the three changes added or moved, ran the simulator and drove it,
compared every golden frame pixel by pixel, measured the composed window against a full re-compose
at three scales, and rebuilt all three targets from scratch.

**Bottom line:** all three changes are the right shape and should be kept — the split has real
module boundaries, the region contract holds once hardened, and the fallback chain is correct.
But two of the three shipped a defect the golden frames cannot see, and both were fixed here:

- **The composed window drew the panel 10% small.** The region blit computed its destination rect
  from an already-offset origin, so the 320×480 panel was drawn at 276×454 in the top-left of the
  recess with a bezel-coloured strip down the right edge and across the bottom. *Eli caught this
  one from a screenshot; the golden set could not, because panel frames come from the HAL panel
  buffer and never go through the compose path.*
- **The chat list wrote one pointer past two arrays on every startup.** `MENU_MAX_CATS` said 4, the
  chat list has 5 categories; it had been silently corrupting the ALL-tray icon and every page
  title on five-category pages since the relay.

Neither came from carelessness in the new code so much as from the test surface: panel-only golden
frames cannot see the window, and nothing exercised the renderer's array bounds.

Ten commits, listed in §8. §12 (HOME screen, contextual softkey legend) was treated as spec
and untouched.

---

## 1. Verdicts

| Change | Verdict | Why |
|---|---|---|
| 1. `ui_shell.c` split into concern modules | **keep** | Boundaries are real: each file owns one thing and says so in its header. The shared row helper is genuinely shared. One array-bound bug found and fixed. |
| 2. Region-aware display flush | **keep, fixed** | The HAL contract is right and now self-checking. Two real defects on the host path (panel drawn small, bezel never composed at startup) — both in the backend, both fixed. Seam-free at fractional scales (measured). |
| 3. Hebrew / RTL / emoji | **keep, fixed** | Chain is complete and cycle-free, every data label is routed, the emoji recipe is reproducible. Fixed one layout side effect of the RTL base direction. Flash/RAM budget now states the cache ceiling honestly. |

---

## 2. The `ui_shell.c` split — keep

`ui_shell.c` went 1,658 → ~1,000 lines. The question was whether this is a thinner facade over the
same god-object. It is not: each new file has a one-paragraph contract at the top ("owns / must not
touch") and the code matches it.

- `ui_home` — the chat source resolved into per-category item slices, the page's `menu_t` + keymap,
  category cycling. No navigation state.
- `ui_mainmenu` — the four-category carousel data, cycling, and the settings-driven layout sync.
- `ui_sidebar` — panel widget, fixed row pool, selection, styling. It does not execute options.
- `ui_leaf` — the two non-list leaves (lock screen, stub) and their keymaps.
- `ui_input` — the HAL drain loop, touch/swipe state machine, press durations, the LVGL pointer
  device. It reports intents; it does not decide what they mean.
- `ui_shell.c` — what is left is genuinely the composition root: frame chrome, screen stack, key
  dispatch, the cross-thread request slot, font loading, flush glue.

**The shared row helper is actually shared.** `ui_row_style()` in `ui_menu.c` is called by the
renderer *and* by `ui_sidebar.c`, which then adds only its text colours. Audit §3.5 is closed.

**Public surface is sane for a small model.** Each page module exposes `build()`/`page()`/`keymap()`
plus its own cycle/sync call; the only navigation surface is `ui_shell.h`. A page is data plus the
six touch points in §10.

### Found and fixed: the category-strip array bound

`ui_list_build()` sizes its tray arrays `MENU_MAX_CATS` (4) and loops over `m->n_cats`. The chat list
has five categories (ALL · FAMILY · UNREAD · FAVORITES · ARCHIVED), so on every startup it wrote
`cells[4]` onto `cell_icon[0]` and `cell_icon[4]` onto `title_lbl`. Static storage, so nothing
crashed; the effect was a wrong pointer for the life of the process:

- the ALL tray icon was recoloured on the *box* instead of the icon, so it stayed cyan whichever
  category was selected;
- the page title of every five-category page was drawn cyan, not the text colour.

Fix: `MENU_MAX_CATS` = 5 with a comment naming which page needs what, and `ui_list_build()` now
clamps `n_cats`/`n_items`/`cat_sel`/`sel` to the arrays and prints a message, so an over-wide page
is a visible warning instead of silent corruption. The affected frames are regenerated; menu pages
are AE=0.

*Left alone:* the same 10-line "make a label" helper now exists four times (`ui_menu.c`,
`ui_shell.c`, `ui_sidebar.c`, `ui_leaf.c`). They differ by one flag each. A shared helper would be
tidier, but it is a rename-level refactor across four files with no behaviour to gain — not worth
the churn on the same night as the split.

---

## 3. Region-aware flush — keep, contract hardened

The HAL gained `hal_display_blit_area(pixels, x, y, w, h)`; the flush callback presents only the
dirty area; `hal_display_blit()` is a thin full-panel wrapper. That design is right and it removes
the audit's §3.1.

### Found and fixed: the panel was drawn 10% small (Eli's catch)

```c
dst.x = px + scaled_x0;                     /* px = recess origin  */
dst.w = scaled_x1 - dst.x;                  /* scaled_x1 is relative */
```
`dst.w` mixed an absolute coordinate with a relative one, so the full frame came out 276×454 in a
320×480 recess — the panel visibly small in the top-left corner of the window, with a rim-coloured
strip on the right and bottom. Now both edges are scaled from the source edges first, then offset:

```c
dst.x = px + sx0;  dst.w = sx1 - sx0;
```

Verified: the recess is pixel-identical to the panel frame up to RGB565→ARGB rounding (max channel
delta 1), and a region-composed window equals a full re-compose at scale 1.0 / 1.37 / 1.6 with
**0 differing pixels** — so the tiling boundaries still line up exactly at fractional scales.

### Found and fixed: the bezel was never composed at startup

With the callback presenting only the recess, `compose` (the window-sized surface) was never
initialised. The cached bezel background is only blitted by the full-compose path, so from boot
until the first physical key press the window showed a black frame around (and, with the dst bug,
through) the panel. `hal_display_snapshot()` — what the browser viewport serves — reads the same
surface. Fix: one full compose at the end of `hal_display_init()`.

### The contract, after hardening

- **Bounds**: the area is checked against `PANEL_W/PANEL_H` before use; a rejected area and a
  rejected blit each warn once instead of dropping a frame silently.
- **Zero/negative areas**: refused by both the callback guard and the HAL.
- **Stride**: the callback reads the active draw buffer's real `stride`. The host's buffers are
  packed (`w*2`, stride align 1), so the fast path always runs here; a device `lv_conf` that pads
  rows for cache alignment now falls back to one blit per row instead of corrupting the display.
- **Shadow framebuffer**: owned by the *backend* (the host blit copies the region into it), not by
  the callback. That removed a duplicate region memcpy per flush and removed the failure mode where
  a backend without a shadow displayed nothing at all.
- **Stats** (`UI_FLUSH_STATS`): everything is inside `#ifdef`; the default build has no counters,
  no branch, no atexit. Opt-in build is clean and reports e.g. `2 flushes, 466040 bytes` for
  chat + sidebar.

### What still assumes a full frame — for the S3

Nothing in the flush path. The two device-side items in §11 remain true and are now stated more
precisely: the draw buffer is still a static full frame (it must become a partial PSRAM buffer or
every invalidated area is bounded by a 307 KB buffer), and the tick/indev timers must be fed by
ESP-IDF drivers. The `screenshots/window-*.png` frames are now documented as the only check on the
compose path.

---

## 4. Hebrew / RTL / emoji — keep

**Chain correctness.** `LV_USE_BIDI` on, Arabic joining deliberately off. `ui_shell_init()` loads
one Hebrew and one emoji face per *text size* (5 sizes: 13/16/18/22/24) and chains every UI face
through them: `primary → hebrew@px → emoji@px`. Checked:

- **all faces reach it** — the seven `ui_font_*()` accessors all return chained faces, and every
  label in the UI resolves its font through them;
- **no cycle, no double-link** — each size exists once in the table, so a shared face never gets two
  different `fallback` pointers;
- **failure modes** — a missing primary TTF falls back to built-in Montserrat and is deliberately
  left *unchained* (rodata is never written); a missing Hebrew face leaves `primary → emoji`; a
  missing emoji face leaves `primary → hebrew`; a missing pair prints one line per size and shows
  boxes. Nothing crashes and nothing loops.

**RTL detection is sane for this corpus.** Strong RTL vs strong LTR letters; ties → LTR; digits,
punctuation and emoji are neutral; Arabic-Indic digits explicitly excluded from voting. Mixed
strings keep their runs for LVGL's bidi pass.

*Watch item:* the preview string is `"You: <body>"`, and `"You:"` contributes three LTR letters to
the vote. A short Hebrew preview (`"You: מה?"`) therefore becomes an LTR paragraph. There is no such
row in `chats.json` today (I checked: zero Hebrew previews), so it is untested, not broken. When the
real WAAPI data lands, decide the prefix's direction from the body alone. Not fixed now because it
needs a direction-aware variant of `ui_text_set()` and no current screen exercises it.

**All data labels are routed.** Grepped every `lv_label_set_text` in `ui/`: the row label, sub-line,
meta column, grid cell label and stub title go through `ui_text_set()`; the remaining direct calls
are chrome only (status bar, clock, softkey caps, lock date, option tables, category titles).

### Found and fixed: RTL labels slid out of their columns

An explicit RTL base direction makes LVGL's AUTO alignment right-align the paragraph. A Hebrew chat
name then sat against the time column instead of next to its icon, and DOTS truncation cut the other
end — while every Latin row stayed left-anchored. Data labels now pin their alignment to LEFT (the
meta column still overrides to RIGHT). Direction still orders the runs.

### Flash budget — honest, and now more so

The bundle is 1,389,480 B and the five files add up exactly. The fallback table does create 10
instances (5 sizes × 2), as stated. What §11 did not say is the *RAM* side: every tiny_ttf instance
carries `LV_TINY_TTF_CACHE_GLYPH_CNT` (128) glyph descriptors plus a second 128-entry bitmap cache,
both filled lazily — 17 instances in total. That is now written down next to the flash numbers,
with the same trim advice (drop the 13/22 px emoji instances on the device).

The emoji recipe is documented well enough to regenerate: variable-font instancing, the exact
`fonttools subset --unicodes` line, hinting/layout tables dropped, the resulting glyph count, and an
honest list of what the pipeline cannot do (ZWJ, flags, keycap combos, `✓`/`✗`).

---

## 5. Drift the three changes left behind — fixed

- **README described a program that no longer exists**: the layout block still said `ui_shell.c`
  holds the chat list / main menu / sidebar / lock screen; the key paragraph still said
  "shoulder-R opens the menu, shoulder-L goes home, digits are reserved" while the table below it
  said otherwise; `hal_display.h` was described as full-frame blit only.
- **Spec §10 (adding a page) was incomplete**: no mention of adding the `.c` to
  `host/CMakeLists.txt` (an "undefined reference", not a silent failure), the `ui_shell_init()`
  NULL check, the show_screen root hide, the swipe callback, or the row/category caps. It also
  named `ui_mainmenu` as the accessor example although that module has no `page()`.
- **Spec §8 said the sidebar owns the shared row paint** — it uses it; `ui_menu.c` owns it.
- **Stale comments/code**: two section headers in `ui_shell.c` for code that moved to
  `ui_sidebar.c`; `ui_theme.h` still credited the sidebar to the shell; `ui_home.c` included
  `ui_theme.h` "for UI_CHAT_MAX" (it is in `chat_model.h`); a missing newline glued a function onto
  its comment in `ui_sidebar.c`; `ui_heldpress.c` budgeted strings against a "52 px meta column"
  that has been 104 px since the token refactor; the fonts README still put the icon table in
  `ui_shell.c`.
- **Eight golden frames were never in git.** The home repo ignores `*.png` globally; `family`,
  `unread`, `favorites`, `archived`, `menu-grid`, `menu-grid-maps`, `settings-grid`, `stub` existed
  only on this disk, so a fresh clone had no baseline for them even though the spec references
  three. Added (with `git add -f`), and the trap is documented.
- **No record of how any frame is made.** New `screenshots/README.md`: the exact command per frame,
  which frames are device-pixel goldens and which are the host backend's own composition, and a
  pixel-diff one-liner (`magick compare` reports a metric I could not reconcile with pixels; PIL
  `getbbox()` is unambiguous).
- **`ui_input` trusted the HAL's key id** to be in range for its two static press tables. Guarded at
  the boundary; no behaviour change today.

---

## 6. Deliberately left alone

- **§12** (dedicated HOME screen, contextual softkey legend) — spec, not defect. `build_softkey_bar()`
  still hardcodes CALLS/OPEN/OPTIONS, and `UI_SCREEN_CHATLIST` is still HOME; both are the §12 work.
- **The 281 `cache not allocated` lines per run.** tiny_ttf's wording for "no such glyph in this
  face, ask the next one" — i.e. the *expected* path for every Hebrew glyph through the pixel fonts.
  Documented in the fonts README and §11 rather than patched: the message is inside vendored LVGL,
  and editing it would be undone by `just vendor`.
- **The four label helpers** (see §2).
- **`ui_text` has no unit test.** `just test-host` is still a stub; the direction rule is
  display-free by design, so this is the cheapest first test in the project — but wiring a test
  framework is a bigger decision than this review.
- **`browser-viewport.png`** is a live browser capture; not reproducible, left as-is.
- **Device RAM for the font caches**: documented, not engineered.

---

## 7. What a local model will still get wrong here

1. **It will trust the golden frames.** They are panel-only. Two of the three changes reviewed here
   shipped a defect invisible to all 35 panel frames. If a change touches `host/src/hal_sdl_display.c`
   or `bezel.c`, the window frames are the test — and they must be re-shot by hand.
2. **It will add a row and lose it silently.** `MENU_MAX_ITEMS` (33) and `MENU_MAX_CATS` (5) now
   clamp with a printed message rather than corrupt memory, but a page with 34 items still *looks*
   fine and is missing one. The same goes for `LIST_POOL` (7) and `MENU_MAX_CATS` in a new page's
   design.
3. **It will forget `host/CMakeLists.txt`.** Sources are listed explicitly; nothing warns you that a
   new `.c` is not compiled until the link fails.
4. **It will set text with `lv_label_set_text()` on data.** The rule is one line in `ui_text.h`, but
   nothing enforces it — a new page that fills `menu_t` fields is safe (the renderer routes them);
   a new page that draws its own labels is not.
5. **It will reason about RTL from the wrong layer.** Base direction is a paragraph decision made in
   the UI layer; LVGL reorders runs only. A model that "fixes Hebrew" by reversing strings will make
   it worse, and `LV_USE_ARABIC_PERSIAN_CHARS` is off on purpose.
6. **It will read the log.** ~280 `[Error] cache not allocated` lines per run are normal; ~1
   `[flush]` line is normal in the stats build; anything else is not.
7. **It will treat `ui_shell.c` as the place to add a page.** It is the composition root now — the
   screen stack, dispatch and request slot. §10 lists the six touch points; the data belongs in a
   `ui_<page>` module.
8. **It will not know the frames' recipes** unless it reads `screenshots/README.md` — and a new
   frame needs `git add -f`.

---

## 8. Verification

- `just build-desktop` from a wiped `build-host`: **0 warnings, 0 errors**.
- `just build-desktop-stats` (the `UI_FLUSH_STATS` path): clean, reports as expected.
- `just build-s3`: `Project build complete`, no warnings.
- Golden frames: **35/35 panel frames AE=0** against the committed baselines after the intended
  rewrites; the 3 window frames reproduce exactly.
- Compose path: the recess matches the panel frame up to RGB565→ARGB rounding (max delta 1); a
  region-composed window equals a full re-compose at scale 1.0 / 1.37 / 1.6 with 0 differing pixels.
- Behaviour: every screen, both menu layouts, all five chat categories, the options sidebar on
  three pages, its execute path, the lock screen, T9 speed dial, taps and swipes — all re-shot from
  their recipes and compared.

**Commits (ten, oldest first):**

`e22c3728` prime the composed window at init · `1939357c` category-strip array bound ·
`e43b9804` RTL data labels keep their column · `4f5f3883` self-checking flush hand-off ·
`7fcee15c` key-id guard in the input pump · `fd518688` comment/include drift ·
`5d154130` docs vs code (README, §8/§10/§11, fonts) · `123e7da5` held-press column comment ·
`e0b82256` panel frames + screenshots README · `037b534a` compose into the whole recess.

*(The last two are the ones Eli's screenshot prompted; the §11 note that window frames are the
only check on the compose path went into `5d154130`.)*

**If you do one thing next:** port the UI to the S3 behind the real drivers, and take
`screenshots/window-*.png` along as the model of what "the screen is 320×480, 1:1, no scaling" has
to mean there too.
