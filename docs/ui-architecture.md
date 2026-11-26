# UI Architecture — Navigation Model & Key Semantics

Hardened from Eli's voice notes (25/11–26/11) + the Step-1 simulator. This is the
reference for future build sessions: what every page IS, and what every key DOES on it.
Companion diagram: `ui-map.png` (same folder) — **stale**: it predates the §12 build
and its generator script is no longer in the tree, so it was not refreshed when HOME
gained its widgets and the softkey bar became a contextual legend. Everything else in
it matches the current model. Visual reference for the main-menu layout:
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
   The pinned action bar's caps are a **contextual legend for the physical keys directly
   beneath them** (§12.2): each cap shows what that key does on the current screen and is
   itself tappable, firing the same action. Unbound actions leave no cap at all.
7. **Options overlay.** SHOULDER-L opens a per-page sidebar of actions that work on that
   page (`ui/ui_options.c` holds the tables). While it is open it owns the pad keys:
   UP/DOWN move, CENTER executes, SHOULDER-L or SHOULDER-R closes it without leaving the
   page, END closes it and goes home. Any navigation closes it — it can never be stranded.

## 2. Page types

| Page | How it opens | Contents / behaviour |
|---|---|---|
| **HOME** | boot / unlock / END from anywhere | Flat wallpaper + a widget row (DATE · UNREAD · BATTERY — real data only, no duplicated clock), **speed dials**: T9 keys 1–9 map to configurable contacts/actions. **Held presses** on L/R/UP/DOWN/T9 = custom actions (configurable in options). `CALL` key goes straight to the Calls page. `CENTER` opens the main menu; `END` ×2 = lock. |
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

The pixel faces stay primary: **Silkscreen** for chrome/titles, **VT323** for the large
list text, **Pixelify Sans** (baked 8-bit) for the small preview/conversational text,
**Font Awesome** for icons. Real chat data is Hebrew, mixed Hebrew/English and emoji, so
every face carries a runtime fallback chain (`lv_font_t.fallback`):

```
Silkscreen / VT323 / FA / Pixelify  ->  Droid Sans Hebrew (same px)  ->  Noto Emoji subset (same px)
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
       tap softkey cap = the physical key beneath it (contextual legend, §12.2)
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
- [x] **HOME screen** (`ui/ui_home.c`): flat wallpaper (dim phone-glyph motif on the
      panel background — no photos, no gradients) + a three-card widget row (DATE,
      UNREAD from the chat list's real total, BATTERY via `ui_shell_battery_pct()`).
      Not a `menu_t`: it is a standalone page exposing `root()`/`keymap()`.
- [x] **MESSAGING / chat list** (`ui/ui_messaging.c`, reached only via the main menu):
      real WAHA chat data from `host/data/chats.json`, category strip
      ALL · FAMILY · UNREAD · FAVORITES · ARCHIVED, two-line rows with time, `[n]` unread
      badge, `You:` prefix, media markers, tappable + scrolling.
- [x] **Calls:** Call History and Contacts list pages (mock data), CALL key from HOME.
- [x] **Settings:** working toggles (menu style, call preference) + Speed Dials and
      Held Presses config sub-pages with in-memory state.
- [x] **HOME speed dials** (T9 1–9) and **held-press actions** (L/R/UP/DOWN/T9,
      `ui/ui_heldpress.c`).
- [x] **Contextual softkey legend** (§12.2): the action bar's three caps are derived
      from the current page's `keymap_t` via `ui_keymap_label(action_t)` — left cap under
      SHOULDER-L, centre under OK, right under SHOULDER-R; unbound = absent; tappable;
      follows the options sidebar and lock screen too. Allocation-free per update.
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
      `ui_home` (the real HOME screen), `ui_messaging` (chat list),
      `ui_mainmenu` (category carousel + layout sync),
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
   and exposes one build call plus `page()`/`keymap()` accessors (see `ui_messaging`, `ui_settings`; `ui_mainmenu` is the data-only variant).
   **Non-list pages** (HOME, lock screen) skip this pattern: they are standalone modules
   exposing `root()`/`keymap()` — see `ui_home.c`.
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
- **Fonts cost flash and RAM (new 26/11).** The TTF bundle is 1,389,480 B total:
  Silkscreen 32,220 B, VT323 153,116 B, Font Awesome 420,332 B,
  **Droid Sans Hebrew 30,024 B**, **monochrome Noto Emoji subset 753,788 B**; plus the
  baked Pixelify Sans small body face, `font_pixelify_m.c` (64,661 B source).
  tiny_ttf keeps one instance per `(face, px)` pair — 6 TTF primary faces (the baked
  Pixelify face needs no instance) plus the fallback table's **10 instances**
  (5 distinct px sizes × 2 faces, 13/16/18/22/24),
  each with its own glyph cache: `LV_TINY_TTF_CACHE_GLYPH_CNT` (128) descriptors plus
  a second 128-entry bitmap cache, allocated lazily. That ceiling — 16 instances —
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

## 12. Design corrections (Eli's voice note, 26/11 11:52) — **BUILT (26/11)**

Two corrections to §1/§2/§6. Both are implemented in the Tier-1 simulator; the S3 port is
unaffected (no new HAL surface). Verified end-to-end in the simulator and by golden frames.

**12.1 The HOME screen is missing — the chat list is not home. — BUILT.**
The WhatsApp/SMS chat list is now **Messaging**, reached only from the main menu's Messaging
category. A dedicated HOME screen exists (`ui/ui_home.c`):
- **Wallpaper:** flat and cheap by design — a dim phone-glyph motif (the FA `IPHONE` glyph at
  theme scale, tinted `text_dim`) on the plain panel background. No photos, no gradients
  (§1.5); every measurement is a token in `ui_theme.h` (`UI_HOME_*`).
- **Widgets:** one row of three cards — DATE (from the shell clock tick), UNREAD (the chat
  list's real total via `ui_messaging_unread_total()`), BATTERY (`ui_shell_battery_pct()`, one
  honest value shared by the status bar and the widget). No duplicated clock, no fake data.
- **Keymap:** CENTER opens the main menu; CALL opens Calls; T9 1–9 = speed dials (moved off the
  chat list); SHOULDER-L = options; END ×2 = lock (the shell arms the lock when an END *lands*
  on HOME, so "END ×2" holds from any page).
- **Navigation baseline:** `LOCK ↦ HOME ↦ menu ↦ category ↦ page`. Boot and unlock land on
  HOME; `UI_SCREEN_HOME` is the stack base; END returns to HOME from anywhere. The harness
  gained `--screen home|chat` so both are drivable independently.
- The old chat-list module was renamed `ui_home` ↦ `ui_messaging` (functions, keymap, options
  table), and `ui_options.c` now keeps separate `opts_home`/`opts_messaging` tables.

**12.2 The softkey bar must be a legend for the physical keys, and it must be contextual. — BUILT.**
The hardcoded `CALLS | OPEN | OPTIONS` bar in `build_softkey_bar()` is gone. The action bar now
holds up to three caps — left under SHOULDER-L, centre under OK, right under SHOULDER-R — each
labelled from the current page's `keymap_t` through one table,
`ui_keymap_label(action_t)` in `ui/ui_keymap.c` (`ACTIVATE`↔OPEN, `OPEN_OPTIONS`↔OPTIONS,
`GO_BACK`↔BACK, `GOTO_HOME`↔HOME, `CALL_APP`↔CALLS, `DIAL`↔DIAL, `UNLOCK`↔UNLOCK; anything
else = no cap). An unbound action leaves **no cap** (the object is hidden, so it is absent,
not blank-but-tappable). Caps are tappable and route through the same `on_key()` path as the
physical key. The legend refreshes on every screen change — including the options sidebar open
(legend follows the sidebar's keymap) and its close — and costs no allocations per update
(static label text, fixed cap objects). Geometry tokens: `UI_SK_CAP_*` in `ui_theme.h`.

**Verification (26/11):** `just build-desktop` zero warnings; `just build-s3` clean. Drove the
full loop headlessly — boot ↦ lock (END×2) ↦ unlock ↦ HOME ↦ menu ↦ each of the four
categories ↦ END back to HOME — plus per-page cap checks for HOME, Messaging, a menu page,
Calls, Settings, options sidebar open and the lock screen (all match their keymaps). Golden
frames regenerated with the documented recipes; every diff is confined to the softkey bar
except the intentional re-points (`home.png` new; `options-home`, `lock-from-chat`,
`speeddial-mom`, `call-key` gained the HOME context); browser viewport re-captured live.

## 13. Queued UI improvements (26/11, after §12)

**13.1 Swipe-to-scroll must be uniform.**
Drag-scrolling works where the surface happens to be an LVGL scroll target (the list page, the
sidebar) but it is not a *rule* yet: settings, speed dials, call history, contacts and the grid view
all have pages longer than the content area, and whether a drag scrolls them depends on how that
page happened to be built. Make it a contract:

- Every page whose content can exceed the content area is a scroll target and drag-scrolls with one
  finger, at the same threshold and the same feel on every page.
- A vertical drag scrolls and must never also select a row on release (the `press_moved` rule in
  `ui_menu.c` is the pattern — apply it everywhere).
- A horizontal drag cycles the category strip only on pages that have categories; elsewhere it does
  nothing rather than half-scrolling.
- The scroll position is per-page and survives going back and returning, or is deliberately reset to
  the top when a page is entered fresh — pick one and document it.
- Verify with the touch harness (`--tap`/`--swipe`), one screenshot per page at the scrolled position.

## 14. The big UI wave (26/11, Eli's voice note) — SPECIFIED, NOT BUILT

Eliyahu's list, to be executed as ONE wave (relay legs), then audited as before. Ordering matters:
§14.7 depends on nothing else, but §14.1–§14.6 all land in the renderer/shell, so they must be one
coherent change, not five overlapping edits.

**14.0 The frame-rate reality that governs all of it.**
The final device will run at a LOW frame rate. Responsiveness must come from *input latency and
state change*, not from smooth motion. Animations are garnish; the UI must feel instant when a key
is pressed even if the panel updates at 15 fps. Corollary: never block input waiting for an
animation to finish — the action applies immediately and the animation is cosmetic. Exception:
scrolling *must* be as smooth as the panel allows, because a finger is dragging it and stutter
there reads as broken.

**14.1 The category strip must be tappable.**
The icons across the top (ALL / FAMILY / UNREAD / FAVORITES / ARCHIVED on Messaging, the four
categories on the main menu) are currently display-only. Each must be a tap target that selects
that category/section — the same state change the d-pad LEFT/RIGHT already produces. One code path:
the tap calls the same cycle/select function the key does; no second implementation.

**14.2 A NEW menu style: the moving carousel.**
Alongside the existing `grid` and static `carousel` styles, add a third: the whole strip translates.
Pressing RIGHT slides the strip left so the item previously to the right arrives at the centre and
becomes the highlighted selection; LEFT mirrors it. A few frames of animation (2–4), driven by the
existing renderer, not a new framework. The centred item is the only one drawn highlighted. The
style is selectable through the same menu-style setting as the others.

**14.3 "Menu style" is one cycling control, not a list of entries.**
In Settings/Options there must be a single row labelled *Menu Style* whose activation cycles
Grid → Carousel → Moving carousel → Grid…, showing the current value in the value column. Remove
the separate per-style entries if any exist.

**14.4 Swipe-to-scroll everywhere (supersedes §13.1 where they overlap).**
Vertical drag scrolls every scrollable page; horizontal drag cycles categories where categories
exist; a drag never also selects the row it started on. Add a small amount of motion so the scroll
feels connected to the finger. This is the one place where frame rate matters most: it must feel
smooth at whatever the panel can do.

**14.5 Carousel icons grow when highlighted.**
The selected icon in the carousel is drawn slightly larger than the others (a design token, not a
hard-coded number), so the selection is legible at a glance without reading the label.

**14.6 Menu cells: text placement.**
Item text in the higher-level menu cells sits badly — it is not centred in its square, unlike the
chat rows which look right. Fix the cell layout so label (and icon) are optically centred in the
cell, with one shared rule for every menu type; add it to the design tokens if a margin is
involved.

**14.7 Opening a conversation — the big one.**
Selecting a chat in Messaging currently does nothing beyond selection. It must open the
conversation: message bubbles, sender grouping, timestamps, scrollback, and the reading pane the
concept art describes — see `concept-art/07-gemini-innerchat-concept.jpg` and the chat-list
reference `concept-art/gen-08-chatlist-clean.png` for the visual language (dark charcoal, flat,
green/cyan accents, no gradients). This needs a real message model (`chat_model.h` today only
describes rows), a per-chat message source (the desktop JSON fixture grows a messages array; the
WAHA source comes later per §9), and a new page built on the existing `menu_t`/renderer where
possible — do not fork the renderer for it. RTL/Hebrew and emoji must work in bubbles; long
messages wrap; the page scrolls with §14.4's rules.

**14.8 Held-down acceleration while scrolling.**
Holding d-pad DOWN (or UP) continues to move the selection; after a short dwell (~400–500 ms) it
accelerates (e.g. repeat interval halving to a floor), so a long list can be traversed without
tapping repeatedly. Release stops immediately. Must not fight the long-press action already bound
to keys (§ held-press config) — decide and document which wins, and why.

**Acceptance for the wave (the relay charter inherits this):**
- All of §14.1–§14.8 demonstrably working in the simulator, each verified by screenshot per state.
- Golden frames regenerated; frames not intended to change are AE=0.
- `just build-desktop` zero warnings, `just build-s3` clean.
- No new frameworks, no hot-path allocation, every new visual number is a token in `ui_theme.h`.
- The perf rule of §14.0 respected: input never waits on an animation.

**14.9 Hide the softkey bar (Settings → Display).**
A toggle that removes the bottom context bar and lets the content area take that space. It is a
layout change, not a cosmetic one: `SOFTKEYS_H` becomes a runtime value, the content area resizes,
and every built page re-lays out (or is rebuilt) on the toggle. When hidden, the legend is gone —
the physical keys still do what the keymap says; that is the accepted trade. Ship it defaulting to
*shown*.

**14.10 Colour: one role table, all menus, switchable palettes.**
Every colour in the UI is a *role*, defined once and used by every page and every menu type. Pages
never name a colour — they ask for a role. A palette is a table of role → RGB565. Adding a theme is
adding one entry to the palette table; nothing else changes (Eli, 26/11).

The roles (this is the complete list — anything not here does not get its own colour):

| Role | What it paints | Palette A (today) | Palette B (light) |
|---|---|---|---|
| `GROUND` | page background | near-black charcoal | white |
| `SURFACE` | rows, cells, panels | dark slate | white / very light grey |
| `BORDER` | frames, rails, dividers | dark slate | light blue-grey |
| `TEXT` | primary text | off-white | black |
| `TEXT_DIM` | meta, timestamps, previews | muted grey-green | grey |
| `ACCENT` | icons, active tab, selection text | green | **bright orange** |
| `HIGHLIGHT` | selection fill | green fill | **bright orange fill** |
| `ACCENT_ALT` | secondary accent, category glyphs | cyan | light blue |
| `ON_HIGHLIGHT` | text drawn on a highlight fill | near-black | black |

Notes that make it usable rather than just tidy:
- `ACCENT` and `HIGHLIGHT` are distinct roles even when a palette gives them the same colour — a
  theme is allowed to make them differ, and several real themes do.
- Contrast is a palette-level obligation, checked when the palette is added, not per page:
  `TEXT` and `TEXT_DIM` must clear 4.5:1 on `GROUND` and `SURFACE`; `ON_HIGHLIGHT` must clear
  4.5:1 on `HIGHLIGHT`. (Orange text on white is 2.6:1 — that is why orange is an accent/highlight
  and never a text colour on a light ground.)
- **Enforcement, not convention:** no RGB literal may appear outside the palette table. Add a check
  to the build or a test that greps the UI for colour literals and fails if one escapes. Without
  that, drift is guaranteed within two weeks.
- **Images are assets, not colours.** The UI will draw JPEG/PNG content (wallpapers, icons, later
  photo attachments). Those carry their own pixels and are *not* recoloured by a theme — a theme
  selects *which* asset is used, it does not tint it. So the rule is: every colour the code
  *generates* comes from a role; a decoded bitmap is data and passes through untouched. Where text
  must stay legible over an image, use a scrim role (a role with alpha) rather than picking a text
  colour that happens to work on that particular picture.
- Consequently a theme owns its own wallpaper asset (or chooses none); the dark motif does not get
  reused under a light ground. For now the theme *bundles* its wallpaper — but keep the wallpaper as
  its own field in the theme struct rather than fused into a single "skin" blob, so a later
  granular control (pick any wallpaper independently of the palette) is a setting, not a refactor.


This is the reason the theme layer exists, and it closes audit §3.6 ("no runtime theme-swap pass"):
colours must stop being compile-time constants in draw code and become a palette table read through
the active-theme pointer, with a repaint pass that restyles every live object when the theme
changes. Anything that cannot be restyled live must be documented as a rebuild-on-switch page.

**14.11 Settings → Display → Themes.**
The sub-menu that selects the palette. Display becomes a parent page holding *Theme*, *Menu Style*
(§14.3) and *Softkey bar* (§14.9). Selection applies immediately (no confirm), and the choice
persists for the session; on-device persistence is still the outstanding settings-store task.

**14.12 Font choice in Settings → Display.**
A third entry under Display, beside Theme and Menu Style: *Font*, cycling the UI typeface (Eli,
26/11). The point of this entry is that **typeface and size become a preference, not a build-time
decision** — Eli tunes them himself as the design settles. So the wave ships the *mechanism* and a
sensible default, not a finely-argued opinion about which face wins.

- The choices are the body typeface; the chrome face (Silkscreen) is part of the product's identity
  and stays fixed unless Eli says otherwise. Flag it rather than assume silently.
- Offer at least three faces, with the size alongside them (e.g. *Pixelify 16*, *Jersey 10*,
  *DotGothic 16*), because size affects legibility more than face does at this panel density.
- Faces may be baked at 1bpp (crisp) or 8bpp (antialiased); both are legitimate *options* — the
  user picks what reads best — but the trade-off must be stated in the UI or README, not hidden.
  Do not remove a face from the list on the grounds that it "looks soft"; that is Eli's call to make.
- Candidates already evaluated: Pixelify Sans, Jersey 10, DotGothic16. Report the flash delta per
  face (ASCII range, two sizes) so the bundle cost is visible when adding more.
- Switching applies immediately through the same restyle pass as a theme change (§14.10), because
  both are appearance-only and both must not touch navigation.
- The selected font persists for the session alongside the theme; on-device persistence is still the
  outstanding settings-store task.

**Status of the current faces (interim, 26/11).** The tree currently has VT323 on the large body face
and an 8bpp baked Pixelify on the small one. **That split is provisional and is not a decision on
record** — it was produced by a session that then wrongly attributed it to Eliyahu. Do not treat it
as settled, and do not cite it as a rationale. It stays only until this setting exists and Eli tunes
it by hand.

**14.9 Hide the softkey bar (Settings → Display).**
A toggle that removes the bottom context bar and lets the content area take that space. It is a
layout change, not a cosmetic one: `SOFTKEYS_H` becomes a runtime value, the content area resizes,
and every built page re-lays out (or is rebuilt) on the toggle. When hidden, the legend is gone —
the physical keys still do what the keymap says; that is the accepted trade. Ship it defaulting to
*shown*.

**14.10 Colour: one role table, all menus, switchable palettes.**
Every colour in the UI is a *role*, defined once and used by every page and every menu type. Pages
never name a colour — they ask for a role. A palette is a table of role → RGB565. Adding a theme is
adding one entry to the palette table; nothing else changes (Eli, 26/11).

The roles (this is the complete list — anything not here does not get its own colour):

| Role | What it paints | Palette A (today) | Palette B (light) |
|---|---|---|---|
| `GROUND` | page background | near-black charcoal | white |
| `SURFACE` | rows, cells, panels | dark slate | white / very light grey |
| `BORDER` | frames, rails, dividers | dark slate | light blue-grey |
| `TEXT` | primary text | off-white | black |
| `TEXT_DIM` | meta, timestamps, previews | muted grey-green | grey |
| `ACCENT` | icons, active tab, selection text | green | **bright orange** |
| `HIGHLIGHT` | selection fill | green fill | **bright orange fill** |
| `ACCENT_ALT` | secondary accent, category glyphs | cyan | light blue |
| `ON_HIGHLIGHT` | text drawn on a highlight fill | near-black | black |

Notes that make it usable rather than just tidy:
- `ACCENT` and `HIGHLIGHT` are distinct roles even when a palette gives them the same colour — a
  theme is allowed to make them differ, and several real themes do.
- Contrast is a palette-level obligation, checked when the palette is added, not per page:
  `TEXT` and `TEXT_DIM` must clear 4.5:1 on `GROUND` and `SURFACE`; `ON_HIGHLIGHT` must clear
  4.5:1 on `HIGHLIGHT`. (Orange text on white is 2.6:1 — that is why orange is an accent/highlight
  and never a text colour on a light ground.)
- **Enforcement, not convention:** no RGB literal may appear outside the palette table. Add a check
  to the build or a test that greps the UI for colour literals and fails if one escapes. Without
  that, drift is guaranteed within two weeks.
- **Images are assets, not colours.** The UI will draw JPEG/PNG content (wallpapers, icons, later
  photo attachments). Those carry their own pixels and are *not* recoloured by a theme — a theme
  selects *which* asset is used, it does not tint it. So the rule is: every colour the code
  *generates* comes from a role; a decoded bitmap is data and passes through untouched. Where text
  must stay legible over an image, use a scrim role (a role with alpha) rather than picking a text
  colour that happens to work on that particular picture.
- Consequently a theme owns its own wallpaper asset (or chooses none); the dark motif does not get
  reused under a light ground. For now the theme *bundles* its wallpaper — but keep the wallpaper as
  its own field in the theme struct rather than fused into a single "skin" blob, so a later
  granular control (pick any wallpaper independently of the palette) is a setting, not a refactor.


This is the reason the theme layer exists, and it closes audit §3.6 ("no runtime theme-swap pass"):
colours must stop being compile-time constants in draw code and become a palette table read through
the active-theme pointer, with a repaint pass that restyles every live object when the theme
changes. Anything that cannot be restyled live must be documented as a rebuild-on-switch page.

**14.11 Settings → Display → Themes.**
The sub-menu that selects the palette. Display becomes a parent page holding *Theme*, *Menu Style*
(§14.3) and *Softkey bar* (§14.9). Selection applies immediately (no confirm), and the choice
persists for the session; on-device persistence is still the outstanding settings-store task.

**14.12 Font choice in Settings → Display.**
A third entry under Display, beside Theme and Menu Style: *Font*, cycling the UI typeface (Eli,
26/11). Requirements:

- The choices are the body typeface; the chrome face (Silkscreen) is part of the product's identity
  and stays fixed unless Eli says otherwise. Flag it rather than assume silently.
- Every offered face is baked at the body sizes, exactly like Pixelify Sans (8-bit) —
  the crispness rule (§14.10's discipline; VT323 was kept for the large face on
  Eliyahu's call, 26/11) applies to any font that is selectable.
  A selectable-but-antialiased face would undo the typography work.
- Candidates already evaluated for look: Pixelify Sans (current), Jersey 10, DotGothic16. Each costs
  tens of KB of flash at 8bpp for the ASCII range at two sizes — report the bundle delta.
- Switching applies immediately through the same restyle pass as a theme change (§14.10), because
  both are appearance-only and both must not touch navigation.
- The selected font persists for the session alongside the theme; on-device persistence is still the
  outstanding settings-store task.

## 15. STAGE 0 (alpha) — runtime resolution selector — DO THIS BEFORE §14

Eli's call (26/11): the panel is not chosen yet. The UI must be *written* to render at a
resolution rather than assuming one, and the simulator must let us switch resolution from outside
the phone UI. This lands before any of §14, because every other visual decision inherits from it.

**15.1 Dimensions become runtime values.**
`PANEL_W`/`PANEL_H` stop being compile-time constants that leak into layout, buffers, HAL and the
frame server. There is one source of truth for the current panel geometry (a `ui_screen_t` or
equivalent), set at init and changeable at runtime. Compile-time defaults remain the 3.5" dev board:
**320×480**.

**15.2 A resolution picker outside the phone UI.**
A developer control in the host harness — *not* a page inside the phone — offering aspect ratios and
resolutions, at minimum:

| Resolution | Aspect | Why |
|---|---|---|
| 320×480 | 2:3 | the dev board (default) |
| 240×320 | 3:4 | small cheap QVGA panels |
| 240×400 | 3:5 | common phone-ish |
| 320×240 | 4:3 | landscape sanity check |
| 480×800 | 3:5 | larger phone panel |
| 480×640 | 3:4 | VGA |
| 1280×720 | 16:9 | gross-out test, not a target |

Plus a free-form width×height entry, and the value shown in the harness (not on the phone screen).

**15.3 What must actually adapt.**
- LVGL display size + draw buffers sized from the geometry (no fixed 307 KB assumptions).
- The stable frame: status bar height, softkey bar height and the content area derived from tokens;
  decide and document whether the chrome heights are fixed px or proportional.
- Page layout: row heights, column widths, the category strip, the menu grid and carousel all
  recompute. Text may wrap or clip differently — that is expected and acceptable.
- The bezel mock in the host window scales to the panel; the browser viewport sends the new size and
  the page rescales its canvas.
- Shot mode takes a geometry argument so golden frames can be produced per resolution.

**15.4 The standard this stage is held to — deliberately lower.**
"Attempts to render, legibly, without crashing or drawing outside the panel." Not pixel-perfect,
not beautiful at every size. Once a real panel is chosen we go back and tune against that one.

**15.5 Why it is worth the effort.**
It forces the constants out of the layout while the code is still small, and it makes the screen
choice a data point we can look at instead of a guess. It is also the cheapest possible time to do
it: after §14 there will be far more layout code to unpick.
