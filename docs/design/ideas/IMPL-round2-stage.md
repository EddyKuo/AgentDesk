# 第二輪實作紀錄：Stage（主舞台，C2）

日期：2026-11-21｜分支：`feature/round2-stage`（從 `feature/ideas` 的 9c46158 開出，含第一階段的設計 token）｜規格：`docs/design/ideas.md` 的「第二輪構想」C2

這份紀錄寫給整合的人：做了什麼、和規格哪裡不同、新增的命令／按鍵／`ui.json` 鍵與檔案、建議寫進使用者指南的文字、待解問題，以及動到的共用檔案。設計文件、PRD、使用者指南都沒有動。

## 做了什麼

新增內建版面 `stage`（顯示名稱 Stage），取代 Mission Control。

- **主舞台**：一個 Agent 的一般面板（`lone` 的 tab 區塊，完全可以操作、打字），佔掉上方大部分空間。舞台上的 Agent 就是目前聚焦的終端機，所以任何換焦點的方式（點縮圖、Alt+方向鍵、點通知、命令面板）都會換舞台。
- **頂端列（42 px）**：「Stage」標題，接著是台上那個 Agent：狀態圈裡的程式圖示、名稱、space 與細節、狀態標記；需要你時再加上它回報的訊息（紅字）。右邊是「← 上一個 Agent」的小按鈕（`stage.back`），然後是新增、設定、回到原本版面三顆按鈕。
- **底片條（filmstrip）**：上面一列是「Agents N」、篩選（All／Needs you N／Working N）與大小（S／M／L）；下面一排即時縮圖，每個 Agent 一張，可選擇連一般終端機也列進來。舞台上的 Agent 不在條裡，換上台時原本台上的那個會回到條裡。
- **縮圖**：卡片（`tok::card`，圓角 10，level 1 陰影）。標頭是狀態圈（需要你、完成是實線圈，工作中是慢慢轉的四分之三圈，閒置是淡色圈）加程式圖示、名稱，右邊是多久前變化或「Needs you」；中間是終端機畫面最後幾行，用縮小的等寬字畫出來（只顯示，不建 TerminalView，最多每秒更新 4 次）；M 和 L 多一行頁尾：需要你時顯示它的訊息，否則顯示所在的 space 與細節，加上子 Agent 數量。機器連不上時畫面變暗，寫「Machine offline」。
- **需要你**：縮圖外圍發出 needs-you 色的柔光，排到條的最前面（等最久的在前）。柔光先呼吸 3 次（每次 1.6 秒）再停在固定亮度，免得視窗一直重畫；Windows 關掉動畫效果時直接固定亮度。高對比模式不畫柔光，只留邊框。
- **換上舞台**：點縮圖、`stage.show:<id>`，或在 Stage 裡按 Alt+Left／Alt+Right（上一個／下一個 Agent）。新的面板從底色淡入（260 ms，ease-out）；縮圖換位置時會滑過去（260 ms），新出現的縮圖淡入。全部遵守「減少動畫」。
- **自動換台**：預設關閉。打開後，有 Agent 開始需要你時會自己上台，但有這些情況就不換：最近 4 秒內有按鍵或點擊（正在打字）、有選單或對話框開著、台上的 Agent 自己也在等你。
- **捲動**：縮圖超出寬度時，滑鼠滾輪（直的、橫的都可以）會水平捲動，兩端有淡出邊緣與圓形箭頭按鈕，點一下捲動約一個畫面寬。
- **大小**：S 160×96、M 224×136、L 304×184（設計 px）。視窗太矮時（條的高度超過視窗 40%）自動改用小一號，條上標的是實際使用的大小，滑鼠移上去會說明原因。
- **空白狀態**：完全沒有可上台的東西（沒有 Agent、也沒有分頁）時，整個舞台用 `draw_empty_state`（Agent 插圖、「Nothing on stage yet」、開始程式的按鈕），底片條隱藏。舞台上有終端機但條裡沒東西時，條裡改用單行版的空白狀態（新的共用元件 `draw_empty_row`）：還沒有 Agent、所有 Agent 都在台上、篩選後沒有東西（附「Show all」）。沒有 agent-host 時沿用載入中／錯誤卡。
- **第一次使用提示**：新增 Stage 的提示卡（5 條，按鍵從 keymap 查），Mission Control 的提示卡拿掉。
- **設定 › Look**：選到 Stage 時多三列：縮圖大小（Small／Medium／Large）、Plain terminals、Switch to an agent that needs you。版面卡片的預覽圖改畫 Stage（上面一個大面板，下面四張縮圖，第一張帶紅邊）。

### 取代 Mission Control

- `builtin_names()` 拿掉 `mission`、加上 `stage`；刪除 `libs/ui/layouts/mission.json` 與 `apps/desktop/mission_layout.cpp`。
- `ui::styles::replacement_for("mission")` 回傳 `stage`：存檔裡的 `"layout": "mission"`（也包括 `layout_return`）讀進來就變成 Stage。
- Ctrl+Shift+M 改綁 `stage.toggle`。舊命令名稱仍然有效：`mission.toggle` 切換 Stage、`mission.expand:<id>` 把那個 Agent 放上台、`mission.filter:<f>` 設定條的篩選（`blocked` 讀成 needs_you，`working` 不變，其他讀成 all）。`mission.wall` 已經沒有對應的畫面，拿掉了。
- `ui.json`：沒有新鍵時從 `mission_filter`、`mission_shells` 讀；下次存檔寫新鍵，並刪掉 `mission_filter`、`mission_sub_agents`、`mission_shells`。
- 留下來的共用邏輯：`ui::mission`（`screen_tail`、`wall_order`、`filter_matches`、`clean_message`、`wall_grid`、`move_selection`，連同原本的單元測試）照舊；畫面最後幾行的快取從 `MissionState::tiles` 搬到 `App::State::screen_tails`，對外用 `screen_tail_of(s, id, lines)` 取用（Inbox 若要顯示畫面最後幾行可以直接用）。`agent_roster`、`StyleUi::changes`、狀態卡都沿用。
- 通知被點擊時直接 `s.focus(session)`，在 Stage 裡等於把那個 Agent 放上台。

## 新增與改名

| 種類 | 名稱 |
|---|---|
| 版面 | `stage`（`libs/ui/layouts/stage.json`：`bar`、`stage-body` 堆疊（`stage` 分頁區塊、`empty`）、`strip`）；mod 名稱 `stage-bar`、`stage-strip`、`stage-empty` |
| 命令 | `stage.toggle`、`stage.show:<id>`、`stage.next`、`stage.prev`、`stage.back`、`stage.filter:all\|needs_you\|working`、`stage.size[:s\|m\|l]`（不帶參數時循環）、`stage.auto_switch`、`stage.shells`；`mission.toggle`、`mission.expand`、`mission.filter` 保留為別名 |
| 按鍵 | Ctrl+Shift+M → `stage.toggle`；keys.json `layout:stage`：Alt+Left → `stage.prev`、Alt+Right → `stage.next` |
| `ui.json` | `stage_filter`（"all"）、`stage_size`（"m"）、`stage_shells`（false）、`stage_auto_switch`（false）；不再寫入 `mission_filter`、`mission_sub_agents`、`mission_shells` |
| 新檔案 | `libs/ui/include/agentdesk/ui/stage.hpp`、`libs/ui/src/stage.cpp`、`libs/ui/layouts/stage.json`、`apps/desktop/stage_layout.cpp`、`tests/unit/stage_test.cpp` |
| 刪除 | `libs/ui/layouts/mission.json`、`apps/desktop/mission_layout.cpp`；`mission_test.cpp` 裡和版面、命令有關的兩個測試（移到 `stage_test.cpp` 改寫） |

純邏輯（`ui::stage`）：條的排序（`strip_order`、`lineup`）、篩選、大小預設與 `fit_size`、`step`（上一個／下一個）、換台紀錄 `History`、自動換台 `auto_switch_target`、水平捲動（`content_width`、`clamp_scroll`、`scroll_to_show`）、柔光 `glow_level`／`glow_moving`。

## 和規格不同的地方

1. **Ctrl+1～9 沒有用**：這組鍵已經是 `tab.select`。Alt+Left／Alt+Right 原本是 `pane.go`，Stage 一次只顯示一個面板，在面板之間移動沒有意義，所以只在 `layout:stage` 區段把它們改成上一個／下一個 Agent，其他版面不受影響。
2. **「交叉淡化」是淡入**：終端機是即時的 TerminalView，沒有離屏的舊畫面可以和新畫面疊在一起。現在的做法是新面板上蓋一層底色，在 260 ms 內淡掉（用放慢的建置確認過效果）。舊的那個 Agent 同時以縮圖的身分淡入回到條裡。
3. **條的排序不跟側欄**：側欄預設依狀態排序，若照用，縮圖會隨著狀態一直換位置。條改成固定順序（第一次看到的先後），只有「需要你」會讓縮圖往前跳，比較不晃。上一個／下一個也照這個固定順序。
4. **條裡的空白狀態是單行版**：條的高度（S 約 140 px）放不下 `draw_empty_state` 的直式排法（插圖 64 px＋標題＋按鈕），所以新增共用的 `draw_empty_row`（插圖、標題與說明、按鈕或連結排成一行）。整個舞台空著的時候仍用 `draw_empty_state`。
5. **子 Agent 沒有自己的縮圖**：縮圖只顯示數量（「+2」）。`mission_sub_agents` 這個設定拿掉了；沒有面板的子 Agent 用 `stage.show` 打開時，和以前一樣在 peek 視窗裡看。
6. **通知改到上方**：底片條佔滿下方，所以 Stage 的通知從頂端列下面出現（`toast_area("stage")`）。
7. **沒有狀態列**：Mission Control 底部有 workspace 狀態列，Stage 的下方是底片條，就不放了。

## 建議的使用者指南文字

### Stage

> Stage 把一個 Agent 放在大大的主舞台上，可以直接在裡面打字；其他 Agent 在下方的底片條排成一排即時縮圖，每張都看得到程式、名稱、狀態和畫面最後幾行。Ctrl+Shift+M 切換到 Stage，再按一次回到原本的版面。
>
> - 點縮圖就把它換上舞台，原本台上的 Agent 回到底片條。也可以按 Alt+Left／Alt+Right 換到上一個／下一個 Agent；頂端列右邊的「←」按鈕會換回剛才那一個。
> - 需要你的 Agent 縮圖會發光，並排到最前面（等最久的在前），同時照常播放提示音。
> - 底片條上方的 All／Needs you／Working 可以篩選縮圖，S／M／L 調整縮圖大小。視窗太矮時會自動用小一號的縮圖，讓舞台保持夠大。縮圖太多時用滑鼠滾輪左右捲動，或點兩端的箭頭。
> - 在縮圖上按右鍵可以放上舞台、偷看、重新命名或關閉。
> - 設定 › Look 選到 Stage 時，可以讓一般終端機也有縮圖，也可以打開「Switch to an agent that needs you」：有 Agent 需要你時自己換上台。這個選項預設關閉；打開後，你正在打字、有選單開著，或台上的 Agent 也在等你時，都不會自動換台。
>
> 以前用 Mission Control 的人，開啟後會直接換成 Stage，原本的篩選設定也會帶過來。

## 截圖

在 `build\shots\stage\`（Debug，`screenshot_ui.exe --cwd build\shots\stage\cwd`；`SHOT_EXTRA` 開六個回報不同狀態的 shell，其中 `test-suite` 10 秒後變成需要你；文字是用 `--input` 打進各終端機的 `Write-Host`）：

- `mixed-glow-dark.png`、`mixed-glow-light.png`：6 個 Agent 混合狀態，兩張需要你的縮圖發光、排在最前面。
- `hover-dark.png`、`hover-light.png`：滑鼠停在第一張縮圖上。
- `swapped-dark.png`、`swapped-light.png`：點縮圖換台之後，原本台上的回到條裡，頂端列的「←」按鈕指向它。
- `fade-mid-dark.png`：換台淡入的中途（用把淡入時間放慢到 4 秒的暫時建置拍的，已改回）。
- `content-dark.png`：用 Alt+Right 逐一換台並輸入文字後的樣子。
- `size-s-dark.png`：S 縮圖；`size-l-light.png`：選了 L，但視窗太矮，實際用 M（條上標 M）。
- `filtered-dark.png`：篩選「Needs you」但沒有符合的 Agent（單行空白狀態與「Show all」）。
- `noagents-dark.png`、`noagents-light.png`：只有一般終端機，條裡顯示「No agents yet」。
- `empty-dark.png`、`empty-light.png`：關掉唯一的分頁之後，整個舞台的空白狀態。
- `empty-step1-dark.png`：關閉時的「Close space?」確認框。
- `tips-dark.png`、`tips-light.png`：Stage 的第一次使用提示卡（`layout.tips:stage`）。
- `settings-dark.png`、`settings-light.png`：設定 › Look 的版面卡片（Stage 預覽）與 Stage 的三列選項。
- `other-workspace-dark.png`、`other-terminals-dark.png`、`other-mac-dark.png`、`other-review-dark.png`：其他版面照舊。

截圖裡的路徑是 worktree 底下中性的示範資料夾。

## 待解問題

1. **大螢幕上的 L**：截圖環境的視窗約 514 設計 px 高，L 都會退成 M，所以 L 只在單元測試與程式裡確認過，沒有實際截到。
2. **淡入只有放慢版截到**：正常 260 ms 的淡入太短，截圖工具抓不到。
3. **縮圖的更新頻率**：每張縮圖最多每秒重讀 4 次畫面，工作中的 Agent 狀態圈會讓畫面約每 50 ms 重畫一次（和原本側欄的轉圈相同）。很多 Agent 同時工作時的 CPU 用量沒有量測。
4. **自動換台的「正在打字」**：判斷依據是 4 秒內有任何按鍵或點擊，不分是不是打在終端機裡。
5. **使用者自己的 `layouts\mission.json`**：如果有人放了同名的版面檔，存檔中的 `mission` 仍會轉成 Stage（轉換發生在讀設定的時候）。檔案本身照樣載入，可以手動切回，但它用到的 `mission-bar`、`mission-wall` 已經沒有畫面，會顯示空白區塊。
6. **第一階段的小問題（沒有動）**：`write_style_settings` 是從準備寫入的 JSON 物件刪 `tiling_*`、`cockpit_*`，不是從檔案內容刪，所以那些舊鍵其實會留在 ui.json。Stage 自己的舊鍵（`mission_*`）是在合併到檔案內容之後才刪，不受影響。

## 改到的共用檔案（合併時注意）

- `apps/desktop/app_internal.hpp`：引入 `stage.hpp`；`Settings` 的 `mission_*` 換成 `stage_*`；`kCommands` 的 Mission 列換成 Stage；`MissionState` 換成 `ScreenTail`／`screen_tails`／`tails_pending` 與 `StageState`；`mission_layout.cpp` 的宣告換成 `stage_layout.cpp` 的；新增 `EmptyRow`／`draw_empty_row`；幾行註解。
- `apps/desktop/state_card.cpp`：新增 `draw_empty_row`（只有新增）。
- `apps/desktop/style_layouts.cpp`：`style_layout_tree`、`draw_style_overlay`、`look_style_options` 各加一個 Stage 分支。
- `apps/desktop/frame.cpp`（區塊分派）、`commands.cpp`（命令分派，拿掉 `mission_entered`）、`notices.cpp`（點通知）、`app.cpp`（喚醒時間）、`settings_store.cpp`（讀寫與轉換）、`settings_pages.cpp`（預覽圖）、`sessions.cpp`（註解）、`CMakeLists.txt`。
- `libs/ui`：`layout.cpp`／`layout.hpp`（`builtin_names()` 是 workspace、terminals、mac、stage、review、classic）、`layout_styles.cpp`／`.hpp`（mod 對照、名稱、`replacement_for`、`toast_area`）、`layout_tips.cpp`（Stage 提示卡、`card_keys()`）、`keymap.cpp`（Ctrl+Shift+M、`layout:stage`）、`command_names.cpp`、`mission.hpp`（開頭註解）、`CMakeLists.txt`。
- `tests`：`layout_styles_test`、`layout_test`、`layout_tips_test`、`mission_test`、新的 `stage_test`、`CMakeLists.txt`；`tools/qa/screenshot_ui.cpp` 一行註解。

Inbox、Pair、Garden 新增內建版面時，會和這裡在 `builtin_names()`、`builtin_text()`、`libs/ui/CMakeLists.txt` 的版面清單、`layout_styles.cpp` 的對照表（陣列大小目前是 17）與名稱表、`layout_tips.cpp` 的 `card_keys()`、`tests/unit/layout_test.cpp` 的名稱清單相鄰，合併時兩邊的名稱都留下即可。用到 Mission 畫面快取的程式改呼叫 `screen_tail_of()`。

## 測試

開工時 430 個（第一階段紀錄的數字），單元測試 343 個。完成後單元測試 352 個：新增 `Stage.*` 11 個，拿掉 `Mission.ShippedLayoutHasBarWallAndMain`、`Mission.CommandsAreKnownAndBound`（改寫進 `Stage.ShippedLayoutHasBarStageAndStrip`、`Stage.CommandsAndKeys`），並更新 `LayoutStyles`、`Layout`、`LayoutTips` 裡的版面名稱與 Mission Control 轉換。

`scripts\build.cmd msvc-x64-debug all`（/W4 /WX）：439 個測試全部通過，這次 `ConPty.CtrlCByteInterruptsPing` 也過了。
