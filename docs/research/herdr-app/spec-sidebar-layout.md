# herdr-app: main window layout and left sidebar (porting spec)

Source: https://github.com/herdrdev/herdr-app (Apache-2.0), commit `321c9a8` (2026-11-16).
These are observations from reading that commit. It describes behaviour only; no code was copied beyond short identifiers and strings.

Path abbreviations used in references:

| Short | Full path |
|---|---|
| `main.json` | `defaults/layouts/main.json` |
| `terminals.json` | `defaults/layouts/terminals.json` |
| `chrome.js` | `defaults/mods/workspace-chrome/main.js` |
| `status.js` | `defaults/mods/workspace-status/main.js` |
| `spaces.js` | `defaults/mods/spaces/main.js` |
| `agents.js` | `defaults/mods/agents/main.js` |
| `tabs.js` | `defaults/mods/tabs/main.js` |
| `menu.js` | `defaults/mods/menu/main.js` |
| `newmenu.js` | `defaults/mods/new-menu/main.js` |
| `pane.js` | `defaults/mods/pane/main.js` |
| `d.ts` | `defaults/herdr.d.ts` |
| `AGENTS.md` | `defaults/AGENTS.md` |
| `settings.rs`, `window.rs`, `model.rs`, `layout.rs` | `src/…` |

Conventions in herdr:
- All px values are design px at zoom 1. Zoom (0.5..3) scales terminals, mods and layout px (`AGENTS.md:723-724`, `settings.rs:817-827`). Mod text sizes are their own px values and follow zoom (`AGENTS.md:347-348`).
- The UI is GPUI with a Tailwind-like builder API: `h_flex` = row, `v_flex` = column, `px/py/pl/pr/pt/pb` = padding, `gap`, `rounded`, `size(n)` = width and height n.
- Theme colours are named tokens (`theme().chrome` etc.); see section 5.4.

---

## 1. Window regions (`main.json`)

### 1.1 Tree

```
root (row, titlebar none, background canvas, divider 1px "border")
├── sidebar (column, 268px, min 44, max 480, background chrome, resizable:false)
│   ├── brand           block 46px, window_drag   → mod workspace-chrome
│   ├── sidebar-lists   column (flex)
│   │   ├── spaces      block weight 1, min 90    → mod spaces
│   │   └── agents      block weight 1, min 90    → mod agents
│   └── sidebar-footer  block 69px                → mod workspace-chrome {footer:true}
└── work (column, flex, resizable:false)
    ├── tabs            block 42px, window_drag   → mod tabs
    ├── work-body       row, padding 8, gap 8
    │   ├── main        block: active tab's pane tree (framed panes, header mod "pane")
    │   └── panel       column 368px, min 280, max 900, hidden:true
    │       └── panel-body  stack, active "files"
    │           ├── files    → mod files
    │           └── changes  → mod changes
    └── status          block 28px                → mod workspace-status
```

| Fact | Ref |
|---|---|
| Root is a `row`, `"titlebar": "none"` (the layout draws its own title bar), macOS `traffic_lights` [12,16] | `main.json:2-8` |
| Root background `canvas`; `terminal_padding` [14,12] (x, y px between terminal edge and text) | `main.json:9-13` |
| Root `divider` 1 px, colour `border`, between sidebar and work | `main.json:14-17` |
| `sidebar`: 268 px, min 44, max 480, background `chrome`, `resizable: false` | `main.json:20-28` |
| `brand` 46 px, `window_drag: true`, mod `workspace-chrome` | `main.json:31-38` |
| `spaces` / `agents`: weight 1 each, min 90 | `main.json:46-64` |
| `sidebar-footer` 69 px, mod `workspace-chrome` with props `{footer: true}` | `main.json:70-79` |
| `work` column `resizable: false` | `main.json:85-87` |
| `tabs` 42 px, `window_drag: true`, mod `tabs` | `main.json:90-97` |
| `work-body` row, `padding: 8`, `gap: 8` | `main.json:101-104` |
| `main` block shows `"tab": "active"` with pane frame: gap 8, radius 10, background `background`, `lone: true` (framed even with one pane), border 1 `border`, focus border 1 `accent`, header mod `pane`, height 30, position top, reveal always | `main.json:107-131` |
| `panel` 368 px, min 280, max 900, `hidden: true` at start; a `stack` `panel-body` with `files` (active) and `changes` | `main.json:136-163` |
| `status` 28 px, mod `workspace-status` | `main.json:171-177` |

### 1.2 What `resizable` means (and which boundaries drag)

- `resizable` belongs to a row or column and controls whether the boundaries between *its own* children can be dragged. Default true. It does not inherit (`AGENTS.md:243`, `layout.rs:498-500`).
- The separator between two children is `gap + divider.width` wide. When resizable, a 6 px hit strip (`HANDLE = 6`, `window.rs:33`) is centred on it, even when the separator is 0 px wide (`window.rs:4649-4665`, `window/pane_size.rs:27-29`).
- Drag rule: the first child's new size is clamped to [24, total - 24], then to that child's own `min`/`max`. Only the first child's (left/top) min/max is applied. Px-sized children store px; weighted pairs redistribute weight. If both together are 48 px or less, nothing happens (`window.rs:4671-4720`).

Resulting behaviour in `main.json`:

| Boundary | Draggable? | Why |
|---|---|---|
| sidebar ↔ work | yes, sidebar 44..480 | root has no `resizable` (default true); sidebar is the first child with min/max |
| brand / lists / footer | no | `sidebar.resizable: false` |
| spaces ↔ agents | yes (6 px hit strip, no visible line) | `sidebar-lists` has no `resizable` |
| tabs / work-body / status | no | `work.resizable: false` |
| main ↔ panel | yes, with an 8 px gap | `work-body` has no `resizable`. Panel min/max (280/900) is not enforced by the drag code, since only the first child's min/max is checked (my reading of `window.rs:4695-4701`) |

### 1.3 Collapsing the sidebar (the "rail")

- There is no collapsed flag. Mods treat the sidebar as a rail when its width is **under 160 px** (`chrome.js:15`, `spaces.js:20`, `agents.js:17`, `AGENTS.md:449`). Dragging it narrow has the same effect.
- Toggle button (brand row) and `»` (bottom of the agents list in rail mode) both call `toggleSidebar()` (`chrome.js:18-24`, `agents.js:20-26`):
  - Expanded → remember the current width in shared `sidebar.width`, then `layout.resize` sidebar to **44 px** (76 on macOS so the traffic lights fit).
  - Rail → resize back to the stored `sidebar.width`, default **268**.
  - It also writes shared `sidebar.collapsed` for older mods.
- Hiding entirely: `ctrl-shift-b` = `layout.toggle:sidebar` (sets `hidden`) (`keys.json:48`, `AGENTS.md:447-448`).

### 1.4 Right panel (Files / Changes)

- Shown and hidden by the two buttons at the right of the tabs bar (section 3). It sits to the right of `main` inside `work-body`, between the tabs bar and the status bar.
- Remembered per space: switching spaces restores that space's choice (closed if it never had one). Stored in mod localStorage `panels` (`tabs.js:62-88`).

### 1.5 Second shipped layout, "Terminals" (`terminals.json`)

For reference only: no sidebar. A column with padding [6,6], gap 8, a 38 px `terminals-bar` and the tab area. Panes have a 38 px header (`terminals-pane`), radius 12. It brings its own colours and `font_size: 15` (`terminals.json:1-48`).

---

## 2. Left sidebar

### 2.1 Brand row (top, 46 px; `chrome.js:48-53`)

- Row, background `chrome`, padding-left 14 (6 in rail mode; 76 on macOS when not fullscreen, to clear the traffic lights), padding-right 10, gap 4, text 13.5.
- Expanded: the app logo (`frame/welcome/herdr.png`, 20 px, radius 5) and the word "Herdr" (semibold, `foreground`), filling the row.
- Then two 26 px icon buttons, radius 6, colour `muted`, hover background `hover` and text `foreground` (`chrome.js:27-31`):
  - **Search** (`search.svg` 15 px): opens the `picker` mod (`mod.open {name: "picker"}`). Hidden in rail mode. Same as `ctrl-shift-k` (`keys.json:51`).
  - **Sidebar toggle** (`sidebar.svg` 15 px): `toggleSidebar()`. On the macOS rail it is left out; the agents list's `»` does the job instead.
- The whole block is a window-drag area (`main.json:36`). Buttons stop propagation on mouse-down so they don't start a drag.

### 2.2 Spaces list (`spaces.js`)

Purpose, in the file's own header comment: coloured avatars, worktrees nested under their checkout, grouped by machine when more than one is known. After the name it shows only *unusual* git info. **Agent state is not shown here in the expanded sidebar; it lives in the agents list.** Click to switch, right-click for a menu, drag to reorder (`spaces.js:1-4`).

#### Header (`spaces.js:541-545`)
- Height 31, padding-left 16, padding-right 10, text 11.5, colour `dim`. Label "Spaces" on the left (hidden in rail mode).
- A 26 px "+" button on the right (`plus.svg` 14 px, radius 6, hover `hover`/`foreground`) runs `space.new`.

#### List container (`spaces.js:546-550`)
- Fills the rest, padding x 8 (0 in rail mode), padding-bottom 14, vertical scrollbar always shown, base text 13, background `chrome`.
- Auto-scroll: when the active space or its position in the list changes, the list scrolls that row into view. It also re-scrolls when the list's own size changes. A row already in view stays put. Machine headers, login notes and loading rows count as list children (`spaces.js:533-539`, `548`; tests `spaces.test.ts:479-547`).

#### Space row, expanded (`spaces.js:444-499`)
- Height **30** (28 for a nested worktree row) (`spaces.js:28`), padding x 8, radius 8, contents centred vertically.
- Active space: background `selected`. Others: background `hover` on hover.
- Contents, left to right, gap 9:
  1. **Avatar** (top-level rows only): 18×18 square, radius 5, filled with the space's colour, containing the first character of the name in upper case (bold, 10.5, white `#ffffff`), or `?`.
  2. Worktree rows replace the avatar with padding-left 22 plus a `branch.svg` icon (13 px, `dim`).
  3. **Name**: `foreground`, truncated with ellipsis, never narrower than 48 px (`SIDE_MIN`, `spaces.js:32`).
  4. **Git tail** (right-aligned, only if the folder is a git repo), text 11.5, `dim`, gap 5 (`spaces.js:425-442`):
     - `repo · branch`, where the repo name is dropped on worktree rows or when it equals the space name (case-insensitive), and the branch is dropped when it is `main` or `master` or equals the space name. Detached HEAD shows `(detached)`.
     - `↑N` in `ansi2` (green) when ahead of upstream, `↓N` in `ansi1` (red) when behind.
     - When the row is full, name and tail shrink in proportion, both down to 48 px minimum.
     - Example from the source comment: `herdr · spike ↑2 ↓1`.
- No status dot, no agent counts, no path shown in the expanded row. (`AGENTS.md:926-927` also mentions "uncommitted changes", but the shipped code reads only branch and ahead/behind. Treat the AGENTS.md wording as stale.)

**Avatar colour** (`spaces.js:52-54`, `517-531`, `447-449`)
- Palette names in order: `ansi6, ansi3, ansi4, ansi5, ansi2, ansi1`. A new space gets the least-used name (ties: earlier in that order). Stored in shared `space.colors` {spaceId: name}. Entries for closed spaces are dropped.
- Fixed colours for those names, the same in light and dark themes: ansi6 `#22a99a`, ansi3 `#e3814f`, ansi4 `#5b8def`, ansi5 `#8b7cf6`, ansi2 `#3fae6a`, ansi1 `#d9649a`. A stored `#hex` value is used as-is.
- A space named `~` is not assigned a colour and falls back to `ansi6`.

**Space name**: the name someone gave, else "where its first terminal is now": the git checkout's top folder (worktree's own), else the pane's folder, else the space's folder. Displayed as its last path segment, `~` for home (`model.rs:271-280`, `model.rs:633-646`, `d.ts:127-129`).

**Git lookup** (`spaces.js:229-264`, `105-147`): per (machine, folder of the space's first terminal) it runs `git rev-parse --show-toplevel` and `--git-common-dir`, then `git symbolic-ref -q HEAD` and `git rev-list --left-right --count HEAD...@{upstream}`. At most 6 places at a time (`AT_ONCE`). It is refreshed when a space is new or its folder changes, when an agent stops working or a program in front of the shell ends, when a remote machine becomes ready, through a folder watch on local repos, and as a 60 s fallback while the window is active. A space created while the list is running waits up to 1.5 s for its git info so a worktree appears nested immediately instead of jumping (`spaces.js:75-78`, `301`).

#### Ordering and worktree nesting (`spaces.js:298-336`)
- Base order is the model order (user-defined via `space.move`). The model keeps local spaces first, then machines by name; `space.move` is clamped inside the space's machine block (`model.rs:184-198`, `AGENTS.md:101-102`).
- A space whose checkout is a *linked worktree* of a repo that also has a main-checkout space is moved directly under the first main-checkout space of that repo, with tree glyph `├─` / `└─`. This sets `tree`, so the row is 28 px with the branch icon. Orphan worktrees, whose main checkout has no space, stay where they are. The mod writes the nested order back to the model with `space.move` calls and retries if a sync conflict loses the write (`spaces.js:316-334`; tests `spaces.test.ts:304-437`).

#### Grouping by machine (`spaces.js:338-419`)
- If no remote machines are known: a flat list, with no header for this computer.
- Otherwise each machine gets a header, starting with `null` ("This computer") and then remote names sorted alphabetically. Remote names come from spaces, `hc machine ls` (refreshed every 15 s) and machines being reconnected.
- **Machine header** (expanded): height 26, padding x 8, gap 7, text 11, `dim`, hover background `hover`. Contents:
  - `⚠` in `blocked` colour if the machine stops agents on logout. Click opens the `machine-add` dialog.
  - `monitor.svg` (local) or `server.svg` (remote), 13 px.
  - The name, or "This computer", truncated.
  - The state word when not connected (for example "asleep", "unreachable", with underscores turned into spaces).
  - When rebuilt or replaced: "rebuilt ·" plus a "Connect" / "Connecting…" link.
  - When connected: a small "+" link that runs `space.new {machine}`.
  - A 6 px dot, `ansi2` when connected, else `dim`.
  - A chevron (`chevron-down` when open, `chevron-right` when folded), 11 px.
  - Click: fold or unfold the machine. Persisted in localStorage `machines.folded`.
- Under a machine that needs a login: "needs a login: run" (11, `blocked`) and the command `hc machine login <name>` with a Copy/Copied button (`spaces.js:405-412`).
- An empty machine shows a 28 px row "+ New space there" (12, `dim`) that runs `space.new {machine}`. While its layout is loading it shows "Loading…" instead (`spaces.js:352`, `396-402`).

#### Mouse interactions on a space row (`spaces.js:466-489`)
| Gesture | Effect |
|---|---|
| Left press then release on the same row | `space.select {id}` (selection happens on mouse-up, not mouse-down) |
| Press, move onto another row of the **same machine**, release | Drag reorder: `space.move {id: dragged, index: targetIndex+1}`. While dragging, the dragged row is drawn at opacity 0.5 and the target shows a 2 px `accent` line at its bottom (dragging down) or top (dragging up). Rows of another machine are not drop targets |
| Right press | Opens the context menu popup (`menu` mod, kind popup, width 200, max height 260) with `{tab: "space", target: {id}}` (`spaces.js:288-291`) |
| Double-click | Nothing on space rows. Double-click rename exists only on tabs (`tabs.js:113`) |
| Hover | Background `hover` (not on the active row) |

Space context menu items (`menu.js:64-70`), rows 31 px, text 12.8, icon 14 px `muted`, shortcut hint on the right 11 `dim`, separators before Close (`menu.js:142`):
1. **Rename** → `space.rename {id}`. With no name given, herdr opens a `prompt` dialog (width 380, title "rename space", hint "empty: named after its folder", prefilled with the given name or empty). An empty name restores the automatic name (`window.rs:3021-3052`). Key `shift-f2` (`keys.json:61`).
2. **New worktree** / **Open worktree** (only when the space's folder is a git checkout) → worktree dialog.
3. **Remove worktree** (only when it is a linked worktree).
4. **Close** → `space.close {id}`. Asks "Close space?" with detail `"<name> — N tabs, M terminals"`, then ends its terminals (`window.rs:3067-3080`). Closing the last tab of a space also routes through this confirmation (`window.rs:3120-3131`).
- Keyboard in the menu: ↑/↓ move, Enter runs, Esc closes (`menu.js:131-137`).
- There is **no pin, archive or delete-without-confirm for spaces** in the shipped mods. Pinning exists only for agents (2.3).

#### New-space flow
- `space.new` without arguments creates the space immediately, with no dialog and no folder picker. Its terminals start in the focused terminal's folder. For a click or key it goes on the focused space's machine (`commands.rs:32-33`, `window.rs:2998-3002`, `5097-5101`). It is shown at once. Name and first tab are unnamed, so the space is called after its folder and the tab after its number.
- Entry points: the header "+" (`spaces.js:544-545`), the machine header "+" and "New space there" (with `{machine}`), the tabs bar "+" → new-menu "New space" (`newmenu.js:69`), and `ctrl-shift-n` (`keys.json:49`).

#### Rail mode (compact) (`spaces.js:501-515`)
- Each space is a 32×26 cell (`RAIL_CELL = 32`), radius 6, centred. It shows the space's **position number** (i+1), `foreground` if active else `muted`, active background `selected`.
- At right 5 px it shows a status glyph: `●` if the space's rollup status is `blocked` or `done`, else `·`. The glyph is coloured with the theme token of that status (`unknown` if none) (`spaces.js:56-63`). This is the only place the spaces list shows agent state.
- Click → `space.select`. There is no drag and no context menu in rail mode.
- Machine headers become a 32 px icon cell with a 6 px connection badge and a tooltip. "New space there" becomes an 18 px bordered "+" box.

### 2.3 Agents list (`agents.js`)

The list holds terminals whose program reports a status (`agents()`, `d.ts:195-196`), in space/tab/pane order. Sub-agents without their own pane appear as tree rows under their parent (`agents.js:170-206`).

#### Header (expanded) (`agents.js:424-446`)
- Padding x 16, height 41 with padding-top 14 and padding-bottom 5, so it reads as a section gap below the spaces list. Label "Agents" (11.5, medium, `dim`).
- On the right a sort icon button (14 px, `dim`, hover `foreground`) with tooltip `Sort agents: <label>`. It opens a 200×140 popup titled "Sort agents" with two choices: **Priority** (`clock.svg`) and **Grouped by space** (`layers.svg`). The current one has a check mark. The choice is stored in shared `agents.sort`; the default is Priority (`agents.js:59-63`, `140-164`).
- The whole agents block has a 1 px top border `border` (`agents.js:421-422`). That line is the visible separator between Spaces and Agents.
- Empty state: "No agent status yet. Agents show up here when they report their status. Older versions and agents inside tmux may not." (12, `dim`, padding x 12) (`agents.js:447-449`).

#### Ordering (`agents.js:231-244`)
1. Sub-agents shown in a parent's tree are removed as top-level rows, unless pinned.
2. Priority mode sorts by urgency desc (`blocked 4 > done 3 > working 2 > idle 1`, `agents.js:52`). A parent takes the most urgent of itself and its sub-agents (`agents.js:238`). Ties go to the most recent state change (`since`), then the higher `seq`.
3. Grouped mode keeps model order (space, tab, pane).
4. Pinned agents always go first. The final order is pushed to `setAgentOrder` so `agent.next`/`agent.prev` (`alt-down`/`alt-up`, `keys.json:43-44`) follow the list.

#### Group headers (`agents.js:376-412`)
- If any agent is pinned, the first group is "Pinned".
- Priority mode: sections **Needs you** (`blocked` colour), **Done** (`done`), **Working** (`working`), **Idle** (`dim`), each with its member count (`agents.js:64-65`).
- Grouped mode: one header per space (the space name, `dim`). Consecutive agents of the same space form a group.
- Header: padding x 8, padding-top 8, height 30, gap 6, text 11, medium. Hover turns the text `foreground`. Click folds or unfolds the group (in memory only).
  - Folded: shows the count, then in grouped mode the most urgent non-idle status as a compact status mark, then `chevron-right` 11 px `dim`.

#### Agent row, expanded (`agents.js:298-374`)
- Height **30**, padding x 8, radius 8, gap 9, pointer cursor. Focused agent: background `selected`. Hover: background `hover`.
- Contents, left to right:
  1. **Program icon** 16 px from the program table (`svg` tinted `foreground` if `tint`, else image) (`agents.js:75-79`). If the agent has sub-agents, clicking the icon folds or unfolds them.
  2. **Name**, colour `muted` when idle and not focused, else `foreground`, truncated, max width 200 (120 when a detail follows):
     - Priority mode or a pinned row ("full"): the **space name**, then `· <detail>` in `dim`.
     - Grouped mode: the detail alone, because the space header already names the space.
     - The detail is the tab name if someone named it, else the agent's live terminal title. The title is cleaned of a leading status glyph or spinner and Codex's `[ ! ] ` / `[ . ] ` mark. It is ignored when it is a path, contains `@`, or equals the program name or key (`agents.js:28-51`, `279-296`). An automatic tab number ("2") is never shown unless two rows of the same space would otherwise read the same. If all that is left is a number, the program name is shown.
  3. **Machine** name (text 10, `dim`) for a remote agent (address `machine/tN`) (`agents.js:301-302`, `369`).
  4. **Fold chevron** if it has sub-agents: 18 px high, text 11. Open: `chevron-down` `dim`. Folded: `chevron-right` plus the child count, coloured by the most urgent child status (`agents.js:253-277`).
  5. On hover: a **pin button** (`pin.svg` / `pin-off.svg` 13 px, tooltip "Pin to the top" / "Unpin") → `terminal.pin {terminal, pinned}`. Pins are shared across clients (`agents.js:322-338`, `d.ts:190-191`).
     Not hovered: **time since last state change**, text 11 `dim`: `<1m`, `Nm`, `Nh`, `Nd`. Done and idle count as one state for this clock. Stamps persist in localStorage `changed` and are pruned after 30 days. The list re-renders every 30 s (`agents.js:67-73`, `92`, `95-110`, `372`).
  6. **Status mark** (see 4.2). It is compact (`!`) in priority mode for unpinned rows, because the section header already says "Needs you".
- Click → `pane.focus {pane}`, which switches space/tab as needed (`agents.js:359`).
- No context menu, no double-click action, no drag on agent rows.

#### Sub-agent row (`agents.js:182-206`)
- Height 26, padding-left 39, padding-right 8, text 12.5, radius 8. Tree lines are drawn as 9×17 boxes with a 1.5 px left/bottom border in colour `input` and a 6 px bottom-left radius.
- Then a 14 px icon, the name (`name || template || program.name || address`; `foreground` if it is the shown agent, else `muted`) and a status mark.
- A sub-agent's `done` shows as idle; its `error` shows as blocked (`agents.js:53-58`).
- Click: focus its pane if it has one, else `terminal.peek` (a peek overlay over the tab).

#### Rail mode (`agents.js:303-318`, `451-467`)
- Rows of height 26, margin x 6, padding x 2, radius 6. They show the 1-based position number right-aligned and a 14 px compact status mark. No groups and no sub-agent trees.
- The bottom of the list has a 28 px row with `»` (13, `dim`) that expands the sidebar.

### 2.4 Bottom-left footer (69 px; `chrome.js:35-46`)
- Column, background `chrome`, top border 1 px `border`, padding x 8, padding-top 4, padding-bottom 8, text 13.
- Two rows, each 28 px high, padding x 8, gap 9, radius 8, hover background `hover` + text `foreground`, with a 14 px icon:
  1. **Settings** (`settings.svg`, `muted`) → `app.settings`. When an app update is downloaded and ready, a 6 px `accent` dot is appended (absolutely at top 5 / right 6 in rail mode).
  2. **Add machine** (`plus.svg`, `dim`) → opens the `machine-add` mod.
- Rail mode: 32 px wide icon-only cells with tooltips.

---

## 3. Tabs bar and top-right buttons

The Files and Changes buttons are in the **`tabs` mod** (42 px bar over the work area), not in workspace-chrome. workspace-chrome only draws the sidebar's brand row and footer.

`tabs.js:144-155`, row with padding x 10, gap 3, background `chrome`, bottom border 1 px `border`, window-drag area:
- **Tabs** (scrollable horizontally; the wheel switches tabs, one per notch): height 29, padding x 10, gap 7, radius 7, text 12.8. Active tab: background `raised`, border `border`, text `foreground`. Inactive tabs: background `chrome`, text `muted`. Contents: program icon 14 px, label (max width 170, ` Z` suffix when zoomed), a `working` spinner (12 px, one turn per 900 ms) or a 6 px dot in `blocked`/`done` colour, and on the active tab a close × (12 px). Double-click → rename. Drag → `tab.move`. Right-click → tab menu: Rename / New tab / Close (`tabs.js:104-125`, `menu.js:58-63`).
- **"+"** (26 px, `dim`) → `new-menu` popup (width 250): "Terminal", then "Start an agent" (installed agents numbered 1-9), "New space", "New worktree…", "Open worktree…" (`tabs.js:150`, `newmenu.js:62-71`).
- **Files** (`file.svg` 14 + "Files") and **Changes** (`changes.svg` 14 + "Changes"): 26 px high, padding x 8, gap 5, radius 6, text 12. When on: background `selected` + `foreground`. Off: `chrome` + `muted`. Click toggles: if that panel is showing → `layout.hide panel`; else `layout.select {stack: "panel-body", child}` + `layout.show panel` (`tabs.js:134-143`). Which buttons appear comes from shared `workspace.panels` (default `["files","changes"]`) (`tabs.js:132`).
  - Files panel: a lazy file browser of the focused pane's folder, with read-only previews (`files/main.js:1`).
  - Changes panel: uncommitted git changes of that folder. The header reads "Changes", `N files`, `+add` (`ansi2`), `−del` (`ansi1`); then a file list (status letter A/D/U/M) and a diff viewer in the terminal font at 11 px. Empty state: "No uncommitted changes" (`changes/main.js:182-206`).
- **Window buttons** when the OS draws none (Windows/Linux with `titlebar: none`): minimise, maximise/restore, close, each a 26 px button with 16 px Windows-style icons (`tabs.js:153-155`).

Status bar (28 px, `status.js:23-29`; line numbers are within that file): row with padding x 14, gap 16, background `chrome`, top border `border`, text 11.8, `muted`:
- machine icon (13 px) + the active space's machine name or "This computer"; the branch icon + the branch if any; a 6 px `dim` dot + "N agent(s)", followed by `· N need(s) you` in `blocked` colour when any agent is blocked; on the right, the palette hint, for example `Ctrl+Shift+P  commands` (click → `app.palette`).

Pane header (30 px, inside each framed pane; `pane.js:59-91`): program icon 14, name (medium, max width 260), folder with `~` for home (`dim`), status (spinner / "Needs you" pill / done dot). On hover it shows split-right, split-down, zoom and close buttons (14 px icons). Click focuses the pane.

---

## 4. Status / attention model

### 4.1 States and rules (`model.rs`)
- Four UI states: `working`, `blocked`, `done`, `idle` (`d.ts:94`). From the agent's reported word: `working` → working; `blocked` or `error` → blocked; anything else → `done` if unseen, else `idle` (`model.rs:596-605`).
- **How done appears and clears.** When an agent goes from working/blocked to anything else, its `seen` flag is set to whether the person was looking at it at that moment. Not looking makes it `done` (`model.rs:529-548`). "Looking" means the agent's terminal is visible on screen *and* the window is active. Whenever that holds, herdr marks those agents seen, so `done` turns into `idle` (`window.rs:1439-1449`, `1619-1622`). Clicking an agent row (focusing its pane) makes it visible, so it clears. A shared `herdr.seen` turn lets another client's viewing clear it too, or a phone's "mark unread" bring it back (`model.rs:588-605`, `window.rs:1630-1640`).
- Priority / rollup: blocked 4 > done 3 > working 2 > idle 1 > none 0 (`model.rs:97-106`). A space's and a tab's `status` is the most urgent of their agents (`model.rs:622-625`, `d.ts:109-110`, `140-141`).
- Alerts (toast, sound): becoming blocked always alerts. Finishing alerts only if the person wasn't looking. Sub-agents and terminals outside the person's spaces never alert (`model.rs:550-572`, `d.ts:431-432`).

### 4.2 Visual marks in the sidebar (`agents.js:208-217`)
| State | Mark |
|---|---|
| working | `spinner.svg` 12 px, margin x 1, colour `working`, rotating one turn per 900 ms |
| blocked (full) | pill: height 18, padding x 6, radius 5, background `blocked` at alpha `0x26` (~15%), text "Needs you" 10.5 semibold in `blocked` |
| blocked (compact) | 14×14 box, radius 4, same background, bold "!" 10.5 in `blocked` |
| done | 7 px filled dot, radius 4, margin x 3, colour `done` |
| idle | 7 px ring, 1.5 px border in `idle` (fallback `dim`), margin x 3 |

- Spaces list: in expanded mode it shows no state. In rail mode it shows `●` for blocked/done, `·` otherwise, coloured by the state token (section 2.2).
- Tabs: spinner for working, 6 px dot for blocked/done (`tabs.js:122-123`).

---

## 5. Fonts, sizes, colours

### 5.1 Fonts
| Item | Value | Ref |
|---|---|---|
| UI font family (mods) | **Geist** (bundled) | `settings.rs:555`, `settings.rs:888` |
| UI base font size | **13** px × zoom (used as GPUI rem size) | `settings.rs:556`, `settings.rs:825-827`, `window.rs:5284` |
| Terminal font | **JetBrains Mono** 14 px, weight 500 (bold stays 700) | `settings.rs:548-550`, `settings.rs:901`, `AGENTS.md:347-360` |
| Also bundled | Geist Mono (variable + italic) | `settings.rs:886-888` |
| Terminal line height / cell width | multipliers, default 1, clamp 0.8..2 | `settings.rs:552-553`, `595-596` |
| Terminal padding | main layout [14, 12] px (x, y); app default [10, 6] | `main.json:10-13`, `settings.rs:564` |

Mods set explicit text sizes. The sidebar mods use: section label 11.5, row text 13, git tail 11.5, group header and machine header 11, durations 11, menu 12.8, status bar 11.8, brand 13.5, pill 10.5.

### 5.2 Sidebar size cheat sheet (design px)
| Element | Height | Other |
|---|---|---|
| Sidebar width | 268 default, 44..480 | rail < 160, rail width 44 (macOS 76) |
| Brand row | 46 | pl 14, pr 10, logo 20, buttons 26 (icon 15) |
| Spaces header | 31 | pl 16, pr 10, "+" 26 (icon 14) |
| Space row | 30 (worktree 28) | px 8, radius 8, gap 9, avatar 18 r5, list px 8, pb 14 |
| Machine header | 26 | px 8, gap 7, icons 13, dot 6, chevron 11 |
| Loading / "New space there" row | 28 | |
| Agents header | 41 (pt 14, pb 5) | px 16, sort icon 14 |
| Agent group header | 30 (pt 8) | px 8, gap 6 |
| Agent row | 30 | px 8, radius 8, gap 9, icon 16, max name 200/120 |
| Sub-agent row | 26 | pl 39, icon 14 |
| Rail cells | 26 | width 32 (spaces) / mx 6 (agents) |
| Footer | 69 total; rows 28 | px 8, pt 4, pb 8, icon 14 |
| Tabs bar | 42; tab 29; buttons 26 | |
| Status bar | 28 | px 14, gap 16 |
| Pane header | 30 | |
| Work body | padding 8, gap 8; pane radius 10 | |
| Right panel | 368 default, min 280, max 900 | |

### 5.3 Popups
Context menu: width 200, padding 5, background `popover`, items 31 px, radius 7, selected item background `selected` (`menu.js:120-160`, `spaces.js:290`). Sort popup: 200×140 (`agents.js:441`). New menu: width 250 (`tabs.js:150`).

### 5.4 Theme tokens
Tokens (`d.ts:323-328`): background (terminal), surface, canvas (behind cards), chrome (sidebars and bars), popover, raised, hover, selected, border, input, foreground, muted, dim, accent, accent_foreground, solid, solid_foreground, selection, working, blocked, done, idle, unknown, ansi0..ansi15, plus terminal_foreground, ring, cursor.

They are derived from a palette (`settings.rs:303-364`). For a dark theme with background `bg` and foreground `fg = #eeeeef` (`#1b1b1e` on light themes):
- chrome = bg darkened 32% (mixed toward black at 0.32); canvas = 16% darker; popover = mixed toward white at 0.06; raised = toward white at 0.09
- hover `#ffffff0e`, selected `#ffffff17`, border `#ffffff18`, input `#ffffff24`
- muted = mix(bg, fg, 0.6); dim = mix(bg, fg, 0.38); idle = unknown = mix(bg, fg, 0.38); ring = mix(bg, fg, 0.7)
- accent = ansi4; accent_foreground = bg
- working/blocked/done: preset dark themes use `#e5c07b` / `#f0705f` / `#76a8ff` (`settings.rs:30`). Theme files use ansi3 / ansi1 / ansi4.
- Light themes use warm-grey chrome and `#000000xx` overlays (`settings.rs:311-313`).

**Graphite** (the default dark theme, `settings.rs:32-42`, `83`). Background `#17191d`, terminal fg `#d2d4d9`, selection `#394454`, ansi4 `#96b2d7`. Derived values, computed by me with the formula above (not stored in the source; ±1 rounding possible):

| token | value | token | value |
|---|---|---|---|
| background / surface | `#17191d` | foreground | `#eeeeef` |
| chrome | `#101114` | muted | `#98999b` |
| canvas | `#131518` | dim / idle / unknown | `#696a6d` |
| popover | `#25272b` | ring | `#aeaeb0` |
| raised | `#2c2e31` | accent | `#96b2d7` |
| hover | `#ffffff0e` | working | `#e5c07b` |
| selected | `#ffffff17` | blocked | `#f0705f` |
| border | `#ffffff18` | done | `#76a8ff` |
| input | `#ffffff24` | solid_foreground | `#111215` |

Graphite ansi0-15: `#45484f #dc8c95 #a6bd91 #d8be87 #96b2d7 #b5a2ce #8cbdc3 #d2d4d9 #818793 #f0a5ad #bad5a2 #ead2a1 #b0cbef #cbb7e6 #a4d5db #f0f0f2` (`settings.rs:38-41`). Other presets: ink, moss (dark), paper (light, default light) (`settings.rs:43-84`).

**atom-one-dark** (`defaults/themes/atom-one-dark.json`): background `#21252b`, foreground `#abb2bf`, selection `#323844`, cursor `#abb2bf`, ansi `#454c59 #e06c75 #98c379 #e5c07b #61afef #c678dd #56b6c2 #abb2bf / #767676 #e06c75 #98c379 #e5c07b #61afef #c678dd #56b6c2 #abb2bf`. Because it is a theme file, working = `#e5c07b`, blocked = `#e06c75` and done = `#61afef`; the chrome colours are derived as above.

Note that avatar colours do not follow the theme (fixed `MARK` table, section 2.2), while the git ahead/behind colours (`ansi2`/`ansi1`) and the machine-connected dot (`ansi2`) do.

---

## 6. Open points / not verified
- How GPUI maps mod px to screen px at non-1 zoom was not traced in code. The source says mod sizes "follow zoom" (`AGENTS.md:347-348`).
- The hover-only pin button replaces the duration in the same slot. The exact pixel width of that slot is content-sized, with no fixed number.
- The `picker` (search), `machine-add`, `worktree`, `files` and `changes` mods were only skimmed, for their purpose and headers.
- Panel min/max during drag: see 1.2. My reading is that only the first child's limits apply. This was not confirmed by running the app.
