# 第二輪實作紀錄：Inbox（Agent 收件匣，C1）

日期：2026-11-21｜分支：`feature/round2-inbox`（從 `feature/ideas` 的 9c46158 開出）｜規格：`docs/design/ideas.md`「第二輪構想」的 C1，以及第二輪共用說明

這份紀錄寫給整合的人：做了什麼、和規格哪裡不同、新增的命令／按鍵／`ui.json` 鍵與檔案、建議寫進使用者指南的文字、還沒解決的事，以及改到哪些共用檔案。設計文件、PRD、使用者指南都沒有動。

## 做了什麼

新的內建版面 `inbox`（顯示名稱「Inbox」），排在 Mission Control 和 Review 之間。全域切換鍵 Ctrl+Shift+I（`inbox.toggle`；keymap 裡原本沒有人用這個組合）：從其他版面切過來，再按一次回到原本的版面，和 Ctrl+Shift+M、Ctrl+Shift+R 的規則一樣（共用 `toggle_layout`）。

版面分三塊：上方 42 px 的列、左邊的佇列（寬度可拖拉，360～720 px）、右邊的面板區（照常顯示目前分頁的終端機，從卡片按 Open 的 Agent 就出現在這裡）。

### 上方的列

- 「Inbox」標題，後面是「N need you」（需要你的顏色）與「N done」（完成的顏色）兩顆小藥丸，有卡片被延後時再加一個安靜的「N snoozed」文字按鈕（按下去全部重新顯示）。
- 右邊是「Newest | Oldest」排序切換、新增、設定、回到原本版面三個圖示按鈕，以及視窗按鈕（Windows 樣式時）。

### 需要你的卡片

每個狀態是 Blocked 的 Agent 一張卡，預設最新開始等的排最前面，可以切成等最久的排最前面（`ui.json` 的 `inbox_order`）。卡片內容由上到下：

1. 程式圖示、Agent 名稱；右邊是問題種類的小藥丸（Permission／Question／Sign-in）和等了多久（時鐘圖示加「now」「40s」「3m」「1h 5m」，等超過 10 分鐘改用需要你的顏色）。
2. 一行說明：space 名稱 · 分頁或 Agent 的標題 · 分支 · 「3 files changed」。
3. Agent 自己說要什麼：結構化回報的訊息（OSC 7501 的 `msg`，或 `agentctl report-state --message`，也就是 SessionView 的 `agent_message`），最多三行，超過的加刪節號。沒有訊息時顯示一行淡色提示「No message in its report — the screen below shows what it asks.」。
4. 畫面最後四行的即時預覽（JetBrains Mono 11，新的一行貼在底部）。只用來看，狀態一律不從畫面文字判斷（ADR-09）。
5. 按鈕列：左邊是快速回答（第一個用 accent 填滿，按鈕上有 1～9 的小鍵帽），右邊是 Snooze 與 Open。卡片沒有快速回答時，左邊改放一個「Reply (R)」主要按鈕。送出後到 Agent 換狀態之前，按鈕列中間會出現綠色的「✓ Sent」（6 秒內淡出），讓人知道已經送了。
6. 選取中的卡片多一列回覆欄位與送出按鈕：Enter 送出文字再補一個 Enter，送完游標留在欄位裡。無法輸入（終端機由別的視窗控制、機器連不上）時欄位停用並說明原因。

卡片外觀用第一階段的 token：圓角 10、level 1 陰影、左側 3 px 需要你色條（和通知一致）、16 px 內距、Title／Body／Caption 三層字級。選取中又握有鍵盤時，外圈是 2 px 的 focus ring；選取但鍵盤在終端機時，只有 1 px 的 `selected_edge` 邊框。

### 完成區

需要你的卡片下面，是「DONE」區：從上次看過之後完成的 Agent（AttentionTracker 的 Done），一列 56 px，綠色色條、名稱、「Finished 4m ago · 訊息或 space」，右邊 Dismiss 與 Open。Dismiss 等於「看過了」（`attention.seen`，完成變回閒置），Open 會切到那個 Agent，也算看過。

只有完成、沒有需要你的卡片時，最上面放一張「Nothing needs you」的小橫幅，帶 `kAllDone` 插圖和摘要。

### Inbox zero

佇列整個空了，就顯示 AgentDesk 自己畫的插圖：一個空的收件盤，上方一個打勾的圓先放大再回彈，勾勾一筆畫出來，接著六道短短的火花往外飛散並淡出（約 1.1 秒，只播一次）。下面是「Inbox zero」、「Nothing needs you right now.」，以及摘要「3 agents working, 2 idle」（有 Agent 在工作時前面加一個工作色的小圓圈）。有被延後的卡片時，再加一個「N snoozed cards — show」連結。Windows 的「動畫效果」關掉時（`tok::reduce_motion()`）直接顯示最後一格。

### 動畫

- 卡片進來：從右邊滑入 16 px 並淡入（`tok::appear`，180 ms）。
- 卡片離開（回答後 Agent 換狀態、Snooze、Dismiss）：在原位往左滑 24 px 並淡出（260 ms）。其他卡片的位置用緩動補上空位，不會突然跳。
- 動畫期間才呼叫 `tok::keep_animating()`，結束就回到原本的喚醒節奏。減少動畫時全部直接到位。
- 聲音規則沒有改：需要你一律響。

### 鍵盤

只有佇列握有鍵盤時才生效：點一下佇列，或剛切進 Inbox（佇列會從終端機手上拿走鍵盤）。點一下終端機，鍵盤就回到終端機，打字不會被攔。另外，有任何元件在用鍵盤（終端機、回覆欄位）、有彈出視窗、或按著 Ctrl／Alt／Win 時，佇列都不處理按鍵。

| 按鍵 | 動作 |
|---|---|
| ↑／↓（或 K／J）、Home／End | 移動選取（需要你的卡片與完成列是同一串） |
| Enter | Open：到面板區處理那個 Agent，鍵盤交給終端機 |
| R | 游標移到選取卡片的回覆欄位 |
| 1～9 | 按下選取卡片的第 N 個快速回答 |
| S | 需要你的卡片：Snooze；完成列：Dismiss |
| D、Delete | 完成列：Dismiss |
| Ctrl+Shift+I | 回到原本的版面（全域鍵，終端機有焦點時也有效） |

回覆欄位裡 Enter 送出，Esc 離開欄位（鍵盤回到佇列）。

### Snooze

Snooze 讓卡片先收起來，直到下面任一件事發生才重新出現：Agent 離開等待狀態再回來（等待開始的時間變了）、它換了一個問題（訊息或種類變了）。這條規則是純邏輯 `ui::inbox::still_snoozed`，有單元測試。Snooze 只存在這個視窗的記憶體裡，重開 agent-ui 就清掉。

### 快速回答（可以自己設定）

不寫死在程式裡。每個程式（programs.json 的 key，例如 `claude`、`codex`）可以在 `ui.json` 的 `inbox_answers` 設定自己的按鈕：

```json
"inbox_answers": {
  "claude": [
    { "label": "Yes", "send": "1", "kinds": ["permission"] },
    { "label": "No", "send": "\u001b", "kinds": ["permission"] }
  ],
  "my-agent": [
    { "label": "Continue", "send": "", "enter": true }
  ],
  "*": []
}
```

- `send`：原封不動寫進終端機的位元組，和 keys.json 的 `send:` 走同一條路（`host::RawInput`）。控制鍵用 JSON 跳脫字元寫，例如 `\u001b` 是 Esc。
- `enter`：送完再補一個 Enter（`\r`）。`send` 和 `enter` 至少要有一個。
- `kinds`：只在這幾種回報出現（`permission`、`question`、`auth`）；沒寫就是都會出現。回報本身沒有種類時，該程式的按鈕全部出現。
- `label`：按鈕文字，最多 40 bytes。沒寫就用送出的內容命名（「Esc」「y」「Enter」）。
- 每個程式最多 9 個（對應 1～9 鍵）。
- 查找順序：自己在 `inbox_answers` 寫的同名程式 → 隨附的預設 → 自己寫的 `"*"`。寫成空陣列等於把該程式的按鈕關掉。
- 格式不對的項目會跳過，原因留在 `Settings::inbox_answer_errors`（目前沒有顯示在畫面上，見待解問題）。
- `ui.json` 改了以後，Inbox 開著時最多 2 秒內重新讀 `inbox_answers`，不用重開。AgentDesk 存設定時不會覆寫這個鍵（只讀不寫，保留使用者自己寫的內容）。

隨附的預設只有兩個程式，而且只用在 permission 回報：

| 程式 | 按鈕 | 送出 | 依據 |
|---|---|---|---|
| Claude Code | Yes／No | `1`／Esc | 權限提示是編號選單，第一項是同意；Esc 是「不要，改告訴 Claude 怎麼做」 |
| Codex | Yes／No | `y`／Esc | 核准提示用 `y` 同意；Esc 取消 |

**這兩組預設都還要用真的 CLI 驗證**：我們不能執行 Claude Code 或 Codex，按鍵是依官方文件與提示畫面的描述推得的。CLI 改了提示格式，預設也要跟著改（`libs/ui/src/inbox.cpp` 的 `default_answers()`），或請使用者在 `inbox_answers` 覆寫。其他程式沒有預設，因為找不到能引用的文件依據。

### 改了幾個檔案

- `GitInfo` 多一個 `changed`（未提交的檔案數，`git status --porcelain=v1 -z --no-renames --untracked-files=normal` 的筆數；-1 代表還不知道）。在 git cache 原本的背景執行緒裡一起算，更新時機和分支、ahead/behind 相同（Agent 一輪工作結束、git 資料夾有變化、或視窗在前景時每 60 秒）。
- `imgui_adapter` 多一個 `clear_keyboard()`：不論誰握著鍵盤都放掉，切進 Inbox 時讓佇列先拿到鍵盤。原本的 `release_keyboard()` 只放掉其他視窗的，但終端機和佇列在同一個視窗裡。
- 截圖工具多三個 state profile：`ask-claude`、`ask-codex`、`ask-question`。它們先印幾行自己的文字，再用 OSC 7501 回報 `state=blocked`、`kind`、`app` 與 base64 的訊息，提示字元改成 `> `，畫面上不會出現資料夾路徑。

## 新增的東西

| 種類 | 名稱 |
|---|---|
| 版面 | `inbox`（`libs/ui/layouts/inbox.json`，mod：`inbox-bar`、`inbox-queue`，style `inbox`） |
| 命令 | `inbox.toggle`、`inbox.open[:<id>]`、`inbox.snooze[:<id>]`、`inbox.unsnooze`、`inbox.dismiss[:<id>]`、`inbox.reply[:<id>]`、`inbox.answer:<n>[:<id>]`、`inbox.order:newest\|oldest`；沒有 id 時作用在選取的卡片。命令面板列出 `inbox.toggle`、兩個 `inbox.order`、`inbox.unsnooze` |
| 按鍵 | 全域 `ctrl-shift-i` → `inbox.toggle`；佇列內的按鍵見上表（不在 keys.json，只在佇列握有鍵盤時有效） |
| `ui.json` | `inbox_order`（`"newest"`，可改 `"oldest"`）、`inbox_answers`（只讀） |
| 第一次使用提示 | `inbox` 卡片：用途一句、五條提示（點佇列後用方向鍵和 Enter、R 回覆和 1～9、S 延後與 Dismiss、`inbox_answers`、Ctrl+Shift+I 回去） |
| 設定 › Look | Inbox 的版面卡片縮圖（列、左側三張卡片，第一張有需要你的色條和兩顆回答按鈕、右側終端機） |
| 新檔案 | `libs/ui/include/agentdesk/ui/inbox.hpp`、`libs/ui/src/inbox.cpp`、`libs/ui/layouts/inbox.json`、`apps/desktop/inbox_layout.cpp`、`tests/unit/inbox_test.cpp`、本文件 |

## 和規格不同的地方

1. **卡片上沒有固定的「同意」「拒絕」**：規格寫卡片上直接有「同意」「拒絕」。這裡改成可設定的快速回答，因為不同 CLI 的按鍵不一樣，我們也無法親自確認。隨附的 Yes／No 只給 Claude Code 與 Codex 的 permission 回報，而且標明待驗證。
2. **「等了多久」優先用持久化的時間戳**：brief 建議用 `StyleUi::changes`。它是這個視窗看到狀態改變的時間，重開 agent-ui 後會從「now」重新算。所以先用側欄記在 `ui.json` 的 `agents_changed`（`s.changed`，狀態是 blocked 時的時間），沒有時才退回 `StyleUi::changes`。完成區只能用 `StyleUi::changes`，因為 `s.changed` 把 done 和 idle 當成同一種狀態。
3. **預覽的快取是自己的一份**：brief 說沿用 Mission Control 的 `screen_tail` 快取。邏輯（`ui::mission::screen_tail`、每 250 ms 最多重讀一次）和資料結構（`MissionState::Tile`）都沿用，但存在 `s.inbox.tails`，不放進 `s.mission.tiles`。Stage 會改寫或拿掉 Mission Control 的程式，分開存比較不會在合併時互相踩到。
4. **Snooze 不存檔**：重開視窗就清掉。被延後的通常是「等一下再處理」的事，留到下次開機反而容易忘記。
5. **回覆用 PasteInput 再補 Enter**：和 mods 的 `send` 同一條路。如果程式有開 bracketed paste，回覆會被當成一段貼上的文字，多行文字就不會打到一半被送出。
6. **選取不分區段**：需要你的卡片與完成列是同一串，↑／↓ 可以一路走下去。這樣比切換兩個清單單純。

## 建議的使用者指南文字

### Inbox（Agent 收件匣）

> 同時跑好幾個 Agent 時，最花時間的是搞清楚「誰在等我、要我做什麼」。Inbox 把需要你的 Agent 排成一列卡片，按 Ctrl+Shift+I 打開，再按一次回到原本的版面。也可以在設定 › Look 選「Inbox」。
>
> 每張卡片寫著 Agent 的名稱、在哪個 space 和分支、資料夾改了幾個檔案、等了多久，以及 Agent 自己在回報裡說它要什麼；下面是它畫面最後幾行，方便你判斷。最新開始等的排最前面，上方的「Newest／Oldest」可以改成等最久的排最前面。
>
> 處理方式：
>
> - **快速回答**：卡片左下的按鈕，例如 Claude Code 權限提示的 Yes／No。按下去就把對應的按鍵送進那個終端機。
> - **回覆**：在卡片下方的欄位打字，Enter 送出（會自動補一個 Enter）。
> - **Open**：在右邊的面板打開那個 Agent，直接操作它的終端機。
> - **Snooze**：先收起來。Agent 換了問題，或離開等待後又回來，卡片會再出現。上方的「N snoozed」可以讓它們全部回來。
>
> 卡片下面的「DONE」區是你還沒看過的已完成 Agent，可以 Open，也可以按 Dismiss 標成看過。全部處理完會出現「Inbox zero」。
>
> 鍵盤：先點一下左邊的佇列，↑／↓ 選卡片，Enter 打開，R 回覆，1～9 按快速回答，S 延後。這些鍵只在佇列被點選時有效，點一下右邊的終端機就回到一般打字，不會被攔下。
>
> 快速回答可以自己設定：在 `ui.json` 加上 `inbox_answers`，依程式（programs.json 的名稱，例如 `claude`、`codex`）列出按鈕的文字和要送出的按鍵。`"*"` 代表其他所有程式，空陣列代表不要按鈕。存檔後幾秒內就會生效。

## 待解問題

1. **Claude Code 與 Codex 的預設按鍵還沒用真的 CLI 驗證**（見上文）。驗證時看三件事：數字鍵或 `y` 是否立刻選擇（不需要 Enter）、Esc 是否等於拒絕、提示格式改版後還對不對。
2. **`inbox_answers` 的錯誤沒有顯示在畫面上**：目前只記在 `Settings::inbox_answer_errors`。可以比照 keys.json 改壞時跳一則通知，或放在設定頁。
3. **變更檔案數的更新時機**：git cache 只在 Agent 一輪結束、git 資料夾變化、或每 60 秒更新。Agent 正在改檔案時，數字可能慢一點才跟上。`git status` 在非常大的儲存庫會花一點時間（在背景執行緒，不卡畫面）。
4. **Done 的判定沿用 AttentionTracker**：Agent 第一次出現時就已經是 done（例如 agent-ui 剛啟動），或從 idle 直接跳到 done，不會算成「沒看過的完成」，所以不會出現在 Done 區。這是原本的規則，Inbox 沒有改。
5. **截圖工具的 `--input` 在同一格移動並按下滑鼠時**，蓋在卡片上的按鈕要多一格才會被點到（ImGui 對可重疊元件的規則）。實際使用時滑鼠一定先移過去，所以沒有影響；腳本要先 `move:` 再 `click:`。
6. **沒有截圖驗證的項目**：Windows 關閉動畫效果、高對比、125%／150% 縮放、遠端機器斷線時的卡片（程式有處理：預覽變暗並寫「Machine unreachable」、按鈕停用）。
7. **`ConPty.CtrlCByteInterruptsPing`**：依指示，在 agent 的 shell 裡失敗時不處理。

## 改到的共用檔案（合併時注意）

- `apps/desktop/app_internal.hpp`：引入 `inbox.hpp`；`Settings` 加 `inbox_order`、`inbox_answers`、`inbox_answer_errors`；`GitInfo::changed`；`App::State::InboxState inbox`；`kCommands` 加四個 Inbox 命令；`inbox_layout.cpp` 的宣告。
- `apps/desktop/frame.cpp`（`inbox-bar`、`inbox-queue` 兩個區塊）、`commands.cpp`（命令分派；`layout.switch` 切到 Inbox 時呼叫 `inbox_entered`）、`app.cpp`（喚醒時間）、`settings_store.cpp`（讀寫 `inbox_order`，讀 `inbox_answers`）、`settings_pages.cpp`（版面卡片縮圖）、`git_cache.cpp`（`changed`）、`CMakeLists.txt`。
- `libs/ui`：`layout.cpp`（`builtin_names()` 現在是 workspace、terminals、mac、mission、inbox、review、classic；`builtin_text`）、`layout.hpp` 註解、`layout_styles.cpp/.hpp`（mod 對照表、顯示名稱）、`keymap.cpp`（`ctrl-shift-i`）、`command_names.cpp`、`layout_tips.cpp`（卡片與 `card_keys()`）、`imgui_adapter.hpp/.cpp`（`clear_keyboard`）、`CMakeLists.txt`（`src/inbox.cpp`、`layouts/inbox.json`）。
- `tests`：`layout_test.cpp`、`layout_tips_test.cpp` 的版面名稱清單加上 `inbox`；`CMakeLists.txt`。
- `tools/qa/screenshot_ui.cpp`：`ask-*` profile。

和 Stage、Pair、Garden 合併時會相鄰的地方：`builtin_names()`、`builtin_text()`、`libs/ui/CMakeLists.txt` 的版面清單、`layout_styles.cpp` 的兩張表（陣列大小要跟著改）、`layout_tips.cpp` 的 `card_keys()` 與 `make()`、`command_names.cpp`、`keymap.cpp` 的全域鍵、`kCommands`、`App::State`、`layout_test.cpp` 與 `layout_tips_test.cpp` 的名稱清單。兩邊的名稱都保留即可。Stage 取代 Mission Control 時，`builtin_names()` 裡的 `mission` 換成 `stage`，`inbox` 留在原位。Pair 若也要算檔案數，可以直接用 `GitInfo::changed`。

## 截圖

在 `build\shots\round2-inbox\`（Debug，`screenshot_ui.exe --cwd build\shots\round2-inbox\cwd\demo`，一個中性的示範 git 儲存庫，有 3 個未提交的變更）：

| 檔案 | 內容 |
|---|---|
| `cards-dark.png`、`cards-light.png` | 三個需要你的 Agent（Claude Code 與 Codex 的權限、一般 shell 的問題），右邊是打開的終端機 |
| `reply-dark.png`、`reply-light.png` | 選取第一張卡片、按 R、打好回覆還沒送出 |
| `sent-dark.png` | 回覆送出之後：終端機收到文字，卡片顯示「✓ Sent」 |
| `leave-dark.png` | Snooze 一張後：剩兩張、上方「1 snoozed」、選取移到補上來的卡片 |
| `zero-dark.png`、`zero-light.png` | Inbox zero（動畫結束後）與摘要 |
| `zero-anim-dark.png` | 三張都 Snooze 後的 Inbox zero，火花剛散開，下方「3 snoozed cards — show」 |
| `done-dark.png`、`done-light.png` | 沒有需要你的卡片、只有完成的 Agent：「Nothing needs you」橫幅與 DONE 區 |
| `tips-dark.png`、`tips-light.png` | Inbox 的第一次使用提示卡 |
| `settings-dark.png` | 設定 › Look 的 Inbox 版面卡片 |

## 測試

開工時 430 個（Debug，`scripts\build.cmd msvc-x64-debug all`，全部通過），完成後 443 個（單元測試 369 個）。完整跑一次時只有 `HostFixture.SlowClientDoesNotStallHost` 失敗一次，單獨重跑通過，是時間相關的偶發失敗。新增 `Inbox.*` 13 個：排序（最新／最久、同時間的順序、`inbox_order` 名稱）、Snooze 規則、預設快速回答只給已知程式的 permission、自訂覆寫與 `"*"`、格式錯誤的項目、最多 9 個與標籤長度、JSON 來回、`describe_send`、鍵盤選取、文字（等待時間、摘要、種類），以及命令名稱、Ctrl+Shift+I、內建版面與提示卡都已登記。

## 合併紀錄（Merge notes）

2026-11-21 在 `feature/round2-merge` 合併時，Inbox 這邊統一了下面幾件事。排序、Snooze、快速回答、回覆的行為都沒有改：

- **畫面預覽改用共用快取**：卡片的畫面最後幾行改呼叫 Stage 的 `screen_tail_of()`（`App::State::screen_tails`），刪掉 Inbox 自己的 `s.inbox.tails` 與 `tail_of()`。等下一次更新時由共用的 `tails_pending` 喚醒；關掉的終端機在 Inbox 畫佇列時從共用快取刪掉。
- **變更檔案數只剩一套機制**：拿掉 `GitInfo::changed`，改用 Pair 的 `GitCache::with_changes()` 與 `GitInfo::changed_files`。只有需要你的卡片會要求計數，完成列只用一般的 `get()`。計數方式也跟著 Pair 改成 `git status --porcelain=v1 -z --untracked-files=all`：未追蹤的資料夾逐檔計算（原本是 `normal`，整個資料夾算一筆）。
- **頂端列右邊的新增、設定、返回三顆按鈕**改用共用的 `layout_bar_buttons()`，外觀不變。
- **版面順序**：Inbox 排在 Stage 之後、Review 之前；Ctrl+Shift+I 不變。
