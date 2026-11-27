# ui/ — the LVGL phone shell

The whole UI lives here. Conventions below are binding; design authority: `../ui-architecture.md` (§10 add-a-page checklist · §5 theme layer · §6 navigation flow).

- **One renderer.** Every list page is a `menu_t` on `ui_menu.c`'s single renderer; `ui_row_style()` is the one shared row paint. New list page = new module + registry row, never a second renderer.
- **Page modules.** One `ui_<page>.c/.h` per page; it owns its build/keymap/options and reads data from `model/`, never stores a copy.
- **Page registry.** `ui_pages.h` (`ui_page_t`) + the `ui_pages[]` table in `ui_shell.c`: one row per page (name, id, screen, callbacks). A page not in the table cannot be reached; the list pool is sized from its length (spec §16.3).
- **Keymaps.** Per-page `keymap_t`; softkey-cap labels come from `ui_keymap.c`'s one action-label table — an unbound action leaves no cap.
- **Palette roles only.** No colour literals in this tree — palette roles from `ui_theme.c` (DARK/LIGHT); `just check-colours` enforces it. Metrics/fonts are tokens in `ui_theme.h`; geometry derives from `ui_geometry.c`.
- **Text.** Every string through `ui_text_set()` (data text RTL, chrome LTR — spec §5).

`ui_shell.c` = frame + screen stack + key dispatch · `ui_input.c` = HAL event pump · `ui_sidebar.c` = SHOULDER-L options overlay.
