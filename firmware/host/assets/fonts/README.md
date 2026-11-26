# Bundled fonts (SIL Open Font License 1.1 unless noted)

Downloaded once and committed so the simulator renders identically offline.

| File | Source | License |
|---|---|---|
| `Silkscreen-Regular.ttf` | google/fonts `ofl/silkscreen` | SIL OFL 1.1 |
| `VT323-Regular.ttf` | google/fonts `ofl/vt323` | SIL OFL 1.1 |
| `FA-Solid.ttf` | FortAwesome/Font-Awesome 6.5.2 `webfonts/fa-solid-900.ttf` | SIL OFL 1.1 (font), CC BY 4.0 (icons) |

Font Awesome codepoints used by the shell are written as C11 `\uXXXX`
escapes in `ui/ui_shell.c`; the authoritative name→codepoint table is
`metadata/icons.json` in the Font Awesome repository (6.5.2).
