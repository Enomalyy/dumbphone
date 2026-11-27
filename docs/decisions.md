# Decisions — the dated record

Why the project looks the way it does. Each entry is a decision, when it was made, and what it
changed. The design itself lives in [`ui-architecture.md`](ui-architecture.md); the work queue lives
in [`ROADMAP.md`](ROADMAP.md). Nothing here is a plan — it is the reason behind one.

Sections keep the numbers they had in the spec (§12, §13, §15), so older references still resolve.
§14 and §16 stay in the spec: they are still the design.

---

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

---

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

---

## 16. Stage C — touch feel (27/11): the scroll contract and key-repeat rules

**16.1 The §14.4 scroll contract (BUILT).**
- Every scrollable surface is an LVGL scroll target: the chat list, every long list page,
  the main menu (carousel and grid), and — new in this stage — the options sidebar's row
  area (`ui_sidebar.c`). They all sit under ONE shared drag dead zone,
  `UI_SCROLL_DEADZONE_PX` (10 px), set on the indev itself in `ui_input.c`. The dead zone is
  a property of the input device, not of any widget, so no page can have a different feel.
- A vertical drag scrolls 1:1 with the finger. That exact tracking is the "motion" §14.0
  asks for — there is no separate scroll animation, so nothing can block or delay input.
  LVGL's native fling may run after a fast flick; it never runs while the finger is down,
  and the harness's slow drags decay to zero velocity before release, so frames stay
  reproducible.
- A drag never selects or executes the row it started on: the `press_moved` rule
  (`ui_menu.c`'s `on_row_tap`, `ui_sidebar.c`'s `on_side_row_tap`). The tap guard from
  `f20dd67b` is untouched.
- A horizontal drag cycles categories only where a page has them (the shell's
  `page_swipe` hands to the page's gesture callback); on pages without categories it does
  nothing rather than half-scrolling.
- Scroll position rule (the "pick one and document it" item from §13.1): **per-page,
  survives going back and returning; resets to the top when a category changes**
  (`ui_list_set_category` — a new list is a new view). No persistence across app restarts;
  that comes with the model layer (spec §16.4) if it ever matters.
- The sidebar's row area is a scroll target even though no page's option table is long
  enough to overflow today: the contract must hold for any table size, and making it one
  costs nothing (same flat styling as the list pages' rows box).

**16.2 The §14.8 key-repeat rules (BUILT).**
- UP/DOWN are the only keys that repeat. LEFT/RIGHT stay single-step: they cycle a handful
  of categories, and repeating them would race the user's intent.
- The first step fires on the PRESS, not the release (spec §14.0: the selection moves on
  the input event). Repeats then start after a 400 ms dwell (`UI_KEY_REPEAT_DWELL_MS`),
  at a 200 ms interval (`UI_KEY_REPEAT_START_MS`) that halves per repeat down to a 40 ms
  floor (`UI_KEY_REPEAT_MIN_MS`). Release stops the repeats instantly — the state machine
  lives in `ui_input.c`'s poll loop and is cleared on KEY_UP.
- Long-press conflict (the "decide which wins" item): **a configured held-press binding
  wins.** If UP/DOWN has a target in Settings ↦ Held Presses, holding it performs only the
  custom action (on release, as today) and never moves the selection; a short press of a
  configured key still moves the selection. Reason: an explicit user setting must not be
  silently overridden by implicit behaviour — the held-press page exists precisely so Eli
  can assign these keys, and the shell asks `ui_heldpress_target()` before arming repeat.
  (SHOULDER-L is seeded with a preset target — New Chat — which is why this rule had to be
  decided at all.)
- The sidebar's UP/DOWN accelerates too: it routes through the same press-time dispatch as
  every other page, so one engine covers both.

---

## 17. AGENTS.md tree with hard token budgets (Eli's call, 27/11)

The project docs are split into one AGENTS.md per scope, each under a measured budget:
root ≦900 tokens, `firmware/` ≦900, `firmware/ui/` ≦400, `firmware/model/` ≦300,
`tools/` ≦350, `reports/` ≦80. The rule that keeps them from growing back: **an
AGENTS.md may contain only what a session working in that folder must know before
reading anything; decisions go to decisions.md, plans to ROADMAP/issues, status to
git.** History (relay legs, audits, commit ranges, build narratives) belongs in this
file, ROADMAP and git — never in an AGENTS.md.

---

## 18. The settings tree is deleted — one list, one place (27/11, issue #15)

Eli reported the duplicate three times: from the main menu, pressing *Display* opened the
settings tree — the same four rows — with one pre-selected, so a second press was needed to
reach Display. The fix is structural, not cosmetic: each SETTINGS-category item carries its
**real** target (Display / Speed Dials / Held Presses), **Call Preference** is a value that
toggles in place through `ui_settings_activate()` with its state on the row's `meta` column,
and the tree screen itself (`PAGE_SETTINGS`, `UI_SCREEN_SETTINGS`, its registry row) is
deleted — while it exists, the duplicate can always come back. The sidebar's "Settings"
entries now switch the menu to the SETTINGS category (a new `OPT_ACTION_SETTINGS_CAT`).
The rule is written into spec §14.13: *a page may not re-list another page's rows — if two
screens show the same list, one of them is a bug.*
