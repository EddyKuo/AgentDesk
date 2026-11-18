# herdr-app: tabs, panes, popups and menus (behaviour spec for the C++/ImGui port)

Source: https://github.com/herdrdev/herdr-app (Apache-2.0), read through the GitHub API on 2026-11-18 (default branch).
Everything here is an observation of herdr's code, not AgentDesk design. References are `path:line` in that repo.
All px values are "design px at zoom 1"; herdr multiplies by its zoom setting (`settings::z`).
Marked **Unclear** where the code did not say.

Terminology: a **space** is a project/workspace (on one machine); a **tab** is a piece of work inside a space; a **pane** is a terminal (or mod view) inside a tab's split tree. The default layout is `main` (`defaults/layouts/main.json`); `terminals` is an alternate example layout.

---

## 1. Tab bar (`defaults/mods/tabs/main.js`, block `tabs` in `defaults/layouts/main.json`)

### 1.1 Placement and container
- Block `tabs`: 42 px tall, top of the `work` column, `window_drag: true` (`defaults/layouts/main.json`, block id `tabs`).
- Bar: full size, `px 10`, `gap 3`, background `chrome`, 1 px bottom border `border` (`tabs/main.js:144`).
- Children, left to right (`tabs/main.js:144-155`):
  1. Scrollable tab strip (`flex_1`, horizontal scrollbar) (`:147-149`)
  2. `+` button (opens the new menu) (`:150`)
  3. `Files` and `Changes` panel toggles, if enabled in shared `workspace.panels` (default both) (`:132`, `:151-152`)
  4. Window buttons minimize / maximize-or-restore / close, only when `window().buttons` (`:153-155`)

### 1.2 What one tab shows (`tabs/main.js:104-125`)
- Row: height 29, `px 10`, `gap 7`, radius 7, font 12.8, never shrinks (`flex_none`).
- Active tab: 1 px border `border`, background `raised`, text `foreground`. Inactive: border and background `chrome`, text `muted`. Hover: background `hover` (`:111-112`).
- Content, in order:
  1. Program icon 14 px, if the tab's lead pane has a program with an icon (tinted SVG uses `muted`) (`:120`).
     Lead pane = first pane in the tab running an agent, else first pane (`:105`).
  2. Label, max width 170, truncated with ellipsis; suffix `" Z"` when the tab is zoomed (`:121`).
  3. Status: `working` = 12 px spinning spinner (900 ms per turn, color `working`); `blocked` or `done` = 6 px round dot in that theme color (`:122-123`).
  4. Close `x` (12 px icon), **only on the active tab** (`:124`).
- Label rules (`label()`, `tabs/main.js:19-28`; tested in `scripts/tabs.test.ts:108-142`):
  - Base = the tab's own name if the person named it, else the lead program's name, else `"Terminal"`.
  - If the lead is an agent and the tab is unnamed (or named exactly as the program), the agent's live terminal title replaces it, but only if that title "says something": not empty, not equal to the terminal address, not the program name/key, not a path (`~`, `/`, `C:\`), no `@`. A leading status glyph (non letter/digit/bracket) and a `[ ! ] `/`[ . ] ` prefix are stripped (`:15`).
  - A shell's title never replaces the name (`tabs.test.ts:137-141`).
  - App-side fallback name of an unnamed tab is its 1-based position in the space (`src/model.rs:283-288`). Note the tab bar mod shows the program name instead when it has a lead pane; the number appears in other places (picker, menus).

### 1.3 Mouse on a tab
- Left press: remember the pressed index; on a double press (`click_count == 2`) run `tab.rename {id}` (`tabs/main.js:113`).
- Left release on a tab (`:114-118`):
  - Pressed on a different tab: `tab.move {id: pressedTab, index: positionOfReleasedTab + 1}` (drag-to-reorder; no drag ghost or insertion marker is drawn).
  - Otherwise: `tab.select {id}`. Selection happens on **release**, not press.
- Release anywhere else on the bar clears the pressed state (`:145`).
- Right press: popup context menu (`menu` mod), width 200, props `{tab: "tab", target: {id, space}}` (`:119`). Items in section 4.1.
- Close `x` click: `tab.close {id}`; its press does not start a tab press (`:124`).

### 1.4 Wheel over the bar (`tabs/main.js:39-60`; `scripts/tabs.test.ts:52-102`)
- Line-based wheel (mouse notch): one notch = one tab; `y > 0` = `tab.prev`, `y < 0` = `tab.next`.
- Pixel deltas (touchpad / smooth wheel): accumulate; first step at 20 px, then one step per 60 px; at most one switch per 100 ms. A pause > 250 ms, a direction change, or a touch "started" phase resets the gesture.

### 1.5 Overflow
- The strip is `overflow_x_scrollbar` (horizontal scrolling with a scrollbar); tabs never shrink (`:147`, `:108`).
- When the active tab or index changes, and whenever the strip or a tab at or before the active one changes size, it scrolls the active tab into view (`:96-100`, `:110`, `:148`).
- No overflow drop-down/chevron exists. Wheel over the bar switches tabs rather than scrolling (1.4).

### 1.6 Add / close / rename / reorder (app behaviour, `src/window.rs`)
- **Add** (`+` button): opens the `new-menu` popup at the pointer, width 250, max height 512 (`tabs/main.js:150`). Keyboard `ctrl-shift-t` runs `tab.new` directly (`defaults/keys.json:50`).
  - New tab with focus (default) is inserted **right after the selected tab** and selected; with `focus: false` it is appended last and selection stays (`src/model.rs:309-329`).
  - With no space open, a person's `tab.new` creates a space instead (`src/window.rs:3115-3118`).
- **Close** `tab.close` (`src/window.rs:3123-3134`): ends the tab's terminals, **no confirmation** while the space has other tabs. Closing the **last** tab becomes `space.close` for its space, which asks for confirmation (dialog "Close space?" with detail `"<space> — N tabs, M terminals"`, `src/window.rs:3067-3081`). Scripts must pass `confirmed: true`.
  - After closing the selected tab, the tab that slides into its slot is selected, else the last (`src/model.rs:430-433`).
- **Rename** `tab.rename` without a name opens the `prompt` dialog (width 380) titled "rename tab" (displayed "Rename tab"), prefilled with the current name, hint "empty: its number" (`src/window.rs:3023-3055`). Pressing rename again while the prompt is open closes it (`:3047-3050`). Empty name = back to automatic. Key: `F2` (`defaults/keys.json:60`); `shift-F2` renames the space.
- **Reorder**: mouse 1.3; command `tab.move {id?, index}` (1 = first) or `{by: ±n}` (wraps, skips settled tabs) (`src/model.rs:200-219`).
- **Navigate**: `ctrl-1..9` select tab by position (counting non-settled tabs), `ctrl-tab`/`ctrl-pagedown`/`alt-]` next, `ctrl-shift-tab`/`ctrl-pageup`/`alt-[` prev, wrapping (`defaults/keys.json:26-40`, `src/window.rs:3167-3187`).

### 1.7 Window controls on Windows
- `main` layout sets root `"titlebar": "none"`; on Windows the system title bar is always hidden and with `none` the layout draws the buttons (`src/window/titlebar.rs:12-14`, `:84-87`, `window().buttons == true`).
- In the tab bar: three 26 px square buttons, radius 6, icon color `dim`, hover background `hover` and text `foreground` (`tabs/main.js:102-103`, `:153-155`). Windows icons are vector SVGs `win-min`, `win-max`/`win-restore` (when maximized), `win-close`, 16 px. The close button has **no red hover** in the tab bar.
- Commands: `window.minimize`, `window.maximize` (toggles maximize/restore via `ShowWindowAsync`), `window.close` (`src/window/titlebar.rs:208-223`, `:252-258`).
- Dragging empty bar area moves the window: after the pointer moves > 4 px the app posts `WM_SYSCOMMAND SC_MOVE|2` so Windows' own move loop runs (Aero snap works) (`titlebar.rs:19-20`, `:121-136`, `:243-250`). Double-click on a drag area toggles maximize (`:100-114`). Buttons and tabs stop mouse-down propagation so they don't start a window drag (`tabs/main.js:101`, `terminals-bar/main.js:55-59`).
- With `"titlebar": "system"` on Windows, the app draws its own 32 px strip with 46 px-wide Min/Max/Close buttons using `WindowControlArea` (snap layouts work); close hover is `#c42b1c` with white icon (`titlebar.rs:31-34`, `:262-288`).

### 1.8 Panel toggles (per space)
- `Files`/`Changes` buttons: 26 px tall pill, selected background when that panel is shown (`tabs/main.js:134-143`). Clicking the shown one hides the panel; otherwise selects it in stack `panel-body` and shows `panel`.
- The open panel is remembered **per space** in localStorage `panels` and restored when switching spaces (`:62-88`).

### 1.9 Alternate bar: `terminals-bar` (layout `terminals`)
- 38 px block. Left: space chip (30 px pill, stack icon + space name, click opens the `picker` popup at top) (`terminals-bar/main.js:142-160`).
- Tabs tray (36 px, radius 16, background `#1d1f22`): each tab up to 260 px wide, shrinkable, radius 13; shows a 26 px program tile with a status dot at top-right (11 px circle) plus tab name and dimmed folder (`:31-53`, `:102-134`). Lead pane here = focused pane, else agent, else first (`:25-29`). Click selects; right-click opens the tab menu. No close button, no reorder.
- Right: palette button (`app.palette`), `+` (runs `tab.new` directly, no menu), then 24 px round window buttons (`:173-185`).

---

## 2. Pane chrome

### 2.1 Frame (layout `main`, block `main`, `content.pane`)
- `lone: true` means the frame and header are drawn even when the tab has only one pane (`defaults/layouts/main.json`, `src/window/pane_size.rs:162`).
- Gap between panes 8, radius 10, background `background`, 1 px border `border`; the **focused pane** uses a 1 px `accent` border (`main.json` `pane.border`/`pane.focus`; `pane_size.rs:15-17`).
- Header: mod `pane`, height 30, position `top`, `reveal: "always"`. Default header height is 30 when not given (`src/window.rs:32`). A header with reveal `hover` takes no layout space (`pane_size.rs:127`).
- Terminal padding inside panes: `terminal_padding: [14, 12]` (vertical, horizontal) on the root (`main.json`).
- The pane's terminal grid size = frame minus 2×border minus header height minus padding, divided by cell size (`pane_size.rs:120-131`, `:159-171`).

### 2.2 Header contents (`defaults/mods/pane/main.js:59-92`)
- Row: full size, `px 10`, `gap 8`, background `background`, 1 px bottom border (top border if header is at the bottom), font 12.
- Left to right:
  1. Program icon 14 px (if any).
  2. Name: pane's own name, else program name for an agent or shell, else terminal title, else program name, else `"shell"` (`:63`). Medium weight, max width 260, truncated.
  3. Folder: live cwd with home shown as `~`, dim, takes remaining width, truncated (`:32-36`, `:83`).
  4. Status: `working` = 12 px spinner; `blocked` = pill "Needs you" (18 px tall, radius 5, 10.5 px semibold, `blocked` text on 15% `blocked` background); `done` = 7 px dot (`:84-86`).
  5. Buttons, **only while the header is hovered** (`:79`, `:87-91`), each 14 px icon with 3 px padding, radius 4, dim, hover background:
     - Split right: `pane.split {direction:"right", pane}`
     - Split down: `pane.split {direction:"down", pane}`
     - Zoom / Unzoom (icon swaps by `zoomed`): `pane.zoom {pane}`
     - Close: `pane.close {pane}`
- Click on the header: `pane.focus {pane}` (`:80`).
- Note: `defaults/AGENTS.md:916-917` calls `pane` "an example pane header, not used by the default layouts", but `main.json` does use `{"mod":"pane"}` as the header. **Unclear** which is current; the JSON was taken as truth.

### 2.3 Pane operations (app side)
- **Split** (`src/window.rs:2643-2661`, `src/layout.rs:598-629`): new terminal in the **target pane's live cwd** (else the space folder), on the same machine. If the parent container already runs along that axis, the new pane is inserted right after the target and both get half of the target's weight; otherwise the target is wrapped in a new row/column with [target, new]. The new pane gets focus; any zoom in that tab is cleared (`src/model.rs:337-345`). `right` = row axis, `down` = column axis. Splitting with an `argv` runs that program in place of the shell; the pane ends when it exits.
- **Close** `pane.close` (`src/window.rs:3201-3214`): non-closable panes ignore it; the **last pane closes the tab** (and so possibly asks to close the space, 1.6). Otherwise ends the terminal; focus returns to the pane focused before it (still present), else the first pane (`src/model.rs:403-422`). No confirmation.
- **Zoom** `pane.zoom` (`src/model.rs:490-505`): toggles; a one-pane tab does not zoom; zooming focuses the pane. Tab label gets `" Z"` (1.2).
- **Focus by direction** `pane.go` (alt-h/j/k/l, alt-left/right), **swap** `pane.swap` (alt-shift-h/j/k/l) (`defaults/keys.json:12-21`).
- **Rename** `pane.rename` opens the prompt, hint "empty: unnamed" (`src/window.rs:3034-3038`).
- Context menu on right-click in a terminal: section 4.1 "pane" items.

### 2.4 Resize by dragging dividers (`src/window.rs:4633-4723`)
- Between visible children of a resizable row/column there is a separator of width `gap + divider.width`. The hit zone is `max(6 px, separator width)` centered on it (`HANDLE = 6`, `:33`, `:4651`), with col-resize / row-resize cursor.
- Drag sets the left/top child to `pointer - child.start`, clamped to `[24, total - 24]` where total = both neighbours' sizes; plus the child's own `min`/`max`. If both together are ≤ 48 px, the drag does nothing (`:4693-4702`).
- Storage: if either neighbour is fixed px, that one gets the new px; otherwise the two weights are redistributed preserving their sum (`:4712-4721`). Sizes are stored unzoomed.
- So the **minimum pane size from dragging is 24 px** along the drag axis; there is no other per-pane minimum in the code read. **Unclear** whether anything enforces a minimum terminal cell count.
- Keyboard: `pane.resize {direction, px=40}` moves the nearest matching divider as if dragged; no-op if nothing splits that way or the tab is zoomed (`src/window.rs:3741-3798`). No default key binding.

### 2.5 Drag a pane to rearrange (`src/window/pane_drag.rs`; `defaults/AGENTS.md:244-251`)
- Only within a tab with ≥ 2 panes, not zoomed, and `movable` not disabled by the nearest ancestor (default true) (`pane_drag.rs:38-47`).
- A **grip** appears when the pointer is over the pane's top strip: the strip height is `max(header height, 18)`, so with the 30 px header it is the whole header row (`:25-31`, `:49-51`, `:74-88`). Grip: 28×18 px, centered horizontally, vertically centered in that row, radius 4, background `background` at 85%, two rows of three 2 px dots (`dim`, `foreground` on hover/drag), open-hand cursor, closed-hand while dragging (`:56-71`). Only the grip starts a drag.
- Drag starts after the pointer moves ≥ 4 px (`:114`).
- Drop target: any other visible pane under the pointer. Inside the outer 25% of that pane, the **nearest edge** is chosen (left/right/top/bottom); in the middle it is a **swap** (`:13-23`).
- Preview: an accent rectangle (18% accent fill, 2 px accent border) over the half of the target on that edge, or the whole target for a swap (`:147-159`).
- Drop on an edge: remove the dragged pane and split the target on that axis (placed before the target for left/top); its old size is dropped (`src/model.rs:352-381`). Center: swap. The moved pane is focused.
- Releasing outside another pane, or Escape, cancels (`AGENTS.md:248-249`; release with no target does nothing, `pane_drag.rs:129-145`).

---

## 3. "New" flows

### 3.1 New menu (`defaults/mods/new-menu/main.js`), opened by the tab bar `+`
Popup at the pointer, width 250, height 512 max (`tabs/main.js:150`); actual height `min(491, 167 + max(1, rows) × 31)` (`new-menu/main.js:53`). Rows are 31 px, `px 9`, gap 10, radius 7; hint on the right in 11 px dim text (`:41-47`).

Contents (`:62-71`):
1. **Terminal**: icon shell; runs `tab.new {space: activeSpace}` (a plain shell tab). Hint = the bound key for `tab.new` (e.g. `Ctrl+Shift+T`).
2. Section title "Start an agent".
3. Agent list (scrollable): every **installed** agent, then up to **2 not-installed** ones shown disabled with hint "not installed". Installed agents get hints `1`..`9`; click runs `tab.new {name: agent.name, space, argv: agent.run}`.
   - The list comes from `knownAgents({on: machine})` for the active space's machine; the last answer per machine is cached in shared `new-menu.agents` and shown immediately while it re-checks (`:5-6`, `:26-39`).
   - While empty: "Checking installed agents…"; on failure: "Could not check installed agents" (and only installed ones are listed) (`:52`, `:67`).
4. Separator.
5. **New space**: `space.new` (no args). Hint = bound key (`Ctrl+Shift+N`).
6. **New worktree…**: opens the worktree dialog (`action: "new"`, 460×280) with the active space and the focused pane's cwd. Disabled without a space.
7. **Open worktree…**: worktree dialog (`action: "open"`, 560×480). Disabled without a space.

Keys: digits `1`-`9` (no Ctrl/Alt/Cmd) start the Nth installed agent (`:55-61`). The mod itself has no arrow/Enter handling; Esc and click-outside close it because it is a popup (`src/window.rs:4775-4792`, `AGENTS.md:1096`). Clicking a row closes the menu first, then runs the action (`:45`).

There is **no machine choice** in this menu: a `space.new` from a key or click defaults to the **focused space's machine** (`docs/commands.md:22`, `src/window.rs:5097`). Machines appear in the picker (3.3) and in machine-add (5.2).

### 3.2 Defaults: what starts, and where
- **A new tab / space / split with no argv starts a shell.** "With no argv, that machine's core picks its own shell" (`src/window.rs:2511-2516`). On Windows the app's own `shell()` is `pwsh.exe` found on PATH, else `powershell.exe` (`src/window.rs:102-111`); it is used to wrap agent launches. **Unclear**: which shell herdr-core itself picks for a plain terminal (core is a separate binary).
- An agent tab (`argv` given) runs the agent inside a shell that stays after it exits ("stay"); a split with argv runs the program in place of the shell, so the pane ends with it (`src/window.rs:2567-2577`, `:113-128`).
- **Folder**:
  - `tab.new`: focused pane's live cwd in that space's selected tab, else the space's creation folder, else home (`src/window.rs:2489-2493`).
  - `pane.split`: the split pane's live cwd, else the space folder (`:2649`).
  - `space.new`: explicit `folder`, else the focused pane's cwd **if on the same machine**, else home (this computer) or the remote machine's home (`:2581-2588`).
  - If starting in an inherited folder fails, it retries in that machine's home; explicit bad folders fail (`AGENTS.md:117-118`).
- **Names**: a new space is unnamed (named after its first terminal's repo or folder, `~` at home, and follows it) (`AGENTS.md:110-113`, `src/model.rs:269-280`); a new tab is unnamed (its number). Agent tabs from the new menu are named after the agent.

### 3.3 Picker ("Go to…", `defaults/mods/picker/main.js`)
Opened by `ctrl-shift-k` (`mod.open:picker`, `defaults/keys.json:51`), the palette entry "Go to space or tab…", and the Terminals layout's space chip. Popup anchored at top: centered, 40 px from the top, width 640, height up to 80% of the window (`picker/mod.json`; `src/window.rs:4776-4779`).

Layout (`picker/main.js:227-275`): padding 18, gap 10. Title "Go to…" (15 px semibold) with an "Esc" chip that closes; search field 36 px with search icon, accent border, placeholder "Go to a tab, space or agent…"; list up to 320 px tall.

Rows (`:67-103`), heights head 29, tab 32, earlier 35, new 36, space 32 (`:136`):
- For each space with at least one tab that matches: a **space header** (18 px rounded square in the space's color with its first letter, then the name). Colors: six fixed accents, assigned to each space id once, choosing the least-used color, kept in shared `space.colors`; the space named `~` is dim (`:11-35`).
- Under it, its **tab rows**: status glyph (spinner = working; `○` idle; `●` in status color; `·` none), tab name (semibold if it is where you are), `needs you` (blocked) / `finished` (done), then the tab's agents' program names joined by ` · ` plus `+N` sub-agents (`:206-224`).
- **Ordering**: spaces with a status ("busy") first, blocked before others; "quiet" spaces (no status) fold into an `earlier: N quiet space(s) ▾/▴` toggle row, expanded only when toggled or while filtering (`:86-98`).
- Last rows: `+ new tab` (`tab.new`), `+ new space` (`space.new {machine: ""}` = this computer), and `+ new space on <machine>` for each connected, non-temporary machine from `hc machine ls --json` (`:54-60`, `:99-101`, `:117`; `scripts/picker.test.ts:79-88`).
- **Filtering** is case-insensitive **substring** (not fuzzy) on `"<space name> <tab name> <agent program names>"` (`:83`).

Keys and selection (`:41-49`, `:105-118`, `:236-249`):
- Opens with the current tab selected. Typing selects the first matching tab row.
- Up/Down move and **wrap**; Enter goes; `Ctrl+D` closes the selected tab (`tab.close`) and closes the picker; Esc closes.
- Enter/click on a tab: `tab.select`. On `earlier`: toggles the fold and **keeps the picker open and the selection on the toggle** (`scripts/picker.test.ts:90-137`).
- Mouse wheel scrolls the list by pixels and drags the selection along so it stays visible (`:266-273`).

---

## 4. Menus and overlays

General overlay rules (`src/window.rs:3838-3925`, `:4725-4817`, `:4944-4949`):
- **Popup**: placed at the pointer (or `at: "top"` = centered, `offset` px from the top), clamped 8 px inside the window; height fits content up to `height`. Click (left or right) outside closes it.
- **Dialog**: centered over a 35% black backdrop; click on the backdrop closes it unless `modal`.
- Both get a frame: `popover` background, 1 px border, radius 8, large shadow (unless `frame: "none"`). Esc closes the top dialog or popup (`AGENTS.md:1096`); terminals keep Esc for themselves.
- Width/height values ≤ 1 are fractions of the window.

### 4.1 Context menu (`defaults/mods/menu/main.js`)
Popup at the pointer. Width 200 for the tab menu (`tabs/main.js:119`); terminal right-click default 220×260 (`AGENTS.md:288`). Rows 31 px, `px 8`, gap 9, radius 7, 14 px icon (`muted`), label, right-aligned bound key in 11 px dim (`menu/main.js:139-158`). Separator (1 px, `my 4`) before any `*.close` item, before "Keybinds" and before the first theme item (`:142`).
Keys: Up/Down wrap, Enter runs, Esc closes; hover highlights; click runs (`:131-138`). Choosing closes the menu, then runs the command (`:112-118`).

Items by context (`menu/main.js:24-81`):
- **pane** (right-click in a terminal; props `{pane, terminal}`):
  - If it is a tab pane: Split right, Split down, Zoom, Rename.
  - If it has a terminal: "Send right-clicks to pane" (`terminal.right_click to:"program"`), or when already set "Use Herdr right-click menu" (`to:"menu"`). Shift+right-click always opens this menu (`docs/commands.md:154`).
  - If the terminal is a sub-agent with a parent: "Send back to <parent>" (`terminal.park`).
  - If it is a tab pane: Close pane.
- **tab**: Rename, New tab, Close.
- **space**: Rename; if the space folder is in git: New worktree, Open worktree; if it is a linked worktree: Remove worktree; Close. Git state is detected asynchronously with `git rev-parse --show-toplevel` and `--git-common-dir` (main checkout when common dir == `<top>/.git`) (`:16-22`, `:96-110`).
- **global** (default): New space, Keybinds, Theme: graphite / ink / moss / atom one dark. **Unclear** where the global menu is opened from (not in the files read).

### 4.2 Command palette (`defaults/frame/palette/main.js`)
Toggle with `app.palette` (`ctrl-shift-p`). Popup at top: centered, 60 px from the top, width 620, height ≤ 85% (`src/window.rs:3569-3578`).

- Search field 36 px (search icon on Windows/Linux, Cmd icon on macOS), placeholder "Type a command…", "Esc" hint (`:286-300`; `scripts/palette.test.ts:47-56`).
- Entries (`:22-97`): a fixed named list `[command, args, group, title, keywords]`:
  - Tab: New tab, Go to space or tab… (`mod.open picker`), Rename tab, Close tab, Next tab, Previous tab
  - Pane: Split right, Split down, Close pane, Zoom pane, Focus pane left/right/up/down, Swap pane left/right/up/down
  - Space: New space, Rename space, Close space, Next space, Previous space
  - Layout: Next layout, Reset layout, Duplicate layout, Duplicate mod, plus one "Layout: <Name>" per layout file
  - Terminal: New terminal, Copy, Paste
  - Agent: Next agent, Previous agent
  - Window (hidden on macOS): Minimize window, Maximize window, Close window
  - App: Settings, Keys, Welcome, What's new, Reset to the default screen, Zoom in, Zoom out, Reset zoom, Quit Herdr, Theme: graphite/ink/moss/atom one dark
  - Plus every other app or mod command that is not in a HIDDEN set (pointer-targeted commands like `tab.move`, `tab.select`, `pane.focus`, `layout.*` internals) and needs no required argument besides `name`, auto-titled "Verb noun" in group "Noun" (`:65-77`, `:90-95`).
  - Entries are filtered to commands the app actually reports (`:81-85`).
- **Empty query**: "Recent" group (up to 5 of the last 20 used, kept in localStorage `recent`), then groups in order Tab, Pane, Space, Layout, Terminal, Agent, Window, App, then others; group titles shown (`:166-174`).
- **Typing**: spaces removed from the query; fuzzy match (`:112-129`): letters in order; each hit +1, +6 at a word start (after space `: . - _ /` or string start), +3×run for consecutive hits, minus 0.3 per skipped char (max 6), minus 0.02 × title length. Title match is preferred; else match on `group title command keywords` with −4 and no highlight. Ties broken by recency (`:175-184`). Matched letters are drawn in `accent` semibold (`:187-203`). Group name shown at the right of each row while typing.
- Each row: 32 px, radius 7, title, then key chips (each key part in a bordered `raised` chip, e.g. `ctrl` `shift` `p`) (`:230-263`).
- Keys: Up/Down **stop at the ends** (no wrap); PageUp/PageDown move 10; Enter runs; Esc closes (`:274-285`). 10 rows visible (320 px); wheel scrolls by pixels with selection following.
- No match: `Nothing matches "<q>". Places are in the picker.` (`:318`).
- Running: closes, records recent, runs. "Duplicate mod" opens a prompt asking which mod; "Duplicate layout" uses the current layout (`:150-163`).

### 4.3 Help / keybinds (`defaults/mods/help/main.js`)
Toggle with `app.help` (`ctrl-?`, `ctrl-shift-/`, `ctrl-shift-?`). Dialog 640×520 (`src/window.rs:3610-3618`).
Title "Keybinds" + clickable "Esc"; filter input ("Type to filter…"); scrolling list grouped as spaces, tabs, panes, agents, terminal (incl. `send:`), window (everything else) (`:8-15`). Row 32 px: key string (190 px column, `accent`, with `(section)` if not global) and a label derived from the action (`pane.split:right` → "pane split right"; `send:x` → `type "x"`) (`:17-22`, `:77-84`). Filter: substring on key text or label. Esc closes.

### 4.4 Peek (`defaults/mods/peek/main.js`, `src/window.rs:3949-3963`)
`terminal.peek {terminal}` opens a dialog 80%×80% containing a column: a 34 px bar (the `peek` mod) above the live terminal. Opening another peek replaces the first. Triggered from the agents list for sub-agents and from terminal address links.
Bar (`peek/main.js:44-75`): status glyph (braille spinner at 90 ms when working; `○` idle; `●` other), program icon, name (name, template, program name or address; semibold), status word (working / needs you / finished / idle), "from <parent>" for sub-agents, spacer, then:
- `↗ go to` if the terminal already has a pane (`pane.focus {terminal}`), else `↗ keep` (`terminal.keep` gives it a pane as a new tab); closes the peek.
- `✕` closes.

### 4.5 Toast (`defaults/mods/toast/main.js`)
- Overlay kind toast, 340×72 by default (`src/window.rs:3875`), stacked at bottom-right, 16 px from the right, starting 16 px from the bottom and growing upward with 8 px gaps (`:4794-4797`).
- Auto-dismiss after `timeout`, default **5000 ms**, **8000 ms** for `error`/`warning` (`:3890-3893`).
- Content: 18 px icon image, or an 8 px dot colored by level (error → `blocked`, warning → `working`, success → `done`, info → `accent`); title (13 px semibold) and body (12 px muted), both one line truncated (`toast/main.js:9`, `:30-36`).
- Click anywhere: runs the toast's `action` (if any) and dismisses (`:26-29`).
- Special case: body `"<name> needs login; run hc machine login <name>"` shows the command with a Copy button ("Copied" after) (`:15-16`, `:35-42`).

### 4.6 Confirm (`defaults/mods/confirm/main.js`)
Dialog width 380 (`src/window.rs:3807-3811`). Title (15 px semibold, default "Are you sure?") with "Esc" hint; scrollable detail text (muted); right-aligned buttons **Cancel** (raised) and **Confirm** (solid). Enter = confirm (runs `command.name` with `command.args`), Esc = cancel (`confirm/main.js:15-51`). Used for "Close space?" (also reached by closing the last tab or last pane).

### 4.7 Prompt (`defaults/mods/prompt/main.js`)
Dialog width 380. Title with first letter uppercased ("Rename tab"), "Esc" hint, field label "Name", 36 px input prefilled with `value` and **fully selected**, optional hint line below (e.g. "empty: its number"), buttons **Cancel** / **Save**. Enter saves → runs `command` with `{...args, name: text}`; Esc cancels (`prompt/main.js:6-49`). Input border is `accent` when focused, `input` otherwise.

---

## 5. Worktree and machine-add flows

### 5.1 Worktree dialog (`defaults/mods/worktree/main.js`)
Opened from the new menu (New/Open worktree…) and the space menu (New/Open/Remove worktree). Props `{space, folder?, action: "new"|"open"|"remove"}` or `{folder, machine}`; optional `{branch, tab, argv}` creates at once (`:1-2`, `AGENTS.md:917-926`). Sizes: new 460×280 from the new menu, 440×260 from the space menu; open 560×480 (`new-menu/main.js:70-71`, `menu/main.js:116`).

Setup, async on open (`:124-173`):
1. Machine = the space's machine. For a remote one, read its home with `sh -c 'printf %s "$HOME"'`, falling back to PowerShell `[Console]::Write($HOME)`.
2. For "new": root folder from `settings.json` `worktrees.directory` (default `~/.herdr/worktrees`; must be absolute or `~/…`; Windows paths accepted; `\` normalised to `/`) (`:16-24`; `scripts/worktree.test.ts:175-203`).
3. Folder = `props.folder`, else where the space is now (first pane with a cwd, else the space's folder), else home (`:8-13`, `:155`).
4. Repo: `git rev-parse --show-toplevel` and `--path-format=absolute --git-common-dir`; main checkout = common dir minus `/.git`; name = last path part; `linked` = top ≠ main (`:115-121`). Not in git → message "<folder> is not in a git repository." and only Esc works.
5. Enter is ignored until the repo is known (`:156`, `:184-187`).

**New** (`:201-213`, render `:326-381`):
- Title "New worktree of <repo>"; text "A new space on a checkout in <root>/<repo>/. A branch that exists is checked out; a new one starts from here."; input "branch name"; footer "↵ create · esc cancel".
- Enter: empty → error "Type a branch name.". Path = `<root>/<repo>/<slug(branch)>`; slug = NFC, every run of chars other than letters/marks/digits/`._-` → `-`, trimmed of `-`; empty → `branch-<FNV-1a hex8>` (`:30-38`; tests `worktree.test.ts:114-146`).
- Existing branch: `git worktree add <path> <branch>`; new branch: `git worktree add -b <branch> <path> HEAD`; run from the current checkout, timeout 600 s.
- Success: `space.new {folder: path, machine, tab, argv}` (the space stays unnamed so it follows the checkout) and the dialog closes. Failure: git's stderr shown in `blocked` color; dialog stays. While busy: spinner + "working…".

**Open** (`:44-61`, `:215-226`, `:260-324`):
- Lists `git worktree list --porcelain` (skips bare and prunable); main checkout first, then by the branch's last commit time (newest first).
- Title "Open worktree of <repo>" with count "N checkouts" or "k/N checkouts"; filter input ("filter worktrees", substring on branch + path); rows 44 px: branch (or "detached"), tag "main checkout", "open" (accent) if a space already shows it, else age (`5m`, `3h`, `2d`, `4mo`), and the path with `~`. Footer "↑↓ choose · ↵ open · esc close". Up/Down wrap.
- Choosing: if a space on that machine has its first terminal inside that checkout (deepest checkout wins), `space.select` it; else `space.new {folder: path, machine}` (`:62-70`; tests `worktree.test.ts:79-95`, `:148-173`).

**Remove** (`:228-245`):
- Only for a linked worktree; for the main checkout: "This is the repository's main checkout, not a worktree." and blocked.
- Title "Remove worktree <space>?"; text "Closes the space and deletes <path>. The branch stays."; warnings in `blocked` color for uncommitted changes ("It has uncommitted changes: they will be lost.") and for submodules with unpushed commits. Footer "↵ remove · esc cancel".
- Enter: re-checks `git status --porcelain` and submodules; if new risk appeared since opening → "It changed since this opened. Enter again removes it anyway." Otherwise closes the space first (`space.close confirmed:true`, so a shell in the folder doesn't lock it on Windows), then `git worktree remove [--force] <top>` from the main checkout (`--force` if dirty or any submodules).

### 5.2 Add a machine (`defaults/mods/machine-add/main.js`)
Opened with `mod.open machine-add` from the sidebar's "Add machine" and the welcome. Dialog width 560 (`machine-add/mod.json`).

Steps:
1. Title "Add a machine" + "Esc"; intro "A VPS, a Mac mini, a box at home: Herdr reaches it over ssh, and agents there keep running while your laptop sleeps." (`:305-313`).
2. Reads `~/.ssh/config` (following `Include`, up to 16 levels; relative includes under `~/.ssh`; globs expanded via `sh`) and lists named `Host` entries (no wildcard patterns, deduplicated). Section "From your ssh config" or "Your ssh config lists no hosts"; "Reading ~/.ssh/config…" while loading (`:16-57`, `:314-326`).
3. Each host row (40 px): server icon, alias, dim `user@hostname` (or "also <aliases>"), and on the right an **Add** button. The whole row is clickable when idle (`:246-291`). Hosts already added are hidden from this list; added machines are listed separately with "✓ Added" (`:325-329`).
4. "Or type how ssh reaches it": input `user@host` + solid **Add** button (dimmed when empty); Enter adds (`:330-366`).
5. Add = `hc machine add <target> --json` (`:120-148`). While running: spinner "Connecting".
   - Success: input cleared, list refreshed (`hc machine ls --json`, non-temporary machines), row shows "Added".
   - Needs interaction (error kinds `hc_missing`, `incompatible`, `login_needed`, `auth_failed`, `host_key`, or message mentioning password/passphrase/host key/permission denied/login): message "Couldn't add <t> here: it needs you to answer something (a password, a new host key, or installing herdr-core there). Run `hc machine add <t>` in a terminal…" with a **Copy** button for that command. The target stays pending; every 3 s the dialog re-reads `hc machine ls` and flips it to "Added" once it appears (`:83-95`, `:97-114`).
   - Other errors: the message text.
6. After adding: if the machine is macOS, a card explains Full Disk Access with a button to open the setting there (`:150-159`, `:177-224`); if the machine stops programs on logout (`keeps_running_after_logout === false`), a card shows `sudo loginctl enable-linger -- <user>` with Copy (`:161-175`).
7. A space on that machine is then made from the picker's "new space on <machine>" or `space.new {machine}`; its terminals start in that machine's home (`docs/commands.md:22`).

---

## 6. Commands (`docs/commands.md`)

Run in the window with `herdr-app do <command> [--flag value | '{json}']`. Commands default to the current selection.

| Command | Args (required in **bold**) | Meaning |
|---|---|---|
| `space.new` | name, folder, tab, argv, task, machine, focus | Make a space with a first tab and terminal (default folder: focused terminal's; default machine: focused space's for people, this computer for scripts) |
| `space.close` | id, confirmed | Close a space and end its terminals; asks first unless confirmed |
| `space.rename` | id, name | Rename a space; no name asks; empty name = automatic |
| `space.move` | **id**, **index** | Move a space to a position (1 = first) |
| `tab.new` | name, space, folder, argv, task, mod, props, focus | Make a tab with a terminal (or a mod as its only pane); no space open → starts a space |
| `tab.close` | id, confirmed | Close a tab and end its terminals |
| `tab.rename` | id, name | Rename a tab; no name asks; empty = its number |
| `tab.move` | id, index, by | Move a tab to a position, or `by` places left/right (wraps) |
| `pane.split` | **direction**, pane, argv, mod, props | Split a pane right or down with a new terminal or a mod |
| `pane.close` | pane | Close a pane and end its terminal |
| `terminal.open` | argv, folder, place, name | Start a terminal as a new tab (default) or right/down of the focused pane |
| `terminal.keep` | **terminal**, place | Give a pane-less running terminal (sub-agent) a pane: new tab or right/down |
| `terminal.park` | **terminal** | Remove a terminal's pane and disable keep; it keeps running |
| `terminal.right_click` | **to**, terminal | Right-clicks go to the program or to Herdr's menu (Shift+right-click is always the menu) |
| `terminal.pin` | **terminal**, **pinned** | Pin/unpin an agent at the top of agent lists |
| `terminal.kill` | **terminal** | End a terminal and its sub-agents |
| `space.select` | index, id | Show a space |
| `space.next` / `space.prev` | – | Next / previous space |
| `tab.select` | index, id | Show a tab (position in space, or id) |
| `tab.settle` | id | Put a tab away (out of lists, still running) until shown or it needs you |
| `tab.unsettle` | id | Bring a settled tab back |
| `tab.next` / `tab.prev` | – | Next / previous tab |
| `pane.zoom` | pane | Show only this pane, or all again |
| `pane.rename` | pane, name | Name a pane; no name asks; empty = unnamed |
| `pane.focus` | pane, terminal | Focus a pane (or the pane showing a terminal) |
| `pane.go` | **direction** | Focus the pane/focusable mod left/right/up/down |
| `pane.resize` | **direction**, pane, px | Move a pane's border by px (default 40), like dragging |
| `pane.swap` | direction, pane, target | Swap with the neighbour in a direction, or with a given pane |
| `terminal.peek` | **terminal** | Peek at a running terminal over the tab (keep-as-tab, close) |
| `terminal.show` | terminal, pane | Show a terminal in a pane temporarily (no terminal: back) |
| `terminal.select_all` | terminal | Select whole screen + scrollback (alt screen: only it) |
| `terminal.select` | terminal, at, mode | Start a selection at cursor or {row,col}; mode cell/word/line |
| `terminal.adjust_selection` | terminal, **direction** | Move the selection's moving end |
| `terminal.clear_selection` | terminal | Clear the selection |
| `terminal.copy` | – | Copy the selection; nothing selected → key goes to the program |
| `terminal.scroll` | lines, pages, to, terminal | Scroll Herdr's view of history (one of lines/pages/to top|bottom) |
| `terminal.paste` | – | Paste (copied files as if dropped; an image alone → paste_image) |
| `terminal.paste_image` | – | Ctrl+V: clipboard image as a file on a remote machine; on Windows paste text; else key goes to the terminal |
| `terminal.search` | – | Find in screen + scrollback; the find bar stays with that terminal |
| `agent.focus` | **terminal** | Show an agent (sub-agents too) |
| `agent.next` / `agent.prev` | – | Next / previous agent in agents-list order |
| `layout.switch` | name | Show a layout, or the next one |
| `layout.clone` | **name**, as | Duplicate a layout as `<name>-copy` and show it |
| `mod.clone` | **name**, as | Duplicate a mod as `<name>-copy` |
| `layout.set` | **layout** | Replace a personal layout tree (shipped: only click/look fields) |
| `layout.toggle` / `layout.show` / `layout.hide` | **id** | Toggle / show / hide a layout node |
| `layout.resize` | **id**, px, weight | Size a layout node |
| `layout.select` | **stack**, **child** | Show one child of a stack |
| `layout.reset` | name | Reset a shipped layout and its clicks (old file kept aside) |
| `mod.open` | **name** | Toggle a mod as the overlay its mod.json describes |
| `overlay.close` | id | Close an overlay (default: top dialog or popup) |
| `app.help` | – | Toggle the keys dialog |
| `app.welcome` | – | Show the welcome again |
| `app.whats_new` | – | Show release notes (auto once after update) |
| `app.feedback` | message, from | Draft feedback for the person to review and send |
| `app.palette` | – | Toggle the command palette |
| `app.settings` | section | Open settings (look, alerts, updates) |
| `app.check_updates` | – | Look for newer Herdr and herdr-core |
| `app.update_core` | – | Update herdr-core; terminals keep running |
| `app.reset` | – | Escape hatch: set layout and keys aside, back to default screen |
| `app.theme` | **name** | Use a theme (preset or themes/<name>.json) |
| `app.zoom_in` / `app.zoom_out` / `app.zoom_reset` | – | Bigger / smaller / normal size |
| `app.update` | – | Restart into the downloaded update (terminals keep running) |
| `app.quit`, `app.hide`, `app.hide_others`, `app.show_all` | – | macOS only: quit / hide / hide others / show all |
| `window.close` | – | Close the window (Linux: quits) |
| `window.minimize` | – | Minimize |
| `window.maximize` | – | Maximize, or restore |

Default global keys (Windows/Linux, `defaults/keys.json`): `ctrl-shift-t` tab.new, `ctrl-shift-n` space.new, `ctrl-shift-w` tab.close, `ctrl-shift-o` split right, `ctrl-shift-e` split down, `ctrl-shift-x` pane.close, `ctrl-shift-z` pane.zoom, `ctrl-shift-p` palette, `ctrl-shift-k` picker, `ctrl-shift-b` toggle sidebar, `ctrl-shift-l` next layout, `ctrl-,` settings, `F2` rename tab, `shift-F2` rename space, `ctrl-1..9` tab by position, `alt-1..9` space by position, `ctrl-(shift-)tab`/`ctrl-pageup/down`/`alt-[ ]` tabs, `ctrl-shift-pageup/down` spaces, `alt-up/down` agents, `alt-hjkl`/`alt-left/right` focus pane, `alt-shift-hjkl` swap pane, `ctrl-+/=/-/0` zoom. Terminal section: `ctrl-shift-c`/`ctrl-insert` copy, `ctrl-shift-v`/`shift-insert` paste, `ctrl-v` paste_image, `ctrl-shift-f` search, `ctrl-backspace`/`ctrl-left`/`ctrl-right` send word-edit sequences.

## Open questions (Unclear in the source read)
- Which shell herdr-core starts for a plain terminal on Windows (core is outside this repo); the app's own `shell()` prefers `pwsh.exe` on PATH, else `powershell.exe`.
- Whether the `pane` header mod is the live default (`main.json` says yes, `AGENTS.md:916-917` says it's an unused example).
- Where the "global" context menu is opened from.
- Any minimum pane size beyond the 24 px drag clamp.
