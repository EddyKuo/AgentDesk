# herdr-app Files and Changes panels: behaviour spec for porting

Source: https://github.com/herdrdev/herdr-app (Apache-2.0), read at commit `321c9a84bc928f1c31be59e73dad966d86a3e252` (2026-11-18). This is an observation of its behaviour, not copied code. Snippets are short and only where the exact value matters.

File abbreviations used in references:

| Abbrev. | Path |
|---|---|
| `files.js` | `defaults/mods/files/main.js` (179 lines) |
| `changes.js` | `defaults/mods/changes/main.js` (212 lines) |
| `tabs.js` | `defaults/mods/tabs/main.js` |
| `main.json` | `defaults/layouts/main.json` |
| `welcome.js` | `defaults/frame/welcome/main.js` |
| `d.ts` | `defaults/herdr.d.ts` |
| `files.rs` | `src/files.rs` (native folder watcher) |
| `mods.rs` | `src/mods.rs` (native `exec`, `readTextFile`) |
| `mod_files.rs` | `src/mod_files.rs` (1 MiB text read) |
| `settings.rs` | `src/settings.rs` (theme colour derivation, fonts) |
| `AGENTS.md` | `defaults/AGENTS.md` |

Both mods are JavaScript views on GPUI (`gpui-kit` / `gpui-base`); `files.rs` holds no listing, git or preview logic: it is only the recursive change watcher behind `watchFolder`. Listing and git all go through `exec` of external programs.

Note on the task brief: `workspace-chrome/main.js` does **not** open the panels (it renders the sidebar brand row and footer only). The buttons live in the **tab bar** mod, `tabs.js`.

---

## 1. Placement, open/close, size, shortcuts

### 1.1 Where the panels live

- Both mods declare `{ "place": "none" }` (`files/mod.json`, `changes/mod.json`): they are not auto-placed; the layout places them.
- `main.json:135-167`: a column node `panel` sits in the `work-body` row, **to the right** of the `main` block (the terminal panes), below the 42 px tab bar and above the 28 px status bar.
  - `"hidden": true` → closed by default (`main.json:138`).
  - Width `"size": { "px": 368 }`, `"min": 280`, `"max": 900` (`main.json:139-143`).
  - `work-body` row has `padding: 8`, `gap: 8` (`main.json:103-104`) and does not set `resizable`, so the main/panel boundary is drag-resizable (default `resizable: true`, `AGENTS.md:243`). The parent `work` column sets `resizable: false` (`main.json:87`), which only applies to that column's own children.
- Inside `panel` is a **stack** `panel-body` with `"active": "files"` and two children, block `files` (mod `files`) and block `changes` (mod `changes`) (`main.json:145-164`). Only one panel shows at a time; they are not side by side.
- Each mod draws its own card: `rounded(10)`, 1 px `border` colour, `background` fill (`files.js:169`, `changes.js:182`).

### 1.2 Opening and closing

- Two pill buttons at the right end of the tab bar, after the `+` new-tab button and before the window buttons (`tabs.js:150-152`): **"Files"** (icon `mods/files/file.svg`) and **"Changes"** (icon `mods/changes/changes.svg`, a git-compare glyph).
- Button: height 26, `px(8)`, gap 5, radius 6, text 12, icon 14; `bg(selected)` + `foreground` text when that panel is the one shown, else `bg(chrome)` + `muted` text; hover `bg(hover)` (`tabs.js:134-143`).
- Click behaviour (`tabs.js:139-142`):
  - If that panel is open and active → `layout.hide {id:"panel"}` (close).
  - Else → `layout.select {stack:"panel-body", child:id}` then `layout.show {id:"panel"}`. So clicking "Changes" while Files is open switches the panel content, it does not open a second panel.
- Which buttons appear: shared value `"workspace.panels"`, default `["files","changes"]` (`tabs.js:132`). The welcome flow's page 3 sets it to `[]` on first view and lets the user toggle each (`welcome.js:17-21`, `112-126`, `399`). Skipping the welcome leaves both on (comment at `welcome.js:397-398`).
- **Remembered per space** (`tabs.js:62-88`): the tab mod stores `{spaceId: "files" | "changes" | null}` in its `localStorage` key `"panels"`. On switching to a space it restores that space's choice (hidden if it never had one). A 1 s `pending` window prevents the restore from being recorded as a new choice (`tabs.js:73`, `81-84`).
- Keyboard: **no default shortcut** opens/closes either panel. `defaults/keys.json` has only `ctrl-shift-b` = `layout.toggle:sidebar` for layout toggling (`keys.json:48`). A user could bind `layout.toggle:panel` themselves (`d.ts:80` documents `layout.toggle|show|hide {id}`), but nothing ships.
- Inside Changes, the only key handling is `Escape` while the commit input is focused: it blurs the input and moves focus to the panel (`changes.js:183-185`). Enter in the input commits (`changes.js:43`). Files has no key handling.

---

## 2. Files panel

### 2.1 Root folder and machine (`files.js:6-12`)

```js
return { cwd: pane?.cwd || f?.cwd || space?.folder, on, space: space?.id };
```

- `cwd` priority: (1) the **focused pane's live cwd** (`PaneInfo.cwd`, "Its live folder", `d.ts:221-222`), (2) `focused().cwd` (a focused layout terminal block, `d.ts:263-274`), (3) the active space's `folder`.
- `on`: the machine name for remote spaces (`space.machine`), or the prefix before `/` of a layout terminal id when no model pane is focused. `null` = local.
- The context key is `JSON.stringify({cwd,on,space})` (`files.js:12`). Every `render()` compares it and re-runs `refresh` when it changes (`files.js:167`), so the tree **follows focus and `cd`**.
- Per-context memory (`files.js:75-80`): on a context switch the current expanded-folder set and opened file are saved in an in-memory `Map` keyed by context key and restored when that key comes back. Not persisted across app restarts.

### 2.2 Listing a folder (`files.js:16-39`)

One `exec` per folder, lazy (only root + expanded folders are listed).

- **Local Windows** (`window().platform === "windows"` and `on` is null): runs `powershell -NoProfile -Command "<script>"` (`files.js:22`, `27`):
  - Sets `[Console]::OutputEncoding` to UTF-8 (no BOM) so non-ASCII names survive.
  - `Get-ChildItem -LiteralPath $p -Force -ErrorAction Stop` → **hidden and system files included**.
  - `Where-Object Name -ne '.git'` → only `.git` is excluded.
  - `Select-Object -First 501` (one over the cap, to detect overflow).
  - Emits `{name, dir}` as compressed JSON. `dir` is true for a container unless it is a reparse point with a `LinkType` whose target does not exist (dangling dir symlink → shown as a file). Reparse points without `LinkType` (OneDrive folders) count as folders; junctions count as folders.
  - The path is passed as a single-quoted PowerShell literal, doubling `'` and the curly quotes `‘ ’ ‚ ‛` (`files.js:21-22`).
  - A single object result is wrapped into an array (`files.js:25`).
- **POSIX / remote**: `sh -c <script> files <path>` (`files.js:28-30`): globs `"$1"/* "$1"/.[!.]* "$1"/..?*` (dotfiles included), skips `.git`, prints `kind\0name\0` with `d` when `[ -d ]` (follows symlinks), stops after 501 entries, exits non-zero if the folder is not a directory.
- Remote Windows: if `sh` cannot start on the remote (`"cannot start sh: program not found"`), retries with the PowerShell path on that machine (`files.js:31-33`). Other errors are not masked (`scripts/files.test.ts:34-45`).
- Non-zero exit → error `stderr || "Cannot list this folder"` (`files.js:23`, `35`).
- **No `.gitignore` filtering in the listing.** Ignored folders such as `node_modules` and `build` are listed like any other. Git status only adds marks (2.5).
- Directory links are listed as folders and expanded one level at a time; cycles are not walked (`scripts/files.test.ts:91-129`, `AGENTS.md:183-186`).

### 2.3 Sort, limits, tree rows

- Sort per folder: **folders first, then `localeCompare` by name** (`files.js:68`). The cap is applied with `slice` before `sort`, so when a folder exceeds 500, which 500 are kept depends on listing order, not on sort order.
- Limits (`files.js:14`, `66-69`, `151`): `PER_FOLDER = 500`, `TOTAL = 5000` entries across all loaded folders, max nesting depth 40 when rendering. When hit, a footer line reads `"File list limited to 500 entries per folder and 5,000 total"` (`files.js:176`).
- No search or filter box.
- Row (`files.js:155-160`), height 28, `pl(8 + depth*18)`, `pr(8)`, gap 7, radius 7, pointer cursor, hover `bg(hover)`:
  1. Chevron 12 px (`chevron-down.svg` / `chevron-right.svg`, colour `dim`) for folders; a 12 px blank spacer for files.
  2. Icon 14 px, `folder.svg` or `file.svg` (Lucide outline, stroke 2), colour `muted`.
  3. Name, flex, truncated with ellipsis, `foreground`.
  4. Optional git mark letter, text 11 (see 2.5).
- No selected-row highlight in the tree.
- Tree container: vertical scrollbar, padding 6 (`files.js:174`).
- Empty / status text (padding 8, text 12, `dim`): the error string, else `"Loading files…"`, `"No files"` or `"No folder"` (`files.js:175`).

### 2.4 Expand / collapse (`files.js:109-128`)

- Single click on a folder row toggles it.
- Expand: add to `folders`; list it asynchronously if not cached (a `pending` set prevents duplicate loads). Errors go to the panel error line.
- Collapse: removes the folder **and every expanded descendant**, and drops their cached entries. Re-expanding re-lists from scratch, and child folders come back collapsed.

### 2.5 Git marks in the tree (`files.js:92-101`, `160`)

- `git rev-parse --show-toplevel` in `cwd`. If it succeeds:
  `git --no-optional-locks -c core.fsmonitor=false status --porcelain=v1 -z --no-renames --untracked-files=all` (the flags avoid writing under `.git`, which would retrigger the watcher, `files.js:95`).
- At most 5000 status entries are used. The `XY` code is mapped to one letter:
  - `??` or any `A` → **A**, colour `ansi2` (green)
  - any `U` → **U**, colour `ansi1` (red)
  - any `D` → **D**, colour `ansi1`
  - else → **M**, colour `blocked` (the status "blocked" colour; red-orange `#f0705f` in the dark presets, ansi1 for theme files, `settings.rs:25-30`, `310`)
- Marks are keyed `"<toplevel>/<relpath>"` and looked up as `"<root>/<treepath>"` where `root` is the cwd. Only **files** get a mark (with `--untracked-files=all` git lists files, not folders); folders show no aggregated mark.
- **Unclear / probable porting trap:** the lookup is plain string concatenation. On Windows, `git rev-parse --show-toplevel` prints `C:/x/y` while a pane cwd is likely `C:\x\y`, so marks would not match unless cwd uses forward slashes. Normalize paths in the port.
- Outside a git repo, there are simply no marks; no message.

### 2.6 File click → read-only preview (`files.js:130-148`, `171-177`)

- Clicking a file row **replaces the tree with a preview of that file in the same panel**. It does not open an external editor, insert the path into a terminal, or offer drag-and-drop.
- Header in preview mode: a `"←"` back button (20 px wide, `muted`) that returns to the tree, title "Files", and on the right the relative file path (instead of the root folder's basename) (`files.js:171-173`).
- Read via `readTextFile(root/path, {on})`: native read, **max 1 MiB**; larger files reject with `"Text file exceeds 1 MiB"`; invalid UTF-8 rejects too (`mod_files.rs:7-24`, `mods.rs:939-951`, `d.ts:27-29`). Remote: `cat --`, or `cmd /d /c type` when there is no `cat`.
- Outcomes:
  - `null` (gone) → `"This file is no longer available"`.
  - Contains NUL → `"Binary file · no text preview"`.
  - Any rejection → `"Cannot preview this file (binary, too large, or unreadable)"`.
  - Text over 400,000 characters → truncated, then `"\n… (file truncated)"` appended.
  - While loading → `"Loading file…"`.
- Viewer: `gpui-base` `Editor`, `readonly: true`, `font_family` = terminal font setting (default `"JetBrains Mono"`, `settings.rs:901`), `font_size: 12`, syntax highlighting by file extension (`files.js:140-144`). Extension aliases: `ts tsx mts cts jsx mjs → js`, `toml → yaml`, `jsonc → json`, `svelte vue → html`; otherwise the extension itself, `txt` when there is none (`files.js:13`). Token colours: keyword `ansi5`, string `ansi2`, comment `dim`, function `ansi4`, type `ansi3`, number `ansi1`, constant `ansi6`.
- The opened file is remembered per context (2.1) and reopened when that context returns.

### 2.7 Refresh and watching

- Triggers: on init, every **10 s** timer (`files.js:46`), on context change (`files.js:167`), and a `watchFolder(cwd)` loop for **local** contexts only (`files.js:49-57`; skipped when `on` is set, so remote relies on the timer).
- A refresh re-lists root plus every expanded folder sequentially, then recomputes git marks (`files.js:86-106`). Refreshes do not overlap (`loading` flag); errors are kept in `error`.
- Native watcher (`files.rs:153-457`, `d.ts:465-485`): events are batched for **150 ms**; up to 64 watched folders app-wide (idle ones are reclaimed); pending paths are capped at 1024, beyond which it reports the root ("refresh everything"). On **Windows and macOS** it watches recursively, **including ignored folders** (`files.rs:302-305`, `d.ts:468`). Linux-only extras: prunes git-ignored dirs via `git ls-files --others --ignored --exclude-standard --directory -z`, skips `.git/objects`, but keeps `.git` `refs`/`info` and HEAD/index; rejects trees above 4096 directories (`files.rs:229-289`). Access events are dropped (`files.rs:349`).

---

## 3. Changes panel

### 3.1 Root and context (`changes.js:5-11`, `63-77`)

- Same cwd/machine resolution as Files, but the key omits `space` (`changes.js:9`).
- `git rev-parse --show-toplevel` in cwd; all later git commands run in that **toplevel** (`cwd: root`).
- Not a repo (non-zero exit): list area shows `"Not a git repository"`; no files; no diff (`changes.js:76`, `198`).
- No cwd at all: `"No folder"`.
- On context change: clears files, open diff, commit message, notes (`changes.js:66-69`).

### 3.2 Git commands (all reads prefixed with `READ`, `changes.js:14`)

`READ = --no-optional-locks -c core.fsmonitor=false -c diff.autoRefreshIndex=false` (write nothing under `.git`, so the watcher does not loop).

1. `git status --porcelain=v1 -z --no-renames --untracked-files=all` (`changes.js:78`).
2. `git rev-parse --verify HEAD`, to detect an unborn branch (`changes.js:80`).
3. Line counts (`changes.js:82`):
   - With HEAD: `git diff HEAD --numstat -z --no-renames` (staged + unstaged combined against HEAD).
   - Without HEAD: `git diff --cached --numstat -z --no-renames` **and** `git diff --numstat -z --no-renames`, summed per path.
   - numstat `-` marks binary (`changes.js:89`).
4. Per-file diff on selection (`changes.js:129`): `git diff [HEAD] --no-ext-diff --no-textconv --no-color -- <path>`. Output before the first `@@` (diff/index/---/+++ headers) is dropped (`changes.js:131-132`).
5. Untracked files (`??`), and added files in a repo without HEAD, are **not** diffed by git: the file is read with `readTextFile` and shown as an all-added hunk `@@ -0,0 +1,N @@` (`changes.js:120-127`). `add` count = line count of the file.

Any non-zero exit in steps 1 and 3 is thrown and shown as the error text.

### 3.3 What is listed (`changes.js:92-100`)

- **One flat list; staged, unstaged and untracked are not separated.** There is no stage/unstage concept in the UI.
- Up to **1000** entries; if more, a line `"Showing the first 1,000 changed files"` (text 11, `dim`) appears under the list (`changes.js:94`, `200`).
- Status letter: `U` if `XY` contains `U` or is `AA`/`DD` (conflict); else `D` if it contains `D`; else `A` if `??` or contains `A`; else `M`. Colours: A `ansi2`, D and U `ansi1`, M `blocked` (`changes.js:195`).
- Order = git porcelain order (no extra sort).
- Row (`changes.js:193-197`), height 28, `px(8)`, gap 8, radius 7, hover `bg(hover)`, `bg(selected)` when it is the open file:
  1. Status letter, 14 px wide, text 11.
  2. Directory part `dir/sub/` in `dim`, truncated, then the file name in `foreground`, `max_w(260)`, truncated.
  3. `+N` (text 11, `ansi2`) if add > 0; `−N` (U+2212 minus, `ansi1`) if del > 0.
- Header (`changes.js:186-189`), height 42, `px(14)`: "Changes" semibold; a badge `"<n> files"` (`px5 py1`, radius 4, 1 px border, text 10, `muted`); spacer; total `+add` (`ansi2`, text 11) and `−del` (`ansi1`, text 11).
- List area sizing (`changes.js:191`): `flex_1`, `min_h(44)`, `max_h(files*28 + 12)`, border-bottom; it grows to fit its rows and the diff takes the rest. Own vertical scrollbar.
- Empty text: error, `"Loading changes…"`, `"No uncommitted changes"` or `"No folder"` (`changes.js:198`).

### 3.4 Diff view (`changes.js:17-32`, `116-140`, `171-176`, `201-206`)

- **Unified only.** No split view, no word-level highlight, no syntax colouring.
- Selection: clicking a row opens its diff; clicking the open row again closes it (`changes.js:142-152`). After a refresh, if nothing is open and the user did not explicitly close one, the **first file opens automatically** (`changes.js:105-106`). If the open file vanishes from status, the diff clears (`changes.js:104`). With files but none open: overlay hint `"Select a file to view its changes"`.
- Row model (`diffRows`): parses `@@ -a,b +c,d @@` to set counters.
  - `@@` hunk header row: full width, `px(12)`, `bg(raised)`, `dim` text, truncated.
  - `-` line: line number = **old** number; `+` and context lines: **new** number. **One** number column only (30 px wide, right-aligned, `dim`), then gap 10, then text.
  - `+` rows `bg = ansi2 + "18"` (green at alpha 0x18 ≈ 9 %), `-` rows `bg = ansi1 + "18"`; context rows no background. The `+`/`-`/space prefix character is **removed** from the displayed text.
  - Other lines (e.g. `\ No newline at end of file`) are shown as-is without a number.
  - Tabs expanded to the next 4-column stop (`untab`, test `scripts/changes.test.ts:8-11`). Empty lines render as a single space.
- Each row height 20; font = terminal font setting (default `"JetBrains Mono"`), text 11 (`changes.js:201`).
- Rendered with a virtual list (only visible rows) plus vertical scrollbar; horizontal scroll over a width estimated as `max(300, (len+1)*7 + 50)` px for the longest row (`changes.js:139`, `202-205`). Lines are truncated visually within that width.
- Limits: at most **1500** diff lines; each line cut at **4000** chars + `"… (line truncated)"` (`changes.js:134`). Untracked files: first 400,000 chars and 1500 lines (`changes.js:126`); plus the 1 MiB `readTextFile` limit.
- Messages instead of a diff: `"Binary file changed"` (numstat binary), `"No text diff available"`, `"Binary file · no text diff"` (untracked with NUL), `"Cannot preview this file (binary, too large, or unreadable)"`, `"Cannot preview this diff (too large or unreadable)"`, the last also as a red note with the error on a click failure (`changes.js:109`, `148`).

### 3.5 Actions

- **Commit only** (`changes.js:154-169`, `208-210`): footer with an input (placeholder `"Commit message"`, height 32, radius 8, border `accent` while focused else `border`, text 12) and a "Commit" button (height 32, `px(14)`, radius 8, text 12; enabled: `bg(solid)` + `solid_foreground`; disabled: `bg(raised)` + `dim`; label `"Committing…"` while busy).
  - Enabled when the trimmed message is non-empty, a repo root exists, and it is not busy or loading.
  - Runs `git add -A` then `git commit -m <message>` in the repo root (**no** READ flags; this commits **everything**, including untracked files). Enter in the input does the same.
  - On success the input clears; on failure a red note (text 11, `ansi1`) `"Commit failed: <stderr|stdout>"`. Refreshes afterwards either way.
- **Not present:** stage/unstage per file, discard/revert, open in editor, open in Files panel, copy path, send to agent, review comments, push, branch switching.

### 3.6 Refresh triggers

- Init, every **10 s** timer (`changes.js:47`), context change on render (`changes.js:179`), and `watchFolder(<repo toplevel>)` loop for local repos only (`changes.js:50-61`). Skipped while `loading` or a commit is `busy` (`changes.js:64`). Each refresh also re-loads the open file's diff (`changes.js:107-110`).

---

## 4. Interaction with terminal / agents

- **None in either mod.** Neither imports `run`, `send`, clipboard or drag APIs (imports: `files.js:2-4`, `changes.js:1-3`). No drag of a path into a pane, no "send to agent", no review comments.
- The only coupling is input: both panels follow the focused pane's **live cwd** (and its machine), so focusing another agent pane, or an agent `cd`-ing, retargets them. The welcome text pitches Changes as "What your agents changed: every file with its diff, and a commit box" (`welcome.js:20`).
- `exec` runs with the user's real environment, default 30 s timeout, 8 MiB output cap, remote through herdr-core when `on` is set (`d.ts:10-14`).

---

## 5. Fonts, sizes, colours

Theme tokens resolve in `settings.rs:306-360`. For dark themes: `hover = #ffffff0e`, `selected = #ffffff17`, `border = #ffffff18`, `raised = mix(bg, white, 0.09)`, `muted = mix(bg, fg, 0.6)`, `dim = mix(bg, fg, 0.38)`, `foreground = #eeeeef`, `solid = fg`, `solid_foreground = mix(bg, black, 0.28)`, `accent = ansi4`, `blocked` = preset status red `#f0705f` (theme files: `ansi1`). `background` = the terminal background, so the panels match the terminal cards.

| Element | Value | Ref |
|---|---|---|
| UI font | `ui_font_family` default "Geist", 13 | `settings.rs:555-556` |
| Panel base text | 13, `foreground` on `background` | `files.js:169`, `changes.js:182` |
| Panel card | radius 10, 1 px `border` | same |
| Panel header | height 42, `px 14`, gap 8, border-bottom; title semibold | `files.js:170-172`, `changes.js:186-187` |
| Files header right text | 12, `dim`, right-aligned, truncated (root basename or file path) | `files.js:173` |
| Tree / list rows | height 28, radius 7, hover `hover` | `files.js:155`, `changes.js:194` |
| Tree indent | 8 + 18 per level | `files.js:155` |
| Icons | chevron 12 (`dim`), folder/file 14 (`muted`) | `files.js:157-158` |
| Git mark / status letter | 11; A `ansi2`, D/U `ansi1`, M `blocked` | `files.js:160`, `changes.js:195` |
| Preview editor | terminal font (default JetBrains Mono), 12 | `files.js:143` |
| Diff text | terminal font, 11, row height 20 | `changes.js:201`, `203` |
| Diff colours | + `ansi2`+`18` alpha bg, − `ansi1`+`18`, hunk `raised` bg + `dim` | `changes.js:172-173` |
| Line number column | 30 px, right-aligned, `dim`, gap 10 | `changes.js:174` |
| Commit input / button | height 32, radius 8, text 12 | `changes.js:209-210` |
| Notices | 11 (limits, notes), 12 (empty states) `dim`; errors `ansi1` | various |

Default terminal font: "JetBrains Mono" 14 (`settings.rs:548-549`, `901`). Number units are GPUI logical pixels (the layout calls them px, `AGENTS.md:241`).

---

## 6. Points that remain unclear

- How `layout.toggle {id:"panel"}` would interact with the per-space memory if a user bound it to a key: the tab mod reads the hidden/active state on each render and records it as that space's choice (`tabs.js:85-87`), so it would most likely be remembered as well. Not verified by running.
- Windows mark matching (2.5): inferred from string handling; not verified by running.
- Whether the panel width (368) is persisted after a drag: it is a layout node, and AGENTS.md says personal layout edits go to `<name>-edited` (`d.ts:86`), but I did not trace whether `layout.resize` from a drag writes there.
