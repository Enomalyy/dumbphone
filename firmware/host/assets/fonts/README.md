# Bundled fonts

Downloaded once and committed so the simulator renders identically offline.
All five faces are free-licensed; the two data-script faces are fallbacks, not
primary UI fonts (see "Fallback chain" below). The small body face is **not**
a TTF here — it is baked to a bitmap C file (`firmware/ui/font_pixelify_m.c`,
see "Baked Pixelify Sans" below).

| File | Source | License | Bytes |
|---|---|---|---|
| `Silkscreen-Regular.ttf` | google/fonts `ofl/silkscreen` | SIL OFL 1.1 | 32,220 |
| `VT323-Regular.ttf` | google/fonts `ofl/vt323` | SIL OFL 1.1 | 153,116 |
| `FA-Solid.ttf` | FortAwesome/Font-Awesome 6.5.2 `webfonts/fa-solid-900.ttf` | SIL OFL 1.1 (font), CC BY 4.0 (icons) | 420,332 |
| `DroidSansHebrew-Regular.ttf` | `google-droid-sans-fonts` package, © 2008 The Android Open Source Project | Apache-2.0 | 30,024 |
| `NotoEmoji-Subset.ttf` | google/fonts `ofl/notoemoji` (`NotoEmoji[wght].ttf`), subset — see below | SIL OFL 1.1 | 753,788 |
| `PixelifySans-Regular.ttf` | google/fonts `ofl/pixelifysans` (`PixelifySans[wght].ttf`), instanced at wght=400 | SIL OFL 1.1 | 51,892 |
| `Jersey10-Regular.ttf` | google/fonts `ofl/jersey10` | SIL OFL 1.1 | 77,732 |
| `DotGothic16-ASCII.ttf` | google/fonts `ofl/dotgothic16`, subset to Latin-1 — see below | SIL OFL 1.1 | 86,296 |

`DroidSansHebrew-Regular.ttf` is copied unmodified from the installed package
(`/usr/share/fonts/google-droid-sans-fonts/`). It is a static 30 KB Hebrew-only
TTF: the Hebrew block, the presentation forms, and only space/NBSP/shekel
outside it, so it can never pull Latin away from the pixel faces. It is **not**
a pixel face: no freely licensed pixel/bitmap Hebrew font was installed or
found in a brief look, so the flat look stays on the Latin side and Hebrew
falls through in a clean sans. If a good pixel Hebrew face turns up later,
swap this file and re-shoot the frames — nothing else changes.

Font Awesome codepoints used by the UI are named once, as C11 `\uXXXX`
escapes, in `ui/ui_icons.h` (the renderer keeps its three private chevron/
caret glyphs in `ui/ui_menu.c`); the authoritative name→codepoint table is
`metadata/icons.json` in the Font Awesome repository (6.5.2).

## Baked Pixelify Sans (small body face)

The preview / conversational text (`UI_FONT_BODY_M_PX`, 16 px) is **Pixelify
Sans** baked to an LVGL bitmap font — `firmware/ui/font_pixelify_m.c`
(64,661 B source, ~40 KiB rodata). It is the provisional default for the
small letters, with VT323 (TTF) for the big ones — provisional is the point:
Settings > Display > Font sets the face and size of all three levels, and no
face has been chosen. 8-bit depth: 1-bit looked too muddy on the dark
palette, 8-bit reads clean while keeping the pixel look. The large body face
(`UI_FONT_BODY_L_PX`, 24 px) is VT323 as a TTF.

Source: Pixelify Sans OFL 1.1 (https://fonts.google.com/specimen/Pixelify+Sans),
static weight 400, subset to printable ASCII (U+0020..U+007E). Exact recipe:

```sh
fonttools varLib.instancer PixelifySans-wght.ttf wght=400 -o PixelifySans-400.ttf
npx lv_font_conv --font PixelifySans-400.ttf --size 16 --bpp 8 --no-compress \
  --format lvgl -r 32-126 -o font_pixelify_m.c
```

Any OTHER size of Pixelify (the Font page's ladder) loads
`PixelifySans-Regular.ttf` — the same font instanced at wght=400 — through
tiny_ttf, so the shipped 16 px stays pixel-exact while the size is still
adjustable.

`--no-compress` matters: compressed (RLE) output needs `LV_USE_FONT_COMPRESSED`
in `lv_conf.h`, which we do not enable. The generated descriptor is const
rodata; `ui_shell_init()` chains the fallbacks onto a mutable shallow copy
(`firmware/ui/font_pixelify.h` documents why).

## The three adjustable faces (Settings > Display > Font)

`ui_theme.h` splits the UI's text into three levels — **Headers**, **Text**,
**Subtext** — and Settings > Display > Font sets the FACE and SIZE of each.
All five candidate faces are shipped, so the trade can be judged on the panel
instead of in a meeting; nothing is pruned on taste.

The shipped defaults are the faces the design was drawn with (and every
golden frame is shot with): Headers = Silkscreen 24, Text = VT323 24,
Subtext = baked Pixelify Sans 16.

Two of the newer faces are *runtime TTF loads* (tiny_ttf from this directory),
not baked tables: Pixelify Sans for sizes other than its 16 px design size,
plus Jersey 10 and DotGothic16. That keeps every size reachable without
baking a table per (face, size) pair.

**Flash cost per face** — the table reports both ways of shipping a face. The
TTF column is what this tree actually ships (one file, every size). The baked
columns are the equivalent LVGL bitmap font for printable ASCII (U+0020-007E)
at two sizes, measured on the ink bounding box of each glyph plus a 16-byte
descriptor per glyph and ~1 KiB of tables; 8bpp is what the shipped Pixelify
subtext uses, 1bpp is the small option §14.12 allows.

| Face | TTF (shipped) | baked 1bpp @16 | baked 1bpp @24 | baked 8bpp @16 | baked 8bpp @24 |
|---|---:|---:|---:|---:|---:|
| Silkscreen | 32,220 | 3,610 | 5,045 | 9,090 | 17,220 |
| VT323 | 153,116 | 3,425 | 4,424 | 7,292 | 12,060 |
| Pixelify Sans | 51,892 | 3,461 | 4,876 | 8,707 | 15,667 |
| Jersey 10 | 77,732 | 3,375 | 4,264 | 6,898 | 11,716 |
| DotGothic16 | 86,296 | 3,674 | 5,378 | 9,948 | 17,719 |

The S3 image does not carry an assets filesystem yet (the UI links into the
IDF build at R5, spec §16.5), so these are the costs the directory will add
when it does — reported per face now, as §14.12 asks, so the choice is made
with numbers rather than impressions.

So: a baked ASCII font costs 3-18 KiB per (face, size); the runtime TTFs cost
their file size once and give every size for free (the S3 keeps them in the
assets filesystem). DotGothic16 is subset to Latin-1 here (2,069,236 B →
86,296 B) because the full font also carries the Japanese syllabaries, which
this UI never draws — and a runtime TTF is read whole.

## Fallback chain (Latin stays pixel-styled)

`ui_shell_init()` loads one Hebrew instance and one emoji instance **per text
size** (13, 16, 18, 22, 24 px today) and chains them:

```
Silkscreen / VT323 / FA / Pixelify  ->  Droid Sans Hebrew @same px  ->  Noto Emoji @same px
```

LVGL resolves a glyph through `lv_font_t.fallback` only when the base face
reports it missing, so ASCII/Latin never leaves Silkscreen, VT323 or Pixelify.
The
chain is attached to every UI face; the two Font Awesome faces never carry
data text, but the uniform chain costs nothing per glyph. What each fallback
instance *does* cost is one tiny_ttf glyph cache
(`LV_TINY_TTF_CACHE_GLYPH_CNT`, 128 entries, plus a 128-entry bitmap cache,
both filled lazily) — 10 fallback instances today, see `ui-architecture.md`
§11. A glyph that no face in the chain has makes tiny_ttf log
`cache not allocated` (LVGL's wording for "not in this face"; the next face
answers) — expected noise, a few hundred lines on the chat list.

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
