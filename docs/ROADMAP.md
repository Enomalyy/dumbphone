# Roadmap

Where the project is going, in the order it will actually happen. The design detail lives in
[`docs/ui-architecture.md`](ui-architecture.md); this file is the index.

Every stage below has a GitHub issue with its acceptance criteria — the issues are the live status,
this is the shape.

## Three vocabularies, one plan

Three sets of labels are in play. They are not alternatives to each other, and nothing here is a
fourth roadmap:

- **Stages 0, A–D** — *what the phone will do*, in the order it gets built. One issue each.
- **§14, §15, …** — *where a stage's plan is written down*. Chapter numbers inside
  `ui-architecture.md`. §14 is the big UI chapter; its 14.1–14.12 are individual behaviours that
  Stages A–D consume. §15 is Stage 0's own chapter. Nothing gets built "as §14" — a stage gets
  built, and it cites the §'s that describe it.
- **R1–R5** — *what the code needs* for those stages to be cheap and safe (the outside review in
  `reorg-plan.md`). Enablers, not features; none is committed work until Eli turns it into an issue.

Mapping: Stage A = §14.9–14.12 · B = §14.1–14.3, 14.5, 14.6 · C = §14.4, 14.8 · D = §14.7 ·
Stage 0 = §15.

Where the R-steps sit, given Stage 0 has shipped: **R1** (golden-check CI + the first unit test) is
the candidate to take before A. **R2** (one port seam for geometry/tick/time/fonts) was meant to fold
into Stage 0 and did not — it is now optional; take it if the S3 port wants the seam. **R3** (page
registry) after A and before D. **R4** (model layer) with or just before D. **R5** (the UI compiled
into the S3 build) when the board arrives.

**Next:** Stage A, unless R1 goes first — Eli's call.

| Stage | What it delivers | Spec | Issue |
|---|---|---|---|
| **0** | **Runtime resolution selector.** Panel geometry stops being compile-time; a picker in the harness lets us see the UI at 240×320, 320×480, 480×800 and friends before a panel is chosen. **Built:** `ui/ui_geometry.c` is the single runtime source of truth (identity at 320×480); `just geom=WxH shot-desktop <screen>` / `--geom WxH`; bezel, buffers and browser viewport all follow it; default goldens unchanged. | §15 | [#1](/Enomalyy/dumbphone/issues/1) (open — pending Eli's sign-off) |
| **A** | **Appearance plumbing.** Colour as a nine-role table shared by every page, palettes as data (dark + light), Settings → Display holding Theme / Font / Menu Style / Softkey bar. | §14.9–14.12 | [#2](/Enomalyy/dumbphone/issues/2) |
| **B** | **Menu feel.** Tappable category strip, the moving-carousel menu style, one cycling Menu Style control, larger highlight icons, centred cell text. | §14.1–14.3, 14.5, 14.6 | [#3](/Enomalyy/dumbphone/issues/3) |
| **C** | **Touch feel.** Uniform drag-scroll everywhere, and held-down acceleration through long lists. | §14.4, 14.8 | [#4](/Enomalyy/dumbphone/issues/4) |
| **D** | **Conversations.** A real message model and a conversation page: bubbles, grouping, timestamps, scrollback, RTL and emoji. The biggest stage. | §14.7 | [#5](/Enomalyy/dumbphone/issues/5) |

## The rule governing all of it

**Responsiveness comes from input latency, not from motion.** The device will be slow; the action
must take effect the instant a key is pressed, and animation is garnish on top. The one exception is
drag-scrolling, which has to be as smooth as the panel allows because a stutter there reads as broken.

## After the board arrives (9–18/12)

- Wire the S3 HAL backends (display flush is already region-based and bounded).
- Integrate cellular, battery, and the physical keypad.
- Move the messaging backbone onto WAHA (spec §9) — the UI currently runs on a JSON fixture of real chats.
