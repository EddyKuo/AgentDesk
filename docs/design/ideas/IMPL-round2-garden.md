# 第二輪 C4 實作紀錄：Garden（Agent 小花園）

日期：2026-11-21｜分支：`feature/round2-garden`（從 `feature/ideas` 的 9c46158 開出）｜規格：`docs/design/ideas.md`「第二輪構想」C4，以及第二輪共用說明

這份紀錄寫給整合的人：做了什麼、和規格哪裡不同、新增的命令與檔案、建議寫進使用者指南的文字、還沒解決的事，以及動到哪些共用檔案。設計文件、PRD、使用者指南都沒有動。

## 做了什麼

新的內建版面 `garden`，顯示名稱「Garden」，登記在 Fun layouts（設定 › Look 的 Fun layouts 關著時不出現在版面卡片與 Ctrl+Shift+L 的輪替裡；`layout:garden` 一律可用）。版面檔只有兩塊：上方 42 px 的列（`garden-bar`）和下方的場景（`garden-scene`）。

### 場景

全部用 ImDrawList 的圓、橢圓、貝茲曲線與圓角矩形畫，沒有圖片，也沒有借用任何現成角色或圖案。顏色都從目前的色票推出來：

- 深色主題是夜晚：天空由深藍漸層到地平線，三十幾顆會輕輕閃的星星，一輪帶光暈的月亮。
- 淺色主題是白天：淡藍天空、太陽、兩朵慢慢飄的雲。
- 兩層和緩的山丘接到草地；草地顏色取自色票的 done（綠）混進背景色。散落的小花與草叢位置固定（同樣的場地大小每次都一樣），會避開小動物、牠的小路與名字。
- 上方列的底色就是天空頂端的顏色，看起來像同一片天空；左邊是一株小芽和「Garden」，接著是「需要你／工作中／完成」三個計數膠囊（只算 Agent 本身，不算子 Agent，和側欄一致），右邊是新增、設定、回到原本版面三個按鈕。

### 小動物

一種身體造型：圓滾滾的身體、肚子、兩隻腳、眼睛與腮紅。每隻 Agent 的不同之處都從 session id 決定（FNV-1a 雜湊，同一個 id 永遠長一樣）：

- 顏色：所在 space 的頭像顏色，依 id 稍微調亮或調暗。
- 耳朵：圓耳、尖耳、天線、頭上一株芽，四選一。
- 尾巴：沒有、捲尾、毛球，三選一；有的背上有兩個淡斑點。
- 身體寬度、動作起始的相位也各不相同，所以不會整群同步跳動。

姿勢只看結構化狀態（ADR-09，畫面文字只拿來顯示，不拿來判斷）：

| 狀態 | 姿勢 |
|---|---|
| Working | 沿著一條虛線小路來回小跳步，每次落地揚起一點塵土，臉朝前進方向 |
| Blocked（需要你） | 站著面向你、舉手揮動，頭上一個 needs_you 顏色的「!」對話框，嘴巴是小小的「o」 |
| Done | 狀態剛變成 Done 的 1.8 秒內開心彈跳並冒出閃光，之後坐下，瞇眼笑，閃光留一點 |
| Idle | 坐著睡覺，閉眼，「z z」慢慢往上飄 |
| Unknown | 安靜坐著，整隻半透明 |

狀態改變時，新舊姿勢用 420 ms 的 ease-in-out 互相過渡（每個姿勢拆成坐姿、睜眼、揮手、跳躍、移動、面向、透明度、對話框、閃光、笑眼、z 等數值，逐項內插），不會突然跳成另一個樣子。

### 實用的部分

- 每隻 Agent 下方有名字（和側欄相同的 `agent_detail`，沒有時用程式名稱）與 space 名稱，太長會加刪節號。子 Agent 不另外標名字。
- 滑鼠停在小動物上，旁邊浮出一張卡片：程式圖示與名字、程式與 space、狀態與持續多久、最後一則訊息（`clean_message` 清理過，最多兩行；需要你而沒有訊息時顯示問題類型），最下面一行提示「Click to open · right-click for more」。卡片用 `tok::card` 與 `tok::appear` 淡入。
- 點一下：回到原本的版面，並把焦點放在那個 Agent（細節見「和規格不同的地方」第 1 點）。沒有面板的子 Agent 改在 peek 視窗打開，花園留著。
- 右鍵：Open、Peek、Rename、Close pane，和 Mission Control 的格子選單同一組。專案裡沒有共用的「Agent 選單」，所以寫在 `garden_layout.cpp`。
- 排列：Agent 依建立時間排成交錯的幾列，每列置中，奇數列稍微往右偏，看起來像一片草地而不是表格；後排略小。欄數在 1～6 之間挑能讓小動物最大的那個；每列留出名字的高度；只有一兩隻時聚在場地中間，不會掉到最下緣；身高上限 68 px（設計單位）。超過 10 隻時天空讓出一些空間、名字只留一行，20 隻也放得下而且看得清楚（見截圖 `garden-many-*`）。
- 子 Agent 以較小的身形站在父 Agent 左右（先左後右），工作時一起跳。超過兩隻時，父 Agent 旁邊顯示「+n」。
- 沒有 Agent 時，草地中間立一塊木牌「No agents yet — start one with +」，底下一個 accent 按鈕「Start a program…」（`agent.new`）。
- agent-host 沒在跑或還在連線時，在場景上顯示共用的狀態卡（含 Retry）。

### 效能

- 視窗在前景、有東西在動（跳、揮手、彈跳、姿勢過渡中）時約每秒 30 格（33 ms）；只有睡覺的 Agent 時每秒 10 格；視窗不在前景時每秒 4 格（250 ms），而且小動物的時鐘放慢成 0.4 倍，4 格也不會顯得一頓一頓；全部坐著或沒有 Agent 時不要求任何畫格。
- `tok::reduce_motion()` 開著時完全靜止：每個姿勢畫成定格（跑步停在步伐中、手舉著、閃光與 z 固定），不要求畫格。
- 視窗被遮住時主迴圈本來就不畫（`DXGI_STATUS_OCCLUDED`）。
- 畫格對齊到時間格（例如 33 ms 一格），主迴圈每次醒來畫的兩格看到的是同一個瞬間。
- Garden 顯示時不再因為有 Working 的 Agent 而每 50 ms 喚醒一次（那是給旋轉狀態圖示用的，Garden 沒有）。
- 熱路徑不配置記憶體：誰在花園裡、名字字串、位置與花的位置每 200 ms（或場地大小改變時）才重建一次，向量重複使用容量；每格只讀狀態（map 查詢）、算姿勢、畫圖。半透明用的是直接調整這段畫過的頂點 alpha，不另開 draw list。懸停卡片的字串在換了一隻或每秒才重建。

## 新增的命令、按鍵、設定

| 種類 | 名稱 | 說明 |
|---|---|---|
| 命令 | `garden.toggle` | 切到 Garden；已經在 Garden 時回到原本的版面（共用 `toggle_layout`，記在 `layout_return`）。命令面板顯示為「Garden: your agents as little creatures」 |
| 命令 | `garden.open:<id>` | 打開該 Agent：回到原本的版面並聚焦；沒有面板的子 Agent 改開 peek |
| 版面 | `layout:garden` | 隨時可用，不受 Fun layouts 影響 |
| 按鍵 | 無預設 | 刻意不綁，理由見下 |
| `ui.json` | 無新鍵 | 沿用第一階段的 `fun_layouts` |

## 新增的檔案

- `libs/ui/include/agentdesk/ui/garden.hpp`、`libs/ui/src/garden.cpp`：純邏輯（外觀變化、狀態對姿勢、姿勢內插、排列、畫格頻率）。
- `libs/ui/layouts/garden.json`：版面檔。
- `apps/desktop/garden_layout.cpp`：場景、小動物、上方列、懸停卡片、右鍵選單、空白狀態、設定卡片的預覽圖、`garden.*` 命令、`garden_wakeup_ms`。
- `tests/unit/garden_test.cpp`：7 個測試。

## 和規格不同的地方

1. **點一下是「回到原本的版面並聚焦」，不是從右邊滑出面板**。我兩個都考慮過，選了前者：Garden 是用餘光看的畫面，塞進一個終端機面板會把場景切掉一半，而且版面引擎要每格改節點寬度才做得出滑入；原本的版面已經是你習慣的分割與分頁，點進去就能直接工作。回來的方法：上方列右邊的按鈕、命令面板的「Layout: Garden」，或自己在 keys.json 綁 `garden.toggle`。點開時會跳一則通知告訴你怎麼回來。
2. **沒有預設按鍵**。Garden 是彩蛋版面，同一輪的 Inbox、Stage、Pair 也可能要搶 Ctrl+Shift 的組合鍵，所以只提供命令，不佔按鍵。
3. **小動物依建立時間排，不依急迫程度排**。依急迫程度排的話，狀態一變整群就換位置，和「平靜、用餘光看」的目的相反；需要你的那隻已經有紅色「!」和上方的計數膠囊。
4. **Done 的彈跳只在「看到它變成 Done」時發生**。剛打開 Garden 時已經是 Done 的 Agent 直接坐著，不會一開畫面全部一起跳。
5. **空白狀態沒有用 `draw_empty_state`**，而是規格要的木牌加按鈕，畫在場景裡；連線失敗與連線中用共用的 `draw_state_card`。
6. **計數不含子 Agent**，和側欄、Mission Control 一致。

## 建議的使用者指南文字

### Garden

> Garden 把每個 Agent 畫成一隻小動物，住在一片小花園裡，適合開在第二台螢幕，用餘光就看得出誰在忙、誰在等你。它是好玩的版面：打開設定 › Look 的「Fun layouts」後會出現在版面卡片裡，Ctrl+Shift+L 也會輪到它；沒打開時，可以從命令面板輸入「Layout: Garden」切過去。
>
> 小動物的動作只看 Agent 回報的狀態：
>
> - 沿著小路跳步：工作中。
> - 面向你揮手、頭上有紅色「!」：需要你。
> - 開心跳一下、冒出閃光，然後坐下：完成。
> - 閉眼睡覺、飄著「z z」：閒置。
> - 半透明、安靜坐著：沒有狀態。
>
> 每隻小動物的顏色是它所在 space 的顏色，耳朵、尾巴這些小地方依 Agent 而不同，同一個 Agent 每次都長一樣。子 Agent 是旁邊比較小的那隻，超過兩隻時會顯示「+n」。
>
> 滑鼠停在小動物上會看到程式、狀態、持續多久和最後一則訊息。點一下會回到原本的版面並切到那個 Agent；右鍵可以 Peek、重新命名或關閉面板。要回花園，按上方列最右邊的按鈕，或在命令面板輸入「Layout: Garden」；想要快速鍵的話，在 keys.json 綁 `garden.toggle`。
>
> 深色主題是夜晚的花園，淺色主題是白天。視窗不在前景時動畫會放慢、降低更新頻率；Windows 的「動畫效果」關掉時，小動物都停在定格姿勢。

## 待解問題

1. **截圖工具的限制**：同時有其他 agent-ui 視窗在前景時，`screenshot_ui` 的 PrintWindow 偶爾抓到舊畫面。這次的截圖都逐張看過，內容正確。
2. **沒有截圖驗證的項目**：Windows 關閉動畫效果、高對比、125%／150% 縮放下的 Garden，只有程式與單元測試覆蓋。
3. **主迴圈每次醒來畫兩格**：這是主迴圈既有的行為（讓 ImGui 的 hover 收斂）。Garden 只控制醒來的頻率，並把時間對齊到時間格，所以兩格的畫面相同；GPU 實際上畫兩次。
4. **`ConPty.CtrlCByteInterruptsPing`**：在 agent 的 shell 裡可能失敗，依指示不處理（見文末測試）。
5. **Stage 取代 Mission Control 之後**：`garden_layout.cpp` 用了 `ui::mission::clean_message`，Stage 會保留 mission 的可重用邏輯，合併時確認這個函式還在。

## 改到的共用檔案（合併時注意）

- `libs/ui/src/layout.cpp`：`builtin_names()` 加 `garden`，`builtin_text()` 加 `kPresetGarden`（含 extern 宣告）。
- `libs/ui/CMakeLists.txt`：加 `src/garden.cpp`、`layouts/garden.json`。
- `libs/ui/src/layout_styles.cpp`／`.hpp`：`garden-bar`、`garden-scene` 兩個 mod 對到 `garden` style；顯示名稱「Garden」；拿掉「Garden 之後才加入」的註解。
- `libs/ui/src/command_names.cpp`：`garden.toggle`、`garden.open`。
- `libs/ui/src/layout_tips.cpp`：Garden 的提示卡，`card_keys()` 加 `garden`（在 classic 與 focus 之間）。
- `apps/desktop/style_layouts.cpp`／`.hpp`：`draw_style_block`、`style_layout_preview`、`exec_style_command` 分派到 Garden；宣告 Garden 的函式。
- `apps/desktop/app.cpp`：`next_wakeup_ms()` 加 `garden_wakeup_ms()`；Garden 顯示時不因 Working 而每 50 ms 喚醒。
- `apps/desktop/app_internal.hpp`：`kCommands` 加 `garden.toggle`。
- `apps/desktop/settings_dialog.cpp`：Fun layouts 列的說明改為「(AgentDesk Classic, Garden)」。
- `apps/desktop/CMakeLists.txt`：加 `garden_layout.cpp`。
- `tools/qa/screenshot_ui.cpp`：新增兩個測試用 profile（`state-cleared`：回報後清掉狀態，顯示為 Unknown；`state-done-later`：5 秒後變成 Done），`SHOT_EXTRA` 可寫 `profile:name:子資料夾`，讓不同 session 落在不同 space。
- 測試：`tests/CMakeLists.txt`、`layout_test.cpp`、`layout_tips_test.cpp`、`layout_styles_test.cpp` 的版面名稱清單加上 `garden`。

和其他第二輪分支相鄰的地方：`builtin_names()`、`builtin_text()`、`libs/ui/CMakeLists.txt` 的版面清單、`layout_styles.cpp` 的兩張表（陣列大小要跟著加總）、`command_names.cpp`、`card_keys()`、`exec_style_command` 的分派鏈、`app.cpp` 的 `next_wakeup_ms()`。合併時兩邊的名稱都留下即可。

## 截圖

在 `build\shots\garden\`（Debug；`--cwd build\shots\garden\cwd\demo`，裡面的 api、db、docs、lab 各是一個空的 git 儲存庫，當成四個 space）：

- `garden-dark.png`、`garden-light.png`：五種狀態都有（工作中兩隻、需要你、完成、閒置、沒有狀態），加一隻跟在父 Agent 旁的子 Agent。
- `garden-hover-dark.png`、`garden-hover-light.png`：懸停卡片。
- `garden-empty-dark.png`、`garden-empty-light.png`：空花園的木牌與按鈕。
- `garden-tips-dark.png`、`garden-tips-light.png`：Garden 的第一次使用提示卡。
- `settings-fun-dark.png`、`settings-fun-light.png`：設定 › Look 打開 Fun layouts 後，出現 AgentDesk Classic 與 Garden 兩張卡片。
- `garden-many-dark.png`、`garden-many-light.png`：20 個 Agent。
- `garden-click-open-dark.png`：點小動物後回到 AgentDesk 版面、聚焦該 Agent，並跳出「Back to the garden」通知。

## 測試

開工時 430 個（第一階段紀錄的數字）。新增 `Garden.*` 7 個：外觀由 id 決定、狀態對姿勢、姿勢內插、1～20 個的排列、子 Agent 跟隨父 Agent、畫格頻率、版面／命令／提示卡；另在 `LayoutStyles`、`Layout`、`LayoutTips` 既有測試裡補上 garden。完成後 437 個（Debug，`scripts\build.cmd msvc-x64-debug all`），全部通過。
