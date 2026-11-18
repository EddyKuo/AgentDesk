# herdr-app settings, fonts, keys, programs and shell folder tracking: behaviour spec for porting

Sources (Apache-2.0), read on 2026-11-18. This describes observed behaviour; it is not copied code. Snippets are short and only where the exact value matters.

- https://github.com/herdrdev/herdr-app at `321c9a84bc928f1c31be59e73dad966d86a3e252`
- https://github.com/herdrdev/herdr-core at `c7517808d64f06609e4ecf0db1757005373cc3a3` (the background process; the Windows folder tracking lives here, not in the app)

File abbreviations:

| Abbrev. | Path |
|---|---|
| `settings.rs` | `src/settings.rs` (1090 lines) |
| `settings.js` | `defaults/frame/settings/main.js` (993 lines) |
| `settings.test.ts` | `scripts/settings.test.ts` |
| `keys.rs` | `src/keys.rs` (437 lines) |
| `keys.json` | `defaults/keys.json` (Windows/Linux defaults, 74 lines) |
| `keys.macos.json` | `defaults/keys.macos.json` |
| `commands.rs` | `src/commands.rs` (every command name) |
| `programs.rs` | `src/programs.rs` (517 lines) |
| `programs.json` | `defaults/programs.json` (131 lines) |
| `window.rs` | `src/window.rs` (7623 lines) |
| `model.rs` | `src/model.rs` |
| `terminal.rs` | `src/terminal.rs` (cell metrics, glyph drawing) |
| `layout.rs` | `src/layout.rs` (`Look`: per-layout look fields) |
| `process_cwd.rs` | `src/process_cwd.rs` |
| `sound.rs`, `persist.rs`, `main.rs`, `mods.rs` | `src/...` |
| `welcome.js` | `defaults/frame/welcome/main.js` (562 lines) |
| `pane.js`, `tabs.js` | `defaults/mods/pane/main.js`, `defaults/mods/tabs/main.js` |
| `AGENTS.md` | `defaults/AGENTS.md` (the guide written into the home folder) |
| `core:foreground.rs` | herdr-core `crates/herdr-core/src/foreground.rs` |
| `core:windows_foreground.rs` | herdr-core `crates/herdr-core/src/platform/windows_foreground.rs` |
| `core:windows_exec.rs` | herdr-core `crates/herdr-core/src/platform/windows_exec.rs` |

---

## 0. Where configuration lives

The home folder is `$HERDR_APP_HOME`, else `$XDG_CONFIG_HOME/herdr-app`, else `~/.config/herdr-app` (also on Windows: `%USERPROFILE%\.config\herdr-app`) (`mods.rs:62-65`).

| File | Content | Written by | Reloaded |
|---|---|---|---|
| `settings.json` | the person's preferences (section 1) | app (zoom keys, first run), Settings window, the person or their agent | live |
| `keys.json` | key bindings (section 3) | installed from defaults when missing | live |
| `programs.json` | overrides on top of the built-in program table (section 4) | the person only | live |
| `themes/<name>.json` | extra themes; shipped `atom-one-dark`, `win98` are (re)installed | app | read on each apply |
| `layouts/<name>.json` + its state file | per-layout look (fonts, theme, padding) and clicked theme | app/person | live |
| `fonts/`, `mods/*/fonts/` | extra `.ttf/.otf/.ttc` the app loads | person/mods | loaded when added (`settings.rs:836-864`) |
| `shared.json` | mod-shared state, including `frame.welcomed` and `workspace.panels` | app | — |
| `settings.error`, `keys.error`, `programs.error` | why a file was not applied; removed once it applies | app (`window.rs:909-912`, `951-993`) | — |

Reload mechanics: a file watcher with an 80 ms debounce calls `reload_config`; a 400 ms poll also calls it, every tick if there is no watcher, else every 25th tick (~10 s) as a safety net (`window.rs:643-663`, `4153-4162`). Change detection is by mtime (`window.rs:951-993`). All app writes use write-to-temp-then-rename and keep the old file's permissions, following symlinks (`persist.rs:24-48`).

Precedence for look-related values: **the shown layout's look > settings.json > built-in default** (`settings.rs:589-598`, test at `settings.rs:973-994`). The Settings window shows the hint "This layout sets its own; yours shows in the others." when the layout overrides a value (`settings.js:497`, `507`, `547`, `559`).

---

## 1. Every setting

### 1.1 settings.json keys (`Person`, `settings.rs:366-482`)

Unknown keys are kept: app writes remove and rewrite only the keys the app owns, so mod keys such as `worktrees` survive (`settings.rs:738-751`, test `920-937`). Keys whose value equals the default are omitted on write where marked "omitted when default".

| Key | Type | Default | Range / values | Settings UI | Effect |
|---|---|---|---|---|---|
| `zoom` | number | `1.0` (first run may pick another, see 1.4) | 0.5..3.0; outside the range the whole file is rejected (`settings.rs:680-681`, `710-712`) | Look › Size and fonts › "Zoom" stepper (−/+ shows `NN%`) + "Reset" button | Scales everything: terminal font, UI rem size, every layout px size (`settings.rs:816-827`, `window.rs:5284`) |
| `sounds` | object `{enabled: bool, done?: string, blocked?: string}` | `{enabled: true}` | `done`/`blocked`: MP3 path, absolute or relative to home | Alerts › Sounds | Section 6 |
| `core` | string? | none (omitted) | path | not shown | herdr-core binary to use; read at start |
| `font` | string? | none → `"JetBrains Mono"` | any installed family | Look › "Terminal font" picker (shows family in itself; list with search box; first row "Default (JetBrains Mono)") | Terminal font family. If the machine lacks the family, the bundled default is used instead, because platform fallback would be proportional (`settings.rs:874-882`) |
| `font_size` | number? | none → `14` | valid 6..72 (else a problem is reported, `settings.rs:664-666`); the UI stepper clamps to 8..32, step 1 (`settings.js:498`) | Look › "Terminal font" row, stepper right of the picker | Terminal font px at zoom 1 |
| `font_weight` | integer? | none → `500` | 100..900 (problem outside, `settings.rs:661-663`); UI step 100 | Look › "Terminal font weight" (about: "Bold text stays at 700.") | Regular cell weight; bold cells always 700 (`terminal.rs:1769-1776`) |
| `font_ligatures` | bool | `false` | — | not shown | Enables `calt`/`liga` (never `dlig`) in terminal text (`terminal.rs:1762-1767`) |
| `terminal_line_height` | number | `1.0` | clamped 0.8..2.0 (`settings.rs:595`); UI step 0.1, shown as % | Look › "Terminal line height" | Cell height multiplier |
| `terminal_cell_width` | number | `1.0` | clamped 0.8..2.0; UI step 0.05, shown as % | Look › "Terminal cell width" | Cell width multiplier |
| `terminal_scroll_speed` | number | `1.0` | clamped 0.1..5.0; UI step 0.1, shown as % | Look › "Terminal scroll speed" | Wheel/trackpad multiplier |
| `ui_font` | string? | none → `"Geist"` | any family | Look › "Interface font" picker (no size stepper) | Font of panels/mods (`window.rs:5332`) |
| `telemetry` | bool | `true` (omitted when true) | — | Privacy › "Share anonymous usage data" toggle; turning on deletes the key | Daily anonymous usage report |
| `clipboard_write` | bool | `true` (omitted when true) | — | not shown | OSC 52 writes allowed (reads never) |
| `copy_on_select` | bool | `true` (omitted when true) | — | not shown | Copy selection on mouse release |
| `pane_scrollbars` | bool | `true` (omitted when true) | — | not shown | Draggable scroll thumb at a terminal pane's right edge (only with scrollback, never on alt screen) |
| `minimum_contrast` | number | `1.0` | 1..21 (problem outside, `settings.rs:658-660`) | not shown | Text below this contrast ratio becomes near-white/near-black; 1 = off |
| `option_as_alt` | `"auto"\|"off"\|"on"\|"left"\|"right"` | `"auto"` (omitted) | — | Keyboard section (macOS only) | macOS Option key; no effect on Windows |
| `appearance` | `"light"\|"dark"` | absent = system | — | Look › Theme › "Appearance" segmented control: System / Light / Dark (System deletes the key) | Picks light or dark half of the theme pair |
| `theme` | string or `{light, dark}` | none | preset or `themes/<name>` | not directly: the theme picker writes the **layout**, not settings.json (see 1.3) | Theme where the layout sets none |
| `colors` | `{name: "#rrggbb[aa]"}` | `{}` | hex | not shown | Theme colour overrides |
| `worktrees` (mod-owned) | `{directory}` | `~/.herdr/worktrees` | path | not shown | New-worktree dialog target (`AGENTS.md:735-744`) |

Composed-only values (not in settings.json; from layout look or fixed): `ui_font_size` is always `13` (`settings.rs:556`, `599`), `terminal_padding` default `[10, 6]` px (main layout sets `[14, 12]`), `terminal_opacity` default 1 (0..1) (`settings.rs:564-565`, `607-608`). So the person cannot change UI text size except through `zoom`.

Validation: a parse error or out-of-range zoom rejects the whole file; the previous values stay applied and `settings.error` explains (`settings.rs:703-714`, `window.rs:954-962`). Other range problems (`font_size`, `font_weight`, `minimum_contrast`, unknown theme, bad colour) are reported but the file still applies (`settings.rs:644-668`, `window.rs:936-940`). If `settings.json` is missing at start it is written with defaults (`settings.rs:703-707`). Telemetry `false` is honoured even when the rest of the file is broken (`settings.rs:716-726`).

### 1.2 The Settings window (`settings.js`)

Opened by `app.settings` (`ctrl-,`), optional `section` arg (`settings.js:31-32`). Modal dialog, height 620 px, left nav 190 px wide with heading "Settings" and a "Send feedback" button at the bottom (`settings.js:886-955`). Escape closes the open font list first, else the window (`settings.js:937-954`).

Sections, in order (`settings.js:9-16`):

1. **Look** (palette icon)
   - "LAYOUT": cards for each `layouts/*.json`, shipped order `main` "Herdr", `terminals` "Terminals", `win98` "Herdr 98", then the person's own by name; click runs `layout.switch` (`settings.js:18`, `272-321`).
   - "THEME": "Appearance" row ("Light or dark, or as your system is now.") then note "For the layout you're in: each layout keeps its own light and dark theme." then "Dark" theme cards and "Light" theme cards; click runs `app.theme` (`settings.js:521-527`, `354-407`).
   - "SIZE AND FONTS": Zoom ("Everything: terminals, panels, text."), Terminal font + size, Terminal font weight, Terminal line height, Terminal cell width, Terminal scroll speed, Interface font (`settings.js:529-563`).
2. **Keyboard** (macOS only): Option key radio list (`settings.js:566-593`).
3. **Machines**: remote machines list (out of scope here).
4. **Alerts**: section 6.
5. **Updates**: app / herdr-core versions, "Check now", "What's new".
6. **Privacy**: telemetry toggle, "What we send" Show/Hide with the JSON reports.

Saving behaviour (must port): there is no Save button; every click writes immediately. Each write re-reads the file, applies the edit to the parsed object, and writes only if the file text is unchanged since the read (compare-and-swap), retrying up to 3 times. If the file does not parse or is not a JSON object, nothing is written and an error toast says "settings.json has errors: fix it first". Read failure → "Cannot read settings.json"; three failed writes → "Not saved to settings.json" (`settings.js:115-148`; tests in `settings.test.ts:38-76`). Output is `JSON.stringify(obj, null, 2) + "\n"`.

### 1.3 Theme storage

The theme picker does not write settings.json. `app.theme` puts `{light, dark}` on the shown layout's root look, replacing only the half that matches the picked theme's lightness, and saves it as a layout "click" (state file) (`window.rs:3620-3635`). Built-in presets: `graphite` (default dark), `ink`, `moss`, `paper` (default light) (`settings.rs:80-84`). A theme's lightness is by background luminance > 0.5 (`settings.rs:265-268`). Full colour derivation is in `settings.rs:306-364` (already covered by the Files/Changes spec).

### 1.4 Zoom

- Commands `app.zoom_in` / `app.zoom_out` / `app.zoom_reset` (`commands.rs:102-104`). Steps of 0.1, rounded to tenths, clamped 0.5..3.0; reset = 1.0 (`settings.rs:804-810`). The new value is written to `settings.json` (`window.rs:3636-3643`, `916-928`); if the file is broken the change applies for this run only.
- Applied as a global pixel scale; terminal px = `font_size * zoom`; UI rem = `13 * zoom` (`settings.rs:772`, `816-827`).
- First run (no settings.json at start, `main.rs:134`): zoom = Windows `HKCU\Software\Microsoft\Accessibility\TextScaleFactor` / 100 (clamped 1..2), times 1.15 if the primary display is ≥ 2400 logical px wide, rounded to tenths, clamped 0.5..3; written only if ≠ 1 (`settings.rs:778-802`, `main.rs:196-202`).

---

## 2. Fonts

### 2.1 Families (all bundled, loaded at start) (`settings.rs:884-901`, `main.rs:168-173`)

| Role | Family | Files |
|---|---|---|
| Terminal default | **JetBrains Mono** | `JetBrainsMono-Variable.ttf`, `-Italic-Variable.ttf` |
| UI default | **Geist** | `Geist.ttf` |
| Also bundled | Geist Mono | `GeistMono-Variable.ttf`, `-Italic-Variable.ttf` (not a default anywhere in the app code observed; available to pick) |
| Icons in terminals | **Symbols Nerd Font** | `SymbolsNerdFont-Regular.ttf` |

Then fonts in `<home>/fonts/` and `<home>/mods/*/fonts/` are added (`settings.rs:836-864`). The picker list is every family the text system knows, minus names starting with `.`, sorted case-insensitively, deduplicated; it shows at most 200 matches for the typed filter (`mods.rs:52-58`, `settings.js:436-438`). The list is not filtered to monospace.

Shipped layouts: `main` sets no font (so 14 px JetBrains Mono / Geist); `terminals` sets `font_size: 15`; `win98` sets `PxPlus IBM VGA 8x16` (`settings.rs:1062-1073`).

### 2.2 Default sizes

| | Default | Where |
|---|---|---|
| Terminal font size | 14 px at zoom 1 | `settings.rs:549` |
| Terminal weight | 500, bold 700 | `settings.rs:550`, `terminal.rs:1773` |
| UI font size | 13 px at zoom 1 (rem size of the window) | `settings.rs:556`, `window.rs:5284` |
| Terminal padding | [10, 6] px default; main layout [14, 12] | `settings.rs:564`, `settings.rs:1065` |

### 2.3 Changing size

- Zoom: keys `ctrl-=`, `ctrl-+` (in), `ctrl--` (out), `ctrl-0` (reset); Settings zoom stepper; welcome page 1 "Size" stepper. Affects terminal and UI together.
- Terminal font size only: Settings stepper (8..32) or `font_size` in settings.json / layout. No key binding changes terminal font size alone.
- UI font size alone: not possible (fixed 13 × zoom).

### 2.4 Cell metrics (`terminal.rs:1796-1925`), modelled on Ghostty

- Pixel size `em = round(font_size * zoom * dpi_scale)` (not rounded on macOS).
- Vertical metrics from the font tables in Ghostty's order: if OS/2 `USE_TYPO_METRICS` → typo ascender/descender/lineGap; else hhea if non-zero; else OS/2 typo if non-zero; else OS/2 win ascent/descent with gap 0.
- `face_height = (ascent − descent + gap) × em/unitsPerEm`; cell height = `round(face_height)`; baseline centred with the half gap; then the line-height multiplier: `adjusted = round(height × multiplier)`, the extra pixels split top/bottom, the odd pixel on the side that best centres the face (`terminal.rs:1886-1893`).
- Cell width = `round(max advance of ' '..'~')`, then `round(width × (1 + percent/100))` computed in f64 for the multiplier (`terminal.rs:1863-1885`).
- Glyph x offset on Windows/Linux: `round((cell − face)/2)` only if the cell is wider, else 0 (`terminal.rs:1900-1915`).
- Underline/strike positions and thickness from `post` / `OS/2`, fallback to x-height based values.
- Metrics cached per (font id, size, scale, line height, cell width).

### 2.5 Fallback order

1. The chosen terminal family; if not installed, **JetBrains Mono** (whole-font swap, `settings.rs:874-882`).
2. Nerd Font private-use icons (U+E000–U+F8FF, U+F0000–U+10FFFD) that the primary font lacks are drawn directly from the bundled Symbols Nerd Font file, scaled to fit their outline (not through shaping, so an old installed Nerd Font cannot substitute a wrong glyph) (`terminal.rs:1564-1603`, `1987-2004`).
3. Other missing characters go through the platform text system's fallback; a fallback glyph wider than its cell (non-ASCII, non-wide cells) is shrunk to fit (`terminal.rs:1927-1941`).
4. Box drawing / block / Powerline characters are drawn as sprites, not font glyphs (`terminal.rs:2023`, `src/sprites.rs`).

Unclear: the exact platform fallback chain on Windows (DirectWrite system fallback is implied by GPUI; no explicit list such as Segoe UI Emoji is configured in herdr-app). A Noto Emoji font is referenced only in a test (`terminal.rs:3826`).

Ligature rule: with the default font and ligatures on, plain ASCII runs without two adjacent punctuation chars are shaped without ligatures (`terminal.rs:1778-1785`).

---

## 3. Keys

### 3.1 File format and chord syntax (`keys.rs:1-17`, `AGENTS.md:508-560`)

```json
{ "global": { "ctrl-shift-t": "tab.new" },
  "terminal": { "ctrl-left": "send:\u001bb", "ctrl-1": "terminal" },
  "layout:terminals": { ... }, "mod:editor": { ... } }
```

- Chord: modifiers then key, joined by `-`: `ctrl`, `alt`, `shift`, `cmd` (macOS), e.g. `ctrl-shift-pagedown`, `alt-]`, `f2`, `shift-f2`. Key names are GPUI keystroke names (`pagedown`, `pageup`, `tab`, `backspace`, `insert`, `left`, `enter`, `escape`, punctuation literally: `ctrl-,`, `ctrl--`, `ctrl-=`). Shifted punctuation can be written either way (`ctrl-shift-/` and `ctrl-?`); the defaults bind both for help (`keys.json:45-47`).
- Sections: `global` (window), `terminal` (a terminal has focus), `layout:<name>` (while that layout shows), `mod:<name>` (that mod has focus). More specific wins: `terminal` > `layout:` > `global` (`keys.rs:115-130`).
- Action forms: a command name; `name:value` where value fills the command's first argument (a number fills `index`), kept literal even if it looks like JSON (`keys.rs:415-429`); an object `{"command": ..., "args": {...}}` for typed arguments (`keys.rs:59-83`); `send:<text>` types the text into the focused terminal; `<mod>.<command>` goes to that mod; `terminal` unbinds the key in that section so the program gets it (in `global` it is simply skipped) (`keys.rs:228-243`).
- Validation: unknown section or action → problem list written to `keys.error`; on the very first load failure no user keys are installed (`keys.rs:191-252`, `window.rs:966-981`). Bad object bindings stay bound and show an error toast when pressed instead of passing the key to the program (`keys.rs:394-413`).
- Reserved: `ctrl-alt-shift-escape` → `app.reset` (escape hatch: set the layout and keys aside, back to defaults); cannot be rebound. `escape` → `overlay.close` inside overlays (`keys.rs:24-25`, `259-272`).
- Install / upgrade: missing file → defaults written. If the file is byte-identical to a previously shipped default, it is replaced by the new defaults; any edit (even whitespace) keeps it (`keys.rs:166-189`).
- Old names are migrated at load: `project.*` → `space.*`, `context.*` → `tab.*`, `mod.open:start` → `tab.new` (`keys.rs:132-149`).
- Plain `ctrl+letter` is never bound by default; it belongs to the program. `ctrl+alt` is AltGr on Windows (`AGENTS.md:530-535`).

### 3.2 Default bindings, Windows/Linux (`keys.json`)

**global** (lines 2-62):

| Keys | Action |
|---|---|
| `alt-1` … `alt-9` | `space.select:1` … `:9` |
| `alt-h` / `alt-j` / `alt-k` / `alt-l` | `pane.go:left` / `down` / `up` / `right` |
| `alt-left` / `alt-right` | `pane.go:left` / `right` |
| `alt-shift-h` / `-j` / `-k` / `-l` | `pane.swap:left` / `down` / `up` / `right` |
| `ctrl-+`, `ctrl-=` | `app.zoom_in` |
| `ctrl--` | `app.zoom_out` |
| `ctrl-0` | `app.zoom_reset` |
| `ctrl-1` … `ctrl-9` | `tab.select:1` … `:9` |
| `ctrl-pagedown`, `ctrl-tab`, `alt-]` | `tab.next` |
| `ctrl-pageup`, `ctrl-shift-tab`, `alt-[` | `tab.prev` |
| `ctrl-shift-pagedown` | `space.next` |
| `ctrl-shift-pageup` | `space.prev` |
| `alt-down` | `agent.next` |
| `alt-up` | `agent.prev` |
| `ctrl-?`, `ctrl-shift-/`, `ctrl-shift-?` | `app.help` (keys dialog) |
| `ctrl-shift-b` | `layout.toggle:sidebar` |
| `ctrl-shift-n` | `space.new` |
| `ctrl-shift-t` | `tab.new` |
| `ctrl-shift-k` | `mod.open:picker` |
| `ctrl-shift-p` | `app.palette` |
| `ctrl-,` | `app.settings` |
| `ctrl-shift-o` | `pane.split:right` |
| `ctrl-shift-e` | `pane.split:down` |
| `ctrl-shift-w` | `tab.close` |
| `ctrl-shift-x` | `pane.close` |
| `ctrl-shift-z` | `pane.zoom` |
| `ctrl-shift-l` | `layout.switch` (next layout) |
| `f2` | `tab.rename` |
| `shift-f2` | `space.rename` |
| `ctrl-alt-shift-escape` (built in, not in file) | `app.reset` |

**terminal** (lines 63-73):

| Keys | Action |
|---|---|
| `ctrl-backspace` | `send:\u0017` (Ctrl+W, delete word back) |
| `ctrl-left` | `send:\u001bb` (Alt+B, word left) |
| `ctrl-right` | `send:\u001bf` (Alt+F, word right) |
| `ctrl-shift-c`, `ctrl-insert` | `terminal.copy` (with no selection, the key goes to the program) |
| `ctrl-shift-v`, `shift-insert` | `terminal.paste` |
| `ctrl-shift-f` | `terminal.search` |
| `ctrl-v` | `terminal.paste_image` (on Windows: pastes clipboard text; image → file only for a remote terminal) (`commands.rs:74`) |

macOS uses `keys.macos.json` (cmd-based; `ctrl-1..9` selects spaces there). Not needed for a Windows port.

Windows-specific input: when the program enables win32-input-mode (`CSI ?9001h`), Enter and Escape with modifiers are sent as win32 key events `CSI Vk;Sc;Uc;Kd;Cs;Rc _` (down then up) so Shift+Enter reaches Claude Code (`src/win32_keys.rs:1-56`).

---

## 4. Program recognition

### 4.1 Table schema (`programs.json`, `programs.rs:16-46`)

```json
{ "shells": ["zsh","bash","fish","sh","dash","nu","pwsh","powershell","cmd","elvish","xonsh","tcsh","csh","ksh","ion"],
  "interpreters": ["node","bun","bunx","deno","python","python3","npx","pnpm","yarn","uv","uvx","ruby","bundle","env","sudo","nice","time","watch","nodemon"],
  "programs": { "<key>": { "name", "icon", "tint", "tile", "color", "match": [...], "run": [...] } } }
```

- `name`: display name. `icon`: path relative to home (shipped ones in `icons/`, written at start and rewritten when they change, `programs.rs:167-177`). `tint: true` = one-colour glyph drawn in `color` on the `tile` background (default true); `false` = full-colour picture. `tile`, `color`: hex. `match`: other process names or OSC 7501 `app` names. `run`: argv to start it from a menu (only agents).
- The person's `<home>/programs.json` is merged on top: `shells`/`interpreters` appended, entries merged field by field (non-empty `match`/`run` replace) (`programs.rs:48-62`, `179-198`). Parse error → `programs.error`, built-in table still used.
- 124 built-in entries (`AGENTS.md:836-845`). Agents (with `run`): claude (`match: claude-code`), codex (`run: codex --no-daemon`), pi, pig, opencode, cursor (`run: agent`, `match: agent, cursor-agent`), copilot, amp, grok, dsh-tui, empryo, goose, antigravity (`agy`), kimi, kiro (`kiro-cli`), hermes, qoder, crush, gemini, omp; agents without `run`: mastracode, muse, command-code, prime-agent, ollama. Generic entries: `agent`, `shell` (tile `#16241a`, colour `#4ec95f`), `program`. Then editors, git tools, runtimes, build tools, databases, cloud CLIs (`programs.json:5-129`).
- Menu order of startable agents: claude, codex, pi, opencode, cursor, copilot, amp, gemini, grok, then the rest by key (`programs.rs:64-83`).

### 4.2 Matching algorithm (`programs.rs:200-373`)

Input: the agent's OSC 7501 `app` (if it reported one) and an argv. The argv is the core's `foreground.argv` (what runs in front now), else the argv the terminal was started with (`window.rs:5210-5213`, `5226-5230`).

1. If `app` is non-empty: look it up (step *find*); found → that entry, `agent: true`; not found → the generic `agent` entry with `name = app`.
2. Unwrap launch wrappers: `<shell> -…c '"$@"; exec "$0" -i' <shell> <cmd…>` (and fish forms) → recurse on `<cmd…>`; on Windows `<pwsh|powershell> -NoExit -Command "& 'prog' 'arg'…"` → parse the PowerShell single-quoted words (doubled quotes, curly quotes count as quotes) and recurse (`programs.rs:317-335`, `249-293`).
3. Empty argv → `shell`.
4. `base(argv[0])`: last path component (`/` or `\`), leading `-` stripped (login shells), trailing `.exe` stripped case-insensitively (`programs.rs:239-247`).
5. If it is an interpreter: for each non-flag argument, take its base name minus `.js/.mjs/.cjs/.ts/.py/.rb`; the first one that finds a non-interpreter entry wins (`node …/vite` → vite, `npx vitest` → vitest, `uv run pytest` → pytest; `python3 -m http.server` → python because `http.server` matches nothing) (`programs.rs:339-349`).
6. *find* `base(argv[0])`; found → that entry.
7. In `shells` → `shell` entry, with `name` = the shell's base name (e.g. "pwsh").
8. Else → `program` entry with name = base name, `tile` = one of 8 colours chosen by FNV-1a hash of the name, `color` white (`programs.rs:356-380`).

*find*: an exact or case-insensitive key match wins over another entry's `match`, except that the generic `agent` key yields to a `match` (Cursor's process is named `agent`) (`programs.rs:295-308`).

Output object: `{key, name, icon, tint, tile, color, shell, agent}` (`programs.rs:362-373`).

### 4.3 How names are used

- Pane header label: pane name if the person set one; else for agents and shells the program name; else the terminal title (OSC 0/2); else program name; else "shell". The header also shows the folder (home prefix shown as `~`) and status (`pane.js:3-4`, `32-36`, `63`).
- Tab label: a name the person gave stays; otherwise the lead pane's program name (or "Terminal"); for an agent, its live title is used when it is informative (not a path, not containing `@`, not just the program name) (`tabs.js:15-28`).
- Toasts: "<name> needs you" / "<name> finished", name from the core record, else `programs::name(app)` (`window.rs:2240-2255`).

---

## 5. Shell current-folder tracking on Windows

### 5.1 Mechanism (herdr-core, not the app)

1. **Prompt hook makes `cd` visible to Windows.** PowerShell's `Set-Location` does not change the process's Win32 current directory. herdr-core starts the default local shell (pwsh.exe found on PATH, else powershell.exe) as:
   `pwsh.exe -NoExit -Command "<hook>"`, where the hook wraps the person's existing `prompt` function: after their prompt runs (so `$?` is preserved), if the current location is a FileSystem path it sets `[Environment]::CurrentDirectory = $l.ProviderPath` (errors ignored), then returns the original prompt text (`core:windows_exec.rs:89-100`). cmd.exe needs no hook because `cd` changes the real current directory. Other shells (nu, bash via MSYS, etc.) get no hook; whether their folder tracks correctly is unclear from the source.
2. **Finding the foreground process.** A pseudo console has no foreground process group. For each terminal, every 500 ms (env `HC_FOREGROUND_MS` overrides), the core lists the PIDs in the terminal's Job object (`QueryInformationJobObject(JobObjectBasicProcessIdList)`, up to 64), reads each one's parent PID (`NtQueryInformationProcess(ProcessBasicInformation).InheritedFromUniqueProcessId`), start time (`GetProcessTimes`) and image name (`QueryFullProcessImageNameW`). If the root process is a known shell (zsh, bash, fish, sh, dash, nu, pwsh, powershell, cmd, elvish, xonsh, tcsh, csh, ksh, ion; `.exe` optional), the front process is the root's **newest direct child** excluding `conhost.exe` / `OpenConsole.exe`; otherwise (or no children) the root itself (`core:windows_foreground.rs:1-59`, `core:foreground.rs:158-167`). Grandchildren never take the front (an agent's helper `node.exe` does not replace the agent).
3. **Reading its folder and command line from the PEB.** `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ)`; skip WOW64 (32-bit) processes; `NtQueryInformationProcess` → `PebBaseAddress`; `ReadProcessMemory` the PEB, then `RTL_USER_PROCESS_PARAMETERS`; `CurrentDirectory.DosPath` is the `UNICODE_STRING` at offset `0x38` of the parameters block (x64); also `ImagePathName` and `CommandLine` (split into argv with the Windows rules). A trailing `\` is removed unless the path is a drive root (`C:\`) (`core:foreground.rs:33-93`).
4. **Publishing.** The terminal's `foreground = {pid, name, argv, cwd}` is updated; on change a `foreground` event is emitted to subscribers and the state saved; `last_cwd` is kept (`core:foreground.rs:158-193`).

The app's own fallback `process_cwd::read(pid)` returns `None` on Windows (`process_cwd.rs:31-34`), so on Windows the app depends entirely on the core's `foreground.cwd`, else the terminal's start `cwd` (`window.rs:2375-2383`). There is no OSC 7 parsing in herdr-app (none found in `vt.rs`/`terminal.rs`).

### 5.2 App side: who consumes the folder (`window.rs:2369-2429`)

The app polls the core every 500 ms (event subscription + listing) and runs `tend_terminals` → `track_folders` each tick (`window.rs:733-790`, `2176-2208`). For each live terminal:

- `cwd` = `foreground.cwd` → (local only) `process_cwd::read(pid)` → record `cwd`. Stored in `cwds[address]`.
- If the folder moved, a space's automatic name may change, so the window state is saved (`window.rs:2398-2402`); `note_pane_folders` copies `cwds` into `model.pane_folders` per pane (`window.rs:1133-1143`).
- Git lookup (`recheck`, `window.rs:4985-4996`): ask again only when the folder changed, or the terminal went from busy to not busy (a command or an agent turn ended: a checkout or commit may have happened), or, while the window is focused and the folder is a repo, every 60 s (`FOLDER_FALLBACK`). "Busy" = agent status `working`, or a program other than a shell/agent is in front (`window.rs:4983-4989`).
- The lookup runs once per (machine, folder) on a background thread: `git -C <cwd> rev-parse --show-toplevel --abbrev-ref HEAD`. Home folder is skipped (not a repo). Output: `top` (repo top; a worktree's own folder) and `branch` (none when detached, `HEAD`). Only git's "not a git repository" counts as "no repo"; other failures are retried (`window.rs:5001-5022`). Results stored in `model.folder_info[address]`; if changed, state is re-published to mods.

What updates when the folder changes:

| Consumer | Uses | Ref |
|---|---|---|
| Space name (when the person gave none) | the first terminal of the space: `folder_info.top` (repo root) else its live folder else the space's start folder; shown as last path component, `~` for home (case-insensitive compare on Windows; `\\?\` prefixes handled) | `model.rs:269-280`, `634-646` |
| Space's branch (sidebar) | `folder_info.branch` of the space's active tab's focused pane | `window.rs:1843-1846` |
| Pane header (pane mod) | `cwd` shown with `~` for home; `branch` available | `window.rs:1870-1873`, `pane.js:32-36`, `83` |
| `focused().cwd` for mods (Files panel root etc.) | the focused pane's (or shown agent's) folder | `window.rs:1888-1898` |
| New tab / split / space folder | the focused terminal's live folder is the default start folder | `window.rs:2646-2649`, `commands.rs:32-38` |
| Link resolution in terminal output | relative paths resolve against the terminal's `cwd` | `terminal.rs:1149`, `1164`, `window.rs:2140-2143` |
| Program icon/name | `foreground.argv` (section 4) | `window.rs:5210-5213` |

The pane's terminal title (OSC 0/2) is independent of the folder; it is tracked by the VT engine's title-changed callback (`vt.rs:663-708`, `1387-1395`).

---

## 6. Sounds, notifications, first run

### 6.1 Sounds (`sound.rs`, `settings.rs:526-543`)

- `sounds.enabled` (default true). Bundled MP3s: `assets/sounds/done.mp3` (finished) and `assets/sounds/request.mp3` (needs you). `sounds.done` / `sounds.blocked` replace them with an MP3 path (absolute or relative to home); if the custom file fails to play, the bundled one plays (`sound.rs:29-72`).
- Windows playback: MCI `mciSendStringW`: `open "<file>" type mpegvideo alias herdrN` → `play herdrN wait` → `close herdrN`, on its own thread, unique alias per play so sounds can overlap (`sound.rs:74-93`). Bundled sounds are first written to `%TEMP%\herdr-app-<pid>-<name>.mp3`. Env `HERDR_APP_NO_SOUND` disables all sound.
- Settings › Alerts (`settings.js:740-758`): heading "SOUNDS"; "Play sounds" toggle (about: "When an agent needs you, and when one finishes while you're not looking. A note shows in the corner either way."); rows "An agent needs you" and "An agent finished" showing the file name (or "Herdr's sound") with a "Play" button; footer hint to set `sounds.done`/`sounds.blocked` to an MP3.

### 6.2 When alerts fire (`model.rs:550-572`, `window.rs:2185-2261`)

- Rule per change of an agent's reported state (OSC 7501 word): to `blocked`/`error` from anything else → **Blocked** (always); from busy (`working`/`blocked`/`error`) to a non-busy word while the person is **not looking** → **Done**. Looking = the terminal is visible and the window is active.
- Only terminals placed in a pane; sub-agents never alert. Changes seen in the first listing after start do not alert.
- Effects: play the sound if enabled (also when looking); bring a settled (hidden) tab back; if not looking, show a toast: title "<name> needs you" (level `error`) or "<name> finished" (level `info`), body "<space> / <tab>", click runs `agent.focus` on that terminal. Toast lifetime: 8 s for error/warning, 5 s otherwise, unless the notification gives `timeout` (`window.rs:3890-3893`).
- No OS-level notification or taskbar flash was found in herdr-app.

### 6.3 First run / welcome (`window.rs:814-876`, `welcome.js`)

- Trigger: `shared.json` has no `frame.welcomed` → open the `frame-welcome` mod as a window overlay 660×600 that a click outside does not close. Otherwise, if the release has spotlight notes, open What's new instead (`window.rs:823-829`, `871-876`). `app.welcome` reopens it.
- Also at first run (no settings.json): auto zoom (1.4), settings.json written with defaults, keys.json and theme/icon files installed.
- 4 pages, dots indicator, Back / Next (page 2 says "Not now" when no machines), last page "Done" or "Open a space" when no space exists (`welcome.js:9`, `540-560`). Keys: Escape = skip, Enter/Right = next (Enter on the last page = start), Left = back (`welcome.js:478-490`).
  1. "Herdr runs your terminals in the background": three feature rows; **Appearance** segment System/Light/Dark (writes `appearance` with the same compare-and-swap, 3 tries) and **Size** zoom stepper (−, `NN%`, +); a "Coming from herdr?" card with a copyable migration prompt (`welcome.js:284-296`, `86-108`, `187-214`).
  2. "Add your other machines" (ssh hosts; refreshes every 3 s while shown).
  3. "Choose your panels": Files and Changes toggles; first visit sets `workspace.panels` to `[]` (skipping the welcome leaves both on) (`welcome.js:396-413`).
  4. "Make it yours": shows the bound key for `app.palette` (e.g. "Ctrl+Shift+P"), a copyable line asking the agent to read `AGENTS.md`, notes on `hc` and Settings (`welcome.js:415-463`).
- Finish writes `frame.welcomed = "finished"` and, with no spaces, runs `space.new`; Escape writes `"skipped"` (`welcome.js:76-84`; page 2 at `311`).

---

## 7. Notes for the C++/ImGui port

- Settings write path: implement the read-modify-compare-write loop and "never overwrite a broken file" exactly; preserve unknown keys.
- On Windows the folder is only as good as the shell hook: start PowerShell with the prompt wrapper, or cmd; then read `CurrentDirectory.DosPath` from the front process PEB (x64 offset 0x38 in `RTL_USER_PROCESS_PARAMETERS`; skip WOW64). Front process = newest direct non-conhost child of a known-shell root in the Job, else the root. Poll ~500 ms.
- Run git per (machine, folder), off the UI thread, with the recheck rule above, to avoid spawning git twice a second.
- Unclear from source: Windows font fallback chain beyond Symbols Nerd Font; folder tracking for shells other than PowerShell/cmd; whether 32-bit processes are common enough to matter (they return no folder).
