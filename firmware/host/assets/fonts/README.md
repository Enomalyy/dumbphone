# Bundled fonts

Downloaded once and committed so the simulator renders identically offline.
All five faces are free-licensed; the two data-script faces are fallbacks, not
primary UI fonts (see "Fallback chain" below).

| File | Source | License | Bytes |
|---|---|---|---|
| `Silkscreen-Regular.ttf` | google/fonts `ofl/silkscreen` | SIL OFL 1.1 | 32,220 |
| `VT323-Regular.ttf` | google/fonts `ofl/vt323` | SIL OFL 1.1 | 153,116 |
| `FA-Solid.ttf` | FortAwesome/Font-Awesome 6.5.2 `webfonts/fa-solid-900.ttf` | SIL OFL 1.1 (font), CC BY 4.0 (icons) | 420,332 |
| `DroidSansHebrew-Regular.ttf` | `google-droid-sans-fonts` package, © 2008 The Android Open Source Project | Apache-2.0 | 30,024 |
| `NotoEmoji-Subset.ttf` | google/fonts `ofl/notoemoji` (`NotoEmoji[wght].ttf`), subset — see below | SIL OFL 1.1 | 753,788 |

`DroidSansHebrew-Regular.ttf` is copied unmodified from the installed package
(`/usr/share/fonts/google-droid-sans-fonts/`). It is a static 30 KB Hebrew-only
TTF: the Hebrew block, the presentation forms, and only space/NBSP/shekel
outside it, so it can never pull Latin away from the pixel faces. It is **not**
a pixel face: no freely licensed pixel/bitmap Hebrew font was installed or
found in a brief look, so the flat look stays on the Latin side and Hebrew
falls through in a clean sans. If a good pixel Hebrew face turns up later,
swap this file and re-shoot the frames — nothing else changes.

Font Awesome codepoints used by the shell are written as C11 `\uXXXX`
escapes in `ui/ui_shell.c`; the authoritative name→codepoint table is
`metadata/icons.json` in the Font Awesome repository (6.5.2).

## Fallback chain (Latin stays pixel-styled)

`ui_shell_init()` loads one Hebrew instance and one emoji instance **per text
size** (13, 16, 18, 22, 24 px today) and chains them:

```
Silkscreen / VT323 / FA  ->  Droid Sans Hebrew @same px  ->  Noto Emoji @same px
```

LVGL resolves a glyph through `lv_font_t.fallback` only when the base face
reports it missing, so ASCII/Latin never leaves Silkscreen or VT323. The
chain is attached to every UI face; the two Font Awesome faces never carry
data text, but the uniform chain costs nothing at render time.

## Emoji subset — how and what it costs

The installed `Noto-COLRv1.ttf` is a colour font; LVGL/tiny_ttf cannot use
colour tables. The monochrome OFL Noto Emoji was used instead. Full file:
1,982,596 B (variable). Two conversion steps bring it to 753,788 B:

1. **Instance the variable font at Regular (wght=400)** —
   `fontTools.varLib.instancer`, result 886,628 B (drops `gvar`/`HVAR`/`VVAR`).
2. **Subset to the WhatsApp working set** — `fonttools subset` with the
   ranges below, hinting off, `GSUB`/`GPOS`/`GDEF`/`gasp` dropped
   (LVGL does no OpenType shaping, so those tables are dead weight):

```sh
python3 -m fontTools.subset NotoEmoji-400.ttf \
  --output-file=NotoEmoji-Subset.ttf \
  --unicodes="U+0023,U+002A,U+0030-0039,U+00A9,U+00AE,U+200D,U+FE0E,U+FE0F,U+20E3,U+2190-21FF,U+2300-23FF,U+2600-27BF,U+2B00-2BFF,U+3030,U+303D,U+3297,U+3299,U+1F000-1F1FF,U+1F300-1F5FF,U+1F600-1F64F,U+1F680-1F6FF,U+1F900-1F9FF,U+1FA70-1FAFF" \
  --layout-features='' --no-hinting --drop-tables+=GSUB,GPOS,GDEF,gasp
```

Result: **1,391 glyphs / 1,385 mapped codepoints, 753,788 B** (~542 B per
glyph). Coverage: smileys and supplemental faces, hearts and dingbats, hands
and gestures, food/objects/activity, transport, flags' regional indicators,
keycaps, arrows, technical symbols. The 102 emoji actually present in the
local WhatsApp corpus are all covered except `✓` (U+2713) and `✗` (U+2717),
which Noto Emoji does not contain — those fall to LVGL's tofu box, the
documented graceful floor.

Known limits of the pipeline, not bugs: LVGL does not apply `GSUB`, so ZWJ
sequences are not joined and flags render as two regional-indicator letter
boxes; combining keycaps render as digit + keycap, not one glyph.

**S3 flash cost:** the two fallback files add 783,812 B (765 KiB) to whatever
filesystem carries them — the emoji face is by far the largest single asset.
If the device partition is tight, cut blocks from the `--unicodes` list (for
example drop `U+1F300-1F5FF` and `U+1F900-1F9FF`, worth ~510 KB, keeping
smileys/hearts/hands/flags), and/or ship the emoji face only at the sizes that
carry data (18/24 px) instead of all five. The exact command above is the
reproducible path.
