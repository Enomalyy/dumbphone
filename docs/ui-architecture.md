# UI Architecture — Navigation Model & Key Semantics

Hardened from Eli's voice notes (25/11–26/11) + the Step-1 simulator. This is the
reference for future build sessions: what every page IS, and what every key DOES on it.
Companion diagram: `ui-map.png` (same folder). Visual reference for the main-menu layout:
`concept-art/gen-09-menu-lg-vx.png` (top strip + item list below).

## 1. Principles

1. **Stack navigation.** Every screen is a *page* pushed onto a nav stack.
   `END` always pops to **HOME** (from anywhere). A second `END` press on HOME locks the phone.
2. **Contextual keymaps.** Each page declares what SHOULDER-L/R, d-pad LEFT/RIGHT, CENTER,
   CALL, END, UP/DOWN, T9-keys do *on that page*, chosen from a standard action vocabulary.
   **Shoulder buttons and d-pad arrows are DIFFERENT controls:**
   - Defaults: **SHOULDER-L = options**, **SHOULDER-R = back to previous page** — overridable per page.
   - **D-PAD LEFT/RIGHT = navigation** (cycle categories, move between columns); UP/DOWN moves selection.
3. **One menu object ("fill in the blanks").** Every list-like page is an instance of a
   single `menu_t` structure (below). The renderer is generic; pages differ only in data.
4. **Stable frame.** Status bar pinned top, action bar pinned bottom; only content swaps.
5. **Flat pixel aesthetic.** Charcoal + green + cyan. No gradients/shadows — icons are
   whole-pixel primitives (see §7).
6. **Touch-capable, keypad-first.** Every UI element is also tappable — touch works
   everywhere; the keypad is the first design principle, touch is the universal fallback.
   **Swipes work across menus** (5-point capacitive panel + LVGL native gestures):
   scroll lists, cycle category carousels, page transitions. Nothing swipe-only —
   every swipe has a key equivalent.
   The pinned action bar's caps are not decoration: each one fires the key it names
   (CALLS = CALL, OPEN = CENTER, OPTIONS = SHOULDER-L) on the current page.
7. **Options overlay.** SHOULDER-L opens a per-page sidebar of actions that work on that
   page (`ui/ui_options.c` holds the tables). While it is open it owns the pad keys:
   UP/DOWN move, CENTER executes, SHOULDER-L or SHOULDER-R closes it without leaving the
   page, END closes it and goes home. Any navigation closes it — it can never be stranded.

## 2. Page types

| Page | How it opens | Contents / behaviour |
|---|---|---|
| **HOME** | boot / END from anywhere | Background, widgets (later), **speed dials**: T9 keys 1–9 map to configurable contacts/actions. **Held presses** on L/R/UP/DOWN/T9 = custom actions (configurable in options). `CALL` key goes straight to the Calls page. `END` = home; 2nd press = lock. |
| **MAIN MENU** | CENTER (OK) from HOME | Carousel **or** grid (toggle in settings; grid = classic 3×3 app layout). Top category strip cycled with **d-pad LEFT/RIGHT arrows**: **MESSAGING · CALLS · MAPS · SETTINGS**. **Messaging is the central/default selection.** Cycling a category live-swaps the item list below it (LG-VX reference layout: strip on top, items below). Up/Down + Center open items. |
| — Messaging | | **Chats** (first option — straight into the unified WhatsApp chat-list interface from the concept art) · New Message · Others |
| — Calls | | Call History · New Call · Contacts |
| — Maps | | Search · Transit Payments · Routes · Lines · Stations |
| — Settings | | Tree of further settings categories |
| **LIST pages** (chat list etc.) | from MAIN MENU / apps | Top category carousel cycled with **d-pad LEFT/RIGHT**: ALL · FAMILY · UNREAD · FAVORITES · … · **ARCHIVED**. Up/Down moves selection, Center opens. |
| **APP GRID** | CENTER (OK) from HOME, when menu style = grid | The **alternative structure to the main menu** — a classic 3×3 app layout that
   accesses the *same underlying category pages*. Switchable in settings (carousel/grid);
   both are just two renderings of the same `menu_t` data. |
| **SETTINGS** | MAIN MENU category | Category tree; leaves are toggles/values. Includes: menu style (carousel/grid), call preference, speed-dial + held-press config. |
| — Speed Dials / Held Presses | from SETTINGS, or the options sidebar | One canonical list page each: one row per slot, CENTER cycles the slot through its presets (no text keyboard needed). |
| **LOCKED** | END×2 on HOME | Flat readout (lock glyph, live clock, date, hint) — not a list. Any keypad key unlocks to HOME; every other key is inert. PIN entry is still open. |

## 3. App consolidation

- **WhatsApp and Phone are the same app** — one unified comms surface: calls *and* SMS in
  one list. SMS entries render with a **slightly different color tone** than call entries.
- **Call preference** (settings): "Prefer WhatsApp calls" / "Prefer standard calls".
  Default rule: numbers from other countries go out over WhatsApp.
- Consequence for the menu: no separate "Messages" or "Phone" app — the Messaging category
  is the unified WhatsApp surface; Calls history/contacts live under the Calls category.
- **Maps is a first-class menu category** (not just an app): Search, Transit Payments,
  Routes, Lines, Stations — each opens its own page.

## 4. The menu object ("fill in the blanks")

```c
typedef struct {
    const char  *title;
    item_t      items[MAX_ITEMS];   // label, icon_id, action
    uint8_t     sel;                // selection index
    cat_t       cats[N_CATS];       // optional top category strip (d-pad L/R cycles)
    keymap_t    keys;               // THIS page's contextual bindings
} menu_t;

typedef struct {
    action_t shoulder_l, shoulder_r;   // options / back (defaults)
    action_t center, call, end;
    action_t up, down, left, right;    // d-pad: navigation (defaults)
    action_t t9[12];                // per-key: speed dials on HOME, T9 compose elsewhere
} keymap_t;
```

**Standard action vocabulary** (what a `keymap_t` slot may hold):
`ACTIVATE · OPEN_OPTIONS · GO_BACK · CYCLE_CAT_PREV · CYCLE_CAT_NEXT · MOVE_SEL_UP ·
MOVE_SEL_DOWN · NEW_ITEM · GOTO_HOME · CALL_APP · LOCK · DIAL · COMPOSE_T9 · NOOP`

A page = a filled-in `menu_t` (+ optional category strip). The shell renders and routes
keys generically — adding a page is data, not code.

## 5. Theme layer — decoupled from navigation

Themes are a **totally separate structure** from how apps navigate. A theme changes
appearance only; it never changes layout semantics or key bindings.

```c
typedef struct {
    palette_t   palette;    // bg, fg, accent-1 (green), accent-2 (cyan)
    btn_style_t buttons;    // shape/size params per key class (disc radius, cap height, corner style)
    icon_set_t  icons;      // per-icon-id drawing variant
    font_t      fonts;       // pixel font family + sizes
} theme_t;
```

- Navigation code (`menu_t` / `keymap_t`) **never references the theme** — pages declare
  structure, the renderer resolves appearance via the active theme.
- A theme may change button shapes/sizes (within tolerance) and icon theming; the **button
  set and its layout are fixed by hardware** and survive any theme.
- Default theme = the current flat pixel look (charcoal + green + cyan).

**Where the knobs actually live:** `ui/ui_theme.h` is the design-token layer as well as the
palette. Every shared measurement — panel/frame geometry, list-row metrics and columns,
category-strip offsets, grid and sidebar metrics, and the pixel size of all seven font
faces — is a named `UI_*` constant there. The renderers alias those tokens and carry no
numbers of their own, so "make all menus smaller" or "all fonts bigger" is one edit in that
file (`ui/ui_theme.c` holds the palette values).

**Swapping a theme at runtime:** pages apply their colours when they are *built*, so a new
palette needs the built pages' styles re-applied (rebuild the pages). Navigation never
references the theme, so a swap cannot break routing — but today there is no `ui_theme_apply()`
pass; adding one is the work a second theme needs.

### Text, fonts and direction

The pixel faces stay primary: **Silkscreen** for chrome/titles, **VT323** for list text,
**Font Awesome** for icons. Real chat data is Hebrew, mixed Hebrew/English and emoji, so
every face carries a runtime fallback chain (`lv_font_t.fallback`):

```
Silkscreen / VT323 / FA  ->  Droid Sans Hebrew (same px)  ->  Noto Emoji subset (same px)
```

Only code points the primary face reports missing reach a fallback, so Latin never leaves the
pixel fonts. `ui/ui_text.h` is the **one** entry point for data text: `ui_text_set(label, utf8)`
sets the string and its base direction — **RTL when Hebrew/Arabic letters outnumber LTR
letters, LTR otherwise**. Chrome strings keep `lv_label_set_text()`. The rule lives in the UI
layer, not in LVGL, so it is testable without a display. Mixed strings keep their internal
runs: `LV_USE_BIDI` in `host/lv_conf.h` makes LVGL's draw pass order them; Arabic *joining*
(`LV_USE_ARABIC_PERSIAN_CHARS`) stays off because the real WhatsApp corpus has no Arabic and
no Arabic face is bundled. Fonts are loaded once in `ui_shell_init()` (no hot-path allocation).

The fonts' licences, the emoji subsetting recipe and the per-file flash cost are in
`firmware/host/assets/fonts/README.md`.

## 6. Navigation flow (see ui-map.png)

```
LOCKED ─unlock--> HOME <---END (anywhere)--- [every page]
HOME --CENTER/OK--> MAIN MENU
HOME --CALL key---> CALLS page
HOME --END x2----> LOCKED
MAIN MENU --L/R--> cycle category strip (MESSAGING | CALLS | SETTINGS)
MAIN MENU --UP/DOWN + CENTER--> open app/page
LIST pages --L/R--> cycle category strip (ALL|FAMILY|UNREAD|FAV|...|ARCHIVED)
APP/LIST/SETTINGS pages: LEFT=options, RIGHT=back (contextual), END=home
SHOULDER-L (any page) --opens--> OPTIONS sidebar --L/R/CENTER/END--> close | execute
touch: tap row = select/open · drag = scroll · sideways drag = cycle category
       tap action-bar cap = that key (CALLS | OPEN | OPTIONS)
```

## 7. How icons/images are actually drawn

No image files, no GPU. The UI thread walks the object tree each frame and issues LVGL
draw primitives into the RGB565 framebuffer: filled/outlined **rects, circles, arcs,
lines**, plus **TTF glyphs rasterized at startup by tiny_ttf** (Silkscreen/VT323/FA,
with the Hebrew + emoji fallbacks described in §5). Each icon
is a handful of whole-pixel primitives in 1–2 palette colors (phone = rects + arc;
envelope = rect + two lines). Cost is trivial on the S3 (pure math, no fill-rate worries)
and crisp at ~165 PPI because everything snaps to integer pixels. This is also why the
aesthetic stays flat: gradients/shadows would burn real fill rate we don't have.

## 8. Status — what is built (desktop simulator, as of 26/11)

Everything below runs in the Tier-1 simulator today; it is not yet compiled for the S3
(the device build is still the boot scaffold — see §11).

- [x] **Shell frame:** pinned status bar + action bar, content area swaps screens; screen
      stack (END = home, END×2 on HOME = lock).
- [x] **Canonical page framework:** `menu_t` + generic renderer (`ui/ui_menu.c`), per-page
      `keymap_t` (`ui/ui_keymap.c`), design tokens + palette (`ui/ui_theme.{h,c}`).
- [x] **MAIN MENU:** 4-category carousel (MESSAGING · CALLS · MAPS · SETTINGS) with the
      icon tray + live-swapping item list; grid alternative rendering the same data,
      switched from Settings and re-synced whenever the menu is shown.
- [x] **HOME / chat list:** real WAHA chat data from `host/data/chats.json`, category strip
      ALL · FAMILY · UNREAD · FAVORITES · ARCHIVED, two-line rows with time, `[n]` unread
      badge, `You:` prefix, media markers, tappable + scrolling.
- [x] **Calls:** Call History and Contacts list pages (mock data), CALL key from HOME.
- [x] **Settings:** working toggles (menu style, call preference) + Speed Dials and
      Held Presses config sub-pages with in-memory state.
- [x] **HOME speed dials** (T9 1–9) and **held-press actions** (L/R/UP/DOWN/T9).
- [x] **Options sidebar** (SHOULDER-L, per page — §1.7).
- [x] **Touch:** tap rows and action-bar caps, drag-to-scroll, sideways drag cycles
      category strips. Deterministic touch injection in the harness.
- [x] **Hebrew, RTL and emoji:** Droid Sans Hebrew and a monochrome Noto Emoji
      subset are chained behind every face; `ui_text_set()` picks each data
      string's base direction. The chat list's Hebrew row and the 🚗 preview
      render real glyphs (`screenshots/chat-hebrew.png`, regenerated
      `screenshots/swipe-scroll.png`); every screen with no Hebrew stays
      pixel-identical.
- [x] **Golden frames:** `just shot-desktop` freezes the clock, so a frame is reproducible.
- [x] **Shell split into concern modules** (pure reorganization, every frame byte-identical):
      `ui_shell.c` keeps the frame/chrome, screen stack, request plumbing and the key
      dispatcher (the composition root); the rest moved out by concern —
      `ui_home` (chat list), `ui_mainmenu` (category carousel + layout sync),
      `ui_sidebar` (the options overlay), `ui_leaf` (lock + stub),
      `ui_input` (HAL event pump → key/swipe intents). The single "paint a row" helper
      `ui_row_style()` in `ui_menu.c` is used by both the renderer and the sidebar.

**Still open (deliberately out of the UI build-out):**
- Maps pages are stub leaves; opening a conversation / composing is not wired.
- The unified WhatsApp surface is one list of chats — the SMS-vs-call colour split of §3
  needs the transport (there are no SMS entries yet).
- PIN entry on the lock screen; settings/chat persistence (state is in memory, seeded from
  `chats.json`); real call log / contacts instead of mock data.
- Emoji is monochrome and unshaped: flags are two regional-indicator letter glyphs,
  ZWJ sequences stay separate, and the few symbols Noto Emoji lacks (e.g. `✓` `✗`)
  fall to LVGL's tofu box. Arabic has bidi support but no bundled face.
- `ui/` + LVGL are not in the S3 build yet (see §11).

## 9. Roadmap — WAHA as the messaging backbone (AFTER the UI)

- The phone will **function through WAHA directly**: incoming and outgoing messages via
  the WAHA API over Wi-Fi — exactly how the real device will run, as fast/efficient as possible.
- **Chat info is stored locally** (on-device store); WAHA is pushed to / pulled from it
  (sync model), so the UI reads local state and stays responsive offline.
- Sequencing: finish the UI first (pages, keymaps, theme layer) — then wire the transport.
  The desktop simulator's mock chat data (`firmware/host/data/chats.json`) is the stand-in
  for this local store until the WAHA backend lands.

## 10. Adding a page — the exact checklist

A page is data, but it still touches six places. This is the whole list, in order:

1. **Data:** a `menu_t` (title, items with `label`/`icon`/optional `sub`/`meta`/`badge`/
   `target`, optional `cats` strip, and its own `keymap_t`) in a small `ui_<page>.{h,c}`
   module following `ui_settings.c` — the page owns its data, its `menu_t` and its keymap,
   and exposes one build call plus `page()`/`keymap()` accessors (see `ui_home`, `ui_settings`; `ui_mainmenu` is the data-only variant).
   Data strings (`label`/`sub`/`meta`) are rendered through `ui_text_set()`, so Hebrew and
   emoji need no work at the call site. Row limits: `MENU_MAX_ITEMS` (33) and
   `MENU_MAX_CATS` (5) in `ui/ui_menu.h` — a page wider/taller than those is **clamped at
   build time with a printed message**, so raise the constant first.
2. **Target id:** add it to `page_id_t` in `ui/ui_menu.h` if something opens it.
3. **Build:** build it once in `ui_shell_init()`, **add its `.c` to `host/CMakeLists.txt`**
   (the desktop build lists sources explicitly — forgetting this is an "undefined
   reference", not a silent failure), raise `LIST_POOL` in `ui/ui_menu.c` (that constant is
   the app's whole page budget; the shell prints an error if it runs out) and add the new
   page to the `ui_shell_init()` NULL check.
4. **Routing:** a `ui_screen_id_t` in `ui/ui_shell.h`, then a case in `ui_shell_show_screen()`,
   `page_keymap()`, `move_selection()`, `activate()`, and `open_page()` if it is a target —
   all in `ui_shell.c`, which is the only place that knows the screen stack (it hides every
   other page's root in `ui_shell_show_screen()`, so add the new root there too).
5. **Options:** a row table for it in `ui/ui_options.c` (the SHOULDER-L sidebar).
6. **Swipe (only if it has a category strip):** `ui_list_set_gesture_cb()` on the built page
   with a handler that cycles its categories — see `chat_gesture`/`menu_gesture`.

Appearance needs no work: colours come from `ui_palette()`, sizes from the `UI_*` tokens,
so a new page matches the others automatically.

## 11. Porting to the S3 — the budget to keep

The simulator hides costs the device will pay. They are known, not solved:

- **Full-frame flush — SOLVED (26/11).** The HAL contract now offers a region blit,
  `hal_display_blit_area(pixels, x, y, w, h)` (`hal/hal_display.h`; `hal_display_blit`
  is a thin full-panel wrapper over it). `display_flush_cb()` presents only the dirty
  LVGL area and owns nothing else: it validates the area against the panel, hands the
  region over, and calls `lv_display_flush_ready()`. The **shadow framebuffer belongs
  to the backend** (its blit copies the region rows before presenting; frame dumps and
  the browser viewport read it through `hal_display_framebuffer()`), so a backend with
  no shadow can still display. The host uploads one scaled window rect; on the S3 this
  becomes a partial SPI write per flush instead of a fixed 307 KB. The callback reads
  the draw buffer's real `stride` and falls back to one blit per row if a device
  `lv_conf` pads rows for cache alignment — the host's stride is packed (`w*2`).
  Flush cost is measurable: build the host with `-DFLUSH_STATS=ON`
  (`just build-desktop-stats`) for a bytes/flush-count/largest-area summary at exit.
  Measured on boot ↦ chat list ↦ d-pad move ↦ menu: 1,228,800 B full-frame vs
  639,616 B region (47.9% less), frames pixel-identical; and a region-composed window
  is pixel-identical to a full re-compose at scale 1.0 / 1.37 / 1.6 (no seams).
  **The `screenshots/window-*.png` frames are the only check on that path** (bezel,
  scaling, dst rects, held keys): the panel frames are written from the HAL's panel
  buffer and stayed AE=0 through a bug that drew the panel 10% small in the window.
- **Rows are rebuilt, not recycled.** `rebuild_rows()` deletes and recreates every row's
  LVGL objects on each category cycle / value refresh (chat list = ~33 rows × 4 labels).
  That is fine on a desktop, but it is allocation churn on the device; a fixed row pool that
  relabels and hides extras removes it.
- **Fonts cost flash and RAM (new 26/11).** The bundle is 1,389,480 B total:
  Silkscreen 32,220 B, VT323 153,116 B, Font Awesome 420,332 B,
  **Droid Sans Hebrew 30,024 B**, **monochrome Noto Emoji subset 753,788 B**.
  tiny_ttf keeps one instance per `(face, px)` pair — 7 primary faces plus the
  fallback table's **10 instances** (5 distinct px sizes × 2 faces, 13/16/18/22/24),
  each with its own glyph cache: `LV_TINY_TTF_CACHE_GLYPH_CNT` (128) descriptors plus
  a second 128-entry bitmap cache, allocated lazily. That ceiling — 17 instances —
  is the RAM item to watch on the chip; lower the glyph count and/or drop the 13/22 px
  emoji instances there. On the device, ship the emoji
  face only at the sizes that carry data (18/24 px, 2 instances) and/or cut
  blocks from the subset (dropping `U+1F300-1F5FF` + `U+1F900-1F9FF` saves
  ~510 KB and keeps smileys/hearts/hands/flags). The exact `fonttools subset`
  command is in the fonts README; the emoji face is the first flash candidate
  to trim. Expected runtime noise: every glyph a *chain* face lacks makes
  tiny_ttf log `cache not allocated` (it means "no such glyph here", and the next
  face answers) — a few hundred lines per run on the chat list, not an error.
- Also still desktop-only: the draw buffer is a static full frame (307 KB) — it must become a
  partial buffer in PSRAM, and the LVGL tick/indev timers must be fed by ESP-IDF drivers.

## 12. Pending design corrections (Eli's voice note, 26/11 11:52) — NOT YET BUILT

Two corrections to §1/§2/§6. Build these before extending the architecture further.

**12.1 The HOME screen is missing — the chat list is not home.**
The current build treats the WhatsApp/SMS chat list as the phone's home surface. Wrong. The chat
list is *Messaging*, reached from the main menu. There must be a **dedicated home screen**: its own
wallpaper/background, optionally widgets. Unlock lands on HOME; the menu (and therefore Messaging,
Calls, Maps, Settings) branches off from HOME. Navigation baseline becomes:
`LOCK -> HOME -> [menu] -> category -> page`, with END always returning to HOME.

**12.2 The softkey bar must be a legend for the physical keys, and it must be contextual.**
The two shoulder buttons are physical, sit directly under the bottom bar, and are
**context-dependent** — their meaning changes per screen. The bottom bar is a small strip that
*explains what the physical button beneath it does on this screen* ("OPTIONS", "BACK", "SELECT",
"CALL", …) and is itself tappable so a touch user can trigger the same action. Today
`build_softkey_bar()` in `ui/ui_shell.c` hardcodes `CALLS | OPEN | OPTIONS` — that is the bug.
Correct behaviour: derive each cap's label from the page's `keymap_t` via `ui_keymap`, so the bar
can never disagree with what the key actually does. Add a label field to the binding (or a
`ui_keymap_label(action_t)` table) — one source of truth, no parallel strings.
