# 主題構想 T1～T5 設計草案

日期：2026-11-20｜狀態：設計草案（未排入）

這份草案展開 `docs/design/ideas.md` 第三節的五個主題構想（T1～T5）。使用者挑選之後，才寫進設計文件（新的 ADR）與 PRD，成為需求；在那之前，這裡的數字、鍵名與指令名稱都只是提案。

## 現況（寫草案前讀過的程式）

- 顏色都經過 `apps/desktop/app_internal.hpp` 的 `Palette`：`bg`（canvas）、`sidebar`（chrome）、`card`（surface）、`border`、`focus`、`hover`、`selected`、`text`、`dim`、`faint`、`needs_you`、`done`、`working`、`idle`、`info`、`term_bg`、`term_fg`、`cursor`、`selection`，加上可選的 16 色 `ansi`。全域指標 `g_pal` 指向目前的 palette。
- `apps/desktop/themes.cpp`：AgentDesk 自己的四套（`kPalettes`：midnight、ember、moss、paper）是編譯期常數；herdr 的四套 preset（`preset-graphite` 等）與主題檔（執行檔旁的 `themes\`、`%LOCALAPPDATA%\AgentDesk\themes\`）由 `add_preset_palette()` 從背景、前景與 ANSI 表推算介面顏色，再套用主題檔 `colors` 物件裡的覆寫（鍵名：`background`、`terminal_foreground`、`foreground`、`surface`、`hover`、`selected`、`border`、`canvas`、`chrome`、`muted`、`dim`、`accent`、`selection`、`cursor`、`working`、`blocked`、`done`、`idle`）。主題檔每次啟動只讀一次，改檔要重開才生效。
- `effective_palette()`：版面檔指定的 `theme` 優先；否則依 `appearance`（`system`／`light`／`dark`）在 `layout_themes[版面]` 或 `theme_dark`／`theme_light` 裡挑。`frame.cpp` 每個 frame 比對，換了才呼叫 `apply_style()`。
- 狀態記號：`chrome.cpp` 的 `status_mark()` 已經部分用形狀區分（工作中是轉動的弧、需要你是「Needs you」膠囊或「!」方塊、閒置是空心圈），但「完成」只是實心圓點，而且有十幾個地方自己畫圓點、不經過 `status_mark()`。
- `Attention` 只有 `kNone`、`kIdle`、`kWorking`、`kDone`、`kBlocked`；agent_state 為 Unknown 時是 `kNone`，畫面上跟「沒有狀態」一樣。
- 沒有「減少動態效果」設定。工作中的 session 會讓 `App::next_wakeup_ms()` 每 50 ms 重畫一次，只為了轉那個弧。

現有配色實測（WCAG 2.1 對比，色盲差異用下面 T1 說明的方法算）：

| 主題 | needs_you 在 bg | working 在 bg | done 在 bg | faint 在 bg | 色盲下最難分的一對（ΔE00） |
|---|---|---|---|---|---|
| Midnight | 6.12 | 10.33 | 9.40 | 2.97 | needs_you／done，綠色盲 8.4 |
| Paper | 4.27 | 2.78 | 3.19 | 2.48 | needs_you／done，綠色盲 6.0 |
| graphite（herdr） | 6.02 | 10.19 | 7.39 | — | needs_you／working，綠色盲 10.9 |

Paper 的 working 連 3:1 都不到；Midnight 與 Paper 的「需要你」和「完成」是紅綠，對綠色盲的人幾乎是同一個顏色。T1 主要就是處理這件事。

---

## T1. 高對比與色盲友善

### 1. 目的與情境

- 視力較弱、在戶外或投影、或開了 Windows 對比主題的人，需要一套真正夠清楚的配色（深、淺各一）。
- 紅綠色盲（男性約 8%）目前分不出「需要你」和「完成」。狀態是 AgentDesk 最重要的資訊，不能只靠顏色。
- 做法分兩層：(1) 兩套新的內建高對比主題；(2) 「狀態形狀」：每個 Agent 狀態都有自己的形狀，任何主題都能開。

### 2. 畫面或效果草圖

設定 › Look › Theme（新增兩列，卡片區多兩張卡）：

```
Theme
  Appearance        [ System | Light | Dark ]
  Window buttons    [ Round | Windows ]
  Status shapes     [ Auto | On | Off ]        形狀加在顏色之外；Auto＝高對比主題才開
  Reduce motion     [ System | On | Off ]      轉動的記號改成靜止；System 跟 Windows 的動畫效果設定
  Follow Windows    [x] Windows 開啟對比主題時，改用高對比主題

  Dark
  ┌────────┐ ┌────────┐ ┌────────┐ ┌──────────────┐
  │Midnight│ │ Ember  │ │  Moss  │ │High contrast │ ...
  └────────┘ └────────┘ └────────┘ └──────────────┘
  Light
  ┌────────┐ ┌──────────────┐
  │ Paper  │ │High contrast │ ...
  └────────┘ └──────────────┘
```

狀態記號，改前與改後（14 px 格子內，設計尺寸）：

```
狀態        現在（只靠顏色的地方）        Status shapes 開啟後
需要你      ●（紅點）                     ▲ 實心三角，格子夠大時內含「!」
工作中      ◜ 轉動的弧                    ◜ 轉動的弧；Reduce motion 時改成 ••• 三個靜止點
完成        ●（綠點或藍點）               ✓ 打勾（兩段折線，1.8 px）
閒置        ○ 空心圈                      ○ 空心圈（不變）
未知        ·（1.2 px 淡點）或不畫        ? 問號（只在曾有狀態、後來變 Unknown 時）
```

側欄 Agents 列（非 compact）：

```
改前： [icon] build-agent ................. [Needs you]   ← 粉紅膠囊
       [icon] docs-agent .................. ●            ← 綠點
改後： [icon] build-agent ................. [▲ Needs you]
       [icon] docs-agent .................. ✓
```

分頁列：

```
改前： ┃ api  ● ┃ web  ◜ ┃ docs ● ┃      （紅點與綠點）
改後： ┃ api  ▲ ┃ web  ◜ ┃ docs ✓ ┃
```

### 3. 規格

#### 3.1 兩套高對比 palette

加進 `kPalettes`（AgentDesk 自己的主題，排在 paper 之後）。id 為 `hc-dark`、`hc-light`，標籤都是「High contrast」。`hover`、`selected` 寫成不透明值（跟 Midnight 一樣），括號內是它等於哪個半透明白／黑疊在 `sidebar` 上。

| Palette 欄位 | hc-dark | hc-light |
|---|---|---|
| `light` | false | true |
| `bg`（canvas） | `#000000` | `#ffffff` |
| `sidebar`（chrome） | `#0c0c0c` | `#f0f0f0` |
| `card`（surface） | `#161616` | `#ffffff` |
| `border` | `#9e9e9e` | `#5f5f5f` |
| `focus`（accent） | `#c9a7ff` | `#5b2bd1` |
| `hover` | `#272727`（白 0x1C） | `#e1e1e1`（黑 0x10） |
| `selected` | `#363636`（白 0x2C） | `#d9d9d9`（黑 0x18） |
| `text` | `#ffffff` | `#000000` |
| `dim` | `#dadada` | `#262626` |
| `faint` | `#ababab` | `#545454` |
| `needs_you` | `#ff7a6b` | `#6b0000` |
| `done` | `#7cb4ff` | `#0b4fd6` |
| `working` | `#ffea00` | `#795c06` |
| `idle` | `#ababab` | `#545454` |
| `info` | `#5cc8ff` | `#0b4fd6` |
| `term_bg` | `#000000` | `#ffffff` |
| `term_fg` | `#ffffff` | `#000000` |
| `cursor` | `#ffea00` | `#000000` |
| `selection` | `#c9a7ff`，alpha 0x60 | `#5b2bd1`，alpha 0x38 |

ANSI 16 色（`ansi` 指標指向靜態表，括號內為在 `term_bg` 上的對比）：

| # | hc-dark | hc-light |
|---|---|---|
| 0 | `#4d4d4d`（2.48，黑色本來就該暗） | `#000000`（21.00） |
| 1 | `#ff6b6b`（7.57） | `#b00000`（7.38） |
| 2 | `#5fd75f`（11.37） | `#006b00`（6.77） |
| 3 | `#ffea00`（17.02） | `#6b5300`（7.34） |
| 4 | `#6cb6ff`（9.77） | `#0040c0`（8.42） |
| 5 | `#ff79f2`（9.27） | `#9a0089`（7.68） |
| 6 | `#33e1e1`（13.00） | `#00666b`（6.75） |
| 7 | `#e0e0e0`（15.91） | `#5c5c5c`（6.69，白改灰才看得到） |
| 8 | `#9e9e9e`（7.84） | `#4d4d4d`（8.45） |
| 9 | `#ff9a9a`（10.34） | `#c00018`（6.45） |
| 10 | `#8aff8a`（16.75） | `#007a00`（5.55） |
| 11 | `#fff27a`（18.23） | `#7a5c00`（6.25） |
| 12 | `#a3d0ff`（13.02） | `#0050d8`（6.69） |
| 13 | `#ffa8f6`（12.18） | `#a3009a`（6.94） |
| 14 | `#8cf5f5`（16.49） | `#00737a`（5.63） |
| 15 | `#ffffff`（21.00） | `#2e2e2e`（13.58） |

hc-light 把 7、15（白、亮白）改成灰與深灰，是故意的：程式用白字時在白底上至少看得到。這改變了 ANSI 的原意，所以只用在高對比主題。

`apply_style()` 對這兩套另外設 `FrameBorderSize = 1`，讓按鈕、輸入框在 hover 色很淡時仍有外框（`border` 對 `card` 6.75／6.39，超過 3:1）。

#### 3.2 實測對比（WCAG 2.1 相對亮度公式）

文字類（目標：內文 AAA 7:1，其餘 AA 4.5:1）：

| 前景 | hc-dark：bg／sidebar／card | hc-light：bg／sidebar／card |
|---|---|---|
| `text` | 21.00／19.56／18.10 | 21.00／18.43／21.00 |
| `dim` | 15.02／13.99／12.94 | 15.13／13.28／15.13 |
| `faint` | 9.14／8.52／7.88 | 7.57／6.65／7.57 |
| `term_fg` 在 `term_bg` | 21.00 | 21.00 |
| `text` 在 `selected` | 12.08 | 14.88 |
| `dim` 在 `selected` | 8.64 | 10.72 |
| `faint` 在 `selected` | 5.26 | 5.36 |
| `term_fg` 在 selection（疊在 term_bg 上） | 9.60 | 14.35 |

內文（`text`、`dim`）在所有背景都過 AAA。`faint` 在 `selected` 上是 AA。

狀態色（狀態記號是圖形，門檻 3:1；狀態色也會當文字用，例如「needs you」「finished」，門檻 4.5:1）：

| 狀態色 | hc-dark：bg／sidebar／card／selected | hc-light：bg／sidebar／card／selected |
|---|---|---|
| `needs_you` | 8.25／7.68／7.11／4.75 | 12.92／11.34／12.92／9.15 |
| `working` | 17.02／15.86／14.67／9.79 | 6.28／5.51／6.28／4.45 |
| `done` | 9.83／9.16／8.47／5.66 | 6.77／5.94／6.77／4.79 |
| `idle` | 9.14／8.52／7.88／— | 7.57／6.65／7.57／— |
| `focus` | 10.45／9.74／9.01／— | 7.75／6.80／7.75／— |
| 「Needs you」膠囊（needs_you 疊 0x26 當底）上的 needs_you 字 | sidebar 6.31／card 5.64 | sidebar 8.34／card 9.47 |

hc-dark 的狀態色當文字時在 bg、sidebar、card 都過 AAA。hc-light 的 `working`、`done` 在 sidebar 是 AA（5.51、5.94），沒到 AAA：深琥珀要再暗，在綠色盲眼中就會跟深紅的 `needs_you` 混在一起（見下節），兩者取捨後選了色盲可分。所有狀態色在 `selected` 上都高於 3:1。

狀態徽章裡的符號（Mac 版面 Dock 的「!」與打勾目前固定白色）改由 `best_on(bg)` 挑黑或白：hc-dark 用黑（needs_you 上 8.25、done 上 9.83；白色只有 2.55、2.14），hc-light 用白（needs_you 上 12.92、done 上 6.77）。順帶一提，現在 Midnight 的白色打勾在 done 綠底上只有 2.01，連 3:1 都不到，這個修正對所有主題都有用。

#### 3.3 色盲友善的狀態色：方法與結果

方法：

1. 模擬：用 Machado、Oliveira、Fernandes（2009）的色覺缺陷矩陣，嚴重度 1.0，在線性 sRGB 上套用，得到紅色盲（protanopia）、綠色盲（deuteranopia）、藍色盲（tritanopia）眼中的顏色。
2. 距離：把原色與三種模擬色都轉到 CIELAB（D65），兩兩算 CIEDE2000（ΔE00）。ΔE00 約 2.3 是剛好看得出的差異；狀態記號只有 7～14 px，所以目標訂在每一對、每種色覺下都 ≥ 15，並另外檢查亮度差 ΔL*。
3. 選色原則：紅綠色盲只剩「藍－黃」一條色相軸加上明暗。所以三個狀態用藍（完成）、黃（工作中）、紅橙（需要你），紅橙與黃再用明暗拉開。這剛好也是 herdr 深色 preset 的 working／blocked／done（黃、紅橙、藍）方向，熟悉 herdr 的人不用重新記。
4. 搜尋：在 HSL 網格上，先過濾對比（hc-dark 在 bg、sidebar、card 都 ≥ 7:1；hc-light ≥ 5.5:1），再找「所有色覺下最小 ΔE00」最大的組合，最後手動微調到好看的值。計算用的是設計時的 Python 腳本（不進版控）；實作時把同樣的公式寫進 `color_math`（見文末共用元件），單元測試要重現下表數字（誤差 ±0.1）。

各狀態兩兩的 ΔE00（一般／紅色盲／綠色盲／藍色盲）：

| 配對 | hc-dark | hc-light |
|---|---|---|
| needs_you／working | 50.2／28.8／19.5／28.1（ΔL* 25） | 32.1／21.5／15.1／18.4（ΔL* 20） |
| needs_you／done | 43.7／41.0／47.8／71.5 | 44.2／51.7／55.9／55.7 |
| needs_you／idle | 26.0／17.2／20.4／27.2 | 27.4／21.3／21.0／27.2 |
| working／done | 66.5／57.2／60.9／37.5 | 57.6／58.1／60.1／44.4 |
| working／idle | 33.4／32.7／33.0／19.6 | 23.3／22.9／23.1／19.0 |
| done／idle | 22.0／21.3／22.5／23.2 | 28.9／28.4／28.5／20.7 |

最難分的一對：hc-dark 是綠色盲下的 needs_you／working（19.5），hc-light 也是同一對（15.1）。對照現況 Midnight 的 needs_you／done 在綠色盲下只有 8.4，Paper 只有 6.0。

模擬後的顏色（給手動檢查對照用）：

| | needs_you | working | done |
|---|---|---|---|
| hc-dark 紅色盲 | `#9b9169` | `#ffe200` | `#96b8ff` |
| hc-dark 綠色盲 | `#bcad68` | `#ffeb2a` | `#86acfd` |
| hc-dark 藍色盲 | `#ff6377` | `#ffd8c7` | `#38c5ce` |
| hc-light 紅色盲 | `#292300` | `#695c00` | `#0064da` |
| hc-light 綠色盲 | `#423900` | `#70630d` | `#0052d3` |
| hc-light 藍色盲 | `#770002` | `#84524e` | `#00738b` |

已知限制：`focus` 與 `done` 在紅、綠色盲下很接近（hc-dark ΔE00 0.6～3.7）。`focus` 用在選取框、勾選框、目前卡片的外框，不是狀態，而且「完成」有打勾形狀，所以接受。`idle` 與 `faint` 同色，本來就是「沒事」的意思。

光靠顏色，15～20 的 ΔE00 在 7 px 的點上仍然不夠保險，所以 T1 的重點其實是下面的形狀。

#### 3.4 狀態形狀

新增 `status_glyph(App::State&, ImDrawList*, ImVec2 center, float r, Glyph g, ImU32 col)`（`chrome.cpp`）。全部用 ImDrawList 的基本圖形畫，不依賴字型（「?」除外）。r 是半徑，跟原本圓點一樣大，所以版面寬度不變。

| 狀態 | `Glyph` | 畫法（r = 3.5 設計 px 時） | 顏色 |
|---|---|---|---|
| 需要你 | `kNeedsYou` | 實心正三角，邊長 2.2r，底邊在中心下方 0.8r；r ≥ 5 時中間挖出「!」（用 `best_on(needs_you)` 的顏色） | `needs_you` |
| 工作中 | `kWorking` | 現有的轉動弧（4.4 rad，900 ms 一圈）；Reduce motion 時改成三個直徑 0.55r 的靜止圓點，間距 0.9r | `working` |
| 完成 | `kDone` | 打勾折線 (−r, 0)→(−0.3r, 0.7r)→(r, −0.8r)，線寬 1.8 px | `done` |
| 閒置 | `kIdle` | 空心圓，線寬 1.5 px（現有） | `idle` |
| 未知 | `kUnknown` | 粗體「?」，字高 2.8r | `faint` |
| 無 | `kNone` | 不畫，或維持 1.2 px 淡點（照現有呼叫端） | `faint` |

「未知」的判定：`Attention` 不改。在 app 端加 `glyph_of(s, id)`：程序 Running、`agent_state == "Unknown"`、而且 `last_agent_state` 不是 Unknown 時回傳 `kUnknown`（狀態紀錄被丟棄或清除，對應 ADR-09 的「原值存 last_agent_state」）；從來沒有狀態的 shell 仍是 `kNone`，不然每個 shell 都會掛一個問號。

形狀開關 `status_shapes`：`auto`（預設，palette 的新欄位 `shapes` 為 true 時開，也就是 hc-dark、hc-light；Windows 對比主題開啟時也開）、`on`、`off`。關閉時畫法跟現在完全一樣，herdr 外觀（ADR-15）不受影響。

#### 3.5 必須改成畫形狀的地方

用 `grep needs_you|working|done|idle|attention_color` 找出所有畫狀態色的地方，分成三類。

A 類：只靠顏色區分狀態，必須改：

| 檔案與位置 | 現在畫什麼 | 改成 |
|---|---|---|
| `chrome.cpp` `status_mark()`，`kDone` 分支 | 實心圓點 | `kDone` 打勾 |
| `chrome.cpp` `status_mark()`，`kBlocked` compact | 淡底方塊加「!」 | 形狀模式下改三角（已有「!」，色盲可辨，但形狀統一） |
| `chrome.cpp` `status_mark()`，`kWorking` | 轉動弧 | Reduce motion 時三個點 |
| `overlay_kit.hpp` `attention_glyph()` | 狀態色實心圓點 | `status_glyph()` |
| `tab_bar.cpp` 分頁標籤（`tab_attention` 後） | blocked／done 都是 3 px 圓點 | 三角／打勾 |
| `sidebar.cpp` 窄欄 space 編號旁 | 2.5 px 或 1.2 px 圓點 | 形狀模式下放大到 3.5 px 並畫形狀（空間是否夠，待確認） |
| `sidebar.cpp` 子 Agent 收合鈕（chevron 與數字依最急狀態上色） | 只有顏色 | 數字前加 3 px 形狀 |
| `terminals_layout.cpp` 程式圖示角落徽章 | 3.5 px 圓點 | `status_glyph()`，外圈保留 |
| `mac_layout.cpp` 選單列「需要你」計數 | 4 px 紅點 | 三角 |
| `mac_layout.cpp` Dock 徽章 | 實心圓加白色「!」或打勾 | 已有符號；符號顏色改 `best_on()` |
| `notices.cpp` toast 左側圓點（`t.color`） | 依顏色角色的圓點 | 依角色畫：needs_you→三角、working→實心菱形（警告）、done→打勾、info→不變 |
| `settings_dialog.cpp` 機器連線狀態點 | connected／connecting／失敗三色點 | 打勾／轉動弧／三角 |
| `sidebar.cpp` 窄欄機器圖示旁的開關點（兩處） | done 色或 faint 色圓點 | 開：打勾；關：空心圈 |
| `welcome.cpp` 檢查項目的點 | done 色或 faint 色圓點 | 打勾／空心圈 |
| `tab_bar.cpp` 狀態列 host 連線點 | dim 或 needs_you 色 | 斷線時三角 |

B 類：已經有文字或符號，只確認對比：

- `sidebar.cpp` Agents 依優先排序的群組標題（「Needs you」「Done」「Working」「Idle」文字上色）：形狀模式下標題前加形狀。
- `notices.cpp` peek 列、`overlays.cpp` 選擇器列、`files_changes.cpp` Details 的「Agent」列：都有「needs you」「finished」等字，保持；顏色對比由 3.2 的表保證。
- `files_changes.cpp` diff 的增減數字與底色：有「+」「−」與行號；底色是 0x18 的淡色，不承擔辨識。
- `panes.cpp` git 狀態字母（A／D／U／M）：字母本身就是形狀。

C 類：借用狀態色但不是狀態，不改：`code_preview.cpp` 的語法上色、`chrome.cpp` `mod_color()`（mod 的 good／warn／bad）、各處錯誤訊息文字（`needs_you` 當錯誤色）、`app_internal.hpp` toast 依顏色決定顯示秒數。

#### 3.6 Windows 對比主題

`follow_windows_contrast` 為 true（預設）時，每 2 秒（跟 `system_light()` 同樣的節流）讀一次 `SystemParametersInfoW(SPI_GETHIGHCONTRAST)`。`HCF_HIGHCONTRASTON` 時，`effective_palette()` 回傳 `hc-dark` 或 `hc-light`：依 `GetSysColor(COLOR_WINDOW)` 的亮度判斷，大於 0.5 用淺色。版面檔指定的 `theme` 這時也不套用。直接把 Windows 對比主題的系統色轉成 palette 會更貼近使用者的選擇，但系統色只有十幾個，不夠填狀態色，列為待確認的後續項目。

#### 3.7 Reduce motion

`reduce_motion`：`system`（預設，讀 `SPI_GETCLIENTAREAANIMATION`，也就是 Windows 設定 › 協助工具 › 視覺效果 › 動畫效果；關閉時視為要減少）、`on`、`off`。生效時：工作中改畫靜止的三點；`App::next_wakeup_ms()` 不再因為 Working 而每 50 ms 喚醒（順便省電）；T3 的漸變改成直接切換。

### 4. 互動

- 設定 › Look › Theme：多兩張主題卡（深色區與淺色區各一張「High contrast」），以及「Status shapes」「Reduce motion」兩個 segmented 和「Follow Windows」勾選框（樣式沿用 `segmented()`、`settings_toggle()`）。
- 命令面板：「Theme: High contrast」由現有的 `theme:<id>` 自動列出（`overlays.cpp` 列舉所有 palette）。新增：
  - `theme.high_contrast`：「Toggle high contrast」。目前不是高對比時，把目前版面的深或淺主題換成 hc 版本，並記住原本的 id；再執行一次換回去。
  - `look.status_shapes:auto|on|off`：「Status shapes: …」。
- 快捷鍵：不設預設值。`keymap.cpp` 預設表裡 Ctrl+Shift 已用掉 T、N、K、W、X、O、E、Z、P、/、B、L、F、C、V，剩下的字母留給 herdr 對齊；需要的人在 keys.json 綁 `theme.high_contrast`。
- 歡迎頁的主題選擇也會出現兩張 hc 卡片（沿用 palette 清單）。

### 5. 資料與架構

- `themes.cpp`：`kPalettes` 加兩筆，以及兩張靜態 ANSI 表；`effective_palette()` 加 Windows 對比主題判斷；`apply_style()` 依 `g_pal->shapes` 設 `FrameBorderSize`。
- `app_internal.hpp`：`Palette` 尾端加 `bool shapes = false;`（尾端加有預設值的欄位，現有的 aggregate 初始化不用改）；`Settings` 加 `status_shapes`、`reduce_motion`、`follow_windows_contrast`；宣告 `status_glyph()`、`glyph_of()`、`shapes_on(s)`、`motion_reduced(s)`。
- `chrome.cpp`：`status_glyph()`、修改 `status_mark()`。
- 3.5 A 類各檔案改呼叫 `status_glyph()`。
- `settings_store.cpp`：讀寫三個新鍵。
- `settings_dialog.cpp`：`look_theme()` 加三列。
- `app.cpp`：`next_wakeup_ms()` 依 `motion_reduced()` 跳過 50 ms 喚醒。
- 新檔：`libs/ui/include/agentdesk/ui/color_math.hpp`、`libs/ui/src/color_math.cpp`（見共用元件）。
- 不變：`Attention` 與 `AttentionTracker`、host 端、主題檔格式、herdr 主題與 AgentDesk 原有四套的顏色。

### 6. 設定

```json
{
  "status_shapes": "auto",
  "reduce_motion": "system",
  "follow_windows_contrast": true
}
```

值不合法時用預設值（依 `settings_store.cpp` 現有規則：型別錯誤就用預設並且不回寫）。

### 7. 邊界情況

1. 使用者主題檔取名 `hc-dark.json`：`palette_by_id()` 先找內建，內建贏；載入時在 `g_settings_problem` 提示一次「themes\hc-dark.json 與內建主題同名，未載入」。
2. Windows 對比主題開著，使用者又手動選了 Midnight：對比主題優先。設定頁主題卡上方顯示一行「Windows 對比主題開啟中，暫時使用 High contrast」，取消勾選 Follow Windows 才會用 Midnight。
3. 版面檔指定 `theme`（例如 herdr 的 terminals.json 寫 `graphite`）：高對比由 Follow Windows 觸發時蓋過版面；使用者手動選 hc 時，照現有規則版面檔優先（待確認是否要讓 hc 一律優先）。
4. 窄欄（rail）寬 44 px 時 space 編號旁只剩約 6 px：形狀放大後可能壓到編號，需要截圖確認，必要時改成只在最急的狀態畫形狀。
5. 縮放 50% 時 r 約 1.75 px，三角和打勾會糊成一團：r 小於 2.5 實際像素時退回實心點，但仍保留不同大小（需要你最大）。
6. 「?」字型缺字：Geist 有半形問號，不會缺；萬一字型載入失敗退回 ImGui 預設字型，仍有「?」。
7. 高對比主題加上 T3 跟著狀態變色：T3 對 `shapes` 為 true 的 palette 不作用（高對比使用者要的是穩定）。

### 8. 驗收標準

1. Given 選了 hc-dark，When 執行 palette 檢查，Then `text`、`dim` 在 bg、sidebar、card、term_bg 的對比都 ≥ 7:1，四個狀態色在 bg、sidebar、card 都 ≥ 4.5:1、在 selected 上 ≥ 3:1。
2. Given 選了 hc-light，When 執行同樣檢查，Then `text`、`dim` ≥ 7:1，狀態色 ≥ 4.5:1（在 selected 上 ≥ 3:1）。
3. Given 任一 hc 主題，When 以紅、綠、藍色盲模擬計算，Then needs_you、working、done、idle 兩兩 ΔE00 都 ≥ 15。
4. Given `status_shapes` 為 `on`，When 有四個 session 分別是 Blocked、Working、Done（未看）、Idle，Then 側欄、分頁列、窄欄、terminals 版面徽章、Mac 選單列與 Dock 上，四種狀態的形狀都不同；把截圖轉成灰階後仍能分辨。
5. Given `reduce_motion` 為 `on` 而且有 Working 的 session，When 連續截兩張相隔 500 ms 的圖，Then 狀態記號完全相同，且 `next_wakeup_ms()` 不因 Working 小於 1000。
6. Given Windows 開啟對比主題「夜空」，When AgentDesk 在前景，Then 2 秒內換成 hc-dark；關閉對比主題後 2 秒內換回原本的主題。
7. Given `status_shapes` 為 `off`、主題為 Midnight，When 截圖與目前版本比較，Then 畫面完全相同。

### 9. 測試方式

- 單元測試（`tests/unit/color_math_test.cpp`）：
  - 對比：白對黑 21.00；`#777777` 對白 4.48；同色 1.00；參數順序互換結果相同。
  - 色盲模擬：`sim(#ff7a6b, deutan)` 為 `#bcad68`（每個通道 ±1）；灰色模擬後不變。
  - ΔE00：用 Sharma 2005 論文的 34 組測試資料，誤差 ±0.0001。
- 單元測試（`tests/unit/palette_check_test.cpp`）：對 hc-dark、hc-light 跑 `check_palette()`，斷言 3.2、3.3 的門檻；對其他內建主題只輸出報告不斷言（現有主題本來就有低於門檻的組合）。palette 資料要能在測試裡取得：把兩套 hc 的十六進位值放進 `libs/ui` 的純資料表（不含 ImGui），`themes.cpp` 從那裡建 `Palette`。
- 截圖（`build\<preset>\tools\qa\screenshot_ui.exe`）：設 `SHOT_CTL_HOST=1` 與 `SHOT_EXTRA`，開四個 session 後用 `agentctl --pipe <pipe> report-state <id> --source qa --state Blocked --seq 1` 等指令設定狀態（實際接法待確認），再以 `--run "theme:hc-dark|look.status_shapes:on"` 截圖；workspace、terminals、mac 三個版面各一張，深淺各一張。
- 色盲模擬截圖：在 `screenshot_ui` 加 `--cvd protan|deutan|tritan`，對擷取的 PNG 套 3.3 的矩陣另存一張（不用安裝新工具）。人工看這三張圖能否分辨四種狀態。
- 人工：Windows 設定 › 協助工具 › 對比主題，依序套用「水生」「沙漠」「黃昏」「夜空」；設定 › 協助工具 › 視覺效果，關閉動畫效果，確認轉動弧停住；用放大鏡 200% 看形狀邊緣。

### 10. 工作量與風險

M，約 5～6 天：palette 與 ANSI 表 0.5 天；`status_glyph()` 與 A 類 15 處 2 天；設定、Windows 對比主題、Reduce motion 1 天；`color_math` 與測試 1 天；截圖與人工檢查 1 天。

風險：
- 形狀在 100% 縮放、7 px 大小時可能不夠清楚，需要先做一張截圖比較再定尺寸。
- 改動散在十幾個檔案，容易漏；以 grep 清單做檢查表，並在 code review 時對照 3.5。
- `status_shapes` 預設 `auto` 代表一般主題仍只靠顏色（為了維持 herdr 外觀）。是否改成預設 `on`，交給使用者決定。

---

## T2. 依時間自動切換

### 1. 目的與情境

白天在亮處用淺色、晚上用深色，不想每天手動切。Windows 本身沒有依時間切換應用程式淺深色的功能，所以「跟隨系統」做不到。

### 2. 畫面或效果草圖

```
Theme
  Appearance        [ System | Light | Dark | Schedule ]

  （選 Schedule 時展開）
  Switch by         [ Fixed times | Sunrise and sunset ]
  Light from        [ 07:00 ]      Dark from  [ 19:00 ]           ← Fixed times
  ─────────────────────────────────────────────────────────
  Latitude          [ 25.0 ]  Longitude [ 121.5 ]  (optional)     ← Sunrise and sunset
  [ Use my location once ]   只在按下時讀 Windows 定位一次，存到小數一位
  Today: light 05:04 → dark 18:47 · now Dark                       ← 預覽列
```

未填緯度時，Sunrise and sunset 選項是灰的，旁邊一行字：「Enter a latitude to use sunrise and sunset.」

### 3. 規格

資料模型：`Settings` 加 `appearance` 的新值 `"schedule"`，以及

```cpp
struct AppearanceSchedule {
    std::string mode = "fixed";  // "fixed" | "sun"
    int light_at = 7 * 60;       // minutes after local midnight
    int dark_at = 19 * 60;
    std::optional<double> latitude, longitude;  // degrees, rounded to 0.1
    int sun_offset = 0;          // minutes added to sunrise and sunset (-120..120)
};
```

演算法（`libs/ui` 的純函式，可單元測試）：

```
bool schedule_is_light(const AppearanceSchedule&, LocalTime now, TzInfo tz)
```

- fixed：把 now 換成當地分鐘數 m。`light_at < dark_at` 時，m 在 [light_at, dark_at) 為淺色；`light_at > dark_at`（夜班）時，m 在 [light_at, 1440) ∪ [0, dark_at) 為淺色；兩者相等視為設定錯誤，退回 `system`，並在設定頁顯示錯誤。
- sun：用 NOAA 的太陽位置簡化公式（年中第幾天 → 均時差 eqtime 與赤緯 decl）。
  - `ha = acos(cos(90.833°) / (cos(lat)·cos(decl)) − tan(lat)·tan(decl))`
  - 日出（UTC 分鐘）＝ `720 − 4·(lon + ha) − eqtime`，日落＝ `720 − 4·(lon − ha) − eqtime`，經度東正。
  - 沒有經度時，用時區的標準經線代替：`lon = UTC 偏移小時 × 15`。台灣（UTC+8，120°E）誤差在 6 分鐘內；時區很寬或跨經度的地方（例如西班牙）可能差一小時以上，設定頁提示這時要填經度。
  - 結果加上 `sun_offset`，轉成當地時間（`GetTimeZoneInformationForYear`，含夏令時間）。
  - acos 的參數 > 1：極夜，整天深色；< −1：永晝，整天淺色。
  - 每天只算一次（日期或時區變了才重算），結果快取。
- 隱私：預設 fixed，不需要位置。緯度、經度只能手動輸入，或按「Use my location once」時讀一次 Windows 定位（`Windows.Devices.Geolocation`，WinRT；是否要加這個依賴待確認，不做也可以只留手動輸入）。不連網、不在背景持續讀位置；存檔時四捨五入到小數一位（約 11 公里）。
- 切換時機：`frame.cpp` 本來就每個 frame 呼叫 `effective_palette()`，加一個參數傳入 `schedule_is_light()` 的結果即可。App 每秒至少喚醒一次（`next_wakeup_ms()` 上限 1000 ms），所以邊界過後 1 秒內會切換；睡眠喚醒後第一個 frame 就會對。切換是瞬間換色，不做淡入淡出。
- 手動暫時切換：`appearance.toggle` 在 schedule 模式下記一個 `schedule_override_until`（下一個邊界時間），在那之前用相反的淺深色，過了邊界自動回到排程。

狀態：
- 一般：預覽列顯示今天的兩個時間與目前狀態。
- 空：sun 模式沒有緯度，選項停用並說明。
- 讀取中：按「Use my location once」後按鈕顯示「Locating…」，最多等 10 秒。
- 錯誤：定位被拒或逾時，顯示「Location is off or unavailable. Enter a latitude instead.」；時間格式錯誤時輸入框紅框、不存檔。

### 4. 互動

- Appearance segmented 多一個「Schedule」。時間用兩個 `HH:MM` 輸入框（按上下鍵每次 15 分鐘）。
- 命令：`appearance:system`、`appearance:light`、`appearance:dark`、`appearance:schedule`（「Appearance: …」），`appearance.toggle`（「Toggle light / dark」：一般模式直接改 appearance；schedule 模式暫時切到下一個邊界）。
- 快捷鍵：不設預設，可在 keys.json 綁 `appearance.toggle`。

### 5. 資料與架構

- 新檔：`libs/ui/include/agentdesk/ui/schedule.hpp`、`libs/ui/src/schedule.cpp`（純計算）；`tests/unit/schedule_test.cpp`。
- `themes.cpp`：`effective_palette()` 的 `appearance == "schedule"` 分支。
- `settings_store.cpp`：讀寫 `appearance_schedule`；`appearance` 接受新值。
- `settings_dialog.cpp`：`look_theme()` 的展開列。
- `commands.cpp`：新命令；命令名稱表加四列。
- 不變：`layout_themes`、主題卡片、版面檔的 `theme` 優先順序。

### 6. 設定

```json
{
  "appearance": "schedule",
  "appearance_schedule": {
    "mode": "fixed",
    "light_at": "07:00",
    "dark_at": "19:00",
    "latitude": null,
    "longitude": null,
    "sun_offset": 0
  }
}
```

`appearance` 預設仍是 `"system"`；`appearance_schedule` 不存在時用上面的值。檔案裡的時間用字串 `HH:MM`，方便手改。

### 7. 邊界情況

1. 夏令時間切換那天：用當天的時區資訊換算，跳過或重複的那一小時照當地時鐘判斷；測試要包含歐洲時區的 3 月與 10 月。
2. 筆電跨時區：時區改變後當天重算；位置是手動填的，仍用原本的緯度，設定頁提示「位置是手動輸入的」。
3. 極區（例如北緯 69.6°，6 月）：沒有日落，整天淺色；12 月整天深色。
4. 使用者在 Windows 改了系統時間：下一個 frame 依新時間判斷，不需要特別處理。
5. 版面檔指定了 theme，或 T1 的 Windows 對比主題生效：排程不作用，預覽列寫明原因。
6. `light_at == dark_at`：視為錯誤，不存檔。

### 8. 驗收標準

1. Given appearance 為 schedule、fixed 07:00～19:00，When 本地時間 18:59:59，Then 用淺色主題；When 19:00:00 之後 1 秒內，Then 換成深色主題。
2. Given light_at 22:00、dark_at 06:00，When 現在 02:00，Then 用淺色。
3. Given sun 模式、緯度 25.0、經度 121.5，When 日期 2026-06-21，Then 計算的日出、日落與 NOAA 線上計算器相差不超過 3 分鐘（參考值待用 NOAA 核對）。
4. Given sun 模式、緯度 69.6，When 日期 2026-06-21，Then 整天淺色；When 2026-12-21，Then 整天深色。
5. Given schedule 模式在白天，When 執行 `appearance.toggle`，Then 立即換深色；When 到了 19:00，Then 維持深色且暫時切換失效；When 隔天 07:00，Then 回到淺色。
6. Given 沒填緯度，When 打開設定，Then Sunrise and sunset 停用，而且整個流程沒有任何網路連線或定位呼叫。

### 9. 測試方式

- 單元測試 `schedule_test.cpp`：fixed 的一般與跨午夜區間、相等時間；sun 的台北夏至與冬至、赤道、北緯 69.6° 的永晝與極夜、南半球（雪梨）；未填經度時用時區經線的誤差；夏令時間切換日（Europe/Berlin）。時間與時區都由參數傳入，不讀系統時鐘。
- 截圖：`screenshot_ui` 加 `--now "2026-06-21T20:00"`（只供測試，覆寫 schedule 用的時間），分別截白天與晚上。
- 人工：把 Windows 時間調到邊界前一分鐘，看是否準時切換；拒絕定位權限後按「Use my location once」，確認錯誤訊息。

### 10. 工作量與風險

S～M，約 3 天（不含 Windows 定位）；加 Windows 定位再 1 天。

風險：日出公式在高緯度誤差變大；Windows 定位要用 WinRT（C++/WinRT 或 WRL），可能牽動建置設定，所以標為可選。

---

## T3. 跟著 Agent 狀態變色

### 1. 目的與情境

切到別的視窗或專心看一個終端機時，眼角還是能察覺「有 Agent 在等我」。現有的提醒是聲音、toast 與狀態記號；T3 是更安靜的一層：整個介面的底色微微偏暖。要很收斂，不能讓人覺得畫面在閃。

### 2. 畫面或效果草圖

```
平常（Midnight）                          有 Agent 需要你
┌──────────┬────────────────────┐        ┌──────────┬────────────────────┐
│ sidebar  │ tab bar            │        │ sidebar  │ tab bar            │  ← 底色色相往暖色偏，
│ #101521  │                    │  ───▶  │ (略偏暖) │                    │     亮度不變
│          │  terminal（不變）   │ 1.2 s  │          │  terminal（不變）   │
│ accent ■ │                    │        │ accent ■ │（色相最多偏 12°）   │
└──────────┴────────────────────┘        └──────────┴────────────────────┘
                        需要你的狀態解除後，2.5 s 慢慢退回
```

設定：

```
Theme
  Tint by agent state   [ Off | Subtle ]     有 Agent 需要你時，介面帶一點暖色
  Also when done        [ ]                  有完成但還沒看的 Agent 時，帶一點冷色（強度減半）
```

### 3. 規格

觸發：每個 frame 從所有 session（含其他機器）取最急的 `Attention`（`attention_rank`）。`kBlocked` → 目標色相是 `needs_you` 的色相，強度 1；`ambient_done` 開啟且 `kDone` → 目標色相是 `done` 的色相，強度 0.5；其他 → 強度 0。

會變的 token：只有 `bg`、`sidebar`、`card`、`hover`、`selected`（底色類）與 `focus`。`text`、`dim`、`faint`、狀態色、`border`、`term_bg`、`term_fg`、ANSI 都不變：程式輸出的顏色不可以被改，狀態色也不能因為變色而互相靠近。

計算（強度 k ∈ [0, 1]，經過緩動後的值）：
1. 底色類：轉到 OKLab，保持 L 不變，把 (a, b) 往目標色相方向加一個向量，長度 `0.010 × k`（OKLab 單位），也就是 chroma 最多增加 0.010。亮度不變，所以與文字的對比幾乎不動。
2. `focus`：轉到 OKLCh，色相沿較短的方向往目標色相移動，最多 `12° × k`；L、C 不變。
3. 對比保護：算完後跑 `check_palette()`（見共用元件）。任何一對的對比如果低於「原本值與 AA 門檻（文字 4.5、圖形 3.0）兩者較小的那個」，就把 k 以二分法縮小（最多 6 次）直到通過。也就是說：絕不讓任何一對掉到 AA 以下；原本就低於 AA 的（例如 Midnight 的 `faint` 在 bg 上 2.97），不准再變得更低。

試算（Midnight）：`focus` 色相偏 10° 後在 card 上對比 5.39（原 5.38）；若用一般的混色把 bg 混入 4% 的 needs_you，bg 會從 `#0d111b` 變 `#16141f`（ΔE00 4.0），`faint` 對比從 2.97 掉到 2.86，所以不用混色，改用保持亮度的 OKLab 位移。

時間：
- 進場 1200 ms，退場 2500 ms，smoothstep 緩動。中途反轉時從目前的 k 接著走，不跳。
- 最短停留 2 秒：Agent 只 Blocked 半秒又恢復，仍完整做完進場再退場，避免一閃一閃。
- 漸變期間 `next_wakeup_ms()` 回傳 ≤ 33 ms（約 30 fps）；k 量化成 64 階，階數改變時才重算 palette。
- Reduce motion（T1 的 `reduce_motion` 生效）：不漸變，直接設成目標強度；最短停留仍是 2 秒。

不作用的情況：`ambient` 為 `off`（預設）；目前 palette 的 `shapes` 為 true（高對比主題）；Windows 對比主題開啟；T5 主題編輯器開著（避免預覽被干擾）。

### 4. 互動

- 設定 › Look › Theme：「Tint by agent state」segmented（Off／Subtle）、「Also when done」勾選框。
- 命令：`theme.ambient:off`、`theme.ambient:subtle`（「Tint by agent state: …」）。
- 快捷鍵：無。

### 5. 資料與架構

- `g_pal` 目前是 `const Palette*`，直接指向 palette 表。加一個模組層級的 `Palette g_ambient_pal`：T3 作用時，`frame.cpp` 把基礎 palette 複製過去、改顏色，再讓 `g_pal = &g_ambient_pal`；不作用時指回原表。`Palette` 裡的 `id`、`label` 是指向原表字串的指標，複製後仍有效。
- `apply_style()` 拆成 `apply_style()`（尺寸與圓角，只在 DPI、縮放改變時跑）與 `apply_style_colors()`（只設 `ImGuiStyle::Colors`），漸變期間只呼叫後者。
- 有些地方用 `color == g_pal->needs_you` 比較顏色（toast 秒數）；狀態色不變，所以不受影響。
- 新檔：`apps/desktop/ambient.cpp`（狀態機與套用）；計算放在 `color_math`。
- 不變：主題檔、`Palette` 欄位、host。

### 6. 設定

```json
{
  "ambient": "off",
  "ambient_done": false
}
```

### 7. 邊界情況

1. 很多 Agent 輪流 Blocked、解除：最短停留加上從目前 k 接續，畫面最多是「暖著不退」，不會閃。
2. 視窗在背景或最小化：不需要畫，`main.cpp` 現有的節流照舊；回到前景時直接跳到目標 k，不補播動畫。
3. 主題的 `needs_you` 色相跟底色色相很接近（例如 Ember 本身就是暖色）：位移方向幾乎與原色相同，看起來只是稍微飽和一點；這可以接受，不另外處理。
4. 淺色主題：OKLab 位移在接近白色時 sRGB 可能超出色域，先夾回 [0, 1] 再跑對比保護。
5. 截圖工具與 `--report`：報告裡另外記 `ambient_k`，方便判斷截圖時的狀態。
6. 遠端機器斷線，其 session 的狀態變成未知：不計入，避免斷線時一直帶暖色。

### 8. 驗收標準

1. Given `ambient` 為 subtle、Midnight，When 一個 session 變成 Blocked，Then 1.2 秒後 `focus` 的 OKLCh 色相與原本相差 ≤ 12°，`bg` 的 OKLab L 與原本相差 ≤ 0.001。
2. Given 同上，When 檢查所有 `check_palette()` 配對，Then 沒有任何一對低於 min(原本值, AA 門檻)。
3. Given Blocked 0.5 秒後解除，When 觀察 5 秒，Then 強度單調上升到峰值、維持到第 2 秒、再單調下降，中間沒有回升。
4. Given `reduce_motion` 為 on，When 變成 Blocked，Then 下一個 frame 就是最終顏色，中間沒有過渡 frame。
5. Given hc-dark，When 有 Blocked 的 session，Then 顏色完全不變。
6. Given 終端機正在顯示 ANSI 彩色輸出，When T3 作用，Then 終端機每一格的顏色與作用前相同。

### 9. 測試方式

- 單元測試：OKLab 來回轉換誤差 < 1e-6；保持亮度位移後 L 不變；色相移動走較短方向（350° 往 10° 是 +20° 而非 −340°），並被夾在 12°；對比保護在人為造出的低對比 palette 上會縮小 k；狀態機（進場、停留、退場、中途反轉、reduce motion）以假時鐘測試。
- 截圖：`screenshot_ui --run "theme.ambient:subtle"` 加上一個 Blocked 的 session，`--input "wait:1500;shot:warm.png"`，再用 `agentctl report-state` 改成 Idle，`wait:3000;shot:back.png`；比對 `back.png` 與一開始的畫面相同。
- 人工：放著工作 30 分鐘，主觀評估會不會分心；在三種色盲模擬圖上看暖色是否還能察覺（不要求能察覺，形狀與聲音才是主要提醒）。

### 10. 工作量與風險

M，約 4 天：`color_math` 的 OKLab 部分 0.5 天（共用元件另計）、狀態機與套用 1.5 天、拆 `apply_style()` 0.5 天、設定與測試 1.5 天。

風險：
- 主觀感受：太淡等於沒做，太濃會干擾。上線前要讓使用者實際試用調整 0.010 與 12° 這兩個數字。
- 每 33 ms 重設 ImGui 顏色的成本很低，但要確認 `apply_style()` 拆開後沒有漏掉的顏色。
- 與 herdr 的外觀一致性（ADR-15）：預設關閉，所以不影響。

---

## T4. 成套主題

### 1. 目的與情境

版面與配色一起設計，看起來才完整。例如 AgentDesk Mac 的桌布是用 `focus` 與 `info` 混出來的漸層（`mac_layout.cpp` 的 `wallpaper()`），配 Midnight 時偏紫，配其他主題就不一定好看。成套主題讓每個版面有一組建議的深、淺主題，第一次切到那個版面時自動套用，之後使用者照樣可以換。

### 2. 畫面或效果草圖

```
Theme
  For the layout you're in: each layout keeps its own light and dark theme.
  ┌──────────────────────────────────────────────────────────────┐
  │ ✦ Made for AgentDesk Mac: Dusk (dark) · Dawn (light)  [Use]  │   ← 目前不是建議組合時才出現
  └──────────────────────────────────────────────────────────────┘
  Dark
  ┌────────┐ ┌────────┐ ┌──────────┐
  │Midnight│ │ Ember  │ │ Dusk  ✦  │   ← 建議的主題卡右上角有小標記
  └────────┘ └────────┘ └──────────┘
```

版面卡片（Look › Layout）的縮圖改用該版面目前會用的主題來畫（現在用目前主題畫所有版面）。

### 3. 規格

資料模型：
- 版面檔加一個非強制的鍵 `suggested_themes`：`{"dark": "<id>", "light": "<id>"}`。它跟現有的 `theme` 鍵不同：`theme` 會蓋掉使用者的選擇（`effective_palette()` 第一條），`suggested_themes` 只是預設值。
- 內建版面的建議組合寫在 C++ 表裡（不改 herdr 的版面檔）：

| 版面 | dark | light |
|---|---|---|
| workspace（AgentDesk） | midnight | paper |
| terminals | preset-graphite | preset-paper |
| mac（AgentDesk Mac） | mac-dusk（新） | mac-dawn（新） |

- 新主題 mac-dusk、mac-dawn 以主題檔格式寫成（`background`、`foreground`、`selection`、`cursor`、`ansi[16]`、`colors{}`），放在新資料夾 `assets/themes/`，建置時跟 herdr 主題一起複製到執行檔旁的 `themes\`。實際配色要另外設計並跑 T1 的 `check_palette()`（文字 AA 以上、狀態色 3:1 以上），本草案不定色值（待確認）。

套用規則（`layout_theme_pair()`）：
1. `layout_themes` 有這個版面 → 用它（使用者選過）。
2. 否則版面有 `suggested_themes`（檔案或內建表），而且兩個 id 都存在 → 用建議值。
3. 否則用 `theme_dark`／`theme_light`。

這表示第一次切到 mac 版面時會自動用 Dusk／Dawn；使用者在那個版面選任何主題，就寫進 `layout_themes`，之後不再套用建議。現有使用者如果已經有 mac 的 `layout_themes`，維持原樣。

「Use」按鈕與 `theme.suggested` 命令：把建議值寫進 `layout_themes[目前版面]`。

狀態：
- 一般：顯示建議列。
- 空：版面沒有建議（例如使用者自訂版面沒寫）→ 不顯示建議列。
- 錯誤：建議的 id 不存在（主題檔被刪）→ 不套用、不顯示建議列，`g_settings_problem` 提示一次。

### 4. 互動

- 設定 › Look › Theme：建議列與「Use」按鈕；主題卡的小標記。
- 命令：`theme.suggested`（「Use the layout's suggested themes」）。
- 快捷鍵：無。`layout.switch`（Ctrl+Shift+L）切版面時，主題跟著版面走，這是現有行為。

### 5. 資料與架構

- `settings_store.cpp`：`layout_theme_pair()` 加第 2 步。
- `layouts.cpp`：`derive_layout()` 讀 `suggested_themes` 到 `LayoutSpec`；內建表放在同一檔。`layout_theme_pair()` 需要拿到 `LayoutSpec`，所以簽名改成多帶一個 `const LayoutSpec*`（或把建議值存進 `Settings` 的快取），待實作時決定。
- `libs/ui/src/layout.cpp`：讀檔時允許 `suggested_themes` 鍵（目前未知的根鍵是否被拒，待確認；看起來是忽略）。
- `settings_dialog.cpp`：建議列；`settings_pages.cpp`：版面縮圖用該版面的主題。
- 新檔：`assets/themes/mac-dusk.json`、`assets/themes/mac-dawn.json`；`apps/desktop/CMakeLists.txt` 多一個 copy_directory。
- 不變：`layout_themes` 的格式、版面檔的 `theme` 鍵、主題檔格式。

### 6. 設定

`ui.json` 沒有新鍵。版面檔（`%LOCALAPPDATA%\AgentDesk\layouts\<名稱>.json`）可加：

```json
{ "suggested_themes": { "dark": "mac-dusk", "light": "mac-dawn" } }
```

### 7. 邊界情況

1. 版面檔同時有 `theme` 與 `suggested_themes`：`theme` 優先（強制），建議列不顯示。
2. 使用者把 `themes\mac-dusk.json` 改壞：主題檔讀取失敗會被略過，建議 id 不存在，退回第 3 步。
3. 使用者自己做了同名的 `mac-dusk.json` 放在資料夾：資料夾的檔案贏（現有規則），建議仍指向這個 id，用的是使用者的版本。
4. `layout.reset` 重設 mac 版面：不清 `layout_themes`，主題維持使用者選的；要回到建議組合用「Use」。
5. 升級時現有使用者沒有 mac 的 `layout_themes`：下次切到 mac 會換成 Dusk／Dawn，外觀跟升級前不同。要在 What's new 寫一句（待確認是否要改成只對新安裝生效）。

### 8. 驗收標準

1. Given 新安裝、`layout_themes` 是空的，When 切到 AgentDesk Mac 而且 appearance 是 dark，Then 使用 mac-dusk。
2. Given 在 mac 版面選了 Midnight，When 切走再切回來，Then 仍是 Midnight，建議列出現「Use」。
3. Given 按下「Use」，When 存檔，Then `ui.json` 的 `layout_themes.mac` 為 `{"dark":"mac-dusk","light":"mac-dawn"}`。
4. Given 使用者版面檔寫了不存在的 `suggested_themes`，When 切到該版面，Then 用 `theme_dark`／`theme_light`，畫面不出錯，並提示一次。
5. Given mac-dusk 與 mac-dawn，When 跑 `check_palette()`，Then 文字 ≥ 4.5:1、狀態色 ≥ 3:1。

### 9. 測試方式

- 單元測試：`layout_theme_pair()` 的三步優先順序（含建議 id 不存在）；`layout_test.cpp` 加讀取 `suggested_themes` 的案例。
- `palette_check_test.cpp`：對 mac-dusk、mac-dawn 斷言 AA。
- 截圖：`screenshot_ui --run "layout:mac"`，深淺各一張，與 Midnight 配 mac 的截圖並排比較桌布。
- 人工：從舊版 `ui.json` 升級，確認既有選擇不被覆蓋。

### 10. 工作量與風險

S～M，約 2～3 天程式，加上 1～2 天配色設計（兩套主題要做截圖比較）。

風險：配色好不好看是主觀的，要先做截圖比較讓使用者選；升級後外觀改變（邊界情況 5）。

---

## T5. 主題編輯器

### 1. 目的與情境

現在要自訂主題只能手改 JSON，而且改完要重開 AgentDesk 才看得到。編輯器讓人在設定裡直接挑色、即時看到效果，並在對比不足時提醒，最後存成 `themes\<名稱>.json`（現有主題檔格式，手改也照樣能用）。

### 2. 畫面或效果草圖

從設定 › Look › Theme 的主題卡右鍵選單「Edit a copy…」或卡片列尾端的「＋ New theme」開啟。獨立的 modal，約 880 × 620 設計 px：

```
┌─ Theme editor ─────────────────────────────────────────────────────────────┐
│ Name [ my-midnight        ]  Based on: Midnight     (Dark)                 │
├───────────────────────────────────────┬────────────────────────────────────┤
│ Base                                  │  Preview                           │
│  ■ background   #0f131e               │  ┌────────┬──────────────────────┐ │
│  ■ foreground   #dee2ee   14.3 ✓      │  │Spaces  │ api ▲ │ web ◜ │ docs ✓│ │
│  ■ selection    #8b7cf6               │  │ api    ├──────────────────────┤ │
│  ■ cursor       (= foreground)        │  │ web    │ $ ls                 │ │
│ Interface                             │  │Agents  │ red green yellow ... │ │
│  ■ canvas       #0d111b               │  │ ▲ Needs you build            │ │
│  ■ chrome       #101521               │  │ ◜ docs                       │ │
│  ■ accent       #8b7cf6       5.5 ✓   │  │ ✓ test                       │ │
│  ■ muted        #8891aa       6.0 ✓   │  └────────┴──────────────────────┘ │
│  ■ dim          #565f78       2.9 ⚠   │  Simulate [ None | Protan | Deutan │
│ Status                                │            | Tritan ]              │
│  ■ blocked      #f0647a       6.1 ✓   │                                    │
│  ■ working      #e9b949      10.3 ✓   │  Checks                            │
│  ■ done         #5ccb9a       9.4 ✓   │  ⚠ dim on canvas 2.97 (< 4.5)      │
│  ■ idle         #8891aa       6.0 ✓   │  ⚠ blocked/done hard to tell apart │
│ Terminal colors                       │    for deuteranopia (ΔE 8.4)       │
│  ■■■■■■■■  ■■■■■■■■  (0–15)           │                                    │
├───────────────────────────────────────┴────────────────────────────────────┤
│ [Revert]                          [Cancel]  [Save as…]  [ Save ]           │
└────────────────────────────────────────────────────────────────────────────┘
```

草圖是從 Midnight（內建主題）複製的例子，每個值都是覆寫；從主題檔或 herdr preset 複製時，沒覆寫的介面色會標 ⛓。點色塊開 ImGui 的 `ColorPicker3`（popover），hex 欄可直接打字。⛓ 表示這個值由背景推算（沒有寫進檔案的覆寫）；改了就變成覆寫，旁邊出現「↺」可回到推算值。對比數字是該色在它主要背景上的比值，✓／⚠／✕ 依門檻。

### 3. 規格

檔案格式：沿用 `read_theme_file()` 讀的格式，不新增必要欄位。

```json
{
  "background": "#0d111b",
  "foreground": "#e2e6f2",
  "selection": "#8b7cf6",
  "cursor": "#e2e6f2",
  "ansi": ["#...", "... 共 16 個"],
  "colors": {
    "accent": "#8b7cf6",
    "blocked": "#f0647a",
    "working": "#e9b949",
    "done": "#5ccb9a"
  }
}
```

- `colors` 只寫與推算值不同的鍵，檔案保持精簡，背景改了推算值也會跟著動。
- 新增可選鍵 `info`（目前 `info` 固定等於 `accent`）。舊版 AgentDesk 讀到會忽略，不影響相容性。
- `hover`、`selected`、`border` 允許 `#rrggbbaa`（`parse_hex_color()` 已支援）；`background`、`foreground`、`selection`、`ansi` 只能 `#rrggbb`（`read_theme_file()` 的限制）。
- 從 AgentDesk 內建主題（midnight 等，`kPalettes`）複製時，它們沒有背景推算關係：每個 token 都寫成覆寫，`ansi` 用 VtEngine 的預設表填滿（內建主題 `ansi` 為 null）。
- 存檔位置：`%LOCALAPPDATA%\AgentDesk\themes\<名稱>.json`，先寫暫存檔再 `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`（跟 `settings_store.cpp` 一樣）。不寫執行檔旁的 `themes\`。

即時預覽：
- 編輯器持有一份 `OwnedPalette` 草稿；每次改值就用 `add_preset_palette()` 同樣的推算重建，`g_pal` 指向草稿，所以整個 App（包括編輯器背後的介面）都即時變色。預覽區另外畫一個縮小的範例：側欄、分頁列、四種狀態、一段 ANSI 彩色文字。
- 「Simulate」只套用在預覽區（用 `color_math` 的色盲矩陣轉換顏色後再畫），不影響整個 App。
- Cancel 或關閉：`g_pal` 指回原本的 palette。Save：寫檔，然後重新載入。

重新載入（目前主題檔每次執行只讀一次）：新增 `reload_theme(name)`。已存在的 `OwnedPalette` 就地更新顏色（deque 位址不變，`g_pal` 指標仍有效；`id` 與 `label` 字串不變，所以 `Palette` 內的指標不會失效）；新主題 `emplace_back` 到 deque 尾端。

驗證：

| 檢查 | 等級 | 行為 |
|---|---|---|
| 名稱空白、含 `<>:"/\|?*`、Windows 保留名（CON、NUL 等）、超過 64 字 | 錯誤 | Save 停用，名稱欄紅框 |
| 名稱與內建 id 相同（midnight、ember、moss、paper、hc-dark、hc-light、`preset-*`） | 錯誤 | 提示「已被內建主題使用」 |
| 名稱與現有主題檔相同 | 確認 | Save 時問是否覆寫 |
| hex 格式錯誤 | 錯誤 | 欄位紅框，保留上一個合法值預覽 |
| `foreground` 在 background 低於 4.5:1 | 警告 | 可以存，Checks 列出 |
| `muted`、`dim` 在 canvas、chrome、surface 低於 4.5:1 | 警告 | 同上 |
| 狀態色在 chrome、surface 低於 3:1 | 警告 | 同上 |
| 三個狀態色在任一色盲模擬下兩兩 ΔE00 < 10 | 提示 | 「色盲的人可能分不出 X 與 Y」，並建議開啟 Status shapes |
| `foreground` 在 background 低於 3:1 | 強警告 | 存檔前再確認一次 |

警告不擋存檔：主題是個人選擇，有人就是喜歡低對比。

狀態：
- 一般：如草圖。
- 空：沒有任何使用者主題時，「＋ New theme」先問要從哪一套開始（列出所有主題卡）。
- 讀取中：開啟現有檔案只是讀一個小 JSON，不顯示讀取狀態。
- 錯誤：檔案讀取失敗或格式不對（`read_theme_file()` 回傳 nullopt）→ 編輯器顯示「This file can't be read: <原因>」與「Open in Notepad」按鈕，不讓編輯；存檔失敗（權限、磁碟滿）→ 底部紅字顯示 Windows 錯誤訊息，草稿保留；開啟後檔案在外部被改（mtime 不同）→ Save 時問「檔案已在外部修改，要覆寫還是重新載入？」

### 4. 互動

- 開啟：主題卡右鍵選單「Edit a copy…」；自己的主題檔多一個「Edit…」與「Delete…」；卡片列尾端「＋ New theme」。
- 編輯器內：Tab／Shift+Tab 在欄位間移動；Ctrl+S 存檔；Esc 關閉（有未存的修改時先問）；Enter 在 hex 欄確認。
- 刪除：改名成 `<名稱>.json.old-<時間>`（與 `layout.reset` 相同做法），不直接刪；正在使用的主題被刪時，那個版面退回 `theme_dark`／`theme_light`。
- 命令：`theme.new`（「New theme…」）、`theme.edit`（「Edit current theme…」，目前主題是內建的就編輯一份複本）、`theme.folder`（「Open the themes folder」，同 `mods.folder`）。
- 快捷鍵：只有編輯器內的 Ctrl+S、Esc；全域不設預設值。

### 5. 資料與架構

- `themes.cpp`：把 `add_preset_palette()` 拆成純粹的 `derive_palette(PresetPalette, colors) -> 顏色表` 與「放進 deque」兩步，讓編輯器重用推算；新增 `reload_theme()`、`write_theme_file()`；`OwnedPalette` 移到 header 讓編輯器使用。
- 新檔：`apps/desktop/theme_editor.cpp`（modal、欄位、預覽、檢查列表）。
- `settings_dialog.cpp`：主題卡右鍵選單與「＋ New theme」。
- `commands.cpp`：三個新命令。
- 主題檔解析與推算可以移到 `libs/ui`（不碰 ImGui，用 0xRRGGBB），方便單元測試；`themes.cpp` 只負責轉成 `ImU32`。
- 不變：主題檔格式（只多一個可選的 `info`）、載入順序（資料夾的檔案蓋過隨附的同名檔）、`layout_themes`。

### 6. 設定

`ui.json` 不需要新鍵。編輯器的視窗大小與「Simulate」選擇只在本次執行有效。主題檔本身就是設定，位置在 `%LOCALAPPDATA%\AgentDesk\themes\`。

### 7. 邊界情況

1. 編輯的正是目前使用中的主題：預覽時 `g_pal` 指向草稿；Save 後就地更新 deque 裡那一筆並指回去。
2. 同時開了兩個 AgentDesk 視窗（同一資料夾）：另一個視窗不會自動重讀，下次啟動或執行 `mods.reload` 時才會（是否把主題檔也納入 `mods.reload`，待確認）。
3. 使用者手改的 JSON 有編輯器不認得的鍵：保留不動，存檔時原樣寫回（讀進 `nlohmann::json` 再只更新已知鍵）。
4. `colors` 裡寫了跟推算值剛好相同的覆寫：讀進來標為覆寫（不是 ⛓），存檔時保留，不擅自刪掉。
5. 背景從深色改成淺色：`light` 旗標跟著亮度變，主題卡從 Dark 區移到 Light 區；`layout_themes` 裡原本指向它的深色欄位不改，切到深色時仍會用它（可以接受，Checks 裡提示一句）。
6. 名稱含中文：檔名允許；`theme_id()` 對含「-」的名稱有舊版相容處理，名稱若是「xxx-graphite」這種形式會被當成 preset，所以名稱結尾不可以是「-graphite」「-ink」「-moss」「-paper」（列為驗證錯誤）。

### 8. 驗收標準

1. Given 從 Midnight 開一份複本，When 把 accent 改成 `#ff8800`，Then 不到一個 frame，側欄的選取框與勾選框就變色。
2. Given 編輯中，When 按 Cancel，Then 所有顏色回到開啟前，`themes\` 裡沒有新檔。
3. Given 名稱 `my-theme`，When 按 Save，Then 產生 `%LOCALAPPDATA%\AgentDesk\themes\my-theme.json`，能被現有的 `read_theme_file()` 讀取，而且主題卡立即出現，不用重開。
4. Given foreground 設成 `#444444`、background `#0d111b`，When 檢查，Then Checks 列出「foreground on background 1.94 (< 4.5)」，Save 前要再確認一次。
5. Given 名稱 `paper`，When 要存檔，Then Save 停用並顯示原因。
6. Given 存檔時磁碟拒絕寫入，When 按 Save，Then 顯示錯誤，草稿與預覽都保留，原本的檔案沒有被改壞。

### 9. 測試方式

- 單元測試：`derive_palette()` 與現在 `add_preset_palette()` 的輸出逐 token 相同（先寫測試鎖住現況再重構）；主題檔寫出再讀回，值相同；只寫與推算值不同的覆寫；未知鍵保留；名稱驗證（保留名、非法字元、內建 id、`-paper` 結尾）；`check_palette()` 的警告清單。
- 截圖：`screenshot_ui --run "theme.new" --input "..."` 開啟編輯器，截一張一般狀態、一張有警告的狀態；再用 `--input` 改一個色值後截圖，確認背景介面跟著變。
- 人工：用編輯器做一套主題，關掉 AgentDesk 用 Notepad 打開檔案，確認格式與手寫的主題檔一致；故意把檔案設唯讀再存檔。

### 10. 工作量與風險

L，約 8～10 天：拆出推算與重新載入 2 天、編輯器版面與欄位 3 天、預覽與色盲模擬 1.5 天、驗證與檢查列表 1 天、測試與截圖 1.5 天。

風險：
- 主題檔「每次執行只讀一次」是刻意的（`g_pal` 的位址穩定性）。改成可重新載入時，要確認沒有其他地方快取了 `Palette*` 以外的東西（例如 `mac_layout.cpp`、`terminals_layout.cpp` 的推算顏色是否每 frame 重算，待確認）。
- ImGui 的 `ColorPicker3` 外觀跟設定頁其他元件不一致，可能要包一層樣式。
- 規模最大，建議放在 T1 之後：T1 的 `color_math` 與 `check_palette()` 會直接被這裡重用。

---

## 共用元件

| 元件 | 位置（提案） | 內容 | 用到的項目 |
|---|---|---|---|
| `color_math` | `libs/ui/include/agentdesk/ui/color_math.hpp`、`libs/ui/src/color_math.cpp` | 以 0xRRGGBB 運算，不依賴 ImGui：sRGB 與線性互轉、WCAG 相對亮度與 `contrast()`、`mix()`、`best_on(bg)`（挑黑或白）、OKLab／OKLCh 互轉、保持亮度的色相位移、CIELAB 與 `delta_e2000()`、Machado 色盲模擬 `simulate(c, kProtan|kDeutan|kTritan)` | T1、T3、T5，T4 的配色檢查 |
| `check_palette()` | 同上，或 `libs/ui/src/palette_check.cpp` | 輸入一組 token 值，輸出每一對（文字類對各背景、狀態色對各背景與 selected、狀態色兩兩在三種色盲下的 ΔE00）的數值與是否過門檻 | T1 的測試、T3 的對比保護、T4 的新主題、T5 的 Checks 列表 |
| 主題資料表與推算 | `libs/ui`（從 `themes.cpp` 拆出） | hc 兩套的色值、`derive_palette()`、主題檔讀寫 | T1（測試可取得色值）、T4、T5 |
| `status_glyph()` 與 `glyph_of()` | `apps/desktop/chrome.cpp` | 依狀態畫形狀；判定「未知」 | T1；T5 的預覽區也用它畫範例 |
| `motion_reduced()` | `apps/desktop`（讀 `SPI_GETCLIENTAREAANIMATION`，2 秒節流） | 是否減少動態效果 | T1 的轉動弧、T3 的漸變 |
| 可替換的目前 palette | `frame.cpp` 的 `g_pal` 指派流程 | 基礎 palette → T5 預覽草稿（取代）→ T3 環境色（修改）→ `g_pal`；拆出 `apply_style_colors()` | T3、T5 |

建議順序：先做 T1（同時建好 `color_math` 與 `check_palette()`），T2、T4 各自獨立可隨時插入，T3 與 T5 放在後面重用前面的元件。
