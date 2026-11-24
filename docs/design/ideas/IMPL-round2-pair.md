# 第二輪實作紀錄：C3 Pair（並排比較）

日期：2026-11-21｜分支：`feature/round2-pair`（從 `feature/ideas` 的 9c46158 開出）｜規格：`docs/design/ideas.md` 的「第二輪構想」C3，以及第一階段的共用設計規則（`IMPL-round2-base.md`）

這份紀錄寫給整合的人：做了什麼、和規格哪裡不同、新增的命令、按鍵、`ui.json` 鍵與檔案、建議寫進使用者指南的文字，以及還沒解決的問題。設計文件、PRD、使用者指南都沒有動。

## 做了什麼

新增內建版面 `pair`，顯示名稱「Pair」，列在一般的版面清單裡（不算彩蛋版面），Ctrl+Shift+L 會輪到它。版面檔是 `libs/ui/layouts/pair.json`：上方 42 px 的 `pair-bar`，中間的 `pair-stage`，下方 60 px 的共用輸入列 `pair-prompt`，最底下是一般的狀態列。

### 還沒有 pair：設定卡

- 卡片標題「Pair two agents」，一句說明，左右兩個程式選擇框（從 agent-host 的 profile 清單挑，有程式圖示），中間一個「vs」。
- 預設左邊 Claude Code、右邊 Codex；沒有 Codex 時兩邊都用 Claude Code；兩個都沒有時取清單前兩個。挑過的會記在 `ui.json`。
- 「Separate worktrees」開關，預設打開。目前資料夾是 git 儲存庫時，下面一行說明每一邊會在 `pair/` 開頭的新分支上有自己的 checkout，未提交的變更留在原地。關掉時改成黃色警告：兩邊會在同一個資料夾裡改檔案。
- 資料夾不是 git 儲存庫時開關停用，用黃色警告說明兩個 Agent 會在同一個資料夾工作、可能改到同樣的檔案，仍然可以開始。
- 下方是資料夾路徑、錯誤訊息（若有），以及「Start pair」。已經有兩個以上的 Agent 在跑時，旁邊多一個「Pair two open agents」，直接把側欄 Agent 清單的前兩個配成一對。
- 開始後卡片改成轉圈加「Making a worktree for each side...」或「Starting both agents...」。
- 視窗太矮時先拿掉卡片上方的小插圖，再收緊區段間距，卡片不會被截掉。

開始的流程沿用現有的路徑：有 worktree 時先呼叫 agent-host 的 `worktree.add`（和「New git worktree」對話框同一個 RPC、同一個存放位置），再用 `session.create` 在那個 checkout 裡開 profile。session 名稱是「<profile 名稱> (A)」與「(B)」。兩個 session 放進目前 space 的一個新分頁，分頁名稱「Pair」，左右分割。

### 有 pair：並排

- 兩個一般的終端機面板，可以直接打字操作。每個面板有 40 px 的標題列：A／B 圓標、程式圖示、名稱、狀態標記，右邊依空間由重要到次要顯示：「Finished first」或「Won」標籤、「Kept」標籤、這一輪的時間、該資料夾的變更（檔案數與 +／- 行數，顏色用 diff 的綠與紅），最右邊是「Change」。
- 標題列放不下時，名稱至少保留 150 px，其餘的項目依上面的順序省略。
- 共用輸入列平常不搶鍵盤：點它或按 Alt+P 才會拿到游標；點終端機或按 Esc 就還給終端機。輸入列有鍵盤時，面板的焦點框會暗下來，不會兩個地方同時看起來在輸入。
- Enter 或「Send to both」把同一段文字加 Enter 送給兩邊，並開始新的一輪比賽；「Left only」「Right only」只送一邊。送出後游標留在輸入列，可以接著打下一個。
- 上方列：「Pair」、兩邊的名稱（A vs B）、交換左右（Alt+Shift+S）、「Close pair」。Close pair 只是解除配對，兩個終端機照常在它們的分頁裡跑。
- 「Change」打開選單，列出其他在跑的終端機（Agent 在前），選一個就換到這一邊；選到另一邊正在用的，就是左右交換。
- 某一邊的終端機結束時，那一半改成空白狀態卡，有「Choose an agent...」與「Close pair」。

### 比賽

- 只看結構化的 agent 狀態（OSC 7501 或 `agentctl report-state`，ADR-09），不讀畫面文字。
- 一邊的這一輪從送出提示的時間開始算；沒有經過輸入列送出時，從第一次回報 Working 的時間開始（用 host 回報的狀態時間 `state_at_ms`）。
- 回報過 Working 之後回報 Done 才算完成；Blocked 仍算在跑。完成後又回報 Working，表示 Agent 繼續做，那一邊重新計時。
- 一邊先完成時，那一邊的面板頂端畫一條棋盤格的「終點線」，由左往右畫出來（約 600 ms）；標題列出現「Finished first」。Windows 關閉動畫效果時直接顯示，不做動畫。
- 兩邊都完成後，面板上方出現結果條：小旗子、「<名稱> finished first, 12s ahead」（相差不到 1 秒時是「A dead heat」），兩邊各自的時間、變更檔案數與 +／- 行數、「Review」與「Keep」。結果條右上角的 × 可以收起到下一輪。
- 「Review」把焦點移到那一邊，切到 Review 版面看它的 diff；再按 Ctrl+Shift+R 會回到 Pair。
- 「Keep」只在畫面上標記你選了哪一邊（標題列與結果條出現「Kept」），不做任何 git 操作。再按一次取消。
- 兩邊都完成後，任一邊再回報 Working，或從輸入列送給兩邊，都會開始新的一輪。

### 變更數

`GitCache` 多了 `with_changes(dir, machine)`：被這樣問過的資料夾，背景執行緒查 git 資訊時會一併數未提交的變更（`git status --porcelain=v1 -z --untracked-files=all` 數檔案，`git diff HEAD --numstat -z` 加總行數，未追蹤的文字檔數行數，最多 200 個、每個 1 MiB 以內，只限這台電腦）。其他資料夾不多跑 git。任一邊狀態改變時，以及有一邊在 Working 的期間每 3 秒，Pair 會讓兩個資料夾重新查一次，所以剛開始工作的幾秒可能還顯示「no changes」。

## 新增與改名

| 種類 | 名稱 |
|---|---|
| 版面 | `pair`（顯示名稱 Pair），mod：`pair-bar`、`pair-stage`、`pair-prompt` |
| 命令 | `pair.toggle`（切到 Pair，再按一次回原本的版面）、`pair.start`（設定卡的選擇；`pair.start:<左 profile>,<右 profile>`；`pair.start:existing` 用兩個開著的 Agent）、`pair.send:<both\|left\|right>`（送輸入列的文字；`pair.send:both:<文字>` 送指定文字）、`pair.prompt`（游標移到輸入列）、`pair.swap`、`pair.repair:<left\|right>`（打開選單）或 `pair.repair:<side>:<session id>`、`pair.close`、`pair.keep:<side>`、`pair.review:<side>`、`pair.dismiss`（收起結果條） |
| 按鍵 | keys.json 的 `layout:pair` 區段：Alt+P → `pair.prompt`、Alt+Shift+S → `pair.swap`。只在 Pair 顯示時有效，其他版面的 Alt+P 照常送進終端機。`pair.toggle` 沒有預設的全域按鍵（見待解問題） |
| `ui.json` | `pair`：`{"left_profile", "right_profile", "worktrees"（預設 true）, "left", "right"}`。`left`／`right` 是目前這一對的 session id，重開視窗時接回來；終端機已經不在就清掉 |
| 新檔案 | `libs/ui/include/agentdesk/ui/pair.hpp`、`libs/ui/src/pair.cpp`（純邏輯）、`libs/ui/layouts/pair.json`、`apps/desktop/pair_layout.hpp`、`apps/desktop/pair_layout.cpp`、`tests/unit/pair_test.cpp` |
| 第一次使用提示 | `pair` 卡：用途句加五條提示（輸入列與 Enter、Alt+P、worktree、結果條的 Review 與 Keep、交換左右） |
| 設定 › Look | Pair 的版面卡片有自己的小圖（兩個面板、左邊的終點線、下方輸入列） |

## 和規格不同的地方

1. **「誰先完成」比的是各自的用時，不是完成的時刻**。兩邊同時送出時兩者一樣；分開送出（例如先 Left only 再 Right only）時，用時比較公平。
2. **比賽只在 Pair 有配對時計算**，資料來自狀態欄位。沒有經過輸入列送出、而是直接在終端機裡打字時，從第一次回報 Working 開始計時，會比實際按下 Enter 晚一點點。
3. **Keep 不做任何 git 操作**，依照指示只是畫面上的標記。合併或刪除 worktree 仍然用既有的工具（Review 的 commit、「Remove worktree」）。
4. **Pair 開出的 worktree 不會自動移除**。關掉 pair 或關掉終端機後，checkout 與 `pair/` 分支都還在，要自己用「Remove worktree」清掉。這樣比較保守，不會誤刪 Agent 的成果。
5. **沒有給 `pair.toggle` 預設的全域按鍵**。同時在做的 Inbox、Stage、Garden 也可能要 Ctrl+Shift+字母，為了避免合併時撞鍵，先不綁；從版面卡片、Ctrl+Shift+L 或命令面板都能切到 Pair。
6. **在別台機器上配對**：用目前 space 所在的機器開 session 與 worktree。變更數只在這台電腦數未追蹤檔案的行數，遠端的資料夾只有 git diff 的行數。

## 建議的使用者指南文字

### Pair（並排比較）

> **Pair** 把兩個 Agent 左右並排，下方一個共用的輸入列，可以把同一個任務同時交給兩邊，例如 Claude Code 和 Codex，或同一個 Agent 開兩次，看誰先做完、做得比較好。
>
> 第一次打開 Pair 時會看到設定卡：
>
> - 左右各挑一個程式。
> - **Separate worktrees**（預設打開）：目前的資料夾是 git 儲存庫時，兩邊各自在一個新的 worktree 裡工作（`pair/` 開頭的新分支），不會改到同一份檔案。未提交的變更不會帶過去。資料夾不是 git 儲存庫時，兩邊只能在同一個資料夾裡工作，卡片上會提醒你。
> - 按 **Start pair**。已經有兩個 Agent 在跑時，也可以按 **Pair two open agents** 直接把它們配成一對。
>
> 配好之後：
>
> - 兩邊都是一般的終端機，可以直接點進去打字。
> - 在下方的輸入列打字（點它或按 Alt+P），按 Enter 送給兩邊；**Left only**、**Right only** 只送一邊。點終端機或按 Esc 就回到終端機。
> - 每一邊的標題列會顯示這一輪花了多久，以及那個資料夾改了幾個檔案、增減幾行。
> - 一邊先做完時，它的面板頂端會出現一條終點線。兩邊都做完後，上方的結果條會告訴你誰先完成、快了多少，並可以 **Review** 任一邊的 diff，或按 **Keep** 標記你要留下的那一邊（只是標記，不會動到 git）。
> - 上方列的交換按鈕（Alt+Shift+S）把左右對調；面板標題列的 **Change** 可以換成另一個正在跑的 Agent；**Close pair** 解除配對，兩個終端機照常在「Pair」分頁裡繼續跑。
>
> 誰先完成只依 Agent 自己回報的狀態判斷，不會從畫面文字猜。Pair 建立的 worktree 不會自動刪除，用完請用「Remove worktree」清掉。

## 驗證

### 截圖

在 `build\shots\pair\`（Debug，`screenshot_ui.exe`，`--cwd` 指向 `build\shots\pair\cwd\demo` 這個中性的示範 git 儲存庫；`USERPROFILE` 指到 `build\shots\pair\cwd`，所以路徑顯示成 `~\demo`、worktree 也建在 build 資料夾裡；`GIT_CEILING_DIRECTORIES` 讓「不是 git」的示範資料夾不會被外層的倉庫認走）。兩個替身 Agent 是 `screenshot_ui` 新增的 profile `pair-quick` 與 `pair-steady`：等一行輸入，回報 working，印幾行步驟，改 README.md 並寫一個自己的筆記檔，回報 done。完全沒有執行真的 Claude Code 或 Codex。

每一張都有深色與淺色兩個版本（`-dark`、`-light`）：

- `setup-*.png`：設定卡，資料夾是 git 儲存庫，Separate worktrees 打開。視窗較矮，所以沒有小插圖。
- `setup-plain-*.png`：資料夾不是 git 儲存庫，開關停用，黃色警告。
- `prompt-*.png`：剛配好的一對，共用輸入列裡打好了任務，還沒送出。
- `running-*.png`：送出後兩邊都在 Working（轉圈），標題列計時。
- `race-*.png`：兩邊都完成後的結果條、左邊的終點線與「Won」、兩邊的時間與變更數。
- `tips-*.png`：Pair 的第一次使用提示卡（`AGENTDESK_TIPS=1`）。

截圖時 PowerShell 會在 worktree 裡留下模組快取資料夾 `Microsoft/`（只在這個隔離的測試環境出現），截圖腳本把它加進示範儲存庫的 `.git/info/exclude`，所以變更數只算 Agent 自己改的檔案。

### 測試

開工時 430 個，完成後 447 個（單元測試 360 個），Debug 的 `scripts\build.cmd msvc-x64-debug all` 全部通過，`ConPty.CtrlCByteInterruptsPing` 這次也通過。新增 17 個： `Pair.*` 與 `PairRace.*`（pair 的生命週期、比賽計時與結果、worktree 與 session 名稱、預設程式、命令與目標解析、只在 Pair 生效的按鍵、版面與提示卡）。`LayoutTips.EveryLayoutAndFocusHasACard`、`Layout.ShippedLayoutsParseAndNameEveryNode` 的名稱清單加上 `pair`。

## 待解問題

1. **`pair.toggle` 的全域按鍵**：建議整合時和 Inbox／Stage／Garden 一起決定，例如 Ctrl+Shift+2。
2. **Pair 建立的 worktree 要不要提供一鍵清除**：例如 Close pair 時詢問「也移除兩個 worktree 嗎？」。目前不做，避免誤刪。
3. **送出後的 Enter**：沿用 mod 的作法，先貼上文字再送 `\r`。真的 Claude Code 與 Codex 在 bracketed paste 之後接 Enter 的行為沒有實測（依指示不執行真的 Agent），需要人工驗收。
4. **沒有截圖驗證的項目**：Windows 關閉動畫效果時的終點線、遠端機器上的 pair、某一邊終端機結束時的空白卡、Change 選單、程式選擇框打開的清單。邏輯有單元測試，畫面沒有截圖。
5. **計時的起點**：直接在終端機裡打字（不經過輸入列）時，從第一次回報 Working 開始算，會比實際按下 Enter 晚一點。

## 改到的共用檔案（合併時注意）

- `apps/desktop/app_internal.hpp`：引入 `pair_layout.hpp`；`Settings::pair`、`App::State::pair`；`Placement::pair_side`；`GitInfo` 的變更數欄位、`GitCache::with_changes`、`Entry::changes`、`query()` 多一個參數；新的 `pair_layout.cpp` 宣告區段，以及 `draw_one_pane`、`worktree_root` 的宣告。
- `apps/desktop/git_cache.cpp`（變更數）、`sessions.cpp`（`place()` 遇到 `pair_side` 交給 Pair）、`worktrees.cpp`（`worktree_root` 從匿名命名空間移出）、`panes.cpp`（`draw_one_pane` 包一層 `draw_pane`）、`style_layouts.cpp`（`styled_panes` 加 `pair`、面板外框分派、設定卡小圖、`style_tick` 呼叫 `pair_tick`）、`frame.cpp`（區塊分派一行）、`commands.cpp`（命令分派一行）、`settings_store.cpp`（讀寫 `pair`）、`CMakeLists.txt`。
- `libs/ui`：`layout.cpp`（`builtin_names()` 在 review 與 classic 之間加 `pair`、`builtin_text`）、`layout.hpp` 註解、`layout_styles.cpp`（mod 對照表、名稱表、通知位置避開輸入列）、`layout_styles.hpp` 註解、`layout_tips.cpp`（`pair` 卡與 `card_keys()`）、`command_names.cpp`、`keymap.cpp`（`layout:pair`）、`CMakeLists.txt`（`pair.cpp`、`pair.json`）。
- `tests`：`layout_test.cpp`、`layout_tips_test.cpp`、`CMakeLists.txt`。
- `tools/qa/screenshot_ui.cpp`：新增 `pair-quick`、`pair-steady` 兩個 profile。

和其他第二階段分支相鄰的地方：`builtin_names()`、`kModStyles` 的陣列大小、`kNames`、`card_keys()`、`command_names.cpp`、`kLayoutDefaults`、`frame.cpp` 的區塊分派、`commands.cpp` 的命令分派、`app_internal.hpp` 的 State 與宣告區段。合併時兩邊的項目都留下、陣列大小相加即可。

## 合併紀錄（Merge notes）

2026-11-21 在 `feature/round2-merge` 合併時，Pair 這邊統一或補上了下面幾件事，比賽與 worktree 的行為沒有改：

- **全域切換鍵 Ctrl+Shift+2**（待解問題 1）：`global` 區段加上 `ctrl-shift-2` → `pair.toggle`。`global`、`terminal` 與每個 `layout:*` 區段的預設都檢查過，沒有用到這個組合，所以照整合時的指示採用它。代價是終端機裡收不到 Ctrl+Shift+2（有些程式把它當成 Ctrl+@），和其他 Ctrl+Shift 全域鍵一樣，可以在 keys.json 改掉。`Pair.KeysOnlyWhilePairShows` 多檢查兩件事：每個內建版面都能用它切換，預設裡也只有這一個綁定。設定卡左下因此會出現「Back: Ctrl+Shift+2」。
- **命令面板**：`kCommands` 加上 `pair.toggle`、`pair.swap`、`pair.close`。
- **第一次使用提示**：最後一條改成「Back to your layout」（Ctrl+Shift+2）。提示卡最多五條，所以拿掉 worktree 那一條；設定卡上本來就有說明。
- **變更數只剩一套機制**：保留 Pair 的 `GitCache::with_changes()` 與 `changed_files`、`lines_added`、`lines_removed`，Inbox 改用同一套（拿掉 Inbox 的 `GitInfo::changed`）。
- **頂端列**：右邊加上新增、設定、回到原本版面三顆按鈕（共用的 `layout_bar_buttons()`），和 Stage、Inbox、Garden 一致，停在設定卡時也能用滑鼠離開。
- **版面順序**：Pair 排在 Review 之後、AgentDesk Mac 之前，仍是一般版面，不是彩蛋。
