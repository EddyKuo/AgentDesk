# 打磨組實作紀錄：視窗按鈕跟著主題、各版面的第一次使用提示

日期：2026-11-20｜分支：`feature/ideas-polish`（從 `feature/ideas` 的 cbfab7a 開出）｜來源：使用者試用 Release 版後回報的兩件事

這份紀錄給整合的人看：改了什麼、為什麼、新增的 `ui.json` 鍵與命令，以及建議寫進使用者指南的文字。正式的設計文件、PRD 與使用者指南都沒有動。

## Commit

| Commit | 內容 |
|---|---|
| f4a52e5 | 程式：視窗按鈕跟著主題、第一次使用提示（兩件事改到同一批共用檔案，所以放在同一個 commit） |
| （本文件） | 這份紀錄 |

測試：開工時 420 個，完成後 431 個（新增 11 個），全部通過（Debug，`scripts\build.cmd msvc-x64-debug all`，`ConPty.CtrlCByteInterruptsPing` 這次也通過）。Release 依指示沒有跑。

## 1. 視窗按鈕跟著主題（bug）

**問題**：左上角的關閉、最小化、縮放圓鈕（`chrome.cpp` 的 `kLightColors`）、Mac 版面浮動視窗上的小圓鈕，以及設定裡 Mac 版面卡片的縮圖，都是寫死的紅、黃、綠，換什麼主題都一樣。

**做法**：

- 新增 `libs/ui` 的 `ui::window_buttons::derive()`（`window_buttons.hpp/.cpp`），是純函式：輸入主題的 needs you、working、done、accent，加上按鈕所在底色、border 與文字色，輸出三個按鈕的填色、符號色與視窗不在前景時的灰色。
  - 關閉從主題的 needs you 出發，縮放從 done 出發；最小化在 working 是暖色（OKLCh 色相 45°～115°、彩度夠）時用 working，否則取關閉與縮放的中間值，色相放在琥珀色。
  - 每顆按鈕的色相限制在各自錨點（紅 25°、琥珀 80°、綠 145°）的 ±25° 內，所以不會互換角色；再往主題 accent 的色相轉最多 8°，帶一點主題的味道。彩度限制在 0.10～0.18，太灰的主題色改用錨點色相。
  - 亮度沿 OKLab L 往遠離底色的方向調，直到和底色的對比達 3:1（WCAG 圖形的 AA）。色域外的顏色用二分法降彩度，不直接截斷。
  - 三顆兩兩的 CIEDE2000 距離至少 15；不夠時（例如 Phosphor 這種整套都是綠色的主題）改用錨點色相並拉開亮度。
  - 符號（×、－、＋）一律優先用深色墨水（和平常的圓鈕一樣），只有填色太暗時才用淺色，對比至少 3:1。
- `apps/desktop/chrome.cpp`：`window_button_colors(bar)` 依主題與底色快取結果（環境色調會改色票，所以以顏色本身當鍵）；`paint_round_button()` 多了 `bar` 與 `look` 兩個參數，原本的呼叫不必改。
- 各版面的樣子：

| 版面 | 樣子 |
|---|---|
| AgentDesk、Terminals、Mac、Mission Control、Review、專注模式 | 圓鈕（和原本一樣大、一樣的位置） |
| Cockpit | 外框圓環：主題色的環，深色主題加一圈淡淡的光暈（淺色主題不畫，免得像髒污），滑鼠移上去時環內填色並顯示較小的符號；視窗不在前景時三個環都變成 Cockpit 的線條色（accent）淡色 |
| Tiling | 11 px 的扁平小方塊，圓角很小，符號比圓鈕小一點 |
| Classic | 照舊是文字按鈕 `(x)(-)(+)`／`[_][#][X]`，平常是主題的 dim 色；滑鼠移上去時換成該按鈕的主題色 |

- Mac 版面浮動視窗標題列上的三顆小圓鈕，以標題列的顏色當底色計算。
- 設定 › Look 的 Mac 版面卡片縮圖，三個小點改用預覽主題的按鈕色。
- 「Window buttons」選 Windows 時：分頁列、Tiling、Cockpit、Mission Control、Review、Mac 右上角的三個按鈕，以及專注模式底列的小按鈕，最小化、最大化照舊用主題的 hover 色；關閉在滑鼠移上去時改用主題的關閉色當底、符號色當前景（原本是 hover 灰），和圓鈕一致。
- 滑鼠移上去顯示符號、按住時變暗、視窗不在前景時變灰，都和原本一樣。

**測試**（`tests/unit/window_buttons_test.cpp`，6 個）：Midnight、Paper、graphite 與隨附的主題檔（Dusk、Dawn、Cockpit Night／Day、Phosphor Green／Amber／Paper）逐一檢查：每顆對底色 ≥ 3:1、三顆兩兩 ΔE ≥ 15、色相最接近自己的錨點、符號對填色 ≥ 3:1、跟著主題變（Midnight 和 Cockpit Night 的關閉色與縮放色維持主題原本的色相 ±12°）、淺色主題的按鈕比底色暗、全綠的主題仍有三個分得開的顏色。

## 2. 各版面的第一次使用提示（功能）

**問題**：「不同 layout 看不出要怎麼用」。

**做法**：

- 每個內建版面（AgentDesk、Terminals、AgentDesk Mac、Mission Control、Review、AgentDesk Tiling、AgentDesk Cockpit、AgentDesk Classic）和專注模式，第一次出現時在畫面中央跳出一張提示卡：小標「LAYOUT TIPS」、版面名稱、一句說明這個版面適合做什麼，接著 3～5 條提示，每條右邊是實際的按鍵。最下面左邊是「Don't show tips」連結，右邊是「Got it」按鈕。
- 按鍵一律用 `style_chord_for()` 查（先查目前版面的 `layout:<名稱>` 區段，再查 global 與 terminal），不寫死。命令沒有綁定按鍵的鍵盤提示會省略；滑鼠操作與說明類提示照樣顯示，沒有按鍵就不畫按鍵晶片。
- 「Got it」、Enter、Esc、點卡片外面都會關閉並記為看過；「Don't show tips」關閉、記為看過，並把提示整個關掉。卡片開著時，終端機原本會一直保有 ImGui 的 active item、照收按鍵（modal 擋不住），所以每一幀用新的 `ui::adapter::release_keyboard()` 把鍵盤從其他視窗的項目拿回來；關閉時清掉這一幀的按鍵（避免關卡片的 Enter 又送進終端機），再把鍵盤還給目前的終端機（`focus_view`）。實測：卡片開著時打 `zzz` 再按 Enter，終端機什麼都沒收到；關閉後打字正常進終端機。
- 「Got it」按鈕的字在 accent 太淺時（例如 Phosphor 的淺綠）改用黑字，其餘維持白字。
- 自己跳出來的條件：提示開著、這張卡沒看過、版面已經顯示 600 ms（連按 Ctrl+Shift+L 切過好幾個版面時不會疊卡）、文件已載入、歡迎頁已經結束、沒有其他 popup 開著。自動化執行（`--exit-after-ms`）不會自己跳，所以既有的截圖基準不變；設環境變數 `AGENTDESK_TIPS=1` 才會。`layout.tips` 命令任何時候都能開。
- 使用者自己做的版面：如果用的是內建版面的 style（例如複製出來的 `mac-copy`），顯示那個 style 的卡；完全自訂的版面不會自己跳卡，`layout.tips` 顯示 AgentDesk 的卡。
- 舊使用者：`ui.json` 沒有 `tips_seen`，所以每個還沒看過的版面都會各出現一次（這正是目的）。
- 內容取自使用者指南與 `docs/design/ideas/` 的 IMPL 紀錄，各卡的提示：

| 卡片 | 提示（按鍵為預設值） |
|---|---|
| AgentDesk | 隱藏／顯示側欄 Ctrl+Shift+B；前往 space、分頁或 Agent Ctrl+Shift+K；往右／往下分割 Ctrl+Shift+O / Ctrl+Shift+E；Files 與 Changes 在分頁列右端；命令面板 Ctrl+Shift+P |
| Terminals | 最左邊的側欄按鈕回到有側欄的版面（Ctrl+Shift+B）；點旁邊的 space 晶片前往 space、分頁或 Agent（Ctrl+Shift+K）；新分頁 Ctrl+Shift+T；齒輪開設定、可以換版面（Ctrl+,）；命令面板 |
| AgentDesk Mac | 上方選單可執行所有命令，AgentDesk 標誌開 app 選單；視窗上紅、黃收進 Dock（程式繼續跑），綠色填滿桌面；Dock 是 space 與分頁，右鍵分頁可改名或關閉；前往 Ctrl+Shift+K；回到有側欄的版面 Ctrl+Shift+B |
| Mission Control | 點格子進去工作，牆面上也可按 1～9 或方向鍵加 Enter；回到牆面、在牆面再按回原版面 Ctrl+Shift+M；點上方 Needs you／Working／Done 只看那一種，Active 隱藏閒置；右鍵格子可 peek、改名、關閉；Esc 不在這裡用，會送給終端機 |
| Review | 下一個／上一個檔案 F8 / Shift+F8；換到別的 Agent 清單會跟著（Alt+↓）；在下方輸入訊息 commit；Agent 做完一輪會重讀，重新整理在右上角；回到原本的版面 Ctrl+Shift+R |
| AgentDesk Tiling | 調整大小模式 Alt+R；往較長的一邊分割 Alt+Shift+Enter；關閉面板 Alt+Shift+Q；開關間距 Alt+Shift+G；移到左邊的面板 Alt+←（其他方向同理） |
| AgentDesk Cockpit | 三個儀表的意思；點 NEEDS YOU 跳到第一個需要你的 Agent；點清單或紀錄的一列跳到那個 Agent；右鍵 Agent 有 Peek 與 Keep in a pane；柔光與格線可在設定 › Look 關掉（Ctrl+,） |
| AgentDesk Classic | 選單列 F10；狀態一律寫成文字 `[!] [~] [+]`；畫面特效在設定 › Look；單色終端機可在設定 › Look 關掉；文字側欄 Ctrl+Shift+B |
| 專注模式 | 底列的點代表其他 Agent，點了切過去；需要你時視窗邊緣會亮並播放提示音，不跳通知；滑鼠停在終端機上緣會出現標題列；下一個 Agent Alt+↓；離開專注模式 Ctrl+Shift+. |

- 隨時再打開：
  - 命令 `layout.tips`（顯示目前版面的卡；`layout.tips:<名稱>` 顯示指定版面的卡），命令面板列為「Tips for this layout」。
  - Mac 與 Classic 版面的 Help 選單多一列「Tips for this layout」（兩者共用 `ui::mac::menu_items`）。
  - 設定 › Look 版面卡片下方多一列「Layout tips」：「Show tips for this layout」按鈕（先關掉設定，再開卡片）與開關（`tips`）。

**純邏輯**（`libs/ui` 的 `ui::tips`，`layout_tips.hpp/.cpp`）：卡片內容、`card_key()`（目前顯示的是哪張卡）、`resolve()`（帶入按鍵，沒綁定的鍵盤提示省略）、`should_show()`、`mark_seen()`。測試 `tests/unit/layout_tips_test.cpp`（5 個）：每個版面與專注模式都有卡、標題等於顯示名稱、3～5 條提示；每條提示的命令都是已知命令（`is_builtin_command`），沒有命令的必須是「一律顯示」；按鍵從 keymap 來、兩個命令顯示成「A / B」、沒綁定的鍵盤提示被省略；`card_key` 的各種情況；看過的紀錄。

## 新增的 `ui.json` 鍵、命令與檔案

| 種類 | 名稱 | 說明 |
|---|---|---|
| `ui.json` | `tips`（bool，預設 `true`） | 第一次使用提示的開關 |
| `ui.json` | `tips_seen`（字串陣列） | 看過的卡：版面名稱，加上 `"focus"` |
| 命令 | `layout.tips`、`layout.tips:<名稱>` | 顯示目前（或指定）版面的提示卡；命令面板「Tips for this layout」 |
| 環境變數 | `AGENTDESK_TIPS=1` | 讓自動化執行（`--exit-after-ms`）也會自己跳卡，開發用 |
| 檔案 | `libs/ui/include/agentdesk/ui/window_buttons.hpp`、`libs/ui/src/window_buttons.cpp` | 按鈕顏色的推算 |
| 檔案 | `libs/ui/include/agentdesk/ui/layout_tips.hpp`、`libs/ui/src/layout_tips.cpp` | 提示卡內容與看過的紀錄 |
| 檔案 | `apps/desktop/layout_tips.cpp` | 提示卡的繪製、`layout.tips`、設定列 |
| 檔案 | `tests/unit/window_buttons_test.cpp`、`tests/unit/layout_tips_test.cpp` | 單元測試 |

沒有新增快捷鍵。

## 改到的共用檔案（合併時注意）

- `apps/desktop/app_internal.hpp`：兩個 include、`Settings::tips`／`tips_seen`、`State::layout_tips`、`kCommands` 一列、`paint_round_button` 宣告加兩個預設參數與 `WindowButtonLook`、`window_button_colors/color/glyph`、`layout_tips.cpp` 的三個宣告。
- `apps/desktop/chrome.cpp`：圓鈕段落（`kLightColors` 拿掉，`paint_round_button`、`traffic_lights_paint` 改寫）。
- `apps/desktop/frame.cpp`：`draw_dialogs` 加 `draw_layout_tips`。
- `apps/desktop/commands.cpp`：轉交 `exec_tips_command` 一行。
- `apps/desktop/settings_dialog.cpp`：`settings_look` 加 `look_tips_row`；`draw_settings` 關閉條件多 `layout_tips.close_settings`。
- `apps/desktop/settings_store.cpp`：讀寫 `tips`、`tips_seen`。
- `apps/desktop/settings_pages.cpp`：Mac 卡片縮圖的三個點。
- `apps/desktop/tab_bar.cpp`（`draw_window_buttons` 的關閉鈕）、`focus_mode.cpp`（`draw_strip_window_buttons` 的關閉鈕）、`classic_layout.cpp`（`draw_text_buttons` 的 hover 色）、`mac_layout.cpp`（浮動視窗圓鈕傳入標題列底色）。
- `libs/ui/src/mac_layout.cpp`：Help 選單多一列。`libs/ui/src/command_names.cpp`：`layout.tips`。
- `libs/ui/include/agentdesk/ui/imgui_adapter.hpp`、`libs/ui/src/imgui_adapter.cpp`：新增 `release_keyboard()`（唯一碰 `imgui_internal.h` 的地方，呼叫 `ClearActiveID`）。別的只有按鈕、沒有輸入框的 modal（例如確認視窗）可能也有同樣的按鍵外漏，這次沒有去改。
- `apps/desktop/CMakeLists.txt`、`libs/ui/CMakeLists.txt`、`tests/CMakeLists.txt`：新檔案。

## 與要求不同的地方

1. **Cockpit 的圓環不是單一線條色**：要求寫「in its line colour」，但三個環都用同一個顏色就只能靠位置分辨。改成三個環各用主題推算出的關閉／最小化／縮放色（Cockpit Night 下就是它自己的粉紅、琥珀、綠），視窗不在前景時才統一成線條色的淡色。
2. **Classic 的文字按鈕 hover 用彩色**：平常仍是單色（dim），只有滑鼠移上去那一顆換成主題的按鈕色，讓人知道按下去會做什麼。Phosphor 主題下 hover 時會短暫出現非綠色，若想維持全單色可以改回 `g_pal->text`。
3. **Windows 式按鈕只有關閉的 hover 改色**：最小化、最大化原本就用主題的 hover 色與 dim 色，沒有再改。
4. **淺色主題的按鈕比一般印象中深**：為了對底色達到 3:1，Paper、Dawn 這類淺色主題的黃色按鈕會偏琥珀褐色。這是 3:1 要求的直接結果。

## 截圖

在 `build\shots\polish\`（Debug，`screenshot_ui.exe --cwd build\shotcwd`，捕捉約 2 倍）：

| 檔案 | 內容 |
|---|---|
| `zoom-themes.png` | 放大 2 倍的按鈕區：Ember、Dusk、Dawn、Cockpit Night、Cockpit Day、Phosphor Green、Phosphor Amber、Phosphor Paper，每個主題一列平常、一列滑鼠移上去 |
| `btn-ws-<主題>.png`、`btn-ws-<主題>-hover.png` | AgentDesk 版面在 11 個主題下的整個畫面（midnight、paper、graphite、ember、mac-dusk、mac-dawn、cockpit-night、cockpit-day、phosphor-green、phosphor-amber、phosphor-paper） |
| `zoom-layouts-a.png` | Terminals、Mac 選單列、Mac 浮動視窗（Dusk 與 Dawn）、Mission Control、Review、Tiling（深色與 Paper）的按鈕，含滑鼠移上去 |
| `zoom-layouts-b.png` | Cockpit（Night、Day）、Classic（Phosphor Green、Phosphor Paper，滑鼠在 `(x)` 上）、專注模式（深色、Dawn） |
| `zoom-ring-square-windows.png` | 最後版本的 Cockpit 圓環與 Tiling 方塊（符號縮小後），以及 Windows 式按鈕在 Paper 下滑鼠移到關閉 |
| `btn-<版面>.png`、`btn-<版面>-hover.png` | 各版面的整個畫面：terminals、mac-top、mac-inner、mac-inner-dawn、mission、review、tiling、tiling-paper、cockpit、cockpit-day、classic、classic-paper、focus、focus-dawn |
| `btn-windows.png`、`btn-windows-hover.png`、`btn-windows-paper-hover.png` | Window buttons 選 Windows：關閉在滑鼠移上去時用主題的關閉色 |
| `tips-<版面>.png` | 每個版面與專注模式的提示卡：workspace、terminals、mac、mission、review、tiling、cockpit、classic、focus；淺色：workspace-paper、mac-dawn、cockpit-day、classic-paper |
| `tips-auto-mission.png` | 設 `AGENTDESK_TIPS=1`、切到 Mission Control，卡片自己出現 |
| `tips-mac-gotit.png`、`tips-cockpit-gotit.png` | 按 Enter（Got it）或 Esc 後回到一般畫面，游標是有焦點的實心方塊 |
| `tips-tiling-gotit.png` | 卡片開著時打 `zzz`、按 Enter，關閉後打 `echo typed-after-close`：終端機只收到後者 |
| `settings-look.png`、`settings-show-tips.png` | 設定 › Look 的「Layout tips」列，以及按「Show tips for this layout」後設定關閉、卡片打開 |
| `lay-<版面>.png` | 改動後各版面的一般畫面（版面本身沒有其他變化） |

## 建議寫進使用者指南的文字

放在「畫面」一節第一段之後（取代「左上角三顆圓鈕……」那句的顏色部分）：

> 左上角三顆鈕依序是關閉、最小化、縮放（最大化），顏色跟著主題：關閉偏紅、最小化偏黃、縮放偏綠，實際色調取自主題的「需要你」「工作中」「完成」顏色，並保證在底色上看得清楚。滑鼠移上去會顯示符號，視窗不在前景時三顆都變灰。Cockpit 版面畫成外框圓環，Tiling 版面畫成小方塊，Classic 版面是文字按鈕。

放在「版面」一節的版面清單之後：

> **第一次使用提示**：每個版面（和專注模式）第一次出現時，畫面中央會跳出一張提示卡，說明這個版面適合做什麼，並列出幾個常用操作與它們的快捷鍵（依你的 keys.json 顯示）。按「Got it」、Esc 或點卡片外面就關閉，之後不會再自己出現；按「Don't show tips」則所有版面都不再跳卡。想再看一次，從命令面板執行「Tips for this layout」，或在 Mac、Classic 版面的 Help 選單、設定 › Look 的「Layout tips」列打開。設定 › Look 的開關可以重新打開提示。

在「設定」一節 Look 的說明加一句：

> 版面卡片下方的「Layout tips」可以重看目前版面的提示，或開關第一次使用提示。

## 待確認

- 圓鈕顏色是演算法推算的，每個主題都通過對比與色差測試，但「好不好看」仍建議使用者在常用主題下看一眼；不喜歡時可以把 `derive()` 的錨點色相或彩度範圍調一下。
- 提示卡的文字是英文（介面語言），使用者指南的說明是中文。
