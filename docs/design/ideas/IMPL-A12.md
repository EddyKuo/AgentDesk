# IMPL-A12：A1 Mission Control 與 A2 Focus 實作紀錄

日期：2026-11-20｜分支：`feature/ideas-a12`｜依據：`docs/design/ideas/A-usage-layouts.md` 的 A1、A2 與「共用元件」

這份文件給 orchestrator 併進正式文件用：每項做了什麼、和規格不同的地方與原因、新的命令／快捷鍵／`ui.json` 鍵／檔案、建議的使用說明文字，以及還沒解決的問題。技術部分用英文寫，使用說明用台灣繁體中文。

## 1. What was built

### A2 Focus (built)

- `focus.toggle` (Ctrl+Shift+.), `focus.on`, `focus.off`. Focus replaces whatever layout is shown with the built-in tree `libs/ui/layouts/focus.json` (compiled in, not in `builtin_names()`, so `layout.switch` never lands on it). The shown layout keeps its theme, font size and keys.json section.
- One terminal: the focused pane of the active tab (else the zoomed one, else the first), drawn without a frame on the terminal background, 12 px sides, 8 px top, 6 px bottom. `Tab::zoomed` and the split tree are not touched; leaving Focus brings the splits back.
- Pane header on hover: after the pointer rests 300 ms in the top 30 px it fades in over 150 ms (the default pane header: icon, title, state, folder, split / zoom / close) and fades out over 600 ms after the pointer leaves. A drag that starts in the terminal does not bring it up.
- 22 px strip: program icon and "name · space" (12 px medium, at most 40 % wide, hidden under 480 px), status dots in the middle (needs you: filled `needs_you`; done: filled `done`; working: still 1.5 px ring; idle: 6 px filled `faint`; unknown: none; the focused agent has no dot), "+N" when they do not fit (click: `app.picker`), "Exit focus  Ctrl+Shift+." at the right. Dot hover: grows to 10 px with a tooltip "Program · space · State · 4m"; click: `agent.focus:<id>`. Sub-agents get no dot; the parent's dot shows the most urgent state of its tree (`kid_status`). Dragging the strip moves the window, double-click maximizes.
- Edge glow: a 3 px band on the window's inner edge, `needs_you` up to alpha 0xB0 or `done` up to 0x60; 150 ms in, 400 ms held, 900 ms out. A second event restarts it (needs-you keeps its colour over done).
- Alerts in Focus with the window in front (`focus_alerts`): `edge_sound` (default, the user's decision: glow and sound, no note), `edge` (glow only), `normal` (sound and note as usual). With the window in the background the usual sound, note and taskbar flash run. Settings > Alerts has a row "In focus mode" (Edge / Edge + sound / Usual).
- Empty, loading, error: "No terminal to focus" with New terminal and Start a program…; "Connecting to agent-host..." card while connecting; the strip says "Connecting…" or "agent-host not connected" (`needs_you`); an exited or lost terminal keeps the usual pane banner.
- Focus is kept across restarts (`focus_on`).

### A1 Mission Control (built)

- Built-in layout `mission` ("Mission Control", `libs/ui/layouts/mission.json` as in the spec): bar 42 px, stack `wall-body` (`wall` / `main`), status bar 28 px. Listed in Settings > Look with its own thumbnail.
- Wall: tiles by `wall_grid` (1, 2, 2x2, 3x2, 3x3, 4x3, then four columns; at least 240 x 150; fewer columns when narrow; scrolls when rows do not fit, a row per wheel notch, 6 px scroll bar). Only tiles in view are drawn.
- Order (`wall_order`): needs you, done, working, idle, others, by `priority_before`; agents on a machine that does not answer last. Sub-agents fold into their parent ("+N sub-agents" in the footer opens the peek window) unless `mission_sub_agents`; an orphan sub-agent gets its own tile.
- Tile: program icon, name, "space · work"; the state mark and how long ago it changed (or "offline"); the body is the screen's last lines (`screen_tail`: blank rows and box-drawing-only rows skipped, colours kept, JetBrains Mono 11 on 15 px lines, cut at the edge, at most 12 lines); the footer is the cleaned OSC 7501 message (`clean_message`), else the blocked kind (Permission / Question / Auth). Needs you: 2 px `needs_you` edge and a 4 px halo; done: 1 px `done` edge; hover: hover colour and an accent edge; keyboard selection: a 2 px accent ring just outside. Loading: three breathing grey bars; machine unreachable: the last screen dimmed with "Machine unreachable".
- Lines are recomputed only when the snapshot's `seq` / `generation` changed, at most every 250 ms per tile; no `TerminalView`, no resize, no `take_control`, no extra IPC.
- Bar: "Mission Control", count pills (needs you / working / done; 0 hidden; a click filters to that state, again to clear), All | Active switch, "+" (new menu), Settings, back (`mission.toggle`). Expanded: "← Wall", the agent's icon, name, space and state, only the needs-you pill, the toggle's key, and the same buttons.
- Interaction: click a tile → `mission.expand:<id>` (the agent's tab becomes active, the pane gets the keyboard, the stack shows `main`; a settled tab comes back); a sub-agent without a pane opens the peek window. Right-click: Expand, Peek, Rename, Close pane. Keys on the wall (only while nothing else wants them): arrows, Home, End move the selection, Enter opens it, 1–9 open tile N. Ctrl+Shift+M: from another layout to the wall; from an expanded agent to the wall; from the wall back to the layout before (`layout_return`). Esc is not taken (the user's decision): it goes to the terminal.
- Notes (toasts) clicked while Mission Control shows run `mission.expand`.
- Empty states: "No agents running" with Start a program… and New terminal, plus "N terminals without agent status — show them" (turns on shell tiles until Mission Control is left); "Nothing needs you" (etc.) with "Show all" when the filter hides everything; "Connecting to agent-host…" with three skeleton tiles; "agent-host is not running" with the error and Retry.

### Shared pieces (built)

| # | Piece | Where |
|---|---|---|
| 2 | `agent_roster(s)`: the sidebar's agent list (ids in its order, urgency with sub-agents, live sub-agents) — also keeps `State::agent_order` current while the sidebar is not drawn (Focus, Mission Control) | `apps/desktop/sidebar.cpp`, declared in `app_internal.hpp` |
| 3 | Toggle with return: `layout_return` in `ui.json`, used by `mission.toggle` (inline in `mission_command`, not a separate `toggle_layout`; A3 can lift it out) | `apps/desktop/mission_layout.cpp` |
| 4 | `draw_state_card` (title, hint, detail, buttons with a command or an action, a link) and `draw_skeleton_rows`; `with_chord` | `apps/desktop/state_card.cpp` |
| 5 | What is drawn counts as seen: `draw_pane` records the terminals it draws (`State::drawn_sessions`); after the layout is drawn they become `State::visible_sessions`, and `update_attention` uses that set instead of the active tab. The workspace, terminals and mac layouts draw exactly the active tab's panes (or the zoomed one), so their behaviour is unchanged; a Mission Control tile, the panes Focus hides, and a layout without a tab block no longer clear "done". | `apps/desktop/frame.cpp`, `panes.cpp`, `sessions.cpp` |
| 6 | `ui::mission::screen_tail` | `libs/ui/src/mission.cpp` |
| 7 | Command registry: every new command in `kCommands` (palette), `is_builtin_command`, and the keys listing (Keybinds shows them under "window"); unit tests check they are known | — |
| 9 (part) | `screenshot_ui`: `SHOT_APPEARANCE=light|dark`, a `state-later` profile (working, blocked 10 s later), and modifier keys in `--input` `key:` steps (`key:ctrl+shift+m`, `key:ctrl+shift+.`) | `tools/qa/screenshot_ui.cpp`, `apps/desktop/frame.cpp` |

Not done: shared piece 1 (`status_dot` for T1; Focus's dots are drawn in `focus_mode.cpp`), 8 (keymap layout-section defaults, needed by A3 only), and `--state` for the screenshot tool (the existing `state-*` profiles plus `state-later` were enough).

## 2. Deviations from the spec, and why

1. **`focus_alerts` default is `edge_sound`** (spec: `edge`). The user decided this so design doc 4.1 ("an agent that needs you always plays a sound") still holds; `edge` stays available.
2. **Focus header overlays the terminal** (the top 30 px) instead of pushing it down, so the terminal never changes size when the header appears.
3. **Ctrl+Shift+B in Focus** leaves Focus and shows the sidebar if it was hidden (a plain toggle after leaving would hide a sidebar that was shown).
4. **Window buttons in Focus**: the round buttons sit at the left of the strip; with "Windows" buttons, three small 20 px buttons sit at the strip's right.
5. **Glow in automated runs**: `--run` / `--input` runs (`config.automated`) still show the glow, silently, so it can be captured; sounds and notes stay off there as before.
6. **Global shortcuts run one frame later** (`run_global_shortcuts` queues into `chord_commands`). Without this, Ctrl+Shift+M on the wall switched back to a layout whose focused terminal saw the same key press and switched to the wall again. The delay is one frame for every global shortcut.
7. **Tile header 40 px, footer 26 px** (spec: 34 and 24): the two header lines (13 px name, 11.5 px "space · work") and the time did not fit in 34 px at 2x scale.
8. **Tile right-click menu**: Expand, Peek, Rename, Close pane only. Split and zoom from a wall tile have no visible result; the full pane menu is in the expanded view.
9. **Selected tile ring** is drawn 3 px outside the tile so it shows together with a needs-you edge.
10. **Exited / Lost tiles** (50 % alpha, "Exited 0"): not drawn. A terminal that ended loses its pane (design doc 2.2), so it is not on the wall long enough to show this.
11. **Back icon** is the bundled "layers" glyph (`ui::icon::kLayers`); the icon table has no swap arrows.
12. **`mission.toggle` turns Focus off** first (both replace the layout).
13. **The stack's choice is not saved** to `layouts\.state\mission.json`: `mission.expand` / `mission.wall` change it in memory only, and entering the layout (switch, first draw after start) always shows the wall.
14. **Counts while expanded**: the needs-you pill shows; clicking it goes back to the wall (spec only says it shrinks).
15. **Performance target** (Release, 12 × `vt_probe flood`, ≤ 8 ms a frame) not measured: the brief allows Debug builds only in this worktree.

## 3. New commands, keys, settings and files

**Commands** (all in `is_builtin_command`; the first six also in the palette):

| Command | Argument | What it does |
|---|---|---|
| `focus.toggle` | — | Focus on / off |
| `focus.on`, `focus.off` | — | For mods and `--run` |
| `mission.toggle` | — | Other layout → wall; expanded → wall; wall → the layout before |
| `mission.expand` | session id | Opens that agent (sub-agent without a pane: peek) |
| `mission.wall` | — | Back to the wall |
| `mission.filter` | `all`, `active`, `blocked`, `working`, `done` | Filter (palette: all, active, blocked) |

**Default keys** (`kGlobal`): Ctrl+Shift+. → `focus.toggle`; Ctrl+Shift+M → `mission.toggle`. Wall-only (not in keys.json): arrows, Home, End, Enter, 1–9.

**`ui.json`**: `focus_on` (false), `focus_alerts` ("edge_sound"; "edge", "normal"), `layout_return` (""), `mission_filter` ("all"), `mission_sub_agents` (false), `mission_shells` (false).

**Report (`--report`)**: adds `layout` and `focus_on`.

**Files added**: `libs/ui/include/agentdesk/ui/focus_mode.hpp`, `libs/ui/src/focus_mode.cpp`, `libs/ui/include/agentdesk/ui/mission.hpp`, `libs/ui/src/mission.cpp`, `libs/ui/layouts/focus.json`, `libs/ui/layouts/mission.json`, `apps/desktop/focus_mode.cpp`, `apps/desktop/mission_layout.cpp`, `apps/desktop/state_card.cpp`, `tests/unit/focus_mode_test.cpp`, `tests/unit/mission_test.cpp`.

## 4. 建議的使用說明（併入 docs/user-guide.md）

### 專注模式（Focus）

想專心寫程式、只偶爾看一下 Agent 時，按 **Ctrl+Shift+.**（或在命令面板執行「Focus mode」）。畫面只剩目前的終端機，底下一條細列：

- 左邊是這個終端機的程式與 space。
- 中間的小點代表其他 Agent：紅色實心是需要你，綠色實心是完成，黃色空心圈是工作中，灰色小點是閒置。滑鼠移上去會顯示是哪個 Agent，點一下就換成它，仍留在專注模式。放不下的會顯示「+N」，點了開啟搜尋。
- 右邊的「Exit focus」或再按一次 Ctrl+Shift+. 就回到原本的版面，分割與放大狀態都不變。

滑鼠在終端機上緣停一下，面板標題列就會浮現，可以分割、放大或關閉。Esc、Ctrl+C 照常送給終端機。

有 Agent 需要你時，視窗邊緣會亮一下，不跳通知。預設也會播放提示音；要完全安靜，到 **設定 › Alerts › In focus mode** 選「Edge」；選「Usual」則和平常一樣跳通知。視窗不在前景時，一律照平常的方式提醒。關掉 AgentDesk 再開，仍會停在專注模式。

### 監看牆（Mission Control）

同時跑好幾個 Agent 時，按 **Ctrl+Shift+M**（或在 設定 › Look 選「Mission Control」）。所有 Agent 排成格子，每格是程式、名稱、狀態、最後幾行輸出與 Agent 回報的訊息。需要你的格子有紅框，排在最前面。

- 上方的「Needs you」「Working」「Done」計數點一下，只看那一種；右邊的 All／Active 切換是否只看忙碌中的 Agent。
- 點一格，就以一般面板開啟那個 Agent，可以直接打字。按「← Wall」或 Ctrl+Shift+M 回到牆面；在牆面再按一次 Ctrl+Shift+M，回到原本的版面。
- 在牆面上可以用方向鍵選格子、Enter 開啟，或直接按 1～9 開啟第幾格。
- 右鍵格子可以開啟、偷看（peek）、重新命名或關閉面板。
- 子 Agent 預設不另外成格，父 Agent 的格子會寫「+N sub-agents」，點了用 peek 視窗查看。

只看牆面不算「看過」：完成的 Agent 會一直標示完成，直到你點開它。

要讓子 Agent 或沒有狀態回報的終端機也各成一格，可以在 `ui.json` 設定 `mission_sub_agents` 或 `mission_shells` 為 `true`。

## 5. Open issues

1. Real Claude Code and Codex were not run (brief). Whether a tile's last lines are readable with Claude Code's input box at the bottom (rows of only box-drawing characters are skipped) needs a person to check, as do Esc and Ctrl+C reaching Claude Code in Focus and in an expanded tile.
2. Performance (acceptance A1-4) needs a Release run with 12 `vt_probe flood` sessions.
3. The glow's strength (0xB0 / 0x60) and the dwell before the Focus header appears may need tuning after a few days of use.
4. The host-unreachable states (Focus strip "agent-host not connected", Mission Control's error card) were not captured: `screenshot_ui` always starts its isolated host.
5. Design doc 4.1 needs a line for Focus ("in Focus mode, as `focus_alerts` says; the default still plays the sound").
6. Keybinds (help) lists `focus.toggle` and `mission.toggle` under "window"; the wall-only keys are not in keys.json and are only described in the user guide.
7. `layout_return` is only used by `mission.toggle`; A3's `review.toggle` should share it (factor the three lines into a `toggle_layout` helper when merging).

## 6. Likely merge conflicts (shared code touched)

- `apps/desktop/app_internal.hpp`: `Settings` (six keys), `kCommands` (six rows), `State` (`drawn_sessions`, `visible_sessions`, `FocusState focus_mode`, `MissionState mission`), new declarations (state_card, focus_mode, mission_layout, `AgentRoster` / `agent_roster`).
- `apps/desktop/frame.cpp`: `draw_layout_block` (three mod names), `draw_layout` (Focus tree), `run_input_step` (`key:` modifiers), `run_global_shortcuts` (queued), `App::frame` (drawn sessions, glow).
- `apps/desktop/commands.cpp`: `exec_command` (`mission_command`), `exec_app_command` (`focus.*`, `sidebar.toggle`), `exec_layout_mod_command` (`layout.switch` → `mission_entered`).
- `apps/desktop/sessions.cpp`: `alert`, `update_attention`.
- `apps/desktop/panes.cpp`: `PaneStyle::kFocus`, `draw_pane_grip`, `draw_pane` (records drawn), new `draw_focus_pane`, `draw_panes`.
- `apps/desktop/chrome.cpp` (`traffic_lights_input`), `notices.cpp` (toast position and click), `app.cpp` (`next_wakeup_ms`, `report`), `layouts.cpp` (`derive_layout` styles), `sidebar.cpp` (`agent_roster`), `overlay_kit.hpp` (`layout_display_name`), `settings_dialog.cpp` (Alerts row), `settings_pages.cpp` (`layout_preview` "mission"), `settings_store.cpp`.
- `libs/ui`: `CMakeLists.txt` (two sources, two layout files), `src/layout.cpp` (`builtin_names` now has `mission`; `builtin_text` knows `focus` and `mission`), `src/keymap.cpp` (`kGlobal`), `src/command_names.cpp`; `tests/CMakeLists.txt`, `tests/unit/layout_test.cpp`.
- `tools/qa/screenshot_ui.cpp`.
