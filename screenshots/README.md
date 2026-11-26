# Golden frames — how each one is made

Every frame here is reproducible from the CLI: `just shot-desktop` / `just shot-window` freeze
the clock at 20:36, so "AE=0 against yesterday" means something. Run from `firmware/` after
`just build-desktop`.

Two forms:

- **just** — `just shot-desktop <screen> [cycledir cyclen] [activate] [menustyle]`
  and `just shot-window <screen> [press]`. These write the PNG straight into this folder.
- **binary** — `./build-host/dumbphone-desktop --shot /tmp/x.ppm <flags> --no-server`
  followed by `magick /tmp/x.ppm <frame>.png`. Needed for the states the justfile has no
  argument for (`--key`, `--swipe`, `--tap`).

Panel frames (`shot-desktop`, 320×480 device pixels — these are the golden diffs):

| Frame | State |
|---|---|
| `chat` | `--screen chat` |
| `family` / `unread` / `favorites` / `archived` | `--screen chat --cycle right 1..4` |
| `menu` | `--screen menu` |
| `menu-calls` / `menu-settings` | `--screen menu --cycle right 1` / `... 3` |
| `menu-grid` / `menu-grid-maps` | `--screen menu --menu-style grid [--cycle right 2]` |
| `settings` / `settings-grid` | `--screen settings [--menu-style grid]` |
| `speeddials` / `heldpress` / `callhist` / `contacts` / `lock` / `stub` | `--screen <name>` |
| `speeddials-cycled` / `heldpress-cycled` | `--screen <name> --activate 1` |
| `speeddial-mom` | `--screen chat --key digit-1` (T9 1 dials the seeded "Mom") |
| `call-key` | `--screen chat --key call` |
| `lock-from-chat` | `--screen chat --key end 2` |
| `callhist-from-menu` | `--screen menu --cycle right 1 --activate 1` |
| `chat-hebrew` | `--screen chat --key down 8` (selects the Hebrew row) |
| `swipe-scroll` | `--screen chat --swipe up 120` |
| `swipe-chat` / `swipe-menu` | `--screen chat|menu --swipe left 120` |
| `tap-row3` | `--screen chat --tap 160 217` |
| `key-down` | `--screen chat --key down` |
| `options-home` / `options-menu` / `options-callhist` | `--screen <name> --key shoulder-l` |
| `options-exec` | `--screen chat --key shoulder-l --key ok` |
| `options-grid` | `--screen menu --key shoulder-l --key down --key ok` |
| `browser-viewport` | a live capture of `just run-desktop`'s web viewport — not reproducible |

Composed frames (`shot-window`, bezel + panel, `--scale 1`): `window-chat`, `window-menu`,
`window-chat-digit-5` (`--press digit-5`, the 5 key held). These exercise the **host display
backend** (`host/src/hal_sdl_display.c`: region compose/present, scaling, held keys), which no
panel frame covers — re-shoot them after touching that file or `host/src/bezel.c`.

Regenerate everything above, then compare pixels (not bytes: magick re-encodes):

```sh
python3 -c "from PIL import Image,ImageChops;print(ImageChops.difference(
  Image.open('a.png').convert('RGB'),Image.open('b.png').convert('RGB')).getbbox())"
```

A non-`None` box means the frame really changed — decide whether that was intended before
committing it.

**Adding a frame needs `git add -f`:** the home-directory repo ignores `*.png` globally, so a
plain `git add` reports "ignored" and the frame silently never reaches the commit.
