# Golden frames — how each one is made

Every frame here is reproducible from the CLI: `just shot-desktop` / `just shot-window` freeze
the clock at 20:36, so "AE=0 against yesterday" means something. Run from `firmware/` after
`just build-desktop`.

**The gate (spec §16.1):** `firmware/scenes.txt` is the machine-readable recipe list — one
line per scene (`<frame> <panel|window> <flags…>`), covering every frame in this folder except
the documented exclusions. The tables below are its human view; if they drift, `scenes.txt`
wins. The commands:

- `just golden-check` — render every scene to a temp dir and compare pixels against the
  committed frames. Read-only here; fails naming each bad scene. Also fails if a committed
  frame has no recipe, so nothing drops out silently.
- `just golden-update` — the ONLY way a frame in this folder changes: rewrites the PNGs from
  `scenes.txt`. A commit that moves pixels must name the frames it moves and why (§16.6).
- `just check` — build + golden-check + host unit tests; the one-command local gate.

Compare pixels, not bytes: ImageMagick embeds a `tIME` chunk in PNGs it writes, so two renders
of the same state differ as bytes but are identical as pixels.

Two forms:

- **just** — `just shot-desktop <screen> [cycledir cyclen] [activate] [menustyle]`
  and `just shot-window <screen> [press]`. These write the PNG straight into this folder.
- **binary** — `./build-host/dumbphone-desktop --shot /tmp/x.ppm <flags> --no-server`
  followed by `magick /tmp/x.ppm <frame>.png`. Needed for the states the justfile has no
  argument for (`--key`, `--swipe`, `--tap`, `--theme`, `--font`).

Appearance states: `--theme dark|light` starts on that palette (applied before any page is
built, so the frame is the theme's own, not a restyle of the other one), and
`--font header|text|subtext[:N]` steps that role's face+size ladder N times. Both are
harness flags — `ui_theme_set()` from Settings is the real path and it repaints the live UI.

Panel frames (`shot-desktop`, 320×480 device pixels — these are the golden diffs):

| Frame | State |
|---|---|
| `home` | `--screen home` (the real HOME screen — wallpaper + widgets) |
| `chat` | `--screen chat` (Messaging, reached from the main menu) |
| `family` / `unread` / `favorites` / `archived` | `--screen chat --cycle right 1..4` |
| `menu` | `--screen menu` |
| `menu-calls` / `menu-settings` | `--screen menu --cycle right 1` / `... 3` |
| `menu-grid` / `menu-grid-maps` | `--screen menu --menu-style grid [--cycle right 2]` |
| `settings` / `settings-grid` | `--screen settings [--menu-style grid]` |
| `speeddials` / `heldpress` / `callhist` / `contacts` / `lock` / `stub` | `--screen <name>` |
| `speeddials-cycled` / `heldpress-cycled` | `--screen <name> --activate 1` |
| `speeddial-mom` | `--screen home --key digit-1` (T9 1 dials the seeded "Mom" — T9 dialing lives on HOME) |
| `call-key` | `--screen home --key call` (the CALL key opens Calls from HOME) |
| `lock-from-chat` | `--screen chat --key end 2` |
| `callhist-from-menu` | `--screen menu --cycle right 1 --activate 1` |
| `chat-hebrew` | `--screen chat --key down 8` (selects the Hebrew row) |
| `swipe-scroll` | `--screen chat --swipe up 120` |
| `swipe-chat` / `swipe-menu` | `--screen chat|menu --swipe left 120` |
| `tap-row3` | `--screen chat --tap 160 217` |
| `key-down` | `--screen chat --key down` |
| `options-home` / `options-menu` / `options-callhist` | `--screen <name> --key shoulder-l` (`home` is the real HOME screen, not the chat list) |
| `settings-display` | `--screen settings --key ok` (Settings > Display: theme, font, menu style, softkey bar) |
| `settings-font` | `--screen settings --key ok --key down --key ok` (each row previews its own level) |
| `settings-font-header32` | ... `--font header:1` (Headers ladder stepped to Silkscreen 32) |
| `chat-light` / `menu-light` / `settings-light` | the same screens with `--theme light` (palette B) |
| `options-exec` | `--screen chat --key shoulder-l --key ok` |
| `options-grid` | `--screen menu --key shoulder-l --key down --key ok` |
| `browser-viewport` | a live capture of `just run-desktop`'s web viewport — not reproducible |

Composed frames (`shot-window`, bezel + panel, `--scale 1`): `window-chat`, `window-menu`,
`window-chat-digit-5` (`--press digit-5`, the 5 key held). These exercise the **host display
backend** (`host/src/hal_sdl_display.c`: region compose/present, scaling, held keys), which no
panel frame covers — re-shoot them after touching that file or `host/src/bezel.c`.

Geometry frames (decisions §15 / issue #1): the panel size is a runtime value
(`firmware/ui/ui_geometry.c`), picked with the `geom` justfile variable:
`just geom=480x800 shot-desktop chat`. Non-default geometries write
`<name>-WxH.png` so the default goldens above are never clobbered. The set here:
`home|chat|menu` at 240×320, 240×400, 320×240, 480×800, 480×640, 1280×720 and the
free-form 200×350, plus `window-home-480x800` / `window-chat-digit-5-240x320`
(bezel re-layout at those sizes). Acceptance bar: legible, no crash, nothing
drawn outside the panel — not pixel-perfect at every size.

To re-verify by hand, render a scene and compare pixels (not bytes: magick re-encodes):

```sh
./build-host/dumbphone-desktop --shot /tmp/a.ppm --screen chat --no-server
magick ../screenshots/chat.png /tmp/b.ppm && cmp /tmp/a.ppm /tmp/b.ppm
```

A difference means the frame really changed — decide whether that was intended before
committing it, and if so re-shoot with `just golden-update`.

**Adding a frame needs `git add -f`:** the home-directory repo ignores `*.png` globally, so a
plain `git add` reports "ignored" and the frame silently never reaches the commit.
