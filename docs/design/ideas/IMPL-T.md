# IMPL-T: theme ideas T2–T5 as built

Branch `feature/ideas-t`. Spec: `docs/design/ideas/T-themes.md`. T1 (high contrast and colour-blind themes, status shapes, Reduce motion setting, Windows contrast themes) is **not** built, as the user decided. The shared colour math that T1 would also use is in place.

## Shared: colour math (libs/ui)

- `libs/ui/include/agentdesk/ui/color_math.hpp`, `src/color_math.cpp`: 0xRRGGBB arithmetic with no ImGui: sRGB ↔ linear, WCAG relative luminance and `contrast()`, `mix()`, `over()` (alpha compositing), `best_on()`, OKLab / OKLCh, `shift_toward_hue()` (keeps L), `turn_hue_toward()` (short way, capped), CIELAB and `delta_e2000()`, Machado 2009 `simulate(c, kProtan|kDeutan|kTritan)` in linear sRGB.
- `check_palette()` / `palette_problems()`: text (foreground, muted, dim on canvas/chrome/surface, foreground on selected, terminal text on terminal background; 4.5:1), graphics (accent on canvas/chrome/surface, the four status colours on chrome/surface; 3:1), and blocked/working/done pairwise CIEDE2000 under normal, protan, deutan and tritan vision (flagged under 10). Fixed order, so two checks line up index by index.
- `tint()` / `tint_protected()` for T3 (below).
- `libs/ui/include/agentdesk/ui/theme_file.hpp`, `src/theme_file.cpp`: theme file parse and write (unknown keys kept), the interface-colour derivation that used to live in `themes.cpp` (`derive_base`, `derive`, same float arithmetic, pinned by a test), `check_colors()`, libvterm's default ANSI table, theme name rules, and the shipped layouts' suggested pairs.
- `themes.cpp` now converts `ui::theme::Derived` to `Palette`; behaviour of existing themes is unchanged (test `ThemeFile.DeriveMatchesTheExistingPresetColours`).

## T2 Appearance by schedule — built

- Settings › Look › Theme › Appearance gets a fourth choice, **Schedule**. Under it: Switch by (Fixed times / Sunrise, sunset; the sun choice is greyed until a latitude is entered), Times (Light from / Dark from, `HH:MM`, Up/Down step 15 minutes, a bad time gets a red border and is not kept), Place (latitude, longitude, one decimal place), Shift (−120…120 minutes, sun mode only), and a preview line: `Today: light 05:05 → dark 18:46 · now Light`, or why the schedule is not in use (equal times → system setting; layout forces a theme; no latitude).
- Pure logic `libs/ui/.../schedule.hpp` (`evaluate`, `sun_times_utc` with the NOAA formulas, `parse_hhmm`, `format_hhmm`, `round_tenth`). Desktop glue `apps/desktop/appearance.cpp`: local date/time and offsets from `GetLocalTime` + `GetTimeZoneInformation`; result cached for 1 s; the app already wakes at least once a second, so a switch lands within a second.
- No network, no location API. **Deviation:** the optional "Use my location once" button is not built (it needs WinRT `Windows.Devices.Geolocation`, a new build dependency); latitude/longitude are typed in only.
- `appearance.toggle`: outside schedule mode flips appearance between light and dark; in schedule mode shows the other look until the next switch (`State::schedule_override_until`, in memory only); running it again ends the override early.
- Test hook: environment variable `AGENTDESK_NOW="YYYY-MM-DDTHH:MM"` replaces the schedule's local date and time. **Deviation:** the spec proposed a `screenshot_ui --now` flag; the environment variable needs no new plumbing and is inherited by agent-ui.
- `--input` gained `wheel:N` (scroll at the last mouse position) so screenshots can reach rows below the fold.

## T4 Suggested themes — built (suggest only)

- As decided: nothing changes the person's look by itself. `layout_theme_pair()` is unchanged (spec step 2 is **not** applied); the suggestion is only a row in Settings › Look › Theme with a **Use** button, plus a small accent star on the suggested theme cards.
- Suggested pairs: the layout file's new optional key `suggested_themes: {"dark", "light"}` (read in `layout.cpp`'s look fields, written back on clone), else the built-in table (workspace → midnight/paper, terminals → preset-graphite/preset-paper, mac → mac-dusk/mac-dawn). `LayoutSpec::suggested`.
- The row hides when the layout's pair already equals the suggestion, when the layout file forces a `theme`, or when a suggested id is missing (then `g_settings_problem` reports it once per layout).
- `theme.suggested` writes the suggestion into `layout_themes[<layout>]`.
- Layout cards in Settings now draw their thumbnail in the theme that layout shows with (`layout_palette()`).
- New theme files `assets/themes/mac-dusk.json` ("Dusk": deep indigo, violet accent, orange info so the Mac wallpaper runs violet → dusk orange) and `mac-dawn.json` ("Dawn": warm cream, indigo accent, sunrise-orange info). Own colours. `apps/desktop/CMakeLists.txt` copies `assets/themes` next to the exe (into `themes\`); telemetry counts them as shipped. Both pass the full palette check (text ≥ 4.5:1 on canvas/chrome/surface, accent and status ≥ 3:1, status colours apart under all three CVD simulations), asserted by `ThemeFile.MacDuskAndDawnPassThePaletteCheck`.
- Theme files may now set an optional `"name"` (card label; older versions ignore it) and `colors.info` (default still = accent).

## T3 Tint by agent state — built

- Settings › Look › Theme: **Tint by agent state** (Off / Subtle; default Off) and, when Subtle, **Also when done**. Commands `theme.ambient:off|subtle`.
- Target: the most urgent `Attention` over every session (other machines included; lost sessions report none): needs you → the `needs_you` hue at strength 1; with "also when done", done → the `done` hue at 0.5.
- `libs/ui/.../ambient.hpp` `AmbientFade`: 1200 ms in, 2500 ms out for the full range (scaled by distance), smoothstep, at least 2 s held, reversals continue from the current strength. `color::tint()`: canvas, chrome, surface (and hover/selected when opaque) move by up to 0.010 OKLab chroma toward the hue with L kept; the accent turns ≤ 12° the short way. `tint_protected()` lowers k by bisection (6 steps) until no checked pair falls below min(before, AA). Text, status colours, border, terminal colours and ANSI never change.
- `apps/desktop/ambient.cpp` keeps a tinted copy `g_ambient_pal`; strength quantised to 64 steps; `apply_style()` split into sizes + `apply_style_colors()` so a fade only resets ImGui colours. `next_wakeup_ms()` ≤ 33 ms while fading. `--report` gains `ambient_k`.
- Off while the theme editor is open and while a Windows contrast theme is on. **Deviation:** T1's `reduce_motion` setting does not exist, so "reduce motion" is read straight from Windows (`SPI_GETCLIENTAREAANIMATION`, checked every 2 s): when animation effects are off the strength jumps to the target. Returning from the background (>500 ms without frames) also jumps instead of replaying the fade.
- Observed: Midnight sidebar `#101521` → `#151320` at full strength, terminal unchanged, back to `#101521` after the fade. On Paper the protection keeps the change tiny (`#e2e2dc` → `#e3e2db`) because Paper already has pairs below AA (working on chrome 2.53, dim 2.26…) that may not drop further. That is the spec's rule working; on light themes the tint is barely visible.

## T5 Theme editor — built

- `apps/desktop/theme_editor.cpp`, a modal (880 × 620 design px). Opens from: a theme card's right-click menu (**Edit a copy…** on any theme; **Edit…** and **Delete…** on the person's own files in `<data>\themes`), a **New theme** card (dashed) at the end of the Dark and Light card rows (copy of the layout's current theme of that kind), and commands `theme.new[:<id>]`, `theme.edit[:<id>]`, `theme.folder`, `theme.delete:<id>`.
- Left: Base (background, terminal text, selection, cursor), Interface (canvas, chrome, surface, text, muted, dim, accent, info, border, hover, selected), Status (needs you, working, done, idle), 16 terminal colours. Each row: swatch (click → `ColorPicker3`, or `ColorPicker4` with alpha for border/hover/selected), hex field (applies when it parses; red border otherwise), contrast against its usual background with a check or warning icon, and `auto` (derived, not in the file) or a reset button back to the derived value.
- Right: a small sample (sidebar with the four states, tabs, terminal with ANSI words, selection and cursor), **Simulate colour vision** (None / Protan / Deutan / Tritan, sample only), and **Checks** (`palette_problems()`, plus a note when the theme switches between dark and light).
- The whole window previews the draft (the editor's palette replaces the base palette in `update_palette()`); Cancel/Close returns to the previous theme; Revert goes back to the opened/last saved state.
- Save: `<data>\themes\<name>.json` via temp file + `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`; only overrides go in `colors`; unknown keys and unknown colour names are kept; then `reload_theme()` updates the loaded entry in place (or adds one) and `theme:<name>` applies it. The editor stays open with "Saved as themes\<name>.json". Questions shown in a bar above the buttons: replace an existing file of that name, the file changed outside since opening (Overwrite / Load again), terminal text under 3:1, discard unsaved changes on Close/Esc. Ctrl+S saves. Name rules: empty, > 64 chars, `<>:"/\|?*`, reserved device names, leading space or trailing dot/space, built-in ids (`midnight`, `ember`, `moss`, `paper`, `hc-dark`, `hc-light`, `preset-*`), endings `-graphite`/`-ink`/`-moss`/`-paper` → Save disabled with the reason.
- Copies: theme files copy their file; presets copy their values with their status colours written as overrides; AgentDesk's own palettes write every colour as an override and fill ANSI with libvterm's table.
- Delete sets the file aside as `<name>.json.old-<unix time>`, drops it from the lists, and points any `theme_dark/light` or `layout_themes` entry that used it back to the defaults.
- **Deviations:** "Save as…" is not a separate button: change the Name field and Save (a new name writes a new file; the old one stays). The "＋ New theme" first-run picker (spec "empty" state) is replaced by the New theme card, which always starts from the current theme. Opening the editor (or the delete confirmation) from Settings closes Settings; closing the editor reopens Settings on Look (the confirmation does not).
- Copying an AgentDesk palette: the file's `selection` cannot hold alpha, so the copy shows selection at the theme-file alpha (0x90 dark / 0xa0 light) instead of the original.

## New commands, settings keys, files

Commands (all in `ui::is_builtin_command` and in the palette): `appearance:system|light|dark|schedule`, `appearance.toggle`, `theme.suggested`, `theme.ambient:off|subtle`, `theme.new[:id]`, `theme.edit[:id]`, `theme.folder`, `theme.delete:id` (internal, used by the card menu). No default shortcuts.

ui.json keys: `appearance` accepts `"schedule"`; `appearance_schedule` `{mode, light_at "HH:MM", dark_at, latitude|null, longitude|null, sun_offset}`; `ambient` (`"off"`/`"subtle"`); `ambient_done` (bool). Invalid values fall back to defaults; equal light/dark times fall back to 07:00/19:00.

Layout file key: `suggested_themes: {"dark": id, "light": id}`. Theme file keys: optional `name`, `colors.info`.

Files added: `libs/ui/include/agentdesk/ui/{color_math,schedule,theme_file,ambient}.hpp`, `libs/ui/src/{color_math,schedule,theme_file,ambient}.cpp`, `apps/desktop/{appearance,theme_commands,theme_suggest,ambient,theme_editor}.cpp`, `assets/themes/mac-{dusk,dawn}.json`, `tests/unit/{color_math,schedule,theme_file,ambient}_test.cpp`.

Shared files changed (small, additive): `app_internal.hpp` (includes, Settings fields, State override fields, `LayoutSpec::suggested`, theme declarations, kCommands rows, one declaration block at the end), `frame.cpp` (palette choice moved into `update_palette()`, `draw_theme_editor()` in `draw_dialogs`, `wheel:` input step), `commands.cpp` (one hand-over to `exec_theme_command`), `settings_dialog.cpp` (Appearance row, schedule/ambient rows, suggestion row, card menu, star, New theme card, layout thumbnails), `settings_store.cpp` (new keys), `themes.cpp` (derivation via libs/ui, reload/forget, `apply_style_colors`), `layouts.cpp`, `app.cpp` (wake-up, report, shipped themes), `libs/ui/src/layout.cpp`, `command_names.cpp`, CMakeLists (three), `tools/qa/screenshot_ui.cpp` (doc line).

## Proposed user-guide text (Traditional Chinese)

### 依時間切換淺色與深色

設定 › Look › Theme 的 Appearance 選 **Schedule**，AgentDesk 就會自己換淺色和深色。

- **Fixed times**：填淺色開始和深色開始的時間（例如 07:00 與 19:00）。欄位裡按上下鍵，一次調 15 分鐘。夜班可以把淺色時間設得比深色晚，例如 22:00 到 06:00。
- **Sunrise, sunset**：先在 Place 填緯度（必填）和經度（可不填，不填就用時區的標準經線推算）。AgentDesk 不連網，也不讀 Windows 定位，位置只存到小數一位。Shift 可以把日出、日落整體提早或延後，最多兩小時。
- 下方那一行會寫出今天幾點換色、現在是哪一種。
- 想暫時換成另一種，執行命令「Toggle light / dark」（`appearance.toggle`），到下一個切換時間就回到排程。

### 版面的建議主題

有些版面附了配好的深淺主題，例如 AgentDesk Mac 配 Dusk 與 Dawn。如果你現在用的不是那一組，設定 › Look › Theme 會出現一列「Made for …」，按 **Use** 才會換。AgentDesk 不會自己改你的主題。建議的主題卡右上角有一顆小星星。

### 跟著 Agent 狀態變色

把 **Tint by agent state** 設成 Subtle 之後，只要有 Agent 需要你，整個介面的底色會稍微偏暖，強調色也跟著偏一點。終端機裡的顏色、文字和狀態色都不變。打開 **Also when done**，有完成但還沒看的 Agent 時會帶一點冷色，強度減半。變色前會先檢查對比，所以淺色主題幾乎看不出變化。Windows 關掉動畫效果時直接換色，不漸變。

### 主題編輯器

在主題卡上按右鍵選 **Edit a copy…**，或按每排最後的 **New theme**，就會打開編輯器。改任何顏色，整個視窗馬上跟著變。

- 每個顏色旁邊是它在主要背景上的對比：打勾表示夠清楚，三角形表示偏低。標著 `auto` 的顏色是從背景推算的，改了才會寫進檔案；旁邊的復原鈕可以改回推算值。
- 右邊的範例可以切換成紅色盲、綠色盲、藍色盲看到的樣子（只影響範例）。Checks 列出對比不足和難以分辨的狀態色。對比低一樣可以存，只有終端機文字低於 3:1 時會多問一次。
- 按 **Save**（或 Ctrl+S）存成 `%LOCALAPPDATA%\AgentDesk\themes\<名稱>.json`，存完立刻套用，不用重開。檔案格式和手寫的主題檔一樣。
- 自己的主題卡右鍵還有 **Edit…** 和 **Delete…**。刪除不會真的刪檔，而是改名成 `.json.old-<時間>`；用到它的版面會回到預設主題。

## Tests

Before: 297. After: 332 (35 new), all passing; `CoreUpdate.AgentctlUpdateHandsTheTerminalsToTheRelease` failed once in the full run with "Access is denied" on a temp `.old` file and passed when rerun alone (known flaky under load).

New tests: `ColorMath.{ContrastKnownValues, MixOverAndBestOn, OkLabRoundTripIsExact, ShiftTowardHueKeepsLightness, HueTurnsTheShortWayAndIsCapped, DeltaE2000SharmaData (all 34 Sharma pairs, ±0.0001), LabOfWhiteAndBlack, SimulationMatchesMachado}`, `PaletteCheck.MidnightValuesAndProblems`, `AmbientTint.{KeepsLightnessAndCapsTheAccentTurn, ProtectionLowersKOnALowContrastPalette}`, `AmbientFade.{EasesInToFullStrengthIn1200Ms, ShortBlockStillRisesHoldsThenFalls, ReversalCarriesOnFromTheCurrentStrength, HalfStrengthForDoneAndInstantMode}`, `Schedule.{HhMmParsingAndFormatting, FixedDaySpan, FixedNightShiftWrapsPastMidnight, EqualTimesAreInvalid, TaipeiSolsticesMatchNoaa, MissingLongitudeUsesTheZoneMeridian, EquatorSydneyAndOffset, PolarDayAndNight, BerlinDaylightSavingChangeDays, DayOfYear}`, `ThemeCommands.NamesAreBuiltIn`, `ThemeFile.{HexParsing, ParseRejectsWrongShapes, WriteKeepsUnknownKeysAndOnlyOverrides, DeriveMatchesTheExistingPresetColours, OverridesAndInfo, NameRules, BuiltinSuggestions, MacDuskAndDawnPassThePaletteCheck}`, `Layout.SuggestedThemesAreReadAndWrittenBack`.

Taipei solstice values are checked against the spec's NOAA figures (05:05/18:47, 06:35/17:12, ±3 min); the Berlin October and equator expectations are this formula's own values (±6 min), not checked against an external calculator.

## Open issues

1. T3 strength (0.010 chroma, 12°) needs the user's subjective tuning; on light themes the contrast guard makes it nearly invisible.
2. Spec T1 3.4 status shapes are not built, so the editor's CVD hint does not suggest turning them on.
3. Other AgentDesk windows sharing the data folder do not reload a saved theme until restart (spec T5 edge case 2, still open).
4. Deleting a user theme that shadowed a shipped one of the same name hides both until restart.
5. Spec T2 acceptance "NOAA ±3 min" relies on the spec's reference values; a manual check against NOAA's online calculator is still advisable.
6. "Use my location once" (WinRT) not built; ask the user whether to add the dependency.

## Merge notes

Merged into `feature/ideas-merge-tb` (from `feature/ideas`, which already had A1–A4), then B1–B3 merged on top. What changed for the T items:

1. **One theme folder.** AgentDesk's own theme files now all live in `libs/ui/themes/` (beside `libs/ui/layouts/`): `mac-dusk.json` and `mac-dawn.json` moved there from `assets/themes/`, next to B's `phosphor-green`, `phosphor-amber`, `phosphor-paper`, `cockpit-night`, `cockpit-day`. `assets/` is gone. `apps/desktop/CMakeLists.txt` has one copy rule (`libs/ui/themes` → `themes\`), and the unit tests' `AGENTDESK_OWN_THEMES_DIR` points at the new folder. Telemetry's shipped theme list names all eight files. B's five files gained a `"name"` ("Phosphor Green", "Cockpit Night", …) so their cards and the suggestion row show proper names.
2. **`themes.cpp`.** T's structure stays (`OwnedPalette`, `fill_palette`, `load_theme_files`, `reload_theme`, `apply_style` / `apply_style_colors`, derivation in `libs/ui` `theme_file`). B's rewrite of `add_preset_palette` was dropped: `colors.info` already works through `ui::theme::derive`. B's `theme_id` fix is kept: an id that names a loaded theme exactly (`phosphor-paper`) is that theme, not an old spelling of preset `paper`.
3. **Suggested themes never change the look.** B's automatic write of a style layout's themes on first show (`style_frame_upkeep`, `StyleUi::themed`, `ui::styles::suggested_themes`) is removed. Classic (Phosphor Green / Phosphor Paper) and Cockpit (Cockpit Night / Cockpit Day) are registered in T4's built-in table (`ui::theme::builtin_suggestion`), so they appear as the "Made for …" row with **Use** and as stars on the cards, exactly like Mac's Dusk / Dawn. Phosphor Amber ships as an extra theme, not as part of a pair. Tiling, Mission Control and Review have no suggestion.
4. **Layout cards.** T's rule (each card drawn in the theme that layout shows with, `layout_palette`) wraps `layout_preview`, which first asks B's `style_layout_preview` (Tiling, Cockpit, Classic) and otherwise draws the built-in previews (workspace, Terminals, Mac, Mission Control, Review). All eight cards render.
5. **Frame order.** `App::frame()` calls only `update_palette()` (T); B's upkeep call before the palette is gone with the auto-apply.
6. **Lists.** `kCommands`, `command_names.cpp`, Settings fields, `settings_store.cpp` keys, CMake sources and test lists are the union of A, T and B. `next_wakeup_ms()` keeps every wake-up (Timeline, Focus, Mission, a style bar's clock via `ui::styles::has_clock`, the ambient fade). `--report` keeps `ambient_k`.
7. **Screenshot input.** One parser in `frame.cpp`: A's `key:` with modifiers and F1–F12, T's `wheel:N`; `AGENTDESK_NOW` and `--layout` / `--appearance` work as before.
8. **New test:** `ThemeFile.SuggestedThemeFilesShip` (every suggested theme that is not a built-in palette or preset is a shipped file that parses, dark first, light second); `ThemeFile.BuiltinSuggestions` covers Classic, Cockpit and Tiling (none).

After both merges: 420 tests in the Debug build, all passing.
