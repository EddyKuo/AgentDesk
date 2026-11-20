# A3 Review（審查台）與 A4 Timeline（時間軸）實作紀錄

日期：2026-11-20｜分支：`feature/ideas-a34`｜規格：`docs/design/ideas/A-usage-layouts.md` 的 A3、A4 與共用元件

這份文件給整合者用：說明做了什麼、哪裡和規格不同、新增的命令與設定，以及建議放進使用者指南的文字。設計文件、PRD、使用者指南都沒有動，等整合時再一起寫進去。

---

## 1. 完成狀況

| 項目 | 狀態 | 說明 |
|---|---|---|
| A3 Review 版面 | 完成 | 第五個內建版面 `review`（顯示名稱 Review）。左邊是目前分頁，右邊是改動清單、diff 與 commit |
| A4 Timeline 浮層 | 完成 | `timeline.toggle` 開關的大型浮層，另有 `timeline` 版面區塊 |
| A4 host 狀態歷史 | 完成 | agent-host 在 session actor 內記錄狀態變化，新方法 `session.timeline`，`session.state` 事件加選填欄位 `at` |
| A4 `agentctl timeline` | 完成（選做項目） | 文字與 `--json` 兩種輸出 |
| 共用元件 3（切到某版面再切回來） | 完成 | `toggle_layout()`，`ui.json` 的 `layout_return` |
| 共用元件 4（空、載入、錯誤狀態卡） | 完成 | `draw_state_card()`、`draw_skeleton_rows()`（`apps/desktop/state_card.cpp`） |
| 共用元件 8（keymap 版面區段預設） | 完成 | `for_each_default` 支援 `layout:<名稱>` 段 |
| 共用元件 9（截圖工具） | 部分 | `--input` 的 `key:` 支援組合鍵與 F1～F12；`screenshot_ui` 加 `--cwd`、`--appearance` 與兩個會輪流換狀態的測試 profile。`--state` 沒有做，改用這兩個 profile |
| 共用元件 1、2、5 | 沒做 | 屬於 A1、A2 的範圍，Timeline 用不到共用版本也能完成（見第 4 節） |

---

## 2. A3 Review

### 2.1 做了什麼

- 版面檔 `libs/ui/layouts/review.json`，建置時編進程式（和 `mac.json` 一樣），`builtin_names()` 多了 `review`，設定 › Look 的版面卡片有自己的縮圖。
- `apps/desktop/review_layout.cpp` 畫三個區塊：
  - `review-head`（42 px）：‹ ›（上一個／下一個 Agent）、程式圖示、名稱、「space · 工作」、狀態標記；右側是分支（含 ↑↓）、重新整理（讀取中改成轉圈），同一個 checkout 有兩個以上執行中的 Agent 時多一個「N agents in this repo」膠囊。視窗按鈕設成 Windows 樣式時，最小化／最大化／關閉畫在這裡。
  - `review-changes`：清單與 diff。右欄寬度 ≥ 760 設計 px 時左右排（清單 260），否則上下排（清單佔 35%，96～300）。中間 6 px 的分隔可以拖曳。清單列高 26，路徑太長時從中間省略（保留開頭的資料夾與檔名），資料夾部分用 `dim`；未追蹤檔寫「untracked」、二進位檔寫「binary」。diff 用 JetBrains Mono 12、行高 17，行號欄寬依字寬計算（至少 44），新增列與刪除列各有底色和「+」「−」標記，hunk 標頭用 `info` 淡底，不換行、可以水平捲動，超過 1,500 列時最後一列寫「Diff truncated at 1500 lines」。
  - `review-commit`（88 px）：訊息框（focus 時框線變強調色）與主要樣式的 Commit 按鈕。下方提示列依序顯示：機器斷線、「⚠ Agent is still working — files may change」、commit 失敗訊息（只顯示第一行，滑鼠移上去看全文），或成功後的「Committed "…"」。
- 跟隨規則：焦點面板是 Agent 時跟著它；焦點移到普通 shell 時，`review_follow = "agent"` 保留上一個 Agent，`"focused"` 則跟著 shell。目標 Agent 從 Working 變成其他狀態時立刻重讀改動，不等 10 秒。
- 清單載入後自動選第一個檔案（`SidePanels::load_changes` 原本就這樣做）。F8／Shift+F8 換檔時，清單會捲到選中的那一列。
- 狀態：沒有目標（「Select an agent to review its changes」＋ Go to…）、不是 repo（標題加資料夾路徑）、沒有改動（清單寫「No uncommitted changes」，diff 區畫淡色勾與「Nothing to review」）、讀取中（清單骨架列、head 轉圈）、錯誤（`needs_you` 色的錯誤卡加 Retry）。
- 在 Review 版面裡，左欄分頁列不畫 Files／Changes 按鈕與視窗按鈕；命令面板執行 `panel:changes` 時跳出「Changes are always shown in Review」。

### 2.2 和規格不同的地方

1. **左右欄的 `min` 從 420 改成 300／320。** 版面引擎遇到所有子節點都到最小值仍放不下時不會再壓縮，結果右欄被切掉（截圖工具的視窗約 700 設計 px 就重現了）。改小之後，700 設計 px 寬的視窗仍能完整顯示兩欄。
2. **清單、diff、commit 沒有重用 `files_changes.cpp` 的函式**（規格第 5 節第 5 項）。規格對 Review 的列高（26）、字級（diff 12／17）、標記與 commit 區都和 Changes 面板不同，硬共用就得改動現有面板的外觀。所以 Review 自己畫，資料仍然共用 `SidePanels`（兩邊選中的檔案一致）。
3. **F8 在最後一個檔案時繞回第一個**，Shift+F8 在第一個時繞到最後一個（`next_change` 的「頭尾」行為規格寫待確認）；二進位檔不跳過。
4. **還沒有看過任何 Agent 時**，`review_follow = "agent"` 也會先跟著焦點的 shell，免得右欄一開始是空的。
5. **`review-head` 加了 `window_drag`**，空白處可以拖曳視窗（規格的 JSON 沒寫）。
6. **終端機有焦點時也能用 F8**：`TerminalView` 原本只把未加修飾鍵的 F2 交給視窗；現在 F1～F12 只要在 keys.json（含 `layout:<名稱>` 段）有綁定就交給視窗，沒綁定的照舊送給程式。預設只有 F2 與 Review 版面的 F8，所以其他版面的行為不變。

### 2.3 截圖（`build\shots\a34\`，不進版控）

| 檔案 | 內容 |
|---|---|
| `review-dark.png` | 深色主題，測試 repo 有 4 個改動，第一個檔案的 diff |
| `review-light-f8.png` | 淺色主題，在終端機按 F8 之後換到第二個檔案 |
| `review-light-committed.png` | 輸入訊息、按 Enter commit 之後：「No uncommitted changes」、「Nothing to review」與綠色的「Committed "Fix token skew"」 |
| `review-agents-dark.png` | 兩個 Agent 在同一個 checkout：head 有「Needs you」狀態與「2 agents in this repo」膠囊 |

「不是 repo」的畫面也檢查過，但終端機提示字元裡有個人資料夾路徑，那張截圖已刪除。

---

## 3. A4 Timeline

### 3.1 host（`libs/host`）

- 新增 `StateTimeline`（`state_timeline.hpp/.cpp`）：只記 `{at_ms, agent_state, process_state}`，**不記訊息、不記畫面內容**。相同狀態不重複記；每個 session 最多 1,000 筆；超過 24 小時的丟掉，但保留範圍開始前的最後一筆當起點狀態；有丟過就把 `truncated` 設成 true。系統時間被調回去時，新的一筆以前一筆的時間記錄，順序不亂、變化也不會漏掉。
- `Session` 在狀態改變的地方、持有 `state_mutex_` 時追加一筆：啟動成功（Running）、`handle(ReportCmd&)`、`refresh_agent_from_engine`（OSC 7501 與紀錄被丟棄）、中斷（Stopping）、`force_stop`（Stopping，在呼叫端執行緒、同樣持鎖）、程序結束（Exited）。VtEngine 仍然只在 actor 執行緒使用，這裡只碰 `info_` 與 timeline，鎖的順序和現有的 `info()` 相同。
- `SessionInfo` 加 `state_changed_ms`；`session.state` 事件在有值時多帶 `at`（毫秒）。
- `prune_exited` 釋放已結束的 Session 前，把它的 timeline 複製到 `Managed::ended_timeline`，Timeline 才看得到已結束的 Agent。紀錄本身沿用現有的 200 筆上限。
- 新方法 `session.timeline`，參數 `{session?, since_ms?}`，回傳：

```json
{ "now_ms": 1763640000000,
  "sessions": [ { "id": "…", "name": "api", "program": "claude", "profile": "claude", "parent": "",
                  "created_at": "…", "ended": false, "truncated": false, "adopted": false,
                  "marks": [[1763639000000, "Working", "Running"], …] } ] }
```

  `since_ms` 最多往回 24 小時，回傳的第一筆是範圍開始時所在的狀態。這個方法列進 `kReadOnly`，換版交接進行中也能讀。從來沒有紀錄的 session（例如上一個 host 留下的 Lost 紀錄）不列出。
- **換版交接（host.update）不搬歷史**（使用者的決定）。新 host 在 `start_adopted()` 時記下當下的狀態作為第一筆，回傳 `adopted: true`；UI 在軸上標「History starts HH:MM (agent-host updated)」。交接的資料格式沒有改。

### 3.2 協定相容（v=1）

- 只新增一個方法和一個選填欄位，版本號不變。
- **新 UI 對舊 host**：舊 host 回 `BAD_REQUEST: unknown method: session.timeline`。UI 把這種回應當成「需要更新」，那台機器的群組寫「Timeline needs a newer agent-host on this machine」，其他機器照常顯示。舊 host 的 `session.state` 沒有 `at`，UI 就用收到事件的時間。
- **舊 UI 對新 host**：舊 UI 只讀自己認得的欄位，`session.state` 多出來的 `at` 會被忽略；舊 UI 不會呼叫 `session.timeline`。
- 測試：`HostFixture.TimelineIsAnOrdinaryReadOnlyMethod` 確認未知方法的錯誤形式（UI 依賴這個字串）；`Timeline.ParseSessionReadsTheAnswerAndSkipsBadMarks` 確認 UI 讀得懂缺欄位或型別不對的回應。真的拿舊版執行檔對測需要舊版建置，這次沒做。

### 3.3 UI

- `libs/ui/include/agentdesk/ui/timeline.hpp`（純邏輯）：`segments()`（裁切到範圍、丟掉時間倒退的 mark 並計數）、`pack()`（小於 2 px 的相鄰段落合併成最緊急的那種：Blocked > Error > Done > Working > Idle > Exited > Unknown）、`ticks()`（依範圍選 2 分、15 分、30 分、3 小時，標籤間距不足 56 px 就加倍）、`rows()`（需要你的在前，其餘依最近變動排序，子 Agent 排在父 Agent 下面）、`parse_session()`、時間與長度的格式。
- `apps/desktop/timeline.cpp`：浮層（視窗的 90%×80%，最小 640×400，視窗更小時佔滿）與 `timeline` 區塊。
  - 標題列：範圍切換（15m／1h／4h／24h）、「Since you left」勾選框、「N agents · M need you」、Esc。
  - 軸：本機時間 24 小時制，右端「now」與貫穿所有列的強調色直線。
  - 列：Agent 32 px、子 Agent 24 px（縮排並加「↳」），奇數列淡底。工作中是實心條；需要你是 `needs_you` 實心條，寬度 ≥ 14 px 時中間畫「!」（白字或背景色，取對比較高的）；Error 是 `needs_you` 加斜線紋；閒置是細線；完成是細線加起點的「✓」；Unknown 只有基線；程序結束畫「×」。滑鼠移到段落上會加框並顯示「Needs you · 13:20–13:35 (15m)」，Unknown 段落顯示「No status (last: Done)」。
  - 點段落或標籤：`agent.focus:<id>` 並關閉浮層（已結束的 Agent 不能點）。
  - 鍵盤：↑↓ 選列、Enter 前往、←→ 平移半個範圍、End 回到現在、1～4 切換範圍、Ctrl+滾輪以滑鼠位置為中心縮放、Esc 或 Ctrl+Shift+H 關閉。
  - 遠端機器：每台機器一組，標頭寫機器名稱；斷線時保留已取得的資料，從最後一次回應到現在畫斜線紋，寫「Machine unreachable since HH:MM」。
  - 「你離開的時間」：視窗失去焦點時（`App::on_focus_changed(false)`）記下時間，只放在記憶體；勾選 Since you left 時，從那時到現在畫 `alpha(focus, 0x14)` 底色，軸上標「you left HH:MM」。
  - 資料：浮層或區塊顯示時才向 host 要，每 15 秒更新一次，範圍變長或往回平移時立刻再要；兩次之間用 `session.state`（含 `at`）把新的一筆接上。浮層開著時 `next_wakeup_ms` 最多 5 秒（「now」線往右移），有請求在路上時 100 ms。
  - 狀態：沒連上本機 host（「agent-host not connected」）、第一次讀取（4 列骨架）、沒有紀錄（「No agent activity in the last hour」，範圍小於 24h 時有「Show 24h」）。

### 3.4 和規格不同的地方

1. **tooltip 沒有來源**（規格範例的「· osc7501」）。host 只記時間與狀態，沒有記來源；要加得改記錄格式，這次先不做。
2. **只顯示曾經是 Agent 的 session**（至少一筆狀態不是 Unknown），一般 shell 不成列。
3. **「earlier history dropped」改成列的左端一行小字「‹ earlier dropped」**，完整說明放在標籤的 tooltip。
4. **「agent-host restarted 14:05」沒有標。** 重新啟動後舊 session 是 Lost，沒有任何 mark，所以不會出現在 Timeline；換版交接的「History starts」有標。
5. **Ctrl+滾輪是在四個範圍之間切換**（以滑鼠位置為中心），不是連續縮放。
6. **區塊版本**（`{"content": {"mod": "timeline"}}`）沒有標題列，也不接鍵盤，免得搶走其他區塊的按鍵；範圍沿用 `timeline_range`。

### 3.5 截圖

| 檔案 | 內容 |
|---|---|
| `timeline-dark-15m.png` | 深色、15m：三個輪流換狀態的 Agent 與一個子 Agent，「3 agents · 2 need you」 |
| `timeline-dark.png` | 同一次執行稍後的畫面 |
| `timeline-light.png` | 淺色、15m |
| `timeline-hover-15m.png` | 滑鼠停在段落上：外框與「Done · 20:50–20:50 (8s)」 |
| `timeline-hover.png` | 按 2 切到 1h 後，tooltip「Needs you · …」 |
| `timeline-empty-dark.png`、`timeline-empty-light.png` | 沒有紀錄時的空狀態與「Show 24h」 |
| `timeline-block-dark.png` | 自訂版面裡的 `timeline` 區塊（窄欄：標籤欄 140，第二行隱藏） |

「Since you left」的底色沒有截到：截圖工具無法讓視窗先失去焦點。需要人工確認（見第 7 節）。

---

## 4. 共用元件

| # | 做法 |
|---|---|
| 3 | `toggle_layout(s, name)`（`layouts.cpp`）：切到 `name` 前把目前版面存進 `ui.json` 的 `layout_return`；已在 `name` 時切回去（沒有紀錄就回 workspace）。`review.toggle` 使用它，A1 的 `mission.toggle` 可以直接用 |
| 4 | `draw_state_card(s, pos, size, StateCard{kind, title, hint, buttons, title_size, framed})` 與 `draw_skeleton_rows(dl, pos, w, rows, row_h)`。第一顆按鈕是主要樣式，error 的標題用 `needs_you` |
| 8 | `keymap.cpp` 的 `kLayoutDefaults`：`layout:review` 段預設 F8、Shift+F8；寫出的預設 keys.json 也包含這一段 |
| 9 | `--input "key:ctrl+shift+r"`、`key:f8`；`screenshot_ui --cwd <資料夾>`（session 在那裡啟動，給 Review 用測試 repo）、`--appearance light|dark`、profile `state-cycle-a`／`state-cycle-b`（powershell 依固定節奏輪流送 OSC 7501 狀態） |
| 1、2、5 | 沒做。Timeline 的樣本畫法在 `timeline.cpp` 的 `sample()`；排序用 `ui::timeline::rows()`，這兩者和側欄的 `priority_before` 是分開的。A1、A2 做出共用的 `status_dot`、`collect_agents` 之後，Timeline 可以改用它們 |

---

## 5. 新命令、快捷鍵、設定、檔案

**命令**（都已加入 `kCommands`、`command_names.cpp`；說明視窗列的是 keys.json，所以預設綁定會自動出現）

| 命令 | 預設按鍵 | 作用 |
|---|---|---|
| `review.toggle` | Ctrl+Shift+R | 切到 Review，再按一次回原本的版面 |
| `review.next_file` | F8（只在 `layout:review`） | 下一個改動的檔案 |
| `review.prev_file` | Shift+F8（只在 `layout:review`） | 上一個改動的檔案 |
| `review.refresh` | 無 | 重讀改動 |
| `timeline.toggle` | Ctrl+Shift+H | 開關 Timeline 浮層 |
| `timeline.range:<15m\|1h\|4h\|24h>` | 浮層內 1～4 | 切換範圍 |

**`ui.json` 鍵**

| 鍵 | 預設 | 說明 |
|---|---|---|
| `layout_return` | `""` | toggle 類命令記住的原版面 |
| `review_follow` | `"agent"` | `agent` 或 `focused` |
| `timeline_range` | `"1h"` | 上次的範圍 |
| `timeline_since_left` | `true` | 是否畫離開期間的底色 |

**協定**：方法 `session.timeline`；`session.state` 選填欄位 `at`。**CLI**：`agentctl timeline [<session>] [--since 15m|1h|24h] [--json]`。

**新檔案**

- `libs/ui/layouts/review.json`、`libs/ui/include/agentdesk/ui/review.hpp`、`libs/ui/src/review.cpp`
- `libs/ui/include/agentdesk/ui/timeline.hpp`、`libs/ui/src/timeline.cpp`
- `libs/host/include/agentdesk/host/state_timeline.hpp`、`libs/host/src/state_timeline.cpp`
- `apps/desktop/review_layout.cpp`、`apps/desktop/timeline.cpp`、`apps/desktop/state_card.cpp`
- `tests/unit/review_test.cpp`、`tests/unit/timeline_test.cpp`、`tests/unit/state_timeline_test.cpp`

**改動的共用檔案（合併時可能衝突）**

| 檔案 | 改動 |
|---|---|
| `apps/desktop/app_internal.hpp` | `Settings` 四個欄位；`kCommands` 九列；`SidePanels::refresh_changes()`；`App::State` 的 `review`、`timeline`；檔尾 state_card、toggle_layout、review、timeline 的宣告；include `timeline.hpp`、`<tuple>` |
| `apps/desktop/frame.cpp` | `draw_layout_block` 兩個分支；`draw_dialogs` 一行；`input_key` 支援 F1～F12；`key:` 支援修飾鍵 |
| `apps/desktop/commands.cpp` | `exec_command` 先問 `exec_review_command`、`exec_timeline_command` |
| `apps/desktop/layouts.cpp` | `derive_layout` 看到 `review-*` 設 style；`toggle_layout()` |
| `apps/desktop/app.cpp` | `on_focus_changed` 記離開時間；`next_wakeup_ms` 一行 |
| `apps/desktop/tab_bar.cpp` | Review 版面不畫視窗按鈕與 Files／Changes 按鈕 |
| `apps/desktop/settings_store.cpp`、`settings_pages.cpp`、`overlay_kit.hpp` | 新設定讀寫、Review 縮圖、顯示名稱 |
| `apps/desktop/CMakeLists.txt`、`libs/ui/CMakeLists.txt`、`libs/host/CMakeLists.txt`、`tests/CMakeLists.txt` | 新檔案 |
| `libs/ui/src/keymap.cpp` | `kGlobal` 兩列、`kLayoutDefaults` |
| `libs/ui/src/terminal_view.cpp` | 未加修飾的 F1～F12 有綁定時交給視窗 |
| `libs/ui/src/layout.cpp`、`layout.hpp` | `builtin_names()` 加 `review` |
| `libs/ui/src/command_names.cpp` | 六個命令名稱 |
| `libs/ui/include/agentdesk/ui/host_client.hpp`、`src/host_client.cpp` | `SessionView::state_at_ms`，讀 `at` |
| `libs/host/…/session.hpp`、`session.cpp` | timeline 記錄 |
| `libs/host/…/session_manager.hpp`、`session_manager.cpp` | `do_timeline`、`kReadOnly`、`ended_timeline`、`at` |
| `apps/cli/main.cpp` | `agentctl timeline` |
| `tools/qa/screenshot_ui.cpp` | `--cwd`、`--appearance`、`state-cycle-*` profile |
| `tests/unit/layout_test.cpp` | 內建版面清單多了 `review` |
| `tests/integration/windows/host_test.cpp`、`handover_test.cpp` | 新測試（檔尾三個）與交接測試的 timeline 檢查 |

最可能衝突的地方：A1 也會在 `builtin_names()`、`layout_display_name`、`derive_layout`、`draw_layout_block`、`kCommands`、`command_names.cpp`、`kGlobal` 加東西；A1、A2 可能也做 `--input` 組合鍵與狀態卡；B1 的 F10 會碰到 `TerminalView` 的 F 鍵處理（這裡已改成 F1～F12 有綁定就交給視窗，B1 不需要再改）。

---

## 6. 測試

- 開始時 297 個，結束時 328 個，全部通過（Debug）。
- 新增的單元測試：`Review.*`（13 個）、`Timeline.*`（10 個）、`StateTimeline.*`（5 個）。
- 新增的整合測試：`HostFixture.TimelineRecordsStateChangesInOrder`（順序、時間、只有狀態沒有訊息、`at` 欄位、程序結束、`agentctl timeline`）、`HostFixture.TimelineIsBoundedAndSaysSo`（1,100 次變化只留 1,000 筆並標 `truncated`、`since_ms`）、`HostFixture.TimelineIsAnOrdinaryReadOnlyMethod`。`HostUpdate.TerminalsKeepRunningThroughAnUpdate` 加了檢查：交接後歷史從接手時開始，`adopted` 為 true。

---

## 7. 待確認與後續

1. 「Since you left」底色要人工確認：切到別的視窗一段時間再回來，打開 Timeline。
2. 真的 Claude Code 跑 30 分鐘以上，離開座位再回來，對照時間軸。
3. Review 在多個 Agent 共用 checkout 時只能提示，不能區分是誰改的（規格已知風險）。
4. Review 在很窄的視窗（< 620 設計 px）時兩欄都到最小值，右欄仍可能被切到；要不要自動收起右欄，規格寫待確認。
5. tooltip 要顯示來源（osc7501／report）的話，host 的 mark 要多記來源，格式要改。
6. 用舊版 agent-host 執行檔實際對測「新 UI 對舊 host」。

---

## 8. 建議放進使用者指南的文字

### 審查台（Review）

按 Ctrl+Shift+R 切到審查台，再按一次回到原本的版面。左邊是目前的終端機，右邊列出這個 Agent 資料夾裡還沒 commit 的改動和 diff，最下面可以直接 commit。

- 焦點換到另一個 Agent，右邊就跟著換。焦點移到一般 shell 時，右邊預設保留上一個 Agent 的改動；想讓它一律跟著焦點，把 `ui.json` 的 `review_follow` 改成 `"focused"`。
- 按 F8、Shift+F8 換到下一個、上一個檔案。這兩個鍵只在審查台有作用，在其他版面照常送給終端機。
- Agent 做完一輪時清單會自動重讀，也可以按右上角的重新整理。
- 同一個資料夾有兩個以上的 Agent 在跑時，上方會出現「N agents in this repo」：清單裡是所有人的改動，分不出是誰改的。要分開，請讓每個 Agent 用自己的 worktree。
- Agent 還在工作時也可以 commit，但會看到提醒，之後它再改的檔案會出現在下一次的清單裡。

### 時間軸（Timeline）

按 Ctrl+Shift+H 打開時間軸，看每個 Agent 什麼時候在工作、什麼時候在等你、什麼時候做完。按 Esc 或再按一次 Ctrl+Shift+H 關閉。

- 上方可以切換範圍：15 分鐘、1 小時、4 小時、24 小時（也可以按 1～4）。←、→ 往前往後移，End 回到現在，Ctrl+滾輪放大縮小。
- 黃色條是工作中，紅色條是需要你（夠寬時中間有「!」），「✓」是做完的時間點，細線是閒置，「×」是程式結束。滑鼠停在色條上可以看起訖時間。
- 點一列或一段色條，就切到那個 Agent。
- 勾選「Since you left」時，視窗離開焦點之後的那段時間會加上底色，回來時可以直接看出那段時間誰卡住、誰做完。
- 紀錄存在 agent-host 裡，關掉 AgentDesk 也會繼續記錄，最多保留 24 小時、每個終端機 1,000 筆，只記時間和狀態，不記畫面內容。更新 agent-host 之後，紀錄會從更新的時間重新開始。
- 終端機用 `agentctl timeline` 也能看同樣的紀錄。
