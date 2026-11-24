# 風格版面設計草案：B1 Terminal Classic、B2 Tiling、B3 Cockpit

日期：2026-11-20｜狀態：設計草案（未排入）

這份文件把 `docs/design/ideas.md` 第二節的三個風格版面寫成可以開工的規格。這些仍不是需求，使用者選定其中一項後，才寫進設計文件（新 ADR）與 PRD。做法照 AgentDesk Mac（ADR-43，設計文件 2.8）：版面檔加上專用的 mod 名稱，由 `derive_layout` 認出 style，交給各自的 C++ 繪製檔。

尺寸一律是設計 px（縮放 1、介面文字 15 px 時的大小），程式裡經 `px()`／`fpx()` 換算。草圖以第一次啟動的視窗 1280×820 為準。

---

## B1. Terminal Classic（復古終端機）

### 1. 目的與情境

- 給喜歡純文字介面的人：整個視窗看起來像一台單色映像管終端機，框線、選單、分頁、狀態全部用字元畫。
- 情境：晚上長時間盯著 Agent 跑、想要低干擾的單色畫面；錄影或截圖分享時想要有辨識度的外觀。
- 功能和 AgentDesk 版面相同（space、分頁、分割、命令面板、通知都照常），只換外觀。
- 版面名稱 `classic`，設定與命令面板顯示為「AgentDesk Classic」。

### 2. 畫面草圖

```text
x=0                                                                        x=1280
y=0   +--------------------------------------------------------------------------+
      |(x)(-)(+) AGENTDESK  File  Edit  View  Window  Help     !2  ~3  Thu 16:25 | A 22
y=22  |[repo-a v] 1 claude [!] | 2 pwsh | 3 codex [~] | +                        | B 22
y=44  |  ╔═ claude · repo-a (main) ══════════[!]═╗  ┌─ pwsh · repo-a ──────────┐ | C 20
      |  ║ > Fix the failing test in ...         ║  │ PS D:\src\repo-a> _     │ |
      |  ║                                       ║  │                          │ | D
      |  ║            （終端機內容）             ║  │                          │ |
      |  ║                                       ║  │                          │ |
      |  ╚═══════════════════════════════════════╝  └──────────────────────────┘ |
y=798 |-[ NEED YOU 2 ]-[ WORKING 3 ]-[ repo-a · main +1 ]----[ ^P commands ]-----| E 22
      +--------------------------------------------------------------------------+

側欄開啟時（Ctrl+Shift+B，預設隱藏），D 區左邊多一欄 248 px：
      | SPACES                 |
      | > repo-a      main     |
      |   ├ 1 claude      [!]  |
      |   └ 2 pwsh             |
      |   docs        dev      |
      | AGENTS                 |
      |   [!] repo-a/claude 2m |
      |   [~] docs/codex   14m |
```

圖例：
- A：`classic-menubar`，22 px。左邊是文字視窗按鈕，接著選單標題；右邊是需要你的數量（`!`）、執行中的數量（`~`）與時鐘。
- B：`classic-tabs`，22 px。目前 space 的下拉晶片，接著分頁，`|` 分隔，最後是 `+`。
- C：面板標題列 `classic-pane`，20 px，畫在面板框線的上緣（框線本身就是標題列）。
- D：分頁的面板。目前面板是雙線框（`╔═╗`），其他是單線框（`┌─┐`）；面板之間 8 px，D 區左右 8、上下 4 px。
- E：`classic-status`，22 px，用 `-[ … ]-` 串起來的狀態列。
- 外圍最多 12 px 的暗角（vignette）只畫在視窗邊緣，不壓到終端機字。

### 3. 元件規格

**字元格（chrome grid）**
- 介面字一律用隨附的 JetBrains Mono Medium（`s.fonts.mono`，粗體用 `mono_bold`），大小 14 設計 px，跟著介面文字大小等比例變（`14 × ui_text / 15`）。
- 格寬取該大小下字型的真實 advance 再四捨五入到整數 px（和 `terminal_view.cpp` 一樣，不用 `CalcTextSize`），格高 20 px。每個字元各自定位；寬字元（中日韓）佔兩格。
- 框線字元（`─│┌┐└┘├┤╔═╗║╚╝`）不靠字型，改用 `sprites::paint` 畫成填滿格子的形狀，線段在格與格之間才會接得起來。
- 每一列文字從所在區塊左緣起算，區塊寬度不是格寬整數倍時，餘數留在右邊。

**配色**：用目前主題，另外隨附三套單色主題（主題檔格式，第 5 節），建議成套使用：

| 角色 | 主題欄位 | phosphor-green（深） | phosphor-amber（深） | phosphor-paper（淺） |
|---|---|---|---|---|
| 畫布 | `canvas` | `#040906` | `#0A0703` | `#EDEFE4` |
| 介面底 | `chrome` | `#06100A` | `#0F0A04` | `#E4E7D8` |
| 終端機底 | `background` | `#07120B` | `#110C05` | `#F4F5EC` |
| 介面字 | `foreground` | `#8CFFA8` | `#FFC766` | `#1E3A26` |
| 終端機字 | `terminal_foreground` | `#7CF29A` | `#F5B850` | `#21402A` |
| 次要字 | `muted` | `#4FB86C` | `#B98736` | `#4D6652` |
| 淡字 | `dim` | `#2F7344` | `#755420` | `#87998A` |
| 框線 | `border` | `#1E5A2C` | `#5A3F12` | `#A9B5A4` |
| 強調（選取、目前面板） | `accent` | `#B8FFC8` | `#FFDE9C` | `#0F5A2A` |
| 需要你 | `blocked` | `#E6FFEC` | `#FFF1D2` | `#0B3D1C` |
| 工作中 | `working` | `#7CF29A` | `#F5B850` | `#2E6B3E` |
| 完成 | `done` | `#A8FFC0` | `#FFD98A` | `#1F5530` |
| 選取底 | `selection` | `#1F6B36` | `#6B4A14` | `#BFD6C2` |

單色主題裡狀態不能只靠顏色（設計文件 4.1），所以每個狀態都有字元：需要你 `[!]` 並反白（底色填介面字色、字用底色），工作中 `[~]`（後面的旋轉字元 `|/-\` 每 125 ms 換一次，只在有 Agent 工作中時），完成 `[+]`，Idle `[.]`，Unknown `[?]`。

**元件與狀態**

| 元件 | 一般 | hover | active | 空 | 載入中 | 錯誤 |
|---|---|---|---|---|---|---|
| 選單標題 | 介面字，兩側各 1 格 | 底色 `accent` 25% 透明 | 整格反白 | — | — | — |
| 下拉選單 | 單線框，直角，列高 20、左右 1 格；快捷鍵靠右用 `muted` | 該列反白 35% | 鍵盤選到的列整列反白 | — | — | — |
| 分頁 | `1 claude [!]`，`|` 分隔 | 底色 `accent` 20% | 目前分頁反白 | 只剩 `+` 與「No tabs」 | — | Lost 的分頁寫 `[lost]` 用 `dim` |
| 面板框 | 單線，`border` | 標題列文字變 `foreground` | 雙線，`accent` | — | — | 斷線時框線改虛線（`┄`，sprites 的 dashes） |
| 狀態列 | `-[ … ]-` 各段 | 可點的段（`^P commands`、`NEED YOU`）底色 `accent` 20% | 按下時反白 | 沒有 Agent 時寫 `[ no agents ]` | host 連線中寫 `[ connecting ⠋ ]`（點字旋轉，braille 也走 sprites） | host 斷線寫反白的 `[ DISCONNECTED ]` |
| 文字視窗按鈕 | `(x)(-)(+)` 或 `[_][#][X]`，`muted` | 該鈕變 `foreground` | 按下時反白 | — | — | — |

- 窗格沒有分頁時，D 區中央畫一個單線框：「No tabs. Press Ctrl+Shift+T for a new tab.」（快捷鍵取自 keys.json）。
- 沒有圓角：所有區塊 radius 0。

### 4. 互動

**滑鼠**
- 點選單標題開下拉；選單開著時滑鼠移到另一個標題就換過去（照 Mac 版選單列）。
- 分頁：點一下切換，雙擊改名，右鍵開 `tab_menu`，滾輪切換分頁；拖曳排序先不做（待確認是否需要）。
- space 晶片 `[repo-a v]`：點一下開 picker（`app.picker`）。
- 狀態列：點 `NEED YOU n` 跳到第一個需要你的 Agent（`agent.focus:<id>`，沒有就開 picker），點 `^P commands` 開命令面板。
- 選單列與分頁列的空白處可拖曳移動視窗，雙擊最大化（`window_drag_area`）。

**鍵盤**（加在 keys.json 的 `layout:classic` 預設區段）

| 鍵 | 命令 | 衝突檢查 |
|---|---|---|
| F10 | `classic.menu`（打開第一個選單，之後左右鍵換選單、上下鍵換列、Enter 執行、Esc 關閉） | `global` 與 `terminal` 預設都沒用到；F10 是 Windows 程式慣用的選單列鍵。原本提議的 Ctrl+Shift+M 改給 A1 監看牆（`mission.toggle`），審稿時調整 |
| 其他 | 沿用 `global` 預設 | — |

- Ctrl+Shift+B：版面裡有 id 為 `sidebar` 的節點，所以直接開關文字側欄（`sidebar.toggle` 現有行為）。

**新命令**
- `classic.menu`：開選單列的第一個選單（只在 classic 版面有作用，其他版面回「not in this layout」）。
- `classic.effects`：在 off、light、full 之間循環，並以 toast 顯示目前值。

選單內容不另外寫，直接用 Mac 版的 `ui::mac::menu_items`（第一個選單標題改成「AgentDesk」全大寫顯示），所以每一列都是現有命令，`MacLayout.EveryMenuRowNamesAKnownCommand` 的檢查同樣適用。

### 5. 資料與架構

**版面檔** `libs/ui/layouts/classic.json`（建置時和 `mac.json` 一樣編進程式）：

```json
{
  "id": "root",
  "type": "column",
  "titlebar": "none",
  "traffic_lights": [10, 5],
  "children": [
    { "id": "menubar", "type": "block", "size": { "px": 22 }, "window_drag": true,
      "content": { "mod": "classic-menubar" } },
    { "id": "tabs", "type": "block", "size": { "px": 22 }, "window_drag": true,
      "content": { "mod": "classic-tabs" } },
    { "id": "body", "type": "row", "children": [
      { "id": "sidebar", "type": "block", "hidden": true, "size": { "px": 248 },
        "content": { "mod": "classic-sidebar" } },
      { "id": "main", "type": "block", "padding": [4, 8],
        "content": { "tab": "active",
          "pane": { "header": { "content": { "mod": "classic-pane" }, "height": 20 },
                    "radius": 0, "gap": 8, "lone": true } } }
    ] },
    { "id": "status", "type": "block", "size": { "px": 22 },
      "content": { "mod": "classic-status" } }
  ]
}
```

**新檔案**
- `apps/desktop/classic_layout.cpp`：選單列、分頁列、側欄、狀態列、面板標題列與特效。
- `libs/ui/include/agentdesk/ui/classic_layout.hpp`、`libs/ui/src/classic_layout.cpp`：純邏輯，不碰 ImGui 繪製，可單元測試：字元格計算、分頁列的截斷（放不下時從最舊的非目前分頁縮成 `1…`）、狀態字元、單色對映 `mono_color`。
- 主題檔 `phosphor-green.json`、`phosphor-amber.json`、`phosphor-paper.json`：放在新資料夾 `libs/ui/themes/`，建置時和 herdr 的主題一起複製到執行檔旁的 `themes\`（待確認 CMake 複製規則的位置）。

主題檔範例（格式見 `themes.cpp` 的 `read_theme_file`；`ansi` 是 16 階綠色，依原本各色的亮度排）：

```json
{
  "background": "#07120B",
  "foreground": "#7CF29A",
  "selection": "#1F6B36",
  "cursor": "#B8FFC8",
  "ansi": ["#0B1A0F", "#3FA35A", "#5CC777", "#7FE296", "#2E7D45", "#4FA866", "#6FCB88", "#9FEFB5",
           "#2A5A36", "#62D17F", "#86EFA0", "#B4FFC6", "#4F9E66", "#73C58A", "#97E7AD", "#D8FFE2"],
  "colors": {
    "canvas": "#040906", "chrome": "#06100A", "surface": "#07120B", "border": "#1E5A2C",
    "foreground": "#8CFFA8", "muted": "#4FB86C", "dim": "#2F7344", "accent": "#B8FFC8",
    "working": "#7CF29A", "blocked": "#E6FFEC", "done": "#A8FFC0"
  }
}
```

**接點（hook points）**
- `derive_layout`（`layouts.cpp`）：出現 `classic-menubar`、`classic-tabs`、`classic-status`、`classic-sidebar` 任一個時 style 設為 `classic`。建議把「mod 名稱 → style」改成 `libs/ui` 裡的一張表（見「共用元件」），不要再加 if。
- `frame.cpp` 的 `draw_layout_block`：五個 mod 名稱各接一個 `draw_classic_*` 函式。`draw_layout` 在畫完所有區塊之後呼叫 `draw_classic_overlay`（介面上的掃描線與暗角）。
- `panes.cpp`：`PaneStyle` 加 `kClassic`，`draw_pane_frame` 改畫字元框；標題列高度取版面的 `header.height`（目前寫死 30／38，待確認 terminals 版面是否要一起改成讀版面）。終端機上的掃描線畫在每個面板自己的 child window 裡、TerminalView 畫完之後，這樣不會蓋到命令面板與選單。
- `TerminalTheme`（`terminal_view.hpp`）加 `std::optional<MonoRamp> mono`；`colors_of` 在有 `mono` 時把 RGB 與 256 色也轉成單色（ANSI 16 色已由主題的 `ansi` 處理）。`panes.cpp` 設定 view 主題的地方（`vs.view.theme.* = g_pal->...`）依 `classic_mono_terminal` 決定要不要填 `mono`。
- 視窗按鈕：`mac_window_buttons()` 在 classic 版面回 false，由 classic 自己畫文字按鈕；`window_buttons` 設定仍決定位置與順序（`mac`：左邊 `(x)(-)(+)`；`windows`：右邊 `[_][#][X]`）。
- 通知：`notices.cpp` 的 toast 往上讓出狀態列的 22 px；樣式沿用現有 toast，顏色跟著主題。
- 設定 › Look 的版面卡片（`settings_pages.cpp` 的 `layout_preview`）加 `classic` 分支：單色底、上方兩條文字列、兩個字元框、底部一條狀態列，全部用目前主題的 `text`／`border` 畫細線，不畫掃描線。
- `layout_display_name`：`classic` →「AgentDesk Classic」。

**單色對映 `mono_color`**（純函式）
- 輸入：原色 RGB、它是前景還是背景、色階（暗端 = 主題 `background`，亮端 = 主題 `terminal_foreground` 往白色推 15%）。
- 前景：取相對亮度 L，輸出 `mix(暗端, 亮端, 0.35 + 0.65 × L)`，所以再暗的字也看得到。
- 背景：輸出 `mix(暗端, 亮端, 0.45 × L)`，背景維持偏暗。
- 對比保護：前景與背景的亮度差小於 0.35 時，前景改成亮端（或背景很亮時改成暗端），確保文字讀得到。反白（reverse）在對映之後才交換。

**特效與成本**

| 特效 | 畫法 | 每幀成本（1080p） | off | light（預設） | full |
|---|---|---|---|---|---|
| 掃描線 | 每 3 設計 px 一條 1 設計 px 高的黑色橫條，位置先對齊整數裝置像素；`AddRectFilled` | 約 360 個矩形、1,440 個頂點 | — | 只在介面區（A、B、C、E、側欄），透明度 18% | 介面 18%，終端機內 6% |
| 字的微光 | 介面文字先以 ±1 px 偏移畫 4 次、透明度 20%，再畫本體 | 介面文字約 40 段、多約 1,500 個字形 | — | — | 只用在介面文字，終端機字不加 |
| 暗角 | 視窗四邊 12 px 寬的 `AddRectFilledMultiColor`，外緣 黑 30% → 內緣 0 | 4 個矩形 | — | 有 | 有 |

- 都是靜態圖形，不增加重繪次數：AgentDesk 只在有變化時才畫下一幀，特效沒有動畫，也不呼叫 `wake`。不做閃爍、不做曲面變形（會讓字糊掉，也會讓滑鼠位置和格子對不上）、不做全畫面光暈（要另外的 render target 與 shader，不划算）。
- 輸入延遲：特效都在同一幀畫完，不碰輸入路徑，也不碰 host。
- 淺色主題（`phosphor-paper`）自動不畫掃描線與微光（淺底上看起來像髒污），只保留字元介面。
- Windows 開了高對比（`SPI_GETHIGHCONTRAST`）時，特效一律視為 off。

### 6. 設定

| `ui.json` 鍵 | 預設 | 說明 |
|---|---|---|
| `classic_effects` | `"light"` | `off`、`light`、`full`。設定 › Look 在選到 classic 時多一列「Screen effects」 |
| `classic_mono_terminal` | `true` | 終端機的顏色是否轉成單色。關掉時終端機照主題原色（ANSI 用主題的 16 階，RGB 保留原色） |

- 主題沿用 `layout_themes`：classic 第一次顯示、`layout_themes` 還沒有這個版面時，寫入 `{dark: "phosphor-green", light: "phosphor-paper"}`；之後使用者可以照常換主題（包括其他非單色主題）。

### 7. 邊界情況

1. 視窗很窄（寬度小於 80 格）：選單只留 `AGENTDESK` 與 `View`（其他收進 AGENTDESK 選單底部），右側先藏時鐘再藏數字。
2. space 或分頁名稱有中文：寬字元佔兩格，截斷時不能切在寬字元中間（截斷函式要用和 VtEngine 相同的字寬規則，待確認共用哪個函式）。
3. 縮放 125%、150% 這類非整數倍：掃描線位置先換成裝置像素再取整數，避免出現粗細不一的摩爾紋。
4. 使用者換成彩色主題（例如 Midnight）：介面照樣用字元畫，顏色取該主題；`classic_mono_terminal` 仍為 true 時，色階用該主題的 `background` 到 `terminal_foreground`，看起來會是該主題的單色。
5. Claude Code 的 diff 用紅綠底色：轉成單色後只剩明暗不同，`+`／`-` 前綴還在。說明文件要寫明「要看彩色 diff 時關掉 Monochrome terminal」。
6. 沒有任何分頁：D 區顯示「No tabs」提示框，B 列只剩 space 晶片與 `+`。
7. host 斷線：狀態列反白 `[ DISCONNECTED ]`，面板框改虛線，面板本身照現有規則覆蓋 Disconnected、禁止輸入。
8. 使用者自訂版面同時放了 `mac-dock` 與 `classic-status`：依「共用元件」的 style 表，取樹中第一個出現的風格 mod（待確認是否改成報錯）。

### 8. 驗收標準

1. **Given** 版面為 classic、`classic_effects` 為 light，**When** 開兩個分割面板，**Then** 目前面板是雙線框、另一個是單線框，終端機內沒有掃描線，選單列、分頁列與狀態列有掃描線。
2. **Given** `classic_mono_terminal` 為 true、主題 phosphor-green，**When** 終端機輸出 24-bit 紅色字在預設底色上，**Then** 畫出來的字是綠色色階，且和底色的對比不低於 7:1。
3. **Given** 有一個 Agent 需要你，**When** 看分頁列與狀態列，**Then** 該分頁顯示反白的 `[!]`，狀態列顯示 `NEED YOU 1`；把主題換成灰階截圖時仍分得出來（不只靠顏色）。
4. **Given** classic 版面，**When** 按 F10、按右鍵兩次、按下鍵到「Split right」、按 Enter，**Then** 目前分頁多一個右側面板，選單關閉，按鍵沒有送進終端機。
5. **Given** `classic_effects` 從 off 切到 full，**When** 在 Release 版連續輸入 200 個字元（`vt_probe echo`），**Then** 每幀時間增加不超過 0.3 ms，輸入回顯延遲的中位數與 off 時相差不超過 1 ms（量測方式見第 9 節）。
6. **Given** Windows 高對比開啟，**When** 切到 classic，**Then** 不畫掃描線、微光與暗角。

### 9. 測試方式

**單元測試**（`tests/unit/classic_layout_test.cpp`）
- `ClassicLayout.ShippedLayoutParsesAndIsClassicStyle`：內建 `classic.json` 解析成功，style 為 `classic`，含 `sidebar` 節點且預設隱藏。
- `ClassicLayout.MonoColorKeepsContrast`：任意前景／背景組合轉換後亮度差 ≥ 0.35；反白時先對映再交換。
- `ClassicLayout.GridSnapsToWholePixels`：不同字級與縮放下格寬是整數、餘數留在右邊。
- `ClassicLayout.TabLineTruncatesOldestFirst`：放不下時目前分頁永遠完整，寬字元不被切半。
- `ClassicLayout.StatusGlyphs`：五種狀態各有不同字元。
- `ClassicLayout.ScanlineRowsOnDevicePixels`：125%、150%、175% 時每條線的起點都是整數裝置像素。
- keys：`layout:classic` 預設區段的鍵不和 `global`、`terminal` 預設重複。

**截圖**（`build\msvc-x64-debug\tools\qa\screenshot_ui.exe`）
- `--out classic-green.png --run "layout:classic|theme:phosphor-green"`
- `--out classic-paper.png --run "layout:classic|theme:phosphor-paper"`（確認淺色時沒有掃描線）
- `--out classic-split.png --run "layout:classic|pane.split:right"`
- `--out classic-menu.png --run "layout:classic|classic.menu"`
- `--out classic-full.png --run "layout:classic|classic.effects|classic.effects"`（light → full → off 的循環要確認次數）
- `--input` 目前只能送單鍵、不能送組合鍵，所以選單與特效都用 `--run` 命令觸發。

**人工檢查**
- 深色與淺色（三套主題各一次），以及切到 Midnight、Paper。
- 縮放 100%、125%、150%、200%（不同螢幕，或用 `app.zoom_in` 近似）：框線接得起來、掃描線粗細一致、終端機字清楚。
- 中文 space 名稱、IME 輸入、長時間輸出（`vt_probe flood`）時的流暢度。
- 輸入延遲：用 `session_bench` 的延遲量測比較 off 與 full（Release 建置）。每幀時間用 `--report` 輸出的資料比較（待確認 report 是否已有 frame 時間欄位，沒有就加一個只在自動化時寫的欄位）。

### 10. 不抄原則

- 自己畫的：所有框線與方塊（`sprites::paint`）、文字視窗按鈕、掃描線、暗角、微光、三套單色主題的色碼（上表的值是我們自己訂的，不取自任何終端機或螢幕產品的規格）。
- 只用隨附字型：JetBrains Mono（OFL），不用任何舊電腦的點陣字型或它們的仿製品。
- 刻意避開：特定廠牌終端機或作業系統的開機畫面、標誌、型號字樣、鍵盤圖、外殼照片、桌布；不用「某某牌綠」這類廠牌色名；介面文字只寫 AgentDesk 與一般用語（File、View、NEED YOU），不寫任何產品名稱。
- 版面配置（選單列＋分頁＋狀態列）是一般文字介面的通用形式，不照某一個程式的選單項目或按鍵排列；選單內容就是 AgentDesk 現有命令。

### 11. 工作量與風險

**工作量：L，約 6～8 天。** 字元格與框線 1.5 天、選單／分頁／狀態列／側欄 2 天、單色對映與 TerminalView 接點 1 天、特效與設定 1 天、主題檔與版面卡片 0.5 天、測試與截圖 1～2 天。

風險：
- 單色會讓彩色資訊（diff、語法色）消失，必須能一鍵關掉（`classic_mono_terminal`）。
- 掃描線在非整數縮放下可能出現摩爾紋；終端機內預設不畫就是為了這個。
- 選單攔下 F10，原本送給程式的這個鍵會失效（少數全螢幕終端程式用 F10 離開，要寫進說明，必要時讓使用者在 keys.json 改鍵）。
- 面板標題列高度目前寫死在 `panes.cpp`，改成讀版面會碰到 workspace 與 terminals，要用既有截圖比對確認沒變。

---

## B2. Tiling（平鋪式視窗管理器）

### 1. 目的與情境

- 給習慣鍵盤操作、想要最大終端機面積的人：沒有側欄、沒有圓角卡片，窗格之間只留間距，頂端一條細狀態列。
- 情境：一個 space 裡同時開 3～6 個面板（Agent、shell、log），用鍵盤切換、交換、調整大小，滑鼠只是輔助。
- space 對應「工作區編號」，分頁對應同一工作區裡的不同排列。Alt+1～9 切 space、Alt+H/J/K/L 換面板這些現有預設剛好就是這類工具的習慣，沿用即可。
- 版面名稱 `tiling`，顯示為「AgentDesk Tiling」。

### 2. 畫面草圖

```text
x=0                                                                        x=1280
y=0   ● ● ●  [1] 2  3 │ 1:claude  2:pwsh* │  claude · ~/repo-a (main)  │ !1 ~2 │ 16:25
      └ A1 ┘ └─ A2 ─┘   └──── A3 ────────┘  └──────── A4 ───────────┘   └A5─┘   A6
y=26  ┌──────────────────────────────────┐ ┌──────────────────────────────────┐  ← 外距 8
      │claude · repo-a              [!]  │ │pwsh · repo-a                     │  ← 18 px 標題
      │                                  │ │                                  │
      │   目前面板：2 px 強調色框          │ │   其他面板：1 px 框               │
      │                                  │ └──────────────────────────────────┘
      │                                  │ ┌──────────────────────────────────┐  ← 內距 8
      │                                  │ │codex · repo-a               [~]  │
      │                                  │ │                                  │
      └──────────────────────────────────┘ └──────────────────────────────────┘
y=820

調整大小模式時，A4 換成：  [ RESIZE  h/j/k/l or arrows · Esc to finish ]
```

圖例：
- A：`tiling-bar`，26 px，可拖曳移動視窗。
  - A1：圓形視窗按鈕（`traffic_lights` [8, 7]，`window_buttons` 為 `mac` 時）。
  - A2：space 編號方塊，目前的填強調色；有需要你的 space 填 `needs_you` 色；只列出存在的 space。
  - A3：目前 space 的分頁，`編號:名稱`，目前分頁後面加 `*`。
  - A4：目前面板的程式、資料夾與分支（置中，放不下時截斷）。
  - A5：需要你（`!`）與工作中（`~`）的數量，0 時不顯示該項。
  - A6：時鐘；`window_buttons` 為 `windows` 時，右端再放最小化、最大化、關閉。
- 面板：直角，外距（視窗邊到面板）與內距（面板之間）預設都是 8 px；面板有 2 個以上時才畫 18 px 標題列。

### 3. 元件規格

全部用目前主題的色票（`app_internal.hpp` 的 `Palette`），不另設配色，深淺色自動跟著主題。

| 元件 | 尺寸 | 字型 | 一般 | hover | active／目前 | 其他狀態 |
|---|---|---|---|---|---|---|
| 狀態列底 | 高 26 | — | `sidebar`（chrome），底部 1 px `border` | — | — | 視窗非作用中：文字改 `dim` |
| space 方塊 | 高 18、最小寬 18、左右內距 6、間距 4、圓角 3 | JetBrains Mono Bold 12 | 底 `hover`、字 `dim` | 底 `selected`、字 `text` | 底 `focus`、字 `accent_foreground()` | 需要你：底 `needs_you`、字白、後面加 `!`；完成：字 `done` |
| 分頁 | 高 18、左右內距 6 | JetBrains Mono 12 | 字 `dim` | 底 `hover` | 字 `text`，後面 `*`，底線 2 px `focus` | 收起的分頁（`settled`）用 `faint` 並加括號 |
| 面板標題 A4 | 置中 | Geist Medium 12.5 | `text` | — | — | 沒有面板時寫「empty」用 `faint` |
| 計數 A5 | — | JetBrains Mono 12 | `!n` 用 `needs_you`、`~n` 用 `working` | 底 `hover` | — | 都是 0 時整段不畫 |
| 面板框 | 未聚焦 1 px、聚焦 2 px，圓角 0 | — | `border` | — | `focus` | 需要你（未聚焦）：2 px `needs_you`；斷線：1 px `faint` |
| 面板標題列 | 高 18、左內距 6 | JetBrains Mono 11.5 | 底 `card`，字 `dim`，右邊 `status_mark` 小尺寸 | 字 `text` | 底 `mix(card, focus, 0.12)`、字 `text` | — |
| 調整大小提示 | 取代 A4 | JetBrains Mono Bold 12 | 底 `working`、字 `bg` | — | — | — |

- 空：沒有分頁時，主區中央一行 `faint` 字「No tabs — Ctrl+Shift+T opens one」（快捷鍵取自 keys.json）。
- 載入中：host 還沒連上時 A4 寫「connecting…」並轉圈（現有 `status_mark` 的 Working 樣式）。
- 錯誤：host 斷線時 A5 換成 `needs_you` 色的「disconnected」。

### 4. 互動

**滑鼠**
- 點面板聚焦（現有）；拖曳面板之間的間距調整大小（現有分隔線，點擊範圍就是間距的寬度）。
- 點 space 方塊切換；在 A2 上滾輪切 space；在 A3 上滾輪切分頁。
- 點分頁切換、雙擊改名、右鍵開 `tab_menu`。
- 標題列隱藏時（單一面板），沒有拖曳把手；要搬面板用鍵盤（Alt+Shift+H/J/K/L，現有 `pane.swap`）。
- 選用：`tiling_focus_follows_mouse` 開啟時，滑鼠停在面板上 150 ms 就聚焦它（預設關，避免打字打到別的面板）。

**鍵盤**（現有 `global` 預設已涵蓋大部分，這裡只加 `layout:tiling` 區段）

| 鍵 | 命令 | 衝突檢查 |
|---|---|---|
| Alt+1～9 | `space.select:N`（現有） | — |
| Ctrl+1～9 | `tab.select:N`（現有） | — |
| Alt+H/J/K/L、Alt+方向鍵 | `pane.go:<dir>`（現有） | — |
| Alt+Shift+H/J/K/L | `pane.swap:<dir>`（現有） | — |
| Ctrl+Shift+Z | `pane.zoom`（現有） | — |
| Alt+R | `tiling.resize_mode`（新） | 預設沒用到。shell 的 readline 用 Alt+R 還原整行，綁定後不再送給程式（寫進說明，可在 keys.json 改掉） |
| Alt+Shift+Enter | `pane.split:auto`（新參數） | 預設沒用到。不用 Alt+Enter：部分 Agent CLI 用它換行 |
| Alt+Shift+Q | `pane.close`（現有命令） | 預設沒用到；Ctrl+Shift+X 仍可用 |
| Alt+Shift+G | `tiling.gaps`（新） | 預設沒用到 |

- 刻意不綁 Alt+F（readline 的往後一個字，`ctrl-right` 也是送這個）、Alt+Enter、Alt+B／Alt+D 等 shell 常用鍵。

**調整大小模式**
- Alt+R 進入：終端機暫時不接鍵盤（和選單打開時一樣），A4 換成 RESIZE 提示。
- H/J/K/L 或方向鍵：`pane.resize:<dir>`（現有，每次 40 px）；加 Shift 每次 10 px（`pane.resize` 新增可選的距離參數，例如 `pane.resize:left:10`，待確認參數格式）。
- Esc、Enter、再按 Alt+R、點任何地方、視窗失去焦點：離開。沒有逾時。

**新命令**
- `tiling.resize_mode`：進入或離開調整大小模式。
- `tiling.gaps`：間距在 `tiling_gaps` 與 0 之間切換（存 `tiling_gaps_on`）。
- `pane.split:auto`：往目前面板較長的那一邊分割（寬大於高 × 1.6 時往右，否則往下；比例取字元格後的實際寬高比，待確認門檻）。所有版面都能用。

### 5. 資料與架構

**版面檔** `libs/ui/layouts/tiling.json`：

```json
{
  "id": "root",
  "type": "column",
  "titlebar": "none",
  "traffic_lights": [8, 7],
  "children": [
    { "id": "bar", "type": "block", "size": { "px": 26 }, "window_drag": true,
      "content": { "mod": "tiling-bar" } },
    { "id": "main", "type": "block", "padding": [8, 8],
      "content": { "tab": "active",
        "pane": { "header": { "content": { "mod": "tiling-pane" }, "height": 18 },
                  "border": { "width": 1, "color": "border" },
                  "focus": { "width": 2, "color": "accent" },
                  "radius": 0, "gap": 8, "lone": true } } }
  ]
}
```

- 狀態列想放底部的人，自己複製版面（`layout.clone`）把 `bar` 移到 `main` 後面即可，不另設選項。
- `layout.toggle:bar` 可整條隱藏（現有命令）。

**新檔案**
- `apps/desktop/tiling_layout.cpp`：狀態列、面板標題列、調整大小模式的按鍵處理、`tiling_layout_tree`。
- `libs/ui/include/agentdesk/ui/tiling_layout.hpp`、`libs/ui/src/tiling_layout.cpp`：純邏輯：`split_direction(w, h)`、調整大小模式的按鍵 → 命令對照、間距套用、狀態列各段的寬度分配與截斷。

**接點**
- `derive_layout`：`tiling-bar` 或 `tiling-pane` → style `tiling`。
- `frame.cpp`：`draw_layout_block` 接 `tiling-bar`；`draw_layout` 比照 `mac_layout_tree`，呼叫 `tiling_layout_tree(s, l.tree)` 依設定改 `main` 的 padding 與 pane gap（`tiling_gaps_on` 為 false 時兩者都是 0、面板框只剩 1 px 分隔線）。
- `panes.cpp`：`PaneStyle::kTiling`。框線改讀版面 pane 的 `border`、`focus`、`radius`；標題列在分頁只有一個面板時不畫（`tiling_titles` 為 `auto` 時），高度讀 `header.height`。
- 調整大小模式：`App::State` 加 `struct TilingState { bool resizing = false; }`；模式中 TerminalView 不要求焦點（`request_focus` 為 false，並讓 ImGui 焦點落在一個隱形項目上），`tiling_layout.cpp` 在每幀讀 H/J/K/L、方向鍵與 Esc。（待確認：TerminalView 在失去焦點的那一幀會不會把已按下的鍵送出去。）
- 設定卡片 `layout_preview` 加 `tiling`：頂端 4 px 細條、三個直角面板（一大兩小）、目前那個用 `focus` 2 px 框。
- `layout_display_name`：`tiling` →「AgentDesk Tiling」。
- 通知：維持右下角。

**特效**：沒有。也沒有動畫，不增加重繪。

### 6. 設定

| `ui.json` 鍵 | 預設 | 說明 |
|---|---|---|
| `tiling_gaps` | `8` | 外距與內距（設計 px，0～24） |
| `tiling_gaps_on` | `true` | `tiling.gaps` 切換的狀態 |
| `tiling_titles` | `"auto"` | `auto`（2 個面板以上才有標題列）、`always`、`never` |
| `tiling_focus_follows_mouse` | `false` | 滑鼠停 150 ms 聚焦面板 |

設定 › Look 選到 tiling 時多三列：Gaps（stepper）、Pane titles（三選一）、Focus follows mouse（開關）。主題沿用 `layout_themes`，沒有預設的專用主題。

### 7. 邊界情況

1. 間距 0 且面板很多：面板框改成共用 1 px 線，聚焦框的 2 px 往內畫，不讓相鄰面板的字被蓋到。
2. space 超過 9 個：A2 只畫 1～9 與目前的那一個，其餘以 `+n` 表示，點 `+n` 開 picker。
3. 分頁名稱很長或很多：A3 先縮短非目前分頁（只留編號），再整段截斷；A4 最先讓位。
4. 調整大小模式中 Agent 跳出「需要你」：照常提示音與通知，但不自動離開模式；面板框照樣變 `needs_you` 色。
5. 面板已到最小尺寸還按縮小：`pane.resize` 不動，A4 提示閃一下（換色 150 ms，不重複閃）。
6. 單一面板、`tiling_titles` 為 auto：沒有標題列，面板資訊只在 A4；放大（zoom）時也一樣。
7. Alt+R 被使用者在 keys.json 改綁別的命令：模式只能從命令面板或新綁的鍵進入，說明文字裡的鍵名取自 keys.json，不寫死。
8. `window_buttons` 為 `windows`：A6 右端的三個按鈕 26 px 寬，狀態列高 26 px 剛好放得下；A1 不畫，A2 從左邊 8 px 開始。

### 8. 驗收標準

1. **Given** tiling 版面、一個分頁三個面板，**When** 看畫面，**Then** 面板之間與視窗邊各有 8 px 間距、直角、目前面板 2 px 強調色框，沒有側欄與分頁卡片。
2. **Given** tiling 版面，**When** 按 Alt+R、L 三次、Esc，**Then** 目前面板往右擴 120 px，期間沒有任何字元送進終端機，Esc 後鍵盤回到原面板。
3. **Given** 一個寬 1200、高 700 的面板，**When** 按 Alt+Shift+Enter，**Then** 往右分割；高大於寬時往下分割。
4. **Given** 有 Agent 需要你而面板沒在聚焦，**When** 看畫面，**Then** 該面板框是 `needs_you` 色、所屬 space 方塊填 `needs_you` 色並有 `!`，狀態列有 `!1`。
5. **Given** Alt+Shift+G，**When** 再按一次，**Then** 間距在 0 與 8 之間切換，重開 AgentDesk 後維持最後的值。

### 9. 測試方式

**單元測試**（`tests/unit/tiling_layout_test.cpp`）
- `TilingLayout.ShippedLayoutParsesAndIsTilingStyle`
- `TilingLayout.SplitDirectionFollowsLongerSide`（含門檻邊界）
- `TilingLayout.ResizeModeKeys`：H/J/K/L、方向鍵、Shift、Esc、Enter 的對照
- `TilingLayout.GapsApplyToTree`：開關與 0～24 的夾限
- `TilingLayout.BarSegmentsShrinkInOrder`：A4 → 非目前分頁 → 時鐘的讓位順序
- keys：`layout:tiling` 的預設鍵不和 `global` 預設重複；`workspace_test` 加 `pane.resize` 距離參數的案例

**截圖**
- `--out tiling-3.png --run "layout:tiling|pane.split:right|pane.split:down"`
- `--out tiling-nogaps.png --run "layout:tiling|pane.split:right|tiling.gaps"`
- `--out tiling-resize.png --run "layout:tiling|pane.split:right|tiling.resize_mode"`
- 深淺色各一：加 `theme:paper`／`theme:midnight`

**人工檢查**
- 深淺色主題、縮放 100%～200%：2 px 聚焦框不糊、1 px 框在 150% 不消失。
- 在 PowerShell、cmd、WSL bash 裡確認 Alt+R、Alt+Shift+Enter 被攔下後，其他 Alt 組合（Alt+F、Alt+B）照常送給程式。
- 調整大小模式中打字不會進終端機；切到別的視窗再回來時模式已結束。

### 10. 不抄原則

- 自己畫的：狀態列、編號方塊、面板框，全部是 ImDrawList 的矩形與文字，配色取自目前主題。
- 只用隨附字型（JetBrains Mono、Geist）與現有的狀態標記，不用任何視窗管理器的圖示、狀態列外掛的字形組或設定檔範本。
- 刻意避開：特定視窗管理器的名稱、標誌、預設配色與設定語法；介面文字不出現任何產品名稱。按鍵只沿用 AgentDesk 已有的預設（Alt+數字、Alt+HJKL 本來就在），新增的幾個鍵是我們自己挑、避開 shell 常用鍵。

### 11. 工作量與風險

**工作量：M，約 3～5 天。** 狀態列 1 天、面板框與標題列 1 天（含讀版面 pane 欄位）、調整大小模式與新命令 1 天、設定與版面卡片 0.5 天、測試 1 天。

風險：
- 攔下 Alt 組合鍵會讓少數程式少一個鍵可用，預設只綁 4 個、都寫進說明，可在 keys.json 改。
- 調整大小模式是「有模式」的操作，使用者忘了自己在模式裡；所以提示要明顯、任何點擊都能離開。
- 面板框改讀版面欄位會碰到 `panes.cpp` 的共用程式，workspace 與 terminals 要用截圖比對確認沒變。

---

## B3. Cockpit（儀表板）

### 1. 目的與情境

- 像控制台一樣看全部 Agent：左邊三個儀表（工作中、需要你、今天完成），右邊 Agent 清單，下方狀態變化紀錄，中間仍是目前分頁的面板，可以照常操作。
- 情境：開著 4 個以上 Agent 時放在副螢幕上看；或純粹好玩。ideas.md 也寫了實用性比較低，所以這項以「看起來有趣、不拖累效能」為目標，功能不超過現有資料。
- 版面名稱 `cockpit`，顯示為「AgentDesk Cockpit」。

### 2. 畫面草圖

```text
x=0                                                                                 x=1280
y=0    ● ● ●  ◆ AGENTDESK / COCKPIT   [R] [D] [A]   1 claude · 2 pwsh · 3 codex    THU 16:25
       └─ 視窗按鈕、標誌、space 頭像、目前 space 的分頁、時鐘 ─────────────────────── H 44
y=56   ┌ GAUGES ──────────┐ ┌ ACTIVE · repo-a / claude ─────────────┐ ┌ AGENTS ───────────┐
       │    .-'''''-.     │ │                                        │ │ [!] repo-a/claude │
       │  .'    3    '.   │ │                                        │ │     needs you  2m │
       │      WORKING     │ │                                        │ │ [~] docs/codex    │
       │    .-'''''-.     │ │        （目前分頁的面板，可以操作）       │ │     working   14m │
       │  .'    1    '.   │ │                                        │ │ [+] api/claude    │
       │     NEEDS YOU    │ │                                        │ │     done       1h │
       │    .-'''''-.     │ │                                        │ │                   │
       │  .'    7    '.   │ └────────────────────────────────────────┘ │                   │
       │    DONE TODAY    │ ┌ LOG ───────────────────────────────────┐ │                   │
       │                  │ │ 16:25:03  repo-a/claude  WORKING → NEEDS YOU               │ │
       │                  │ │ 16:11:40  api/claude     WORKING → DONE                    │ │
y=808  └──────────────────┘ └────────────────────────────────────────┘ └───────────────────┘
```

圖例與尺寸：
- 整個版面外距 12、區塊之間 12；背景是 24 px 的淡格線。
- H：`cockpit-header`，44 px。`traffic_lights` [14, 16]；標誌（`draw_brand_mark` 18 px）與「AGENTDESK / COCKPIT」；space 頭像 24 px（點了切換）；目前 space 的分頁（文字晶片）；右邊時鐘。
- 左欄 `cockpit-gauges`，寬 240：三個儀表，每個 216×200（含標籤），上下平分剩餘高度，太矮時改成三個橫條（第 7 節）。
- 中間上：分頁的面板（`tab: active`），外框是螢光細線加四角括弧。
- 中間下：`cockpit-log`，高 120，id `log`，可用 `layout.toggle:log` 隱藏。
- 右欄：`cockpit-roster`，寬 280，id `roster`，可用 `layout.toggle:roster` 隱藏。

### 3. 元件規格

**配色**：以目前主題為準，由 `cockpit_look()` 從色票推算（和 `terminals_look()` 同樣做法），換任何主題都能用。另外隨附兩套建議主題：

| 角色 | 來源（任何主題） | cockpit-night（深） | cockpit-day（淺） |
|---|---|---|---|
| 畫布 | `bg` | `#05080F` | `#E9EEF2` |
| 面板底 | `mix(bg, card, 0.6)`，透明度 92% | `#0A1220` | `#FFFFFF` |
| 終端機底 | `term_bg` | `#0A0F1A` | `#FAFCFD` |
| 主線（螢光） | `focus` | `#2FE3EE` | `#0B8FA0` |
| 次線 | `info` | `#FF4FA3` | `#C2187A` |
| 格線 | `alpha(focus, 深 14／淺 18)` | `#2FE3EE` 14 | `#0B8FA0` 18 |
| 字 | `text` | `#CFE6F0` | `#0E1A22` |
| 標籤 | `dim` | `#7FA3B5` | `#5B6B75` |
| 工作中 | `working` | `#FFB547` | `#A86400` |
| 需要你 | `needs_you` | `#FF4F7B` | `#C42550` |
| 完成 | `done` | `#3DF29A` | `#11804F` |
| 框線 | `border` | `#16324A` | `#B9C8D2` |

（主題檔的 `colors` 目前不能覆寫 `info`，`add_preset_palette` 把 info 設成 accent。要讓次線用洋紅，需在 `themes.cpp` 讓 `colors` 也接受 `info`，預設仍等於 accent。這是一行的改動。）

**字型**
- 標籤：Geist SemiBold 11，全大寫，字距 +1 px（ImGui 沒有字距設定，逐字畫）。
- 儀表數字：JetBrains Mono Bold 30；清單與紀錄：JetBrains Mono 12；時鐘：JetBrains Mono Medium 14。

**儀表（gauge）**
- 240° 的弧（從 210° 到 −30°），半徑 70，弧寬 6；底弧 `alpha(focus, 40)`，數值弧用該儀表的狀態色。12 個刻度，每 3 個一個長刻度。
- 刻度上限：工作中與需要你取 `max(4, Agent 總數)`；今天完成取 `max(10, 往上取整到 5 的倍數)`。
- 指針與數值弧以 `ui::mac::approach` 平滑過渡（約 150 ms），動的時候才要求下一幀。
- 需要你大於 0 時：數字變 `needs_you` 色，標籤右邊多一個 `!` 晶片；不閃爍。

**區塊框**：1 px 主線色，四個角各一個 10 px 的 L 形括弧（2 px）；區塊標題寫在上緣左側，標題背後蓋一段面板底色把線斷開。

**清單列（roster）**：每列 40 px，兩行：`[狀態字] space/程式`（12 px `text`）與「狀態文字＋經過時間」（11 px `dim`）。左邊 3 px 狀態色豎條。排序沿用側欄 Agents 的優先序（Needs you、Done、Working、Idle，同段最近變動的在前）。

**紀錄列（log）**：每列 18 px，`HH:MM:SS  space/程式  舊狀態 → 新狀態`；只記狀態與時間，不記終端機內容；記憶體裡最多 200 筆，不存檔。

**狀態**

| 元件 | 一般 | hover | active | 空 | 載入中 | 錯誤 |
|---|---|---|---|---|---|---|
| 儀表 | 如上 | 底弧亮一階，游標變手形，tooltip 說明點了會做什麼 | 按下時數值弧加粗到 8 | 值 0：只畫底弧，數字用 `dim` | host 連線中：三個儀表畫虛線弧，數字「--」 | host 斷線：數字「--」、標籤下加 `needs_you` 色「DISCONNECTED」 |
| 清單列 | 如上 | 底 `hover` | 目前聚焦的 Agent：左豎條加寬到 5、底 `selected` | 「No agents running」置中 `dim` | 同上「--」 | 已 Lost 的 Agent 列 `faint` 並寫 lost |
| 紀錄 | 新的在上 | 列底 `hover` | — | 「Nothing yet」 | — | — |
| 頂列 space 頭像 | `draw_space_avatar` 24 px | 外圈 1 px `focus` | 目前的 space 外圈 2 px `focus` | — | — | — |

### 4. 互動

**滑鼠**
- 點「NEEDS YOU」儀表：跳到第一個需要你的 Agent（`agent.focus:<id>`）；沒有時開 picker。點「WORKING」：開 picker。點「DONE TODAY」：顯示 tooltip「Counted since midnight, while AgentDesk was watching」。
- 點清單列：`agent.focus:<id>`（會切到該 space 與分頁）；右鍵：peek、keep（現有 `agent.peek`、`agent.keep`）。
- 點紀錄列：同清單列，跳到那個 Agent。
- 頂列：點頭像切 space，點分頁晶片切分頁，右鍵分頁開 `tab_menu`；空白處拖曳移動視窗、雙擊最大化。

**鍵盤**：不加新的預設鍵，避免和程式搶鍵。現有的 Alt+↑／↓（`agent.prev`／`agent.next`）剛好對應清單上下。說明文件提供 keys.json 範例：

```json
{ "layout:cockpit": { "alt-shift-l": "layout.toggle:log", "alt-shift-r": "layout.toggle:roster" } }
```

（`alt-shift-l` 在 `global` 是 `pane.swap:right`，所以只放在範例、不當預設；使用者自己決定要不要蓋掉。）

**新命令**
- `cockpit.reset_today`：把今天完成數歸零（只出現在命令面板）。
- 其餘都用現有命令（`layout.toggle`、`agent.focus`、`app.picker`）。

### 5. 資料與架構

**版面檔** `libs/ui/layouts/cockpit.json`：

```json
{
  "id": "root",
  "type": "column",
  "titlebar": "none",
  "traffic_lights": [14, 16],
  "padding": [12, 12],
  "gap": 12,
  "children": [
    { "id": "header", "type": "block", "size": { "px": 44 }, "window_drag": true,
      "content": { "mod": "cockpit-header" } },
    { "id": "body", "type": "row", "gap": 12, "children": [
      { "id": "gauges", "type": "block", "size": { "px": 240 },
        "content": { "mod": "cockpit-gauges" } },
      { "id": "center", "type": "column", "gap": 12, "children": [
        { "id": "main", "type": "block",
          "content": { "tab": "active", "pane": { "radius": 4, "gap": 8, "lone": true } } },
        { "id": "log", "type": "block", "size": { "px": 120 },
          "content": { "mod": "cockpit-log" } }
      ] },
      { "id": "roster", "type": "block", "size": { "px": 280 },
        "content": { "mod": "cockpit-roster" } }
    ] }
  ]
}
```

**資料來源**（都在 UI 端，host 不改）
- 工作中、需要你：`ui::mac::count_agents`（和 Mac 版選單列同一個函式）。
- 今天完成：在 `sidebar.cpp` 的 `track_agents` 裡，同一個 Agent 從 `working` 變成 `idle`（done 與 idle 在那裡是同一類）時加一。這也涵蓋 UI 關著時完成的情況：重開後第一次比對時，存檔的 `working` 會變成 `idle`，在那時候算進去（時間是重開的時間，不是真正完成的時間）。數字與日期存在 `ui.json` 的 `cockpit_done_today`，本地時間過午夜歸零。這是近似值；要精確，需要 ideas.md A4 提到的 host 狀態歷史（待確認是否等 A4 一起做）。
- 紀錄：同一個地方，每次狀態改變推一筆到 `App::State` 的環狀緩衝（200 筆）。
- 清單：側欄 Agents 的同一份清單與排序函式（`draw_agents` 用的資料，抽出共用）。

**新檔案**
- `apps/desktop/cockpit_layout.cpp`：頂列、儀表、清單、紀錄、背景格線與區塊框。
- `libs/ui/include/agentdesk/ui/cockpit.hpp`、`libs/ui/src/cockpit.cpp`：純邏輯：`gauge_angle(value, max)`、刻度上限、`DoneToday`（日期換日、加一、歸零）、`EventLog`（環狀緩衝）、時間格式。
- 主題檔 `cockpit-night.json`、`cockpit-day.json`，和 B1 的主題放在同一個資料夾。

**接點**
- `derive_layout`：`cockpit-header`、`cockpit-gauges`、`cockpit-roster`、`cockpit-log` → style `cockpit`。
- `frame.cpp`：四個 mod 名稱各接一個繪製函式；`draw_layout` 在 `draw_layout_backgrounds` 之前呼叫 `draw_cockpit_backdrop`（格線），比照 `draw_mac_backdrop`。
- `panes.cpp`：`PaneStyle::kCockpit`，外框改成螢光細線與角括弧，面板標題列沿用預設的 30 px（顏色改用 cockpit_look）。
- `app.cpp` 的下一幀時間：時鐘只到分鐘，沿用 Mac 版的 `ms_to_next_minute`；儀表在過渡中時要求下一幀；其餘不額外喚醒。
- 設定卡片 `layout_preview` 加 `cockpit`：深底格線、左邊三個小弧、中間一個框、右邊幾條橫線。
- `layout_display_name`：`cockpit` →「AgentDesk Cockpit」。
- 通知：維持右下角，但往上讓出 log 區塊（toast 不蓋住紀錄最新的一列）。

**特效與成本**

| 特效 | 畫法 | 成本 |
|---|---|---|
| 背景格線 | 每 24 px 一條 1 px 線 | 1280×820 約 90 條線，靜態 |
| 線條微光 | 區塊框與角括弧畫三次：寬 6（透明度 8%）、寬 3（20%）、寬 1（100%） | 約 4 個區塊 × 8 段 × 3 次 ≈ 100 條線 |
| 儀表 | `PathArcTo` 48 段，底弧＋數值弧＋微光兩層 | 3 個儀表 × 4 條路徑 |
| 動畫 | 只有指針過渡（約 150 ms）與現有的工作中轉圈 | 穩定時不重繪 |

- `cockpit_effects` 為 off 時：不畫格線與微光，框線只畫 1 px；淺色主題自動不畫微光（淺底上看不出來）。
- Windows 關閉動畫（`SPI_GETCLIENTAREAANIMATION` 為 false）時，指針直接跳到新值。
- 終端機內不加任何特效。

### 6. 設定

| `ui.json` 鍵 | 預設 | 說明 |
|---|---|---|
| `cockpit_effects` | `"full"` | `full` 或 `off`。設定 › Look 選到 cockpit 時多一列「Glow and grid」 |
| `cockpit_done_today` | `{"date": "", "count": 0}` | 資料，不顯示在設定頁 |

- 主題：cockpit 第一次顯示且 `layout_themes` 沒有它時，寫入 `{dark: "cockpit-night", light: "cockpit-day"}`。
- log 與 roster 的開關存在版面的 `.state`（`layout.toggle` 現有行為），不另設鍵。

### 7. 邊界情況

1. 視窗高度不夠放三個儀表（左欄高度 < 560 px）：改成三個橫條儀表（每個 64 px 高，左邊數字、右邊長條）。
2. 視窗寬度 < 1000 px：先隱藏 roster，再隱藏 log；隱藏是畫面上的自動調整，不寫進 `.state`，視窗放大後回來。
3. Agent 數量很多（> 30）：roster 可捲動；儀表刻度上限跟著放大，數字仍完整顯示（超過 99 時字級降到 24）。
4. 過午夜時 AgentDesk 開著：下一次 `track_agents` 時發現日期不同，先歸零再計算；儀表從原值平滑降到 0。
5. 系統時間被往回調：`cockpit_done_today.date` 比今天晚時視為新的一天歸零，不顯示負值。
6. 沒有任何 Agent（只有 shell）：三個儀表都是 0，roster 顯示「No agents running」，版面不報錯。
7. 狀態來源不可信（Unknown）：不算進任何儀表，roster 寫 `[?] unknown`；不從畫面文字推斷（ADR-09）。
8. 遠端機器的 Agent：照樣計入，roster 的 space 名稱前加機器名稱；遠端斷線時那些列變 `faint`。

### 8. 驗收標準

1. **Given** cockpit 版面、三個 Agent（一個需要你、兩個工作中），**When** 看左欄，**Then** NEEDS YOU 顯示 1、WORKING 顯示 2，數值弧與數字顏色分別是 `needs_you`、`working`。
2. **Given** 一個 Agent 由工作中轉為完成，**When** 狀態更新，**Then** DONE TODAY 加 1，log 最上方多一列「WORKING → DONE」，roster 該列移到 Done 段。
3. **Given** DONE TODAY 是 5，**When** 本地時間過了午夜後下一次狀態更新，**Then** 數字歸零，`ui.json` 的 `cockpit_done_today.date` 變成新日期。
4. **Given** 點 NEEDS YOU 儀表，**When** 有需要你的 Agent，**Then** 切到它的 space 與分頁並聚焦該面板；沒有時開 picker。
5. **Given** 沒有任何狀態變化，**When** 停留 10 秒，**Then** AgentDesk 不額外重繪（和 workspace 版面相同的閒置行為，用 `--report` 的幀數比較）。
6. **Given** `cockpit_effects` 為 off，**When** 截圖，**Then** 沒有背景格線，所有框線只有 1 px。

### 9. 測試方式

**單元測試**（`tests/unit/cockpit_test.cpp`）
- `Cockpit.ShippedLayoutParsesAndIsCockpitStyle`
- `Cockpit.GaugeAngleClampsToSweep`：0、上限、超過上限、負值
- `Cockpit.GaugeMaxRoundsUp`
- `Cockpit.DoneTodayRollsOverAtMidnight`、`Cockpit.DoneTodayClockGoesBack`
- `Cockpit.EventLogKeepsNewest200`
- `Cockpit.CountsIgnoreUnknown`（沿用 `count_agents`）

**截圖**
- `--out cockpit.png --profile claude --run "layout:cockpit"`
- `--out cockpit-split.png --run "layout:cockpit|pane.split:right"`
- `--out cockpit-day.png --run "layout:cockpit|theme:cockpit-day"`
- `--out cockpit-nolog.png --run "layout:cockpit|layout.toggle:log"`
- `--out cockpit-midnight.png --run "layout:cockpit|theme:midnight"`（確認換一般主題也能看）
- 狀態變化用 `vt_probe report` 送 OSC 7501（`--child` 或第二個 profile），確認儀表與 log 的數字。

**人工檢查**
- 深淺色（兩套專用主題、Midnight、Paper）。
- 縮放 100%、150%、200%：弧線平滑、角括弧對齊、字不被裁切；窄視窗的自動隱藏。
- 開 8 個 vt_probe 狀態輪替 10 分鐘，看 CPU 與幀數不因儀表動畫而持續升高。

### 10. 不抄原則

- 自己畫的：儀表、刻度、格線、角括弧、微光，全部用 ImDrawList 的線、弧與矩形；標誌用現有的 `draw_brand_mark`。
- 色碼：上表兩套主題的值是我們訂的，不取自任何電影、遊戲或品牌的設定集。
- 只用隨附字型（Geist、JetBrains Mono）與 Nerd Font 字形（MIT）；不用任何科幻作品的字型、介面元素、音效或標誌，也不用「某某艦橋」之類的名稱。介面文字只用一般詞（GAUGES、AGENTS、LOG、NEEDS YOU）。
- 刻意避開：特定作品的配色組合與版面（例如某部片的控制台排列），只取「深底、螢光細線、儀表」這種通用的風格元素。

### 11. 工作量與風險

**工作量：M～L，約 5～7 天。** 版面與頂列 1 天、儀表 1.5 天、roster（含抽出側欄清單的共用部分）1 天、log 與今天完成數 1 天、特效與主題 1 天、測試與截圖 1 天。

風險：
- 今天完成數只是近似值（UI 關著時的完成，算在重開那一刻）；若使用者在意精確度，要等 A4 的 host 狀態歷史。
- 抽出側欄 Agents 清單的共用程式會碰到 `sidebar.cpp`，要用現有截圖確認 workspace 不變。
- 實用性低，日後維護成本（每次面板或狀態模型改動都要顧到這個版面）可能高於使用頻率；建議排在 A1、T1 之後。

---

## 共用元件

三個版面彼此共用、也和 AgentDesk Mac 共用的部分。先做這些，三個版面的工作量都會下降。

| 元件 | 現在在哪裡 | 誰會用 | 要做的事 |
|---|---|---|---|
| mod 名稱 → style 對照 | `derive_layout` 裡的 if（只有 mac、terminals） | Mac、B1、B2、B3 | 改成 `libs/ui` 的一張表 `style_of_mod(name)`，加單元測試；多個風格同時出現時的規則寫在一處 |
| 依 style 分派繪製 | `frame.cpp` 的 `draw_layout_block` 與 `draw_layout` | 全部 | 每個風格提供 `xxx_layout_tree`（可選）、`draw_xxx_backdrop`（可選）與各區塊的繪製函式；Mac 已有這個形狀，照抄介面 |
| 「Look 從色票推算」 | `terminals_look()` | B1（ClassicLook）、B3（CockpitLook） | 同樣做法：沒有寫死色碼，換主題照樣能用 |
| 面板樣式 | `panes.cpp` 的 `PaneStyle`（default、terminals） | B1、B2、B3 | 加三個值；框線與標題列高度改讀版面 pane 的 `border`、`focus`、`radius`、`header.height`，用既有截圖確認 workspace／terminals 不變 |
| 選單內容 | `ui::mac::menu_items`、`MenuContext` | Mac、B1 | 移到中性的命名空間（例如 `ui::menus`），Mac 與 B1 只是畫法不同 |
| Agent 計數、時鐘、緩動 | `ui::mac::count_agents`、`clock_text`、`ms_to_next_minute`、`approach` | Mac、B1、B2、B3 | 同上，移到共用位置，測試跟著搬 |
| 視窗按鈕 | `chrome.cpp` 的 traffic lights、`draw_window_buttons`、`clear_of_traffic_lights` | B2、B3 直接用；B1 只取位置與順序，自己畫文字按鈕 | `mac_window_buttons()` 加 style 判斷 |
| 拖曳移動視窗 | `window_drag_area` | 全部 | 不變 |
| 分頁右鍵選單、新增選單、狀態標記、程式圖示、space 頭像 | `tab_menu`、`draw_new_menu`、`status_mark`、`program_icon`、`draw_space_avatar` | B1（tab_menu、new_menu）、B2、B3 | 不變 |
| 框線字元 | `sprites::paint`（終端機用） | B1；B3 可選 | 不變，介面直接呼叫 |
| 版面卡片 | `settings_pages.cpp` 的 `layout_preview` | 全部 | 每個版面一個分支 |
| 通知位置 | `notices.cpp`（目前只判斷 mac） | B1、B3 | 改成每個 style 回傳「避開的區域」，toast 依此定位 |
| 成套主題 | `layout_themes` | B1、B3 | 新增「版面第一次顯示時寫入建議主題」，Mac 若要配專屬配色（ideas.md T4）也用同一條路；我們自己的主題檔放 `libs/ui/themes/`，以 MIT 隨附 |
| 特效開關與系統設定 | 無 | B1、B3 | 共用一個 `effects_allowed()`：設定為 off、淺色主題、Windows 高對比、關閉動畫時回 false |
| 自動化截圖 | `screenshot_ui.exe` 的 `--run` | 全部 | `--input` 不能送組合鍵，所以新命令都要能從 `--run` 觸發 |

不需要新的第三方素材：三個版面只用隨附的 Geist、JetBrains Mono（OFL）、Symbols Nerd Font（MIT）與程式自己畫的圖形，新增的主題檔是我們自己寫的。
