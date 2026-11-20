# B 組實作紀錄：B1 Terminal Classic、B2 Tiling、B3 Cockpit

日期：2026-11-20｜分支：`feature/ideas-b`（從 `feature/ideas` 的 634652a 開出）｜規格：`docs/design/ideas/B-style-layouts.md`

這份紀錄給整合的人看：做了什麼、哪裡和規格不同、新增哪些命令／按鍵／`ui.json` 鍵與檔案，以及建議寫進使用者指南的文字。正式的設計文件、PRD 與使用者指南沒有動。

## Commit

| Commit | 內容 |
|---|---|
| e9e831d | 共用元件（mod → style 對照表、面板讀版面欄位、各 style 的掛勾） |
| 87707c3 | B2 AgentDesk Tiling |
| d5b29dd | B3 AgentDesk Cockpit |
| 250f8dc | B1 AgentDesk Classic |

測試：開工時 297 個，完成後 334 個，全部通過（Debug，`scripts\build.cmd msvc-x64-debug all`）。Release 依指示沒有跑。

## 共用元件

| 規格項目 | 狀態 | 做法 |
|---|---|---|
| mod 名稱 → style 對照 | 完成 | `libs/ui` 的 `ui::styles::style_of_mod`／`style_of_tree`（`layout_styles.hpp`），取代 `derive_layout` 裡的 if。規則寫在一處：Mac 優先（和原本一樣），其餘取樹中第一個出現的 style；分頁區塊的 pane header mod 也算 |
| 依 style 分派繪製 | 完成 | `apps/desktop/style_layouts.{hpp,cpp}` 集中分派：區塊（`draw_style_block`）、調整過的樹（`style_layout_tree`）、背景（`draw_style_backdrop`）、最上層（`draw_style_overlay`）、每幀與每個 tick 的維護 |
| Look 從色票推算 | 完成 | Cockpit 的 `cockpit_look()`、Classic 直接用色票，都沒有寫死色碼 |
| 面板樣式 | 完成 | `panes.cpp` 改讀版面 pane 的 `radius`、`border`、`focus`、`header.height`、`gap`（`ui::styles::pane_look`，各 style 有自己的預設）。Tiling 與 Classic 由各自的檔案畫框與標題列（`PaneStyle::kStyled`） |
| 選單內容 | 完成（改用別名） | `ui::menus`、`ui::chrome_common` 是指向 `ui::mac` 的 using 宣告，程式沒搬，見「偏離」 |
| 視窗按鈕 | 完成 | `mac_window_buttons()` 在 Classic 回 false，Classic 畫文字按鈕；Tiling、Cockpit 用原本的圓鈕 |
| 版面卡片 | 完成 | `layout_preview` 先問 `style_layout_preview`，三個版面各有自己的卡片 |
| 通知位置 | 完成 | `ui::styles::toast_area`：Mac 在上方，Classic 往上讓 22 px，Cockpit 讓出 log 區塊 |
| 成套主題 | 完成 | 版面第一次顯示、`layout_themes` 還沒有它時寫入建議主題（只限內建版面，而且兩個主題都要存在）。主題檔放 `libs/ui/themes/`，建置時複製到 `themes\` |
| 特效開關 | 完成 | `ui::styles::effects_allowed`（設定為 off、淺色主題、Windows 高對比時回 false）；`system_high_contrast()`、`system_animations()` 每秒讀一次系統設定 |
| `--run` 可觸發 | 完成 | 新命令都能從 `--run` 執行 |

**既有版面外觀不變**：改動前後用同一組指令各截一次圖（`--run "layout:<name>|pane.split:right"`），逐像素比對。workspace 與 terminals 完全相同；mac 只有選單列時鐘（時間不同、寬度跟著變）和視窗標題一欄 7 個像素的字形反鋸齒差異，標題的繪製程式沒有改動。圖檔：`build\shots\B\before\*.png`、`build\shots\B\after\*.png`。

## B2 AgentDesk Tiling（完成）

- 版面檔 `libs/ui/layouts/tiling.json`，照規格。
- 狀態列（26 px）：圓鈕、space 編號方塊（需要你填 `needs_you` 並加 `!`、完成用 `done` 字色、超過 9 個顯示 `+n`，點了開 picker）、目前 space 的分頁（`編號:名稱`，目前分頁加 `*` 與 2 px 底線，收起的加括號）、置中的目前面板（程式 · 資料夾 (分支)）、`!n ~n` 計數（點了跳到第一個需要你的 Agent）、時鐘；`window_buttons` 為 `windows` 時右端放三個按鈕。滾輪在方塊上切 space、在分頁上切分頁；雙擊分頁改名、右鍵開 `tab_menu`。空間不夠時依序讓位：先藏 A4、再把其他分頁縮成編號、再藏時鐘、最後截斷分頁。
- 面板：直角，1 px 框，聚焦 2 px accent，未聚焦又需要你時 2 px `needs_you`，斷線時 1 px `faint`。兩個面板以上才畫 18 px 標題列（`tiling_titles`）。間距為 0 時未聚焦的框多畫 1 px，和鄰居共用一條線。
- 調整大小模式（Alt+R）：一個看不見的 popup 蓋在 A4 的提示上，拿走鍵盤，所以按鍵不會進終端機。H/J/K/L 或方向鍵 40 px，加 Shift 10 px；Esc、Enter、再按一次綁定的鍵、點任何地方、視窗失去焦點都會離開。面板到底時提示換色 150 ms。實測：Alt+R 後按 L 三次，左側面板寬了 120 px，沒有字元送進終端機，Esc 之後打字回到原面板。
- 新命令：`tiling.resize_mode`、`tiling.gaps`、`pane.split:auto`（所有版面都能用）、`pane.resize:<dir>:<px>`（距離參數）。
- 設定 › Look 選到 Tiling 時多三列：Gaps、Pane titles、Focus follows mouse。

## B3 AgentDesk Cockpit（完成）

- 版面檔 `libs/ui/layouts/cockpit.json`，照規格。
- 頂列：圓鈕、標誌與「AGENTDESK / COCKPIT」（逐字畫，字距 +1 px）、space 頭像（目前的 2 px 外框）、目前 space 的分頁晶片、時鐘（`FRI 16:25`）。
- 儀表：240° 弧、12 個刻度（每 3 個長）、短指針、數字與標籤；數值以 `approach` 緩動，動的時候才要求下一幀，Windows 關閉動畫時直接跳到新值。需要你大於 0 時數字變色並加 `!` 晶片。左欄矮於 560 設計 px 時改成橫條。host 連線中畫虛線弧與「--」，斷線時加「DISCONNECTED」。
- AGENTS 清單：兩行一列、左邊狀態色豎條，目前聚焦的 Agent 豎條較寬、底色 `selected`；點了 `agent.focus`，右鍵有 Peek 與 Keep in a pane；超過高度可捲動。
- LOG：`HH:MM:SS  space/程式  舊 → 新`，新的在上，最多 200 筆、不存檔；點了跳到那個 Agent。
- 今天完成：每個 tick 比對每個 Agent 的狀態，從 working 變成 done 或 idle 時加一。存在 `ui.json` 的 `cockpit_done_today`，本地時間換日（或系統時間往回調到存檔日期之前）時歸零；重開時第一次看到某個 Agent，若 `agents_changed` 存的是 working、現在是 idle／done，也算一次。`cockpit.reset_today` 手動歸零。
- 背景 24 px 格線、區塊框的柔光（6／3 px 兩層）與四角括弧；`cockpit_effects` 為 off 時不畫格線與柔光；淺色主題不畫柔光；高對比時都不畫。
- 窗寬小於 1000 設計 px 先藏 roster，小於 820 再藏 log（畫面上的調整，不寫進 `.state`）。
- 主題檔 `cockpit-night.json`、`cockpit-day.json`。`themes.cpp` 讓主題檔的 `colors` 可以設定 `info`（沒設定時仍等於 accent）。
- 實測：在 PowerShell 送出 OSC 7501 的 `state=done`，DONE TODAY 變成 1，LOG 出現一列 `WORKING → IDLE`。

## B1 AgentDesk Classic（完成）

- 版面檔 `libs/ui/layouts/classic.json`，照規格（含預設隱藏的 `sidebar`，Ctrl+Shift+B 開關）。
- 字元格：介面字用 `s.fonts.mono`，14 設計 px（跟著介面文字大小），格寬取該字級 `M` 的 advance 四捨五入，格高 20 設計 px。每個字各自定位，寬字元佔兩格；框線、`├`、braille 等字元用 `sprites::paint` 畫成填滿格子的形狀。
- 選單列：`(x)(-)(+)`（或右端的 `[_][#][X]`）、`AGENTDESK File Edit View Window Help`、右邊的 `!n ~n` 與時鐘。下拉選單是單線框，列高 20、左右各 1 格，快捷鍵靠右；滑鼠所在列淡色反白、鍵盤選到的列整列反白；選單開著時移到別的標題就換過去，左右鍵換選單。窄於 80 格只留 AGENTDESK 與 View，其他選單收進 AGENTDESK 底部，再窄先藏時鐘、再藏數字。
- 分頁列：`[space v]`（點了開 picker）、`1 claude [!]` 用 `|` 分隔、目前分頁反白、`[!]` 以 `needs_you` 色反白、`[~]` 後面跟著旋轉字元、Lost 寫 `[lost]`、最後是 `+`（開新增選單）。放不下時從最舊的非目前分頁縮成 `1…`。
- 狀態列：`─[ NEED YOU n ]─[ WORKING n ]─[ space · 分支 +n ]────[ Ctrl+Shift+P commands ]─`；NEED YOU 與 commands 可以點。沒有 Agent 寫 `[ no agents ]`，連線中 `[ connecting ⠋ ]`，斷線時反白的 `[ DISCONNECTED ]`。
- 文字側欄：SPACES（目前的加 `>`，底下用 `├ └` 列出分頁與狀態）、AGENTS（狀態、space/程式、多久前變動）。
- 面板：字元框，聚焦雙線 accent、其他單線 `border`、斷線時虛線；標題寫在上框線上，狀態字元靠右。
- 單色終端機：`TerminalTheme::mono`，`colors_of` 把非預設的顏色放到主題的單色色階上（線性光混色），前景與背景亮度差小於 0.35 時把前景推到色階較遠的一端，反白在對映之後才交換；預設的前景與背景保持原值。同一畫面用到的色對有快取。單元測試確認：phosphor-green 上的 24-bit 紅字變成綠色，和底色對比不低於 7:1。
- 特效：light（預設）在選單列、分頁列、狀態列與側欄畫掃描線（每 3 設計 px 一條，起點對齊整數像素），加 12 px 暗角；full 再加介面字微光與終端機內 6% 掃描線。淺色主題與高對比一律不畫。都是靜態圖形，不增加重繪。
- F10：keys.json 的 `layout:classic` 預設區段 `f10 → classic.menu`。終端機有焦點時，`TerminalView` 現在也會把沒有修飾鍵的 F10 交給綁定（和 F2 一樣）；其他版面沒有綁 F10，照常送給程式。
- 主題檔 `phosphor-green.json`、`phosphor-amber.json`、`phosphor-paper.json`（色碼照規格表，ANSI 16 階是我們自己排的）。
- 新命令：`classic.menu`（其他版面回「not in this layout」）、`classic.effects`（off → light → full 循環並以 toast 顯示）。
- 設定 › Look 選到 Classic 時多兩列：Screen effects、Monochrome terminal。

## 偏離規格與原因

1. **選單與共用小工具沒有搬家**：`ui::menus`、`ui::chrome_common` 是 using 宣告，實作仍在 `ui::mac`。搬動會改到 Mac 的檔案和測試，和其他組合併時容易衝突；名稱已經中性化，之後要搬只是機械工作。
2. **多個 style 同時出現**：Mac 優先（保留原本行為），其餘取樹中第一個。規格第 7 節第 8 點舉的 `mac-dock` 加 `classic-status` 會是 mac，不是「第一個出現」。
3. **框線粗細**：寬 1 的線維持 1 個裝置像素（原本的畫法），更寬的線依縮放換算並取整數。這樣既有版面逐像素不變。
4. **Terminals 版面的面板框顏色**：仍用 `terminals_look()` 從主題推算的顏色，不用版面檔裡寫死的 hex（和原本一樣）；寬度、圓角、標題列高度改讀版面。
5. **Cockpit 的面板**：沿用預設面板（30 px 標題列、圓角 4 讀自版面），聚焦面板的角括弧在最上層補畫，沒有另設 `PaneStyle::kCockpit`；標題列顏色是主題色，沒有改用 cockpit_look。看起來與規格相同，程式少一條分支。
6. **Cockpit 的資料來源**：狀態變化與今天完成數由 Cockpit 自己每個 tick 追蹤（`cockpit_tick`），不放進 `sidebar.cpp` 的 `track_agents`，因為那段只在側欄畫出來時執行，而 Cockpit 沒有側欄。roster 排序也是自己做（需要你、完成、工作中、idle，同段最近變動的在前），沒有從側欄抽出共用；釘選的 Agent 不會排最前面。
7. **Cockpit 的 LOG**：從沒有狀態變成有狀態（剛啟動）或反過來（結束）不記；面板看得到時，done 會馬上變 idle，所以常見的是 `WORKING → IDLE`。
8. **Cockpit 窄視窗門檻**：規格只給 1000 px 一個門檻，「先藏 roster、再藏 log」的第二個門檻訂為 820 設計 px。
9. **Classic 的單色對映範圍**：ANSI 16 色也一起轉（規格說 16 色交給主題的 `ansi`）。這樣換成 Midnight 這類彩色主題時，終端機也是單色（規格邊界情況 4）。淺色主題的色階方向相反（紙色到墨色），對映時用暗度而不是亮度。
10. **Classic 的介面字型**：用 `s.fonts.mono`，預設是隨附的 JetBrains Mono；使用者換了終端機字型時，介面字跟著換。
11. **F10 驗收步驟**：「Split right」在 File 選單，從 AGENTDESK 按一次右鍵就到，不是兩次。
12. **狀態列的命令提示**：顯示實際綁定的按鍵文字（`Ctrl+Shift+P commands`），不寫 `^P`。
13. **Classic 選單**：拿掉 Window 選單裡只對 Mac 有意義的「Fill the desktop」。
14. **Tiling 拖曳面板**：標題列本身就是拖曳把手（按下後移動 4 px 才開始），沒有另外畫握把。
15. **主題 id**：`phosphor-paper` 的結尾和 preset `paper` 同名，舊設定的轉換（`theme_id`）會把它誤認成 preset。改成「有主題檔正好叫這個名字時就是它」。

## 新增的命令、按鍵、ui.json 鍵與檔案

**命令**（都在 `is_builtin_command` 與命令面板）：`pane.split:auto`、`pane.resize:<dir>:<px>`（新參數）、`tiling.resize_mode`、`tiling.gaps`、`cockpit.reset_today`、`classic.menu`、`classic.effects`。

**按鍵**（keys.json 的版面區段，和 `global`、`terminal` 預設都不重複，有單元測試）：

| 區段 | 鍵 | 命令 |
|---|---|---|
| `layout:tiling` | Alt+R | `tiling.resize_mode` |
| `layout:tiling` | Alt+Shift+Enter | `pane.split:auto` |
| `layout:tiling` | Alt+Shift+Q | `pane.close` |
| `layout:tiling` | Alt+Shift+G | `tiling.gaps` |
| `layout:classic` | F10 | `classic.menu` |

**ui.json 鍵**：`tiling_gaps`（8，0～24）、`tiling_gaps_on`（true）、`tiling_titles`（"auto"／"always"／"never"）、`tiling_focus_follows_mouse`（false）、`cockpit_effects`（"full"／"off"）、`cockpit_done_today`（`{"date", "count"}`）、`classic_effects`（"light"／"off"／"full"）、`classic_mono_terminal`（true）。

**新檔案**：

- `libs/ui/include/agentdesk/ui/layout_styles.hpp`、`libs/ui/src/layout_styles.cpp`：style 對照表與共用規則
- `libs/ui/include/agentdesk/ui/tiling_layout.hpp`、`libs/ui/src/tiling_layout.cpp`：Tiling 的純邏輯
- `libs/ui/include/agentdesk/ui/cockpit.hpp`、`libs/ui/src/cockpit.cpp`：Cockpit 的純邏輯
- `libs/ui/include/agentdesk/ui/classic_layout.hpp`、`libs/ui/src/classic_layout.cpp`：Classic 的純邏輯與單色對映
- `libs/ui/layouts/tiling.json`、`cockpit.json`、`classic.json`
- `libs/ui/themes/cockpit-night.json`、`cockpit-day.json`、`phosphor-green.json`、`phosphor-amber.json`、`phosphor-paper.json`
- `apps/desktop/style_layouts.hpp`、`style_layouts.cpp`、`tiling_layout.cpp`、`cockpit_layout.cpp`、`classic_layout.cpp`
- `tests/unit/layout_styles_test.cpp`、`tiling_layout_test.cpp`、`cockpit_test.cpp`、`classic_layout_test.cpp`

**改動的共用檔案**（合併時注意）：`app_internal.hpp`（include、`Settings::styles`、`State::styles`、`kCommands` 尾端、`draw_panes` 多一個參數）、`frame.cpp`（`layout_color` 移出匿名命名空間；`draw_layout` 與 `tick`／`frame` 的掛勾）、`panes.cpp`（`draw_pane`／`draw_panes`／`draw_pane_frame` 改寫，改動最大）、`commands.cpp`（一行掛勾）、`chrome.cpp`（`mac_window_buttons`）、`notices.cpp`（toast 位置）、`app.cpp`（時鐘喚醒）、`layouts.cpp`（`derive_layout`）、`overlay_kit.hpp`（`layout_display_name`）、`settings_dialog.cpp`、`settings_pages.cpp`、`settings_store.cpp`（各一兩行掛勾）、`themes.cpp`（`theme_id`、`info`）、`libs/ui/src/keymap.cpp`（版面預設區段）、`command_names.cpp`、`layout.cpp`（`builtin_names` 多了 tiling、cockpit、classic）、`terminal_view.{hpp,cpp}`（`mono`、F10）、三個 CMakeLists、`tests/unit/layout_test.cpp`。其他組若也新增內建版面（例如 A1），`builtin_names`、`builtin_text`、`libs/ui/CMakeLists.txt` 的版面清單與 `layout_test` 的名稱清單會有衝突，合併時把兩邊的名稱都留下即可。

## 測試與截圖

新增的單元測試：

- `LayoutStyles.*`：ModTableNamesEveryStyleMod、ShippedLayoutsKeepTheirStyles、TreeStyleRules、DisplayNames、PaneLookOfShippedLayoutsIsUnchanged、PaneLookReadsTheFrame、ToastAreas、EffectsAllowed、SuggestedThemes
- `TilingLayout.*`：ShippedLayoutParsesAndIsTilingStyle、SplitDirectionFollowsLongerSide、ResizeModeKeys、ResizeArgument、GapsApplyToTree、TitleRows、BarSegmentsShrinkInOrder、SpaceChipsPastNine、LayoutKeysDoNotClashWithDefaults
- `Cockpit.*`：ShippedLayoutParsesAndIsCockpitStyle、GaugeAngleClampsToSweep、GaugeMaxRoundsUp、DoneTodayRollsOverAtMidnight、DoneTodayClockGoesBack、FinishedMeansWorkingToDoneOrIdle、EventLogKeepsNewest200、CountsIgnoreUnknown、NarrowWindowsDropRosterThenLog
- `ClassicLayout.*`：ShippedLayoutParsesAndIsClassicStyle、MonoColorKeepsContrast、RedTextBecomesGreenWithStrongContrast、GridSnapsToWholePixels、TextColumnsAndCuts、TabLineTruncatesOldestFirst、StatusGlyphs、ScanlineRowsOnDevicePixels、NarrowMenuBar、LayoutKeysDoNotClashWithDefaults

截圖（`build\shots\B\`；淺色截圖用 `--layout` 帶一份指定主題的版面檔，放在 `build\shots\B\layouts\`，因為 `--run` 沒辦法切換 Appearance）：

| 檔案 | 內容 |
|---|---|
| `before\*.png`、`after\*.png` | workspace、terminals、mac 改動前後（逐像素比對） |
| `tiling-3.png` | Tiling 三個面板，深色 |
| `tiling-nogaps-blocked.png` | 間距關閉、未聚焦面板需要你（紅框、`1!`、`!1`） |
| `tiling-resize.png` | 調整大小模式的提示 |
| `tiling-light-resizing.png`、`tiling-light-resized.png` | 淺色（Paper）；L 三次後左側面板變寬，Esc 後打字回到面板 |
| `tiling-settings.png` | 設定 › Look 的 Tiling 選項與版面卡片 |
| `cockpit.png` | Cockpit（cockpit-night 自動套用），一個需要你 |
| `cockpit-day.png` | cockpit-day，一個工作中 |
| `cockpit-done.png` | 狀態變化後：DONE TODAY 1、LOG 一列 |
| `cockpit-narrow.png` | 窄視窗：roster 與 log 隱藏、儀表變橫條 |
| `classic-split.png` | Classic（phosphor-green），雙線聚焦框、反白的 `[!]` |
| `classic-menu.png` | `classic.menu` 後按右鍵與下鍵，File 選單鍵盤選到 Split down |
| `classic-full.png` | 特效 full；紅字與深藍底被轉成綠色色階 |
| `classic-paper.png` | phosphor-paper（無掃描線）、文字側欄、上下分割 |
| `classic-settings.png` | 設定 › Look 的 Classic 選項與三張新版面卡片 |

Cockpit 的完整畫面需要較寬的視窗，截圖時先執行五次 `app.zoom_out`。

## 建議的使用者指南文字

### 風格版面

設定 › Look 的版面卡片多了三個風格版面，也可以在命令面板輸入「Layout」切換。功能和 AgentDesk 版面一樣（space、分頁、分割、命令面板、通知），只換外觀和操作方式。

**AgentDesk Tiling**：沒有側欄和分頁卡片，頂端一條細狀態列，面板直角、彼此留間距，適合用鍵盤操作很多個面板。

- 狀態列由左到右：space 編號、目前 space 的分頁、目前面板、需要你（`!`）與工作中（`~`）的數量、時鐘。
- Alt+R 進入調整大小模式：H、J、K、L 或方向鍵每次移動 40 px，加 Shift 每次 10 px，按 Esc 或 Enter 結束。模式中按鍵不會送進終端機。
- Alt+Shift+Enter 往面板較長的那一邊分割；Alt+Shift+Q 關閉面板；Alt+Shift+G 開關間距。
- 這幾個鍵只在 Tiling 版面有作用。shell 的 Alt+R（readline 還原整行）在這個版面會被攔下，不想要的話可以在 keys.json 的 `layout:tiling` 區段改掉。
- 設定 › Look 可以調整間距、面板標題列（自動、一律顯示、不顯示）與「滑鼠停留就聚焦」。

**AgentDesk Cockpit**：左邊三個儀表（工作中、需要你、今天完成），右邊 Agent 清單，下方狀態變化紀錄，中間照常是目前分頁的面板。

- 點「NEEDS YOU」儀表跳到第一個需要你的 Agent；點清單或紀錄的一列跳到那個 Agent，清單右鍵可以 Peek 或放進面板。
- 「今天完成」從本地時間午夜起算，只計 AgentDesk 看得到的完成；AgentDesk 關著時完成的，會在重開時算進去。命令面板的「Cockpit: set Done today back to 0」可以手動歸零。
- 紀錄只保留最近 200 筆，不存檔。
- 視窗太窄時會先藏 Agent 清單、再藏紀錄；太矮時儀表改成橫條。
- 背景格線與柔光可以在設定 › Look 的「Glow and grid」關掉。

**AgentDesk Classic**：整個視窗用字元畫成單色終端機的樣子，選單、分頁、框線和狀態全是文字。

- F10 打開選單列，左右鍵換選單、上下鍵選列、Enter 執行、Esc 關閉。這個鍵只在 Classic 版面有作用；全螢幕終端程式若用 F10，可以在 keys.json 的 `layout:classic` 區段改成別的鍵。
- Ctrl+Shift+B 開關文字側欄。
- 狀態一律有字元，不只靠顏色：`[!]` 需要你（反白）、`[~]` 工作中、`[+]` 完成、`[.]` 閒置、`[?]` 未知。
- 設定 › Look 的「Screen effects」：Off、Light（掃描線與暗角，預設）、Full（再加文字微光）。淺色主題與 Windows 高對比時一律不畫特效。
- 「Monochrome terminal」會把終端機的顏色轉成主題的單色色階。要看彩色 diff 時請關掉。

Cockpit 與 Classic 第一次顯示時，若你還沒替這個版面選過主題，會套用為它設計的主題（Classic：phosphor-green／phosphor-paper；Cockpit：cockpit-night／cockpit-day）。之後可以照常換成任何主題。Classic 另外附一套 phosphor-amber。

## 待解問題

1. **建議主題自動套用**：B1、B3 規格要求版面第一次顯示時寫入建議主題，我照做了。使用者對 T4 的決定是「只在設定頁建議，不自動改外觀」，雖然那條講的是既有版面，新版面沒有「原本的外觀」，仍請整合時確認是否要改成 T4 的建議列做法。
2. **效能量測**：Classic 驗收第 5 點（Release 版每幀時間與輸入延遲）沒有量，Release 依指示留給整合後跑。
3. **沒有自動驗證的項目**：Windows 高對比與關閉動畫時的行為、Tiling 的「滑鼠停留就聚焦」、`window_buttons` 為 `windows` 時的三個版面、非整數縮放（125%、150%）下的框線與掃描線，都只有程式與單元測試，沒有截圖。`--input` 不能送 F10 與組合鍵，F10 只透過 `classic.menu` 與 keymap 單元測試驗證。
4. **Cockpit 的今天完成是近似值**：UI 關著時完成的，算在重開那一刻；要精確得等 A4 的 host 狀態歷史。
5. **單色快取**：`colors_of` 的快取是函式內的 static，只在 UI 執行緒使用；若之後有多個視窗或別的執行緒畫終端機，要改成每個 view 一份。
