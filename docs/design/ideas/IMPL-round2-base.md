# 第二輪第一階段實作紀錄：共用設計規則、版面收斂、整體美化

日期：2026-11-21｜分支：`feature/round2-base`（從 `feature/ideas` 的 cd873cf 開出）｜規格：`docs/design/ideas.md` 的「第二輪構想（2026-11-21）」與第二輪共用說明

這份紀錄寫給整合的人，也寫給接著在這條分支上做 Inbox、Stage、Pair、Garden 的四個 agent：做了什麼、和規格哪裡不同、新增的命令／按鍵／`ui.json` 鍵與檔案、共用元件怎麼用、建議寫進使用者指南的文字，以及刻意留給第二階段的事。設計文件、PRD、使用者指南都沒有動。

## Commit

| Commit | 內容 |
|---|---|
| 0330c93 | 第 1 步：共用設計 token、動畫工具、空白狀態插圖 |
| ff31f78 | 第 2 步：Tiling 併入 Terminals、拿掉 Cockpit、Classic 改為彩蛋 |
| 65396cd | 第 3 步：用新 token 整理各畫面 |
| 5e1b74a | 第 4 步：第一次使用提示跟著版面調整 |
| c003b16 | Mac 與 Classic 選單裡的版面清單也遵守 Fun layouts |
| （本文件） | 這份紀錄 |

測試：開工時 431 個；完成後見文末「測試」。

## 1. 共用設計規則（給第二階段用）

純數值與運算放在 `libs/ui`（`agentdesk/ui/design_tokens.hpp`，有單元測試），換成螢幕像素、字型與繪圖的部分放在 `apps/desktop/ui_tokens.hpp`（已經由 `app_internal.hpp` 引入，用 `tok::` 開頭）。新畫面請直接用這些，不要再自己寫 `px(12)`、`fpx(13)` 這類數字。

### Token 對照表

| 類別 | 名稱（`ui::tokens::`） | 值 | 螢幕端用法（`tok::`） | 用在哪裡 |
|---|---|---|---|---|
| 間距 | `kGrid`、`space(n)` | 4 px 一格 | `sp(n)` | 所有間距都是 4 的倍數 |
| 間距 | `kSpaceXs` | 4 | `gap_xs()` | 圖示到文字、晶片內 |
| 間距 | `kSpaceS` | 8 | `gap_s()` | 相關項目之間、列的內距 |
| 間距 | `kSpaceM` | 12 | `gap_m()` | 小卡內距、清單列左右 |
| 間距 | `kSpaceL` | 16 | `gap_l()` | 卡片內距、群組之間 |
| 間距 | `kSpaceXl` | 24 | `gap_xl()` | 區段之間 |
| 間距 | `kSpaceXxl` | 32 | `sp(8)` | 空白狀態四周 |
| 圓角 | `kRadiusSmall` | 6 | `radius_small()` | 晶片、按鈕、清單列、輸入框 |
| 圓角 | `kRadiusCard` | 10 | `radius_card()` | 卡片、選單、通知 |
| 圓角 | `kRadiusPanel` | 12 | `radius_panel()` | 對話框、大面板 |
| 線 | `kHairline` | 1 個裝置像素 | `hline()`、`vline()` | 分隔線，落在整數像素上不糊 |
| 字級 | `kTitle` | 15 px、Semibold（Geist）、行高 20 | `font()`、`size()`、`text()`、`measure()` | 卡片與對話框標題 |
| 字級 | `kBody` | 13 px、Regular、行高 18 | 同上 | 列、說明文字 |
| 字級 | `kCaption` | 11.5 px、Medium、行高 16 | 同上 | 標籤、提示、數量 |
| 顏色 | `hover_fill` | 文字色 5%（淺色）／6%（深色）疊在底色上 | `hover(bg)` | 滑鼠移上去 |
| 顏色 | `selected_fill` | accent 12%／16% 疊在底色上 | `selected(bg)` | 選取的列（比 hover 明顯） |
| 顏色 | `selected_edge` | accent，對選取底色不足 3:1 時往文字色調 | `selected_edge(bg)` | 選取列上的標記、晶片文字 |
| 顏色 | `focus_ring` | accent，對底色至少 3:1 | `focus_ring(bg)` | 鍵盤焦點 |
| 顏色 | `divider` | 文字色 10%／11% 疊在底色上 | `divider(bg)` | 安靜但看得到的 1 px 線 |
| 陰影 | `elevation(surface, 1)` | 深色 alpha 60、擴散 6、下移 1；頂邊高光 alpha 14 | `card()`、`shadow()`、`window_shadow(…, 1)` | 卡片、通知、選單 |
| 陰影 | `elevation(surface, 2)` | 深色 alpha 90、擴散 14；淺色更淡 | `window_shadow(…, 2)` | 對話框、浮在最上層的東西 |
| 動畫 | `kFastMs` | 120 ms | | hover、小的顏色變化、選單 |
| 動畫 | `kNormalMs` | 180 ms | `fade()`、`appear()`、`cross_fade()` 的預設 | 淡入淡出、滑入、狀態點換色 |
| 動畫 | `kSlowMs` | 260 ms | | 較大的面板 |
| 動畫 | `kSlideDistance` | 8 px | | 滑入的距離 |
| 動畫 | `ease_out`、`ease_in_out`、`Tween` | 三次曲線 | | 自己做動畫時用 |

`bg` 傳 0 代表用色票的 card 色。顏色全部從目前的色票算，不寫死色碼，換主題、淺色、環境色調都跟著走。

### 動畫與「減少動畫」

- `tok::reduce_motion()`：Windows「設定 › 協助工具 › 視覺效果 › 動畫效果」關掉時為 true（最多每 2 秒讀一次 `SPI_GETCLIENTAREAANIMATION`）。原本 `ambient.cpp` 和 `style_layouts.cpp` 各讀一份，現在都改呼叫這裡；`motion_reduced()`、`system_high_contrast()` 兩個舊名稱保留，內容轉到 `ui_tokens.cpp`。
- `tok::fade(id, on)`：依 `on` 在 0 到 1 之間緩動。第一次呼叫直接給目前的值，不會一開畫面就全部在動。
- `tok::appear(id)`：東西出現時從 0 緩到 1（通知、選單）。中斷超過 `forget_ms` 沒有呼叫，下次出現會重來；`tok::forget(id)` 可以立刻重設。
- `tok::cross_fade(id, color)`：顏色從上一次的值緩到新的值（狀態點）。
- 減少動畫打開時，上面三個都直接跳到結果。
- 動畫進行中會呼叫 `tok::keep_animating()`，主迴圈的 `next_wakeup_ms()` 因此在動畫期間每 16 ms 醒一次，動畫結束就回到原本的節奏，閒置時不多耗 CPU。
- `tok::end_frame()`（`App::frame` 尾端）定期清掉很久沒用到的動畫紀錄。

### 陰影與高光

- `tok::card(dl, a, b, radius, level)`：陰影、底色、1 px 邊框、深色主題的頂邊高光，一次畫完。自己畫的卡片用這個。
- `tok::window_shadow(dl, a, b, radius, level)`：給 ImGui 視窗（已經畫好自己的底色）用，只畫在視窗外側，可以在視窗內容之後呼叫。
- 最上層的 modal 對話框不用各自處理：`frame.cpp` 在 `draw_dialogs()` 之後透過 `ui::adapter::topmost_modal()` 幫它畫一圈 level 2 陰影。這個函式在 `imgui_adapter.cpp`（唯一碰 `imgui_internal.h` 的檔案）。
- Windows 高對比時不畫陰影和高光。

### 空白狀態

- `StateCard` 多了 `art` 欄位（`tok::Art`）：`kTerminal`、`kChanges`、`kSearch`、`kAgents`、`kAllDone`、`kFiles`。設了就在標題上方畫 64 px 的小插圖，其他排版照舊。
- `draw_empty_state(s, pos, size, art, 標題, 提示, 按鈕, 命令)`：一張插圖、一句話、可選的一個主要按鈕，排版和其他狀態卡一致。
- 插圖是 AgentDesk 自己畫的簡單圖形（圓角矩形、圓、線），顏色取自色票：線條是文字色淡淡疊在底色上，只有一個地方用 accent 或狀態色。
- 窄的側邊面板用 `files_changes.cpp` 裡的 `panel_empty()`（48 px 插圖加一行字，太矮時退回原本的一行字）。

### 其他共用的東西

- `StyleUi::changes`（`style_layouts.hpp`）：每個執行中 Agent 最後一次狀態改變的時間（steady ms），每個 tick 更新，和顯示哪個版面無關。原本是 Cockpit 自己追蹤、Classic 借用，現在中性化留下來。Inbox 的「等了多久」、Stage 的排序都可以直接讀。
- `StyleUi::resize`：鍵盤調整大小模式（見下一節），任何版面都能開。
- `ui::styles::is_fun_layout()`、`is_listed()`、`next_listed()`：彩蛋版面的清單規則。Garden 已經登記為彩蛋版面（`kFunLayouts` 裡有 `"garden"`），Garden 做好後只要加進 `builtin_names()` 就會自動遵守 Fun layouts 開關。
- `ui::styles::replacement_for()`：被拿掉的內建版面要換成什麼。Stage 取代 Mission Control 時，在這裡加一列 `"mission" → "stage"`，設定檔的轉換就會自動生效（`settings_store.cpp` 讀 `layout` 與 `layout_return` 時都會查）。

## 2. 版面收斂

### Tiling 併入 Terminals

- 設定 › Look 選到 Terminals 時多兩列：「Square corners」（直角面板）與「Gaps」（開關加 0～24 px 的調整）。
- 預設值（圓角、間距 8）時樹完全不改，Terminals 和原本逐像素相同。間距 g 套到面板之間，視窗邊緣與上方列的間距是 round(0.75 × g)，所以預設 8 剛好是檔案原本的 6。
- 關掉間距時面板彼此相貼、貼齊視窗邊。相鄰兩個面板各往右、下多伸 1 px，共用一條邊線（不會變成 2 px），聚焦的面板最後再補畫一次邊框。
- 直角打開時，左上角三顆視窗按鈕也改成小方塊（沿用原本 Tiling 的畫法），和直角面板一致。
- Tiling 的按鍵搬到 keys.json 的 `layout:terminals` 區段：Alt+R 調整大小模式、Alt+Shift+Enter 往較長的一邊分割、Alt+Shift+Q 關閉面板、Alt+Shift+G 開關間距。
- 調整大小模式（H／J／K／L 或方向鍵 40 px，加 Shift 10 px，Esc／Enter 結束）改成在面板區下方浮出一張小提示卡；面板到底時提示卡邊框閃一下「需要你」的顏色。任何版面（專注模式除外）都可以用命令開啟，只有 Terminals 預設綁了 Alt+R。
- 新命令：`terminals.resize_mode`、`terminals.gaps`、`terminals.corners`。舊名稱 `tiling.resize_mode`、`tiling.gaps` 仍是已知命令，舊的 keys.json 繼續能用。
- `ui.json`：新鍵 `terminals_square`（false）、`terminals_gaps`（8）、`terminals_gaps_on`（true）。沒有新鍵時沿用舊的 `tiling_gaps`、`tiling_gaps_on`；下次存檔時寫新鍵，並刪掉 `tiling_*`、`cockpit_*`。
- 存檔裡的 `"layout": "tiling"` 開成 Terminals，並自動打開直角。
- 純邏輯：`libs/ui` 的 `ui::terminals`（`terminals_options.hpp`），由原本的 `tiling_layout` 改寫。

### 拿掉 Cockpit

- 刪除 `cockpit_layout.cpp`、`ui::cockpit`、`cockpit.json`、`cockpit-night.json`、`cockpit-day.json`、Cockpit 的測試、設定列、建議主題、提示卡，以及視窗按鈕的圓環畫法。
- 存檔裡的 `"layout": "cockpit"` 改開第一個版面（AgentDesk）。主題選了 cockpit-night／cockpit-day 的人，載入時找不到該主題，照原本的機制退回預設主題。
- 留下的東西：每個 Agent 的狀態變化時間（`StyleUi::changes`，Classic 的 Agent 清單在用）。狀態變化紀錄（LOG）與「今天完成」數字沒有留，目前沒有其他程式用到；Inbox 或 Stage 若需要，`StyleUi::changes` 是最接近的起點。

### Classic 改為彩蛋

- 設定 › Look 新增「Fun layouts」開關（`ui.json` 的 `fun_layouts`，預設 false）。關著時 Classic 不出現在版面卡片、Mac 與 Classic 的 View 選單，Ctrl+Shift+L 也會跳過它；正在使用的版面一律保留自己的卡片，不會突然消失。
- 命令面板的「Layout: AgentDesk Classic」與 `layout:classic` 照樣可以用，這就是彩蛋的入口。
- Garden 也登記為彩蛋版面，第二階段做好後直接適用。

### Mission Control

沒有動。Stage 取代它時要處理的地方：`builtin_names()`、`layout_styles.cpp` 的對照表與 `replacement_for()`、`mission.toggle` 改成切換 Stage、提示卡。可重用的 `screen_tail` 快取、`wall_order`、篩選與 Agent 清單都還在 `mission.cpp`／`mission_layout.cpp`。

## 3. 美化

原則是收斂：沒有新增裝飾，只統一間距、字級、陰影與狀態，讓動作變得柔和。行為與視窗大小的邏輯沒有改。

| 畫面 | 改了什麼 |
|---|---|
| 通知（toast） | 卡片圓角 10、level 1 陰影；左側一條 3 px 的狀態色豎條取代圓點；標題用 Body 字級、內文用 Caption；只有標題時高度變矮（52 px，有內文 68 px）；出現時從右側滑入 16 px 並淡入（180 ms）；疊放間距 8 |
| 對話框（設定、提示卡、確認框、啟動器等所有 modal） | 外側一圈 level 2 柔和陰影；深色主題在暗化的背景上看得出來，淺色主題幾乎看不到，這是刻意的 |
| 提示卡 | 「LAYOUT TIPS」改用 Caption 字級，底部分隔線改成落在整數像素上的 1 px 線 |
| Review 的變更清單 | 選取的檔案列改用 `tok::selected` |
| 設定的分段按鈕（Appearance、Window buttons 等） | 選中的一段改用 `tok::selected`，和其他選取狀態同一種顏色 |
| 右鍵選單 | level 1 陰影 |
| AgentDesk Mac 下拉選單 | 打開時淡入並往下落 4 px（120 ms），加 level 1 陰影 |
| 側欄 | 選取的 space 列、Agent 列改用 `tok::selected`（accent 淡淡疊在側欄色上），淺色主題下不再是偏灰的厚重色塊 |
| 命令面板／Go to | 選取列同上；hover 用 `tok::hover` |
| Changes 面板 | 選取的檔案列同上 |
| 狀態標記 | 側欄、分頁、Review 標題列等所有 `status_mark`：狀態改變時新標記淡入；分頁列的需要你／完成小點、Terminals 分頁圖示上的點會從舊顏色緩到新顏色 |
| 沒有分頁 | 改用共用狀態卡：終端機插圖、標題、說明、兩個按鈕；連線中顯示轉圈 |
| Files／Changes 面板 | 沒有資料夾、沒有檔案、不是 git 儲存庫、沒有未提交的變更：48 px 插圖加一行字 |
| Review | 沒選 Agent、不是 git 儲存庫、沒有變更：加插圖，標題改用 Title 字級，「沒有變更」多一句說明 |
| 專注模式 | 沒有終端機時加插圖 |
| Mission Control | 沒有 Agent、篩選後沒有東西：加插圖（Stage 會取代它，這裡只是讓它在過渡期一致） |
| Timeline | 沒有活動時加插圖 |
| Terminals | 直角與間距選項、調整大小提示卡（見上一節） |

變化最大的是通知、所有空白狀態和淺色主題下的選取列；其次是對話框與選單的陰影。設定對話框、提示卡、Classic、Focus 底列、Mac 的選單列與 Dock、分頁列本身的版面沒有改，看過截圖後判斷原本已經整齊，再加東西只會更雜。

### 截圖

在 `build\shots\round2-base\`（Debug，`screenshot_ui.exe --cwd build\shots\round2-base\cwd\demo`，一個中性的示範 git 儲存庫；`SHOT_EXTRA` 開三個回報 needs you／working／done 的 shell）：

- `before\<畫面>-<dark|light>.png` 與 `after\<畫面>-<dark|light>.png` 成對：workspace、files、changes、terminals、review、focus、mac、classic、mission、settings、tips、palette、picker、notab（關掉分頁後的畫面，實際會跳出「Close space?」確認框，可以看到對話框與通知的新樣子）。
- `step2\`：`tm-square-nogaps.png`（直角、無間距、三個面板）、`tm-resize.png`（調整大小提示卡）、`tm-settings.png`（設定 › Look 的 Terminals 選項與 Fun layouts，淺色）。
- `after\` 另有 `mission-empty-dark.png`、`mission-empty-light.png`（沒有 Agent 時的插圖與通知）、`mac-menu-dark.png`、`mac-menu-light.png`（View 下拉選單的陰影，Classic 不在清單裡）；`step2	m-square-nogaps-light.png` 是直角無間距的淺色版。

## 4. 第一次使用提示

- 拿掉 Tiling 與 Cockpit 的提示卡。
- Terminals 的卡改寫：用途句提到可以用鍵盤鋪排面板；提示包含調整大小模式（Alt+R）、往較長的一邊分割／關閉面板、直角與間距在設定 › Look（間距開關 Alt+Shift+G）。按鍵一律從 keymap 查，不寫死。
- Classic 的卡多一條：這是彩蛋版面，打開設定 › Look › Fun layouts 才會留在版面清單；畫面特效與單色終端機合成一條。

## 新增與改名

| 種類 | 名稱 |
|---|---|
| 命令 | `terminals.resize_mode`、`terminals.gaps`、`terminals.corners`（舊名 `tiling.resize_mode`、`tiling.gaps` 保留為已知命令）；拿掉 `cockpit.reset_today` |
| 按鍵 | `layout:terminals`：Alt+R、Alt+Shift+Enter、Alt+Shift+Q、Alt+Shift+G（原本在 `layout:tiling`） |
| `ui.json` | `terminals_square`、`terminals_gaps`、`terminals_gaps_on`、`fun_layouts`；不再寫入 `tiling_gaps`、`tiling_gaps_on`、`tiling_titles`、`tiling_focus_follows_mouse`、`cockpit_effects`、`cockpit_done_today` |
| 新檔案 | `libs/ui/include/agentdesk/ui/design_tokens.hpp`、`libs/ui/src/design_tokens.cpp`、`apps/desktop/ui_tokens.hpp`、`apps/desktop/ui_tokens.cpp`、`libs/ui/include/agentdesk/ui/terminals_options.hpp`、`libs/ui/src/terminals_options.cpp`、`apps/desktop/terminals_options.cpp`、`tests/unit/design_tokens_test.cpp`、`tests/unit/terminals_options_test.cpp` |
| 刪除 | Tiling 與 Cockpit 的版面檔、程式、測試，`cockpit-night`／`cockpit-day` 主題 |

## 和規格不同的地方

1. **Tiling 的「Pane titles」與「Focus follows mouse」沒有搬過來**。Terminals 有自己的 38 px 面板標題列，標題列選項沒有意義；使用者決定要併入的只有直角、間距與按鍵，所以滑鼠停留就聚焦沒有帶過來，設定頁也少一列。要加回來時，原本的做法在 git 歷史的 `apps/desktop/tiling_layout.cpp`。
2. **調整大小模式不限 Terminals**：命令在任何版面都能用（專注模式除外），只有 Terminals 預設綁了按鍵。原本的提示畫在 Tiling 的狀態列上，Terminals 沒有那一列，所以改成浮在面板區下方的提示卡。
3. **直角時視窗按鈕也變方塊**：規格沒寫，為了和直角面板一致。
4. **右鍵選單沒有淡入**：ImGui 的右鍵選單由呼叫端自己 `EndPopup`，要讓整個選單淡入得改所有呼叫端；只加了陰影。Mac 的下拉選單是自己管開關，所以有淡入。
5. **對話框陰影沒有往下偏移**：從視窗內部畫的陰影若往下偏，頂端那一圈會壓到視窗本身，所以對話框的陰影四邊平均。
6. **Classic 的外觀沒有改**：它整個畫面是字元畫的，加陰影或圓角會破壞那個風格；只改了「列在哪裡」。

## 建議的使用者指南文字

### 版面

> 設定 › Look 列出 AgentDesk、Terminals、AgentDesk Mac、Mission Control、Review 五個版面，Ctrl+Shift+L 依序切換。
>
> **Terminals**：只有終端機，分頁在頂端一條列上。設定 › Look 選到 Terminals 時可以調整：
>
> - **Square corners**：面板改成直角，像平鋪式視窗管理員。左上角的視窗按鈕也會變成小方塊。
> - **Gaps**：面板之間與視窗邊緣的間距（0～24 px），開關也可以按 Alt+Shift+G。關掉時面板彼此相貼，共用一條邊線。
>
> Terminals 另有幾個鍵盤操作：Alt+R 進入調整大小模式（H、J、K、L 或方向鍵每次 40 px，加 Shift 每次 10 px，Esc 或 Enter 結束，模式中按鍵不會送進終端機）；Alt+Shift+Enter 往面板較長的那一邊分割；Alt+Shift+Q 關閉面板。shell 的 Alt+R 在這個版面會被攔下，不想要的話可以在 keys.json 的 `layout:terminals` 區段改掉。
>
> 以前用 AgentDesk Tiling 的人，開啟後會自動換成 Terminals 並打開直角；AgentDesk Cockpit 已經拿掉，原本用它的人會回到 AgentDesk 版面。

### 好玩的版面

> 設定 › Look 的「Fun layouts」打開後，AgentDesk Classic 會出現在版面卡片裡，Ctrl+Shift+L 也會輪到它。沒打開時，一樣可以從命令面板輸入「Layout: AgentDesk Classic」切過去。

### 畫面

> 通知從右下角滑進來，左邊的色條代表狀態（紅色需要你、綠色完成）。Windows 的「動畫效果」關掉時，AgentDesk 所有的淡入、滑入與顏色漸變都會直接顯示結果。

## 待解問題

1. **陰影的質感**：用多圈半透明線條模擬，深色主題下看得出來，淺色主題在 modal 的暗化底下幾乎看不到（這是刻意的低調）。之後若 ImGui 升到有原生陰影的版本，可以直接換掉 `tok::shadow`／`window_shadow` 的實作。
2. **沒有截圖驗證的項目**：Windows 關閉動畫效果、高對比、125%／150% 縮放下的陰影與 1 px 線、`window_buttons` 選 Windows 時的 Terminals 直角，只有程式與單元測試。
3. **自訂的 `layouts\tiling.json`**：使用者若有自己放的同名版面檔，存檔中的 `tiling` 仍會被轉成 Terminals（轉換發生在讀設定時，那時還不知道有哪些版面檔）。檔案本身照樣載入、可以手動切回；它用到的 `tiling-bar` 已經不是任何 style 的 mod，會顯示成空白區塊。
4. **`ConPty.CtrlCByteInterruptsPing`**：在 agent 的 shell 裡偶爾失敗，依指示不處理。

## 改到的共用檔案（合併時注意）

- `apps/desktop/app_internal.hpp`：引入 `ui_tokens.hpp`、`StateCard::art`、`draw_empty_state`、`kCommands`（Terminals 命令）、`WindowButtonLook` 拿掉 `kRing`、註解。
- `apps/desktop/style_layouts.hpp/.cpp`：`StyleSettings`、`StyleUi` 重寫（Tiling／Cockpit 拿掉，加 `resize`、`changes`、`fun_layouts`、`terminals_*`），分派表、讀寫 `ui.json`、`track_state_changes`。
- `apps/desktop/frame.cpp`（modal 陰影、`tok::end_frame`）、`app.cpp`（喚醒時間、隨附主題清單）、`panes.cpp`（無分頁卡、無間距時共用邊線）、`chrome.cpp`（視窗按鈕、`status_mark` 淡入、右鍵選單陰影）、`notices.cpp`（通知）、`commands.cpp`（Ctrl+Shift+L）、`settings_dialog.cpp`（卡片過濾、Fun layouts 列）、`settings_store.cpp`（版面轉換）、`sidebar.cpp`、`overlays.cpp`、`files_changes.cpp`、`tab_bar.cpp`、`terminals_layout.cpp`、`mac_layout.cpp`、`review_layout.cpp`、`focus_mode.cpp`、`mission_layout.cpp`、`timeline.cpp`、`ambient.cpp`、`classic_layout.cpp`。
- `libs/ui`：`layout.cpp`（`builtin_names()` 剩 workspace、terminals、mac、mission、review、classic）、`layout_styles.hpp/.cpp`、`keymap.cpp`、`command_names.cpp`、`theme_file.cpp`、`layout_tips.cpp`、`imgui_adapter.hpp/.cpp`（`topmost_modal`）、CMakeLists。
- `tests`：`layout_styles_test`、`layout_test`、`layout_tips_test`、`theme_file_test`、`window_buttons_test`、CMakeLists。

第二階段新增內建版面時，會和這裡在 `builtin_names()`、`builtin_text()`、`libs/ui/CMakeLists.txt` 的版面清單、`layout_styles.cpp` 的對照表與名稱表、`layout_tips.cpp` 的 `card_keys()`、`tests/unit/layout_test.cpp` 的名稱清單相鄰，合併時兩邊的名稱都留下即可。

## 測試

開工時 431 個（Debug，`scripts\build.cmd msvc-x64-debug all`），完成後 TEST_COUNT 個。新增：`DesignTokens.*`（7 個）、`TerminalsOptions.*`（7 個）、`LayoutStyles.FunLayoutsStayOutOfTheListUntilTurnedOn`、`LayoutStyles.RemovedLayoutsHaveReplacements`、`LayoutTips.MergedTerminalsAndFunLayoutsAreExplained`；拿掉 `TilingLayout.*`（9 個）與 `Cockpit.*`（9 個）。
