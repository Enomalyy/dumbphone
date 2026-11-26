# Roadmap

Where the project is going, in the order it will actually happen. The design detail lives in
[`docs/ui-architecture.md`](ui-architecture.md); this file is the index.

Every stage below has a GitHub issue with its acceptance criteria — the issues are the live status,
this is the shape.

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
