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
lines**, plus **bitmap font glyphs** (Silkscreen/VT323 baked into the binary). Each icon
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
- [x] **Golden frames:** `just shot-desktop` freezes the clock, so a frame is reproducible.

**Still open (deliberately out of the UI build-out):**
- Maps pages are stub leaves; opening a conversation / composing is not wired.
- The unified WhatsApp surface is one list of chats — the SMS-vs-call colour split of §3
  needs the transport (there are no SMS entries yet).
- PIN entry on the lock screen; settings/chat persistence (state is in memory, seeded from
  `chats.json`); real call log / contacts instead of mock data.
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

A page is data, but it still touches five places. This is the whole list, in order:

1. **Data:** a `menu_t` (title, items with `label`/`icon`/optional `sub`/`meta`/`badge`/
   `target`, optional `cats` strip, and its own `keymap_t`) — either a small `ui_<page>.{h,c}`
   module following `ui_settings.c`, or a `static const` table in `ui_shell.c` like the
   main-menu categories.
2. **Target id:** add it to `page_id_t` in `ui/ui_menu.h` if something opens it.
3. **Build:** build it once in `ui_shell_init()` and raise `LIST_POOL` in `ui/ui_menu.c`
   (that constant is the app's whole page budget; the shell prints an error if it runs out).
4. **Routing:** a `ui_screen_id_t` in `ui/ui_shell.h`, then a case in `ui_shell_show_screen()`,
   `page_keymap()`, `move_selection()`, `activate()`, and `open_page()` if it is a target.
5. **Options:** a row table for it in `ui/ui_options.c` (the SHOULDER-L sidebar).

Appearance needs no work: colours come from `ui_palette()`, sizes from the `UI_*` tokens,
so a new page matches the others automatically.

## 11. Porting to the S3 — the budget to keep

The simulator hides costs the device will pay. They are known, not solved:

- **Full-frame flush.** LVGL renders partial regions (`LV_DISPLAY_RENDER_MODE_PARTIAL`), but
  the flush callback copies into the full framebuffer and calls
  `hal_display_blit(frame, 320*480*2)` — the HAL contract only offers a full-frame push
  (`hal/hal_display.h`). On the S3 that is a 307 KB SPI write per flush. The HAL needs an
  area blit (`x1,y1,x2,y2`) and the display backend a matching partial write.
- **Rows are rebuilt, not recycled.** `rebuild_rows()` deletes and recreates every row's
  LVGL objects on each category cycle / value refresh (chat list = ~33 rows × 4 labels).
  That is fine on a desktop, but it is allocation churn on the device; a fixed row pool that
  relabels and hides extras removes it.
- Also still desktop-only: the draw buffer is a static full frame (307 KB) — it must become a
  partial buffer in PSRAM, and the LVGL tick/indev timers must be fed by ESP-IDF drivers.
