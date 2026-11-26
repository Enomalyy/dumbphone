# Roadmap

Where the project is going, in the order it will actually happen. The design detail lives in
[`ui-architecture.md`](ui-architecture.md); this file is the index.

The GitHub issues are the live status: <https://github.com/Enomalyy/dumbphone/issues>. Every item now has its issue.

## Three vocabularies, one plan

Three sets of labels are in play. They are not alternatives to each other, and nothing here is a
fourth roadmap:

- **Stages 0, A–D** — *what the phone will do*, in the order it gets built.
- **§14, §15, §16 …** — *where an item's plan is written down*. Chapter numbers inside
  `ui-architecture.md`. §14 is the big UI chapter (14.1–14.12 are individual behaviours that Stages
  A–D consume), §16 is the structural work below; the dated decisions (§12, §13, §15) live in
  `decisions.md`. Nothing gets built "as §14" — a
  stage gets built, and it cites the §'s that describe it.
- **R1–R5** — *what the code needs* for those stages to be cheap and safe, from the outside review.
  Since 26/11 they are part of the plan (spec §16), not a proposal beside it.

Mapping: Stage A = §14.9–14.12 · B = §14.1–14.3, 14.5, 14.6 · C = §14.4, 14.8 · D = §14.7 ·
Stage 0 = decisions §15 · R1, R3, R5 = §16.1, §16.3, §16.5 · R4 = §16.4 (first half of D) · R2 = §16.2
(folded into R5).

## The order

| # | Item | What it delivers | Spec | Issue |
|---|---|---|---|---|
| 1 | **Stage 0** ✅ *done 26/11* | Runtime panel geometry: `ui/ui_geometry.c` is the single source of truth (identity at 320×480); `--geom WxH` and the harness picker; bezel, buffers and browser viewport follow it. | §15 | [#1](https://github.com/Enomalyy/dumbphone/issues/1) *(open — Eli's sign-off)* |
| 2 | **R1** — safety net | `scenes.txt`, `just golden-check` / `golden-update`, `just test-host` with real tests (`ui_text` direction, then `ui_geometry` derivations), one `just check` = build + frames + tests. Local gate: the public repo is a publish mirror, so a GitHub workflow there is a smoke test at most. | §16.1 | [#7](https://github.com/Enomalyy/dumbphone/issues/7) |
| 3 | **R3** — page registry | One `ui_page_t` per page in one table; show/keymap/activate/move/swipe/options/`--screen` become loops; `LIST_POOL` becomes the table length; §10 shrinks to "write the module, add a row". | §16.3 | [#8](https://github.com/Enomalyy/dumbphone/issues/8) |
| 4 | **Stage A** — appearance plumbing | Colour as a nine-role table shared by every page, palettes as data (dark + light), Settings → Display holding Theme / Font / Menu Style / Softkey bar, and a real apply pass. Font = mechanism only; the typeface choice stays provisional. | §14.9–14.12 | [#2](https://github.com/Enomalyy/dumbphone/issues/2) |
| 5 | **Stage B** — menu feel | Tappable category strip, the moving-carousel menu style, one cycling Menu Style control, larger highlight icons, centred cell text. | §14.1–14.3, 14.5, 14.6 | [#3](https://github.com/Enomalyy/dumbphone/issues/3) |
| 6 | **Stage C** — touch feel | Uniform drag-scroll everywhere, and held-down acceleration through long lists. | §14.4, 14.8 | [#4](https://github.com/Enomalyy/dumbphone/issues/4) |
| 7 | **R4** — model layer | `model/` for chats, messages, contacts, call log and settings; `chat_model.h` generalised; settings persist through `hal_storage`. | §16.4 | folded into #5 |
| 8 | **Stage D** — conversations | A real message model and a conversation page: bubbles, grouping, timestamps, scrollback, RTL and emoji. The biggest stage. | §14.7 | [#5](https://github.com/Enomalyy/dumbphone/issues/5) |
| 9 | **R5** — UI in the S3 build | `hal`/`ui`/`model` as ESP-IDF components that also build as plain CMake libraries; LVGL pinned once; draw buffer in PSRAM; the `ui_port.h` seam implemented for the chip. | §16.5, §16.2 | [#9](https://github.com/Enomalyy/dumbphone/issues/9) |

**Why this order.** R1 first because every later item moves pixels and the only current check is a
person. R3 before A because A adds pages, and the registry is what makes adding one cheap. R4 is the
first half of D rather than a stage of its own. R5 waits for the board (9–18/12) and absorbs R2 —
the port seam Stage 0 was meant to carry and did not.

## The rule governing all of it

**Responsiveness comes from input latency, not from motion.** The device will be slow; the action
must take effect the instant a key is pressed, and animation is garnish on top. The one exception is
drag-scrolling, which has to be as smooth as the panel allows because a stutter there reads as broken.

## After the board arrives (9–18/12)

- R5 — wire the S3 HAL backends (display flush is already region-based and bounded), then the UI
  compiles and runs on the chip.
- Integrate cellular, battery, and the physical keypad.
- Move the messaging backbone onto WAHA (spec §9) — the UI currently runs on a JSON fixture of real chats.
