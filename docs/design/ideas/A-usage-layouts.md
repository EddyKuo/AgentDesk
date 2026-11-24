# 依用途設計的版面：A1～A4 設計草案

日期：2026-11-20｜狀態：設計草案（未排入）
這份草案依 `docs/design/ideas.md` 的「一、依用途設計的版面」展開。使用者選定其中一項後，才寫進設計文件（新的 ADR）與 PRD，成為需求。

**通用約定**

- 尺寸一律是設計 px（scale 1），畫的時候乘上 `g_scale`（程式裡的 `px()`、`fpx()`）。草圖以第一次開啟的視窗大小 1280×820 為準。
- 字型：介面用 Geist（`s.fonts.ui`、`ui_medium`、`ui_bold`），終端機文字用 JetBrains Mono（`s.fonts.mono`）。
- 顏色寫 `Palette` 的欄位名（`apps/desktop/app_internal.hpp`）：`bg`、`sidebar`、`card`、`border`、`focus`（強調色，版面 JSON 叫 `accent`）、`hover`、`selected`、`text`、`dim`、`faint`、`needs_you`、`done`、`working`、`idle`、`info`、`term_bg`、`term_fg`、`cursor`、`selection`。半透明寫成 `alpha(g_pal->X, 0xNN)`，混色寫 `mix(a, b, t)`，都是現有的 helper。
- 狀態標記沿用 `status_mark()`（`chrome.cpp`）：工作中是轉圈、需要你是「Needs you」膠囊或「!」。依設計文件 4.1，狀態不能只靠顏色。
- 這四個版面在 herdr-app 都沒有對應的東西（`docs/research/herdr-app/` 的研究筆記沒提到，這是觀察結果），所以是 AgentDesk 自己的設計；照 ADR-31 的做法，每個新版面都要先寫一條 ADR。
- 現有快捷鍵（`libs/ui/src/keymap.cpp` 的 `kGlobal`、`kTerminal`）已經用掉：Ctrl+Shift+T／N／K／W／X／O／E／Z／P／/／B／L／C／V／F、Ctrl+,、Ctrl+=／-／0、Ctrl+Tab、Ctrl+PageUp／PageDown（含 Shift）、Ctrl+1～9、Alt+1～9、Alt+[／]、Alt+方向鍵、Alt+H／J／K／L（含 Shift）、F2、Shift+F2、Ctrl+Backspace、Ctrl+←／→、Ctrl+V、Ctrl+Insert、Shift+Insert，以及 Ctrl+Alt+Shift+Esc。下面提的新快捷鍵都避開這些。
- 新命令都要加進三個地方：`kCommands`（命令面板，`app_internal.hpp`）、`is_builtin_command` 的名稱表（`libs/ui/src/command_names.cpp`），以及說明視窗。

---

## A1. Mission Control（監看牆）

### 1. 目的與情境

給同時跑 4～8 個 Agent 的人用，例如在幾個 worktree 各放一個 Claude Code 或 Codex，分頭處理不同的 issue。現有三個版面一次只顯示一個分頁；要知道其他 Agent 在做什麼，只能看側欄 Agents 清單的一行字，或一個一個切過去。監看牆把所有 Agent 排成格子，每格是程式圖示、名稱、狀態與最後幾行輸出，需要你的那格亮框並排到最前面。點一格就放大成一般的終端機操作，做完再回到牆面。

### 2. 畫面草圖

六個 Agent、視窗 1280×820：

```
x=0                                                                                       x=1280
┌──────────────────────────────────────────────────────────────────────────────────────────────┐ y=0
│ ●●●  Mission Control   [! 2 Needs you] [◌ 3 Working] [✓ 1 Done]        [All|Active]  +  ⚙  ⇄ │ mission-bar 42
├──────────────────────────────────────────────────────────────────────────────────────────────┤ y=42
│ 12                                                                                           │
│  ┏━ tile A 410×355 ━━━━━━━━━━━┓  ┏━ tile B ━━━━━━━━━━━━━━━━━━━┓  ┌─ tile C ─────────────────┐ │
│  ┃▣ Claude Code      [Needs you]┃  ┃▣ Codex            [Needs you]┃  │▣ Claude Code        ◌    │ │ header 34
│  ┃  api · fix-auth          4m ┃  ┃  web · login-form        1m ┃  │  docs · readme       12s │ │
│  ┃┌───────────────────────────┐┃  ┃┌───────────────────────────┐┃  │┌────────────────────────┐│ │
│  ┃│ Edit src/auth/token.ts    │┃  ┃│ $ npm test                │┃  ││ Reading docs/setup.md  ││ │ body
│  ┃│ Allow this edit? (y/n)    │┃  ┃│ 3 failing                 │┃  ││ ...                    ││ │ (尾端輸出)
│  ┃│ ...                       │┃  ┃│ ...                       │┃  ││                        ││ │
│  ┃└───────────────────────────┘┃  ┃└───────────────────────────┘┃  │└────────────────────────┘│ │
│  ┃ Permission: Edit file       ┃  ┃ Question                    ┃  │ +2 sub-agents            │ │ footer 24
│  ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛  ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛  └──────────────────────────┘ │
│  12                                                                                          │
│  ┌─ tile D ───────────────────┐  ┌─ tile E ───────────────────┐  ┌─ tile F ─────────────────┐ │
│  │ ...  Working               │  │ ...  Done ✓                │  │ ...  Idle                │ │
│  └────────────────────────────┘  └────────────────────────────┘  └──────────────────────────┘ │
├──────────────────────────────────────────────────────────────────────────────────────────────┤ y=792
│ This PC · main · 6 agents · 2 need you                                          Ctrl+Shift+P │ workspace-status 28
└──────────────────────────────────────────────────────────────────────────────────────────────┘ y=820
```

計算：牆面區 1280×750，四周 padding 12，格距 12，三欄兩列：(1256 − 2×12) ÷ 3 ≈ 410 寬，(726 − 12) ÷ 2 = 357 高。

放大一格之後（同一個版面，stack 換到 `main`）：

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ ●●●  ← Wall   Claude Code · api · fix-auth   [Needs you]   Ctrl+Shift+M   ⇄  │ mission-bar 42（放大模式）
├──────────────────────────────────────────────────────────────────────────────┤
│  ┌─ 一般面板（現有 tab 區塊：分割、標題列、輸入都照舊）───────────────────┐  │ padding 8
│  │                                                                        │  │
│  └────────────────────────────────────────────────────────────────────────┘  │
├──────────────────────────────────────────────────────────────────────────────┤
│ status 28                                                                    │
└──────────────────────────────────────────────────────────────────────────────┘
```

圖例：`┏━┓` 粗框是需要你（2 px `needs_you`）；`▣` 是程式圖示；`◌` 是工作中的轉圈；`[All|Active]` 是篩選；`⇄` 是回到原本的版面。

### 3. 元件規格

**mission-bar（42 px）**

| 項目 | 規格 |
|---|---|
| 底色 | `g_pal->sidebar`，底部 1 px `g_pal->border` |
| 左側 | 圓形視窗按鈕讓位（`clear_of_traffic_lights(s, x, top, bottom, 12)`），標題「Mission Control」Geist 13 semibold `text` |
| 計數膠囊 | 高 22、圓角 11、左右內距 8、間距 6；字 Geist 12 medium。需要你：底 `alpha(needs_you, 0x26)`、字 `needs_you`、前綴「!」；工作中：底 `alpha(working, 0x26)`、前綴靜止的空心圓（不轉，避免整條列一直重畫）；完成：底 `alpha(done, 0x26)`、前綴「✓」。數量為 0 的膠囊不畫 |
| 計數膠囊狀態 | hover：底的 alpha 提高到 0x40；按下：當成篩選（只看這一種），再按一次取消；選中時加 1 px 同色框 |
| 篩選 | `segmented()`（設定頁的元件）兩段：All、Active（Active = 需要你＋工作中＋完成），高 26 |
| 右側按鈕 | `icon_button()` 28×28、圖示 15：「+」開新增選單（`draw_new_menu`）、齒輪 `app.settings`、⇄ `mission.toggle`（回原版面）。tooltip 帶快捷鍵（`keymap.chord_for`） |
| 放大模式 | 標題換成「← Wall」按鈕（Geist 13 medium `dim`，hover `text`）＋ Agent 名稱、space 與狀態膠囊；計數膠囊縮成只剩需要你的數量 |
| 拖曳 | 沒按到東西的地方可拖曳移動視窗（版面的 `window_drag`），雙擊最大化 |

**mission-wall（牆面區）**

| 項目 | 規格 |
|---|---|
| 格子大小 | 依數量自動排：1 個 1×1、2 個 2×1、3～4 個 2×2、5～6 個 3×2、7～9 個 3×3、10～12 個 4×3；格子最小 240×150，放不下就減欄，再放不下就垂直捲動。上限 4 欄 |
| 間距 | padding 12、格距 12 |
| 捲動 | 超出時垂直捲動，捲軸 6 px `alpha(dim, 120)`；滾輪每格捲 1 列 |

**tile（單格）**

| 部分 | 規格 |
|---|---|
| 外框 | 圓角 10；底 `g_pal->card`；1 px `g_pal->border` |
| header 34 | 左內距 12：程式圖示 16（`program_icon()`，程式表的顏色）、名稱 Geist 13 semibold `text`（`title_of`）；第二行 Geist 11.5 `dim`：space 名稱 · 工作名稱（`agent_detail`）。右側：`status_mark(..., compact=false)`，下面是距離上次變動多久（`changed` 的 `at`），Geist 11 `faint` |
| body | 內縮 8、圓角 6，底 `g_pal->term_bg`；最後 N 行輸出，JetBrains Mono 11 px、行高 15、字色照格子的前景色（預設 `term_fg`，ANSI 色走主題的 `ansi` 表），超出寬度直接裁掉不換行。N = 依 body 高度能放下的行數，最多 12 |
| footer 24 | Geist 11.5 `dim`：OSC 7501 的訊息（`agent_message`，先中和 bidi 與不可見字元）；沒有訊息時，blocked 寫 kind（Permission／Question／Auth），有子 Agent 時寫「+N sub-agents」 |
| normal | 如上 |
| hover | 底 `mix(card, hover, 1)`、框 `alpha(focus, 120)`、游標變手指 |
| selected（鍵盤選取） | 2 px `g_pal->focus` 框 |
| 需要你 | 2 px `needs_you` 框，外加 4 px `alpha(needs_you, 0x26)` 光暈；排到最前面 |
| 完成（Done） | 1 px `done` 框，狀態膠囊「Done ✓」 |
| 已結束／Lost | 整格 alpha 降到 50%；header 右側改寫「Exited 0」或「Lost」（`faint`） |
| loading（還沒有畫面） | body 放 3 條 `alpha(faint, 0x30)` 的灰條，寬 70%、50%、60%，每 1.2 秒明暗一次 |
| error（那台機器斷線） | body 保留最後的畫面但蓋一層 `alpha(bg, 0xA0)`，中央寫「Machine unreachable」Geist 12 `needs_you` |

**整個牆面的狀態**

| 狀態 | 畫面 |
|---|---|
| empty（0 個 Agent） | 牆面中央一張卡（沿用 `draw_no_tab` 的排法）：標題「No agents running」Geist 18 semibold，說明「Start Claude Code or Codex in a terminal and it shows up here.」Geist 13 `dim`，兩顆按鈕：Start a program…（`agent.new`）、New terminal（`tab.new`） |
| empty（篩選後沒有） | 「Nothing needs you」＋「Show all」連結（切回 All） |
| loading | host 還沒連上或 UI 文件還沒到（`machine_loading("")`）：三格骨架 tile（只有外框與灰條），上方一行「Connecting to agent-host…」 |
| error | host 連不上且 `last_error()` 不是空的：中央卡寫錯誤內容與「Retry」（`request_start_host`） |

### 4. 互動

**滑鼠**

- 點一格：`mission.expand:<session>`。那個 Agent 所在的分頁成為目前分頁、焦點給它，stack `wall-body` 換到 `main`。
- 子 Agent（沒有自己的面板）：點格子開現有的 peek 視窗（`agent.peek:<id>`），不切到 `main`。
- 右鍵格子：沿用面板的右鍵選單（`pane_menu_rows`），另外加「Expand」與「Peek」。哪些列適用待確認。
- 點計數膠囊：只看這一種狀態；點 All／Active 切換篩選。
- 放大模式下點「← Wall」：`mission.wall`。

**鍵盤**

| 按鍵 | 動作 | 說明 |
|---|---|---|
| Ctrl+Shift+M | `mission.toggle` | 在其他版面：切到監看牆。在牆面：回到原本的版面。在放大模式：回到牆面 |
| ←↑→↓、Home、End | 移動選取格 | 只在牆面拿到鍵盤焦點時（放大模式下鍵盤屬於終端機） |
| Enter | 放大選取格 | 同上 |
| 1～9 | 放大第 N 格 | 同上；不用 Ctrl，所以和 `tab.select` 不衝突 |

**Esc 不回到牆面**。ideas.md 寫「再按一次或 Esc 回到牆面」，但放大後鍵盤屬於終端機，Claude Code 用 Esc 中斷目前的動作，攔下 Esc 會讓使用者沒辦法中斷 Agent。所以放大模式只用 Ctrl+Shift+M 或「← Wall」回去；牆面本身沒有可以回去的地方，Esc 在牆面不做事。這點和 ideas.md 不同，排入前要請使用者確認。

**新命令**

| 命令 | 參數 | 作用 |
|---|---|---|
| `mission.toggle` | 無 | 見上表 |
| `mission.expand` | session id | 放大這個 Agent |
| `mission.wall` | 無 | 回到牆面 |
| `mission.filter` | `all`、`active`、`blocked`、`working`、`done` | 篩選 |

與現有命令的關係：切版面用的是 `layout.switch:mission`，放大用的是 `layout.select`（`{"stack":"wall-body","child":"main"}`）加上 `agent.focus`；`mission.*` 只是把這些包起來，並記住從哪個版面來。通知（toast）在這個版面點下去時改跑 `mission.expand`，不用 `agent.focus`。

### 5. 資料與架構

**現有資料**

| 資料 | 位置 |
|---|---|
| 所有 session 與畫面快照 | `App::State::sessions`、`by_id`（`ui::RemoteSession`）；`RemoteSession::snapshot()` 回傳快取的 `shared_ptr<const terminal::ScreenSnapshot>`。所有 session 都已經 attach 並持續收增量（`HostClient::refresh_sessions`），監看牆不會增加 IPC |
| 狀態、訊息、子 Agent | `ui::SessionView`：`agent_state`、`agent_kind`、`agent_message`、`parent`、`process_state`、`lost` |
| 注意力（Done＝沒在看時完成） | `State::attention`（`ui::AttentionTracker`），由 `update_attention`（`sessions.cpp`）每幀更新 |
| 排序 | `ui::priority_before`、`AgentSortKey`（`workspace.hpp`），側欄 Agents 已經在用；上次變動時間在 `State::changed` |
| 程式名稱與圖示 | `program_of()`、`program_icon()`（`programs.cpp`） |

**要新增的東西（全部在 UI）**

監看牆只讀取 host 已經提供的資料，host 不用改。

1. `libs/ui/include/agentdesk/ui/mission.hpp`（純邏輯，可單元測試）：
   - `GridShape wall_grid(int count, float w, float h, float gap, float min_w, float min_h, int max_cols = 4)`：欄數、列數、格子大小、要不要捲動。
   - `std::vector<TailLine> screen_tail(const terminal::ScreenSnapshot&, int max_lines)`：從畫面底部往上取最後 `max_lines` 個非空白列，每列切成同色的段（文字、前景色或 ANSI 編號）。`sessions.cpp` 的 `report_failed_start` 現在自己取最後 5 行，可以改用這個函式。
   - `std::vector<std::string> wall_order(...)`：需要你 → 完成 → 工作中 → 閒置 → 其他，同組內依 `priority_before`；子 Agent 不單獨成格（父 Agent 已不存在時例外）。
2. `apps/desktop/mission_layout.cpp`：畫 `mission-bar` 與 `mission-wall`；`frame.cpp` 的 `draw_layout_block` 多兩個 mod 名稱分支；`layouts.cpp` 的 `derive_layout` 看到這兩個名稱時把 style 設成 `mission`。
3. `App::State` 加 `struct MissionState { std::string came_from; std::string selected; std::string filter = "all"; std::map<std::string, TileCache> tiles; float scroll = 0; } mission;`。`TileCache` 存 `seq`、取出的尾端行與上次更新時間。
4. `update_attention`：牆面顯示時（stack 在 `wall`），`visible` 要是空的，因為看到小格子不算「看過」，Done 不能因此被清掉。現在它直接拿 `ws.active_tab()`，跟目前畫面上實際畫了什麼無關。這個改法見文末「共用元件」第 5 項。

**版面檔 `libs/ui/layouts/mission.json`**（和 `mac.json` 一樣建置時編進程式，`builtin_names()` 加上 `mission`，顯示名稱 Mission Control）：

```json
{
  "id": "root", "type": "column", "titlebar": "none",
  "traffic_lights": [12, 15], "background": "canvas",
  "children": [
    { "id": "bar", "type": "block", "size": { "px": 42 }, "window_drag": true,
      "content": { "mod": "mission-bar" } },
    { "id": "wall-body", "type": "stack", "active": "wall",
      "children": [
        { "id": "wall", "type": "block", "padding": 12, "content": { "mod": "mission-wall" } },
        { "id": "main", "type": "block", "padding": 8,
          "content": { "tab": "active",
            "pane": { "gap": 8, "radius": 10, "background": "background", "lone": true,
              "border": { "width": 1, "color": "border" }, "focus": { "width": 1, "color": "accent" },
              "header": { "content": { "mod": "pane" }, "height": 30 } } } }
      ] },
    { "id": "status", "type": "block", "size": { "px": 28 }, "content": { "mod": "workspace-status" } }
  ]
}
```

stack 沒有 header，所以不會畫分頁條。`layout.select` 的結果會存進 `layouts\.state\mission.json`；切進這個版面時一律重設成 `wall`，不讓上次停在放大模式的狀態留下來。`mission-wall` 也可以放進使用者自己的版面（例如 workspace 的右側面板），這時沒有 `wall-body` stack，點格子就只跑 `agent.focus`。

**效能**

- 每格**不建** `TerminalView`，不呼叫 `resize`、`take_control`，所以不會改變 ConPTY 大小（控制者才決定尺寸，設計文件 3.7）。放大時用的是現有 tab 區塊與 `State::views` 裡同一個 `TerminalView`，回到牆面不留多餘的物件。
- 尾端行快取：`snapshot()->seq` 沒變就不重算；有變也最多每 250 ms 重算一次（每格 4 Hz）。
- 繪製量：最多畫看得到的格子（捲出畫面的跳過），12 格 × 12 行 × 約 120 字 ≈ 1.7 萬字；同色的連續格子合成一次 `AddText`。
- 喚醒：host 的增量本來就會叫醒 UI，監看牆不多加計時器；工作中的轉圈已經讓 `next_wakeup_ms` 降到 50 ms（現有行為）。視窗被蓋住時不畫（`g_gfx.occluded`）。
- 量測目標：Release、12 個 `vt_probe flood`，一幀 ≤ 8 ms（目標機器待確認）。

### 6. 設定

| `ui.json` 鍵 | 預設 | 說明 |
|---|---|---|
| `mission_filter` | `"all"` | 上次的篩選 |
| `mission_sub_agents` | `false` | `true` 時子 Agent 也各成一格（排在父 Agent 後面，header 加「↳」） |
| `mission_shells` | `false` | `true` 時沒有狀態來源的終端機（`is_agent()` 為 false）也列出，狀態顯示 Unknown |

設定 › Look 的版面卡片要多一張監看牆的縮圖（`layout_preview()`）。

### 7. 邊界情況

1. **0 個 Agent**：顯示 empty 卡；有終端機但都不是 Agent 時，卡片多一行「3 terminals without agent status — show them」，點了等於暫時打開 `mission_shells`。
2. **30 個 Agent**：4 欄、格子保持最小 240×150 以上，垂直捲動；需要你的永遠在第一列。計數膠囊讓使用者一鍵只看需要你的。
3. **Claude Code 的畫面底部是輸入框和狀態列**：「最後幾行」可能只看到輸入框，看不到輸出。對策：取尾端行時跳過只有框線字元（U+2500～U+257F）和空白的列。這只影響顯示、不影響狀態判斷，所以不違反 ADR-09；實際效果要用真的 Claude Code 確認（待確認）。footer 的 OSC 7501 訊息當作主要資訊。
4. **alternate screen（全螢幕程式）**：直接取畫面上最後 N 個非空白列，不讀歷史。
5. **遠端機器斷線**：格子保留最後的畫面，蓋上「Machine unreachable」；斷線的機器自成一組，排在最後。
6. **子 Agent**：預設不成格，父格 footer 顯示「+N sub-agents」，點了開 peek。父 Agent 已結束但子 Agent 還在跑時，子 Agent 自己成一格。
7. **收起的分頁（settled）**：裡面的 Agent 照樣成格；放大時分頁回到分頁列（和 picker 選到收起分頁的行為一致，實際是否自動 unsettle 待確認）。
8. **很小的視窗**（寬 < 540）：降成 1 欄；高度放不下一格 150 時，body 只留 2 行。
9. **高 DPI**：所有尺寸經 `px()`；字最小 11 px，在 150% 是 16.5 實際像素。格子最小尺寸也是設計 px，所以 4K 上一樣是 4 欄。
10. **同一個 Agent 被另一個 UI 控制**（`controlled_elsewhere`）：格子照常顯示；放大後由現有的面板橫幅說明唯讀。

### 8. 驗收標準

1. Given 有 6 個 Agent 在跑，其中 2 個 Blocked，When 按 Ctrl+Shift+M，Then 畫面變成 3×2 的格子，兩個 Blocked 的排在第一列，並有 2 px `needs_you` 框與「Needs you」標記。
2. Given 牆面顯示中，When 點一個工作中的格子，Then 那個 Agent 以一般面板顯示、拿到鍵盤焦點，打字會送到它；按 Ctrl+Shift+M 回到牆面，且終端機大小沒有因為牆面而改變（`session.list` 的 cols／rows 在進出牆面前後一致）。
3. Given 一個 Agent 在牆面上變成 Done，When 使用者只看牆面、沒有放大它，Then 它維持 Done；放大後才變成 Idle。
4. Given 12 個 Agent 持續大量輸出（`vt_probe flood`），When 監看牆顯示 1 分鐘，Then Release 版一幀不超過 8 ms，記憶體沒有隨時間增加。
5. Given 沒有任何 Agent，When 切到監看牆，Then 顯示「No agents running」與兩顆按鈕，按 Start a program… 開啟現有的啟動視窗。
6. Given 放大模式下 Claude Code 在跑，When 按 Esc，Then Esc 送給 Claude Code，畫面停在放大模式。

### 9. 測試方式

- **單元測試**（`tests/unit/mission_test.cpp`）：`wall_grid` 在 0、1、2、4、6、9、12、30 個與各種視窗大小下的欄列數與是否捲動；`screen_tail` 對空白列、寬字元、只有框線的列、alt screen 快照、ANSI 色分段；`wall_order` 的分組與排序、子 Agent 收進父格、孤兒子 Agent 自成一格。
- **截圖**（`build\msvc-x64-debug\tools\qa\screenshot_ui.exe`）：
  - `--run layout:mission` 加上 `SHOT_EXTRA` 開 6 個 session，檢查格子排法。
  - 狀態要靠 `agentctl report-state` 設定。建議 screenshot_ui 新增 `--state <名稱>:<state>`，在 UI 啟動前對隔離的 host 送 report-state（工具改動，不出貨）。
  - `--input "click:220,200;wait:500;shot:expanded.png;key:..."` 檢查放大與回到牆面（回牆面要送 Ctrl+Shift+M；`--input` 目前不支援組合鍵，要擴充 `key:` 的寫法，待確認）。
  - 0 個 Agent、host 連不上（`--pipe` 指向不存在的管線）各截一張。
- **人工**：真的 Claude Code 與 Codex 各 2 個，確認尾端行看得懂、Blocked 的格子一眼就看得到；放大後按 Esc 能中斷 Claude Code；4K 150% 與 1080p 100% 各看一次。

### 10. 工作量與風險

- **大小：L，約 6～8 天**（純邏輯與測試 1.5、牆面與 tile 繪製 2.5、版面檔與命令 1、注意力改動 0.5、截圖工具與驗收 1.5）。
- **主要風險**：小格子能不能看懂。Claude Code 的畫面底部是 UI 元件，最後幾行不一定是有用的輸出，要先用真的 Agent 試一個雛形再決定取哪幾行。其次是大量輸出時的效能，靠快取與 4 Hz 上限控制。
- **依賴**：共用元件 1、2、4、5；主題 T1（狀態形狀）做了之後，格子直接沿用。

---

## A2. Focus（專注模式）

### 1. 目的與情境

給自己在寫程式、偶爾看一下 Agent 的人。側欄、分頁列、右側面板和通知都會分散注意力；Focus 把畫面收成一個終端機，底下一條 22 px 的細列，用小點表示其他 Agent 的狀態。有 Agent 需要你時，視窗邊緣輕輕亮一下，不跳通知、不出聲。Focus 做成**任何版面都能切換的模式**，不另外做版面：切換時保留各版面自己的主題，也不用在版面清單裡多一項。

### 2. 畫面草圖

```
x=0                                                                                   x=1280
┌──────────────────────────────────────────────────────────────────────────────────────┐ y=0
│ (滑鼠移到這裡 30 px 內才出現面板標題列：▣ pwsh · ~/api · main      ⊟ ⤢ ✕)           │ hover header 30
│                                                                                      │
│  12                                                                                  │
│    PS C:\code\api> npm run build                                                     │
│    ...                                                                               │ 終端機（唯一的面板）
│                                                                                      │ 左右 12、上 8
│                                                                                      │
│                                                                                      │
├──────────────────────────────────────────────────────────────────────────────────────┤ y=798
│ ▣ pwsh · api                         ● ● ◯ ◯ ·  +3                     Exit focus ⌃⇧. │ focus-strip 22
└──────────────────────────────────────────────────────────────────────────────────────┘ y=820

需要你時（約 1.5 秒）：
┌══════════════════════════════════════════════════════════════════════════════════════┐ ← 3 px needs_you 內框
║                                                                                      ║   淡入 150 ms、停 400 ms、淡出 900 ms
╚══════════════════════════════════════════════════════════════════════════════════════╝
```

圖例：●（實心 `needs_you`）需要你；●（實心 `done`）完成；◯（空心 `working`）工作中；·（`faint`）閒置；`+3` 是放不下的數量。

### 3. 元件規格

**終端機面板**

| 項目 | 規格 |
|---|---|
| 位置 | 佔滿視窗扣掉 strip；終端機內距左右 12、上 8、下 6 |
| 外框 | 無框、無圓角；底 `g_pal->term_bg` |
| 標題列 | 平常不畫；滑鼠在面板上緣 30 px 內停 300 ms 才淡入，內容與按鈕同現有面板標題列（`draw_default_pane_header`）；離開後 600 ms 淡出 |
| 只顯示哪個 | 目前焦點的面板（`ws.focused_session()`）。只在繪製時這樣處理，**不改** `Tab::zoomed`，所以 UI 文件不會因為切 Focus 而改變 |

**focus-strip（22 px）**

| 部分 | 規格 |
|---|---|
| 底色 | `g_pal->sidebar`，上緣 1 px `g_pal->border` |
| 左側 | 程式圖示 14 ＋ 名稱 · space，Geist 12 medium `dim`，最多 40% 寬，超出裁掉 |
| 小點 | 直徑 8、間距 6、垂直置中；需要你：實心 `needs_you`；完成：實心 `done`；工作中：1.5 px 空心 `working`（不轉）；閒置：實心 `faint`、直徑 6；Unknown 不畫。目前焦點的 Agent 不在點裡 |
| 小點順序 | 需要你 → 完成 → 工作中 → 閒置（`priority_before`）；放不下就以「+N」（Geist 11 `faint`）收尾 |
| 小點 hover | 點放大到 10；tooltip：「Claude Code · api · Needs you · 4m」 |
| 小點點擊 | `agent.focus:<id>`，換成那個 Agent，仍在 Focus |
| 右側 | 「Exit focus」＋快捷鍵，Geist 11.5 `faint`，hover `text`；點了 `focus.toggle` |
| 拖曳 | 其餘地方可拖曳移動視窗，雙擊最大化 |

**邊緣光**

| 項目 | 規格 |
|---|---|
| 形狀 | 視窗內緣 3 px 的框（`AddRect`，圓角 0），畫在所有內容之上、對話框之下 |
| 需要你 | `needs_you`，alpha 0 → 0xB0（150 ms）→ 停 400 ms → 0（900 ms），只亮一次 |
| 完成 | `done`，最高 alpha 0x60，其他同上 |
| 重複 | 同一個 Agent 要等狀態再變一次才會再亮；1.5 秒內有第二個事件時重新計時，不疊加 |

**畫面狀態**

| 狀態 | 畫面 |
|---|---|
| empty（沒有任何分頁） | 中央卡：「No terminal to focus」＋ New terminal（`tab.new`）、Start a program…（`agent.new`）；strip 照常顯示其他 Agent |
| loading | host 連線中：沿用 `draw_no_tab` 的「Connecting to agent-host...」；strip 左側寫「Connecting…」 |
| error | 焦點的終端機已結束或 Lost：現有面板橫幅（`draw_pane_banner`）照常顯示；host 斷線：同 `draw_no_tab` 未連線畫面，strip 左側 `needs_you` 色「agent-host not connected」 |

### 4. 互動

| 操作 | 結果 |
|---|---|
| Ctrl+Shift+.（句點） | `focus.toggle`：進入或離開 Focus。Ctrl+Shift+. 目前沒有綁定；有人反映不好按時，備案是 F11（待確認） |
| Alt+↓／Alt+↑ | 現有的 `agent.next`／`agent.prev`，在 Focus 裡一樣換 Agent，畫面跟著換 |
| Ctrl+Shift+K | 現有的 picker，選到的終端機成為 Focus 顯示的那個 |
| Ctrl+Shift+B | 現有的 `sidebar.toggle`；在 Focus 裡先離開 Focus 再執行 |
| Esc | 不攔，照常送給終端機 |
| 點小點 | `agent.focus:<id>` |
| 滑鼠到上緣 | 顯示面板標題列（分割、放大、關閉照舊；在 Focus 裡分割會讓新面板成為焦點，畫面就換成它） |

**新命令**：`focus.toggle`（無參數）、`focus.on`、`focus.off`（給 mod 與 `--run` 用）。

與現有命令的關係：`pane.zoom` 改的是 UI 文件，Focus 不改；兩者可以同時存在，離開 Focus 後面板維持原本的放大狀態。

### 5. 資料與架構

**現有資料**：焦點面板（`ws.focused_session()`）、Agent 順序（`State::agent_order`）、注意力（`attention`）、提示音與通知（`sessions.cpp` 的 `alert()`）、`busy_terminal`。

**要新增的東西（全部在 UI，host 不用改）**

1. `App::State` 加 `struct FocusState { bool on = false; Clock::time_point glow_at{}; ImU32 glow_color = 0; Clock::time_point header_hover_since{}; } focus;`。是否在 Focus 存進 `ui.json`（`focus_on`），重開時回到原狀。
2. `draw_layout`（`frame.cpp`）：`focus.on` 時不放目前版面的樹，改放內建的 Focus 樹（`libs/ui/layouts/focus.json`，編進程式但不列入 `builtin_names()`，所以 `layout.switch` 不會輪到它）：

```json
{
  "id": "root", "type": "column", "titlebar": "none",
  "children": [
    { "id": "main", "type": "block",
      "content": { "tab": "active",
        "pane": { "lone": true, "radius": 0, "background": "background",
          "header": { "content": { "mod": "pane" }, "height": 30, "reveal": "hover" } } } },
    { "id": "focus-strip", "type": "block", "size": { "px": 22 }, "window_drag": true,
      "content": { "mod": "focus-strip" } }
  ]
}
```

3. `panes.cpp`：`pane_style()` 多一種 `PaneStyle::kFocus`，`draw_panes` 在 Focus 時只畫焦點面板，標題列依 hover 淡入。現在的程式不看 `header.reveal`（設計文件 2.4 說這些欄位「只讀不用」），所以要在 `kFocus` 自己做。
4. `alert()`：Focus 開著**而且視窗在前景**時，依設定改成邊緣光，不播音、不跳 toast；`ws.unsettle_session` 照常執行。視窗不在前景時照原本的流程（邊緣光看不到）。
5. `update_attention`：Focus 時 `visible` 只有焦點面板。
6. `next_wakeup_ms`：Focus 時 strip 的工作中是靜止的空心圓。只有畫面上確實有轉圈（焦點面板的標題列正在顯示）時才保留 50 ms 的喚醒，其他時候不需要為了轉圈而每 50 ms 重畫一次，省電。
7. `apps/desktop/focus_mode.cpp`：strip、邊緣光、空狀態卡。
8. 純邏輯放 `libs/ui/include/agentdesk/ui/focus_mode.hpp`：`strip_dots(agents, focused, width, dot, gap) -> {ids, overflow}`、`glow_alpha(elapsed_ms, peak) -> int`（淡入、停留、淡出的曲線）。

**效能**：只多畫一條 strip 和最多幾十個小點，幾乎沒有成本；背景 Agent 的快照不讀。

### 6. 設定

| `ui.json` 鍵 | 預設 | 說明 |
|---|---|---|
| `focus_on` | `false` | 是否在 Focus |
| `focus_alerts` | `"edge"` | `edge`：只有邊緣光；`edge_sound`：邊緣光加提示音；`normal`：和平常一樣（音效＋通知） |

設定 › Alerts 加一列「In focus mode」，三段 `segmented()`。

**和設計文件衝突的地方**：設計文件 4.1 寫「需要你時一定播放提示音」。ideas.md 要 Focus 預設不出聲，兩者衝突。這裡的預設照 ideas.md（`edge`），但排入時要請使用者決定，並改 4.1 的寫法（例如加上「Focus 模式依設定」）。

### 7. 邊界情況

1. **0 個分頁**：empty 卡；strip 仍顯示其他 space 的 Agent。
2. **焦點面板在分頁裡有分割**：只畫焦點面板；離開 Focus 後分割原樣回來。
3. **焦點面板結束**：照現有規則，session 清單更新時面板移除（設計文件 2.2 第 7 項），焦點移到同分頁的下一個；分頁空了就換分頁。Focus 不自己關閉。
4. **30 個 Agent**：1280 寬的 strip 中間約 700 px，可放約 50 個點；再多就顯示「+N」，點「+N」開 picker（`app.picker`）。
5. **視窗不在前景**：一律走原本的提示音、通知與工作列閃爍。
6. **很小的視窗**（寬 < 480）：strip 左側名稱隱藏，只留小點與「Exit」。
7. **AgentDesk Mac 版面**：Focus 時不畫選單列、桌布與 Dock；通知原本在右上角，在 Focus 裡不跳。
8. **子 Agent**：不列成小點（它們向父 Agent 回報）；父 Agent 的點用最緊急的狀態（`kid_status` 的規則）。
9. **高 DPI**：小點直徑最小 6 設計 px，150% 時 9 實際像素，hover 範圍另外放大到 14×22。

### 8. 驗收標準

1. Given workspace 版面、目前分頁有兩個分割面板，When 按 Ctrl+Shift+.，Then 只剩焦點面板與底部 22 px 的列，側欄、分頁列、右側面板都不見；再按一次全部回來，分割與面板放大狀態不變。
2. Given 在 Focus、視窗在前景、`focus_alerts` 為 `edge`，When 另一個 Agent 變成 Blocked，Then 視窗邊緣以 `needs_you` 色亮一次（約 1.5 秒），不播音、不出通知，strip 上那個 Agent 的點變成實心紅點。
3. Given 在 Focus、視窗不在前景，When 另一個 Agent 變成 Blocked，Then 和平常一樣播音並顯示通知。
4. Given 在 Focus，When 點 strip 上的一個點，Then 畫面換成那個 Agent，仍在 Focus。
5. Given 在 Focus 且沒有任何分頁，When 畫面顯示，Then 出現「No terminal to focus」與兩顆按鈕。
6. Given 在 Focus，When 關掉 AgentDesk 再開，Then 仍在 Focus。

### 9. 測試方式

- **單元測試**（`tests/unit/focus_mode_test.cpp`）：`strip_dots` 的排序、去掉焦點者、溢出數量；`glow_alpha` 在 0、75、150、550、1000、1450、1600 ms 的值。
- **截圖**：`screenshot_ui --run focus.on --out focus.png`；搭配 `--child` 開子 Agent、建議新增的 `--state` 讓另一個 session 變 Blocked，再用 `--input "wait:200;shot:glow.png"` 抓邊緣光。0 個分頁的狀態用 `--run tab.close --run focus.on`。
- **人工**：一邊打字一邊讓另一個 Agent 要求權限，確認邊緣光看得到、不會打斷；確認 Focus 下 Esc、Ctrl+C 都送給終端機。

### 10. 工作量與風險

- **大小：S～M，約 2～3 天**（覆蓋樹與 pane style 1、strip 與邊緣光 1、設定與測試 0.5～1）。
- **主要風險**：預設不出聲和設計文件 4.1「一定播放提示音」衝突，要使用者決定。其次是邊緣光太淡會被忽略、太強又會干擾，需要實際用幾天再調。
- **依賴**：共用元件 2、4、5；不依賴其他構想。

---

## A3. Review（審查台）

### 1. 目的與情境

Agent 做完一件事之後，使用者要「看它改了什麼」：哪些檔案、改了哪幾行、要不要 commit。現在的 Changes 面板已經能做，但它是 368 px 寬的右側面板，檔案清單和 diff 上下擠在一起，diff 一行常被截斷。Review 版面把畫面切成左右兩半：左邊是 Agent 的終端機，右邊固定是改動清單與 diff，底部是 commit。焦點換到另一個 Agent，右邊就換成那個 Agent 資料夾的改動。

### 2. 畫面草圖

```
x=0                                       x=640                                         x=1280
┌──────────────────────────────────────────┬─────────────────────────────────────────────┐ y=0
│ ●●●  [api ▾] (●Claude) (pwsh)  +   F  C  │ ‹ › Claude Code · api · fix-auth  ↻   ⎇ main │ tabs 42 │ review-head 42
├──────────────────────────────────────────┼─────────────────────────────────────────────┤ y=42
│ 8                                        │ CHANGES 4                       +128  −31   │ 列表標題 30
│ ┌─ 面板（tab 區塊，照舊）──────────────┐ │ M  src/auth/token.ts              +40  −12  │ 每列 26
│ │▣ Claude Code · api · main [Needs you]│ │▶M  src/auth/session.ts            +61  −19  │ ← 選中
│ │                                      │ │ A  tests/auth/token.test.ts       +27       │
│ │  > Allow edit? (y/n)                 │ │ ?  notes.md                       untracked │
│ │                                      │ ├─────────────────────────────────────────────┤ ← 可拖曳 6
│ │                                      │ │ src/auth/session.ts                  ‹  ›   │ diff 標題 30
│ │                                      │ │  41   function refresh(t) {                 │
│ │                                      │ │  42 - return t.exp > now                    │ diff（JetBrains Mono 12，行高 17）
│ │                                      │ │  42 + return t.exp - skew > now             │
│ │                                      │ │  ...                                        │
│ └──────────────────────────────────────┘ │                                             │
│                                          ├─────────────────────────────────────────────┤ y=732
│                                          │ [Commit message                     ] [Commit]│ review-commit 88
│                                          │ ⚠ Agent is still working — files may change │
├──────────────────────────────────────────┴─────────────────────────────────────────────┤ y=792
│ status 28                                                                              │
└────────────────────────────────────────────────────────────────────────────────────────┘ y=820
```

左右各約 640（中間 1 px 分隔線，可拖曳）。右欄寬 ≥ 760 時，清單改放在 diff 左邊（清單 260 寬），否則上下排（清單佔右欄高度的 35%，最少 96、最多 300）。

### 3. 元件規格

**review-head（42 px）**

| 部分 | 規格 |
|---|---|
| 底色 | `g_pal->sidebar`，底部 1 px `border` |
| ‹ › | `icon_button()` 24×24：`agent.prev`／`agent.next` |
| 目標 | 程式圖示 14 ＋ 名稱 Geist 13 semibold `text` ＋ space · 工作 Geist 12 `dim`；右側狀態膠囊（`status_mark`） |
| ↻ | `review.refresh`；載入中改成轉圈 |
| 分支 | 「⎇ main ↑2」Geist 12 `faint`（`GitCache::get` 的 branch／ahead／behind） |
| 共用 repo 警告 | 同一個 checkout 頂層還有其他執行中的 Agent 時，多一個膠囊「2 agents in this repo」，底 `alpha(working, 0x26)`、字 `working`；tooltip 說明改動清單包含所有 Agent 的改動 |

**改動清單**

| 部分 | 規格 |
|---|---|
| 標題 30 | 「CHANGES 4」Geist 11.5 semibold `dim`、字距 0.5；右側總計 +N（`done`）−N（`needs_you`） |
| 列 26 | 狀態字母 Geist 12 bold，顏色用 `mark_color()`（A `done`、D／U `needs_you`、其他 `working`）；路徑 Geist 12.5 `text`，資料夾部分 `dim`，太長時從中間省略；右側 +N −N Geist 11.5；二進位寫「binary」、未追蹤寫「untracked」 |
| hover | 底 `g_pal->hover` |
| selected | 底 `g_pal->selected`，左緣 2 px `focus` |
| 超過上限 | 最後一列「More files not shown」（`changes_more`），`faint` |

**diff**

| 部分 | 規格 |
|---|---|
| 標題 30 | 檔案路徑 Geist 12.5 medium `text`；右側 ‹ ›（上一個、下一個檔案） |
| 內容 | JetBrains Mono 12、行高 17；行號欄 44 寬，`faint`；新增列底 `alpha(done, 0x1F)`、前綴「+」`done`；刪除列底 `alpha(needs_you, 0x1F)`、前綴「−」`needs_you`；hunk 標頭 `alpha(info, 0x18)` 底、`dim` 字；不換行，可水平捲動 |
| 上限 | 沿用 `load_diff` 的 1500 列，超過時最後一列「Diff truncated at 1500 lines」 |

**review-commit（88 px）**

| 部分 | 規格 |
|---|---|
| 版面 | 內距 12；輸入框高 32、圓角 6、底 `card`、1 px `border`（focus 時 `focus`）；Commit 按鈕高 32，主要按鈕樣式（`primary_button_style`） |
| 提示列 | 輸入框下 6 px：上次 commit 的結果（`commit_note`），或 Agent 工作中時的「⚠ Agent is still working — files may change」，字 Geist 12 `working` |
| 停用 | 沒有改動、不是 repo、正在 commit、正在載入時，按鈕停用（現有的 `can` 條件） |

**畫面狀態**

| 狀態 | 右欄顯示 |
|---|---|
| empty（沒有焦點面板） | 「Select an agent to review its changes」，`dim`，下方按鈕 Go to…（`app.picker`） |
| empty（沒有改動） | 清單區「No uncommitted changes」；diff 區畫淡的勾與「Nothing to review」 |
| empty（不是 repo） | 「Not a git repository」＋資料夾路徑（`faint`） |
| loading | 清單區 4 列骨架（灰條），head 的 ↻ 轉圈；沿用 `changes_loading` |
| error | `changes_error` 用 `needs_you` 色顯示在清單區，下方 Retry（`review.refresh`）；遠端機器斷線時寫「Machine unreachable」 |

### 4. 互動

| 操作 | 結果 |
|---|---|
| Ctrl+Shift+R | `review.toggle`：切到 Review，再按一次回原本的版面 |
| F8／Shift+F8 | `review.next_file`／`review.prev_file`。**只綁在 keys.json 的 `layout:review` 區段**，其他版面的 F8 照常送給終端機 |
| Alt+↓／Alt+↑ | 現有的 `agent.next`／`agent.prev`；右欄跟著換 |
| 點清單列 | `select_change(path)`，現有行為 |
| Enter（在訊息框） | commit，現有行為 |
| 點 ↻ | `review.refresh` |
| 拖曳中間分隔線 | 調左右比例（版面引擎的分隔線，存進 `.state`） |

**新命令**：`review.toggle`、`review.next_file`、`review.prev_file`、`review.refresh`。

與現有命令的關係：Review 和 workspace 的 Changes 面板用同一份資料（`SidePanels`），兩邊選中的檔案一致。`panel:changes` 在 Review 版面沒有作用（右欄固定顯示），命令面板執行時跳提示「Changes are always shown in Review」。

### 5. 資料與架構

**現有資料**（`apps/desktop/app_internal.hpp` 的 `SidePanels`）：`changes`、`repo_top`、`not_repo`、`changes_error`、`changes_loading`、`changes_more`、`open_diff`、`diff`（`DiffRow`，含行號）、`commit_msg`、`commit_note`、`committing`；`track_changes(cwd, machine)` 每幀呼叫，資料夾變了或每 10 秒重讀，本機有資料夾監看；遠端走 `git.run`。資料夾來自 `panel_folder()`，也就是焦點面板的 cwd。

**要新增的東西（全部在 UI，host 不用改）**

1. **跟著哪個 Agent**：`App::State` 加 `std::string review_target`。焦點面板 `is_agent()` 時更新為它；焦點移到普通 shell 時，`review_follow = "agent"` 保留上一個 Agent，`"focused"` 則跟著 shell。`panel_folder()` 多一個參數，讓 Review 用 `review_target` 的 cwd。
2. **Agent 一輪結束就重讀**：目標 Agent 的狀態從 Working 變成其他值時，呼叫新的 `SidePanels::refresh_changes()`（把 `changes_at_` 歸零），不必等 10 秒。
3. **自動選第一個檔案**：清單載入後沒有選中的檔案，而且使用者沒有手動關掉（`user_closed`）時，選第一個。
4. **共用 repo 偵測**：比對所有 `live_agent()` 的 `GitCache::get(cwd).top`，相同就顯示警告膠囊。
5. `files_changes.cpp` 裡的 `draw_changes_list`、`draw_diff`、`draw_commit_footer` 現在在檔案內部；改成在 `app_internal.hpp` 宣告，讓 `review_layout.cpp` 重用，避免複製一份。
6. 新檔 `apps/desktop/review_layout.cpp`：`review-head`、`review-changes`（清單＋diff，自己決定左右或上下）、`review-commit` 三個區塊。`derive_layout` 看到 `review-*` 時 style 設成 `review`。
7. 純邏輯 `libs/ui/include/agentdesk/ui/review.hpp`：`next_change(list, current, step)`（跳過二進位？不跳，待確認）、`split_path(path) -> {dir, name}`、`middle_ellipsis(text, max_chars)`、`review_split(width, height) -> {side_by_side, list_rect, diff_rect}`。
8. keymap：預設綁定目前只有 `global` 與 `terminal` 兩段；要在 `for_each_default` 加 `layout:review` 段的預設（F8、Shift+F8）。

**版面檔 `libs/ui/layouts/review.json`**：

```json
{
  "id": "root", "type": "column", "titlebar": "none",
  "traffic_lights": [12, 16], "background": "canvas",
  "children": [
    { "id": "body", "type": "row", "divider": { "width": 1, "color": "border" },
      "children": [
        { "id": "left", "type": "column", "size": { "weight": 1 }, "min": 420,
          "children": [
            { "id": "tabs", "type": "block", "size": { "px": 42 }, "window_drag": true,
              "content": { "mod": "tabs" } },
            { "id": "main", "type": "block", "padding": 8,
              "content": { "tab": "active",
                "pane": { "gap": 8, "radius": 10, "background": "background", "lone": true,
                  "border": { "width": 1, "color": "border" }, "focus": { "width": 1, "color": "accent" },
                  "header": { "content": { "mod": "pane" }, "height": 30 } } } }
          ] },
        { "id": "review", "type": "column", "size": { "weight": 1 }, "min": 420, "max": 1400,
          "background": "chrome",
          "children": [
            { "id": "review-head", "type": "block", "size": { "px": 42 }, "content": { "mod": "review-head" } },
            { "id": "review-changes", "type": "block", "content": { "mod": "review-changes" } },
            { "id": "review-commit", "type": "block", "size": { "px": 88 }, "content": { "mod": "review-commit" } }
          ] }
      ] },
    { "id": "status", "type": "block", "size": { "px": 28 }, "content": { "mod": "workspace-status" } }
  ]
}
```

**效能**：沒有新的背景工作。`SidePanels` 只有一個 worker、一次追一個資料夾；Review 和 Changes 面板不會同時追兩個不同的資料夾，因為兩者都跟著同一個目標。diff 每幀只畫看得到的列（ImGui clipper）。

### 6. 設定

| `ui.json` 鍵 | 預設 | 說明 |
|---|---|---|
| `review_follow` | `"agent"` | `agent`：焦點移到普通 shell 時仍顯示上一個 Agent 的改動；`focused`：一律跟著焦點面板 |

### 7. 邊界情況

1. **多個 Agent 在同一個 checkout**：git 的未提交改動分不出是誰做的。只顯示警告膠囊，不嘗試區分；建議每個 Agent 用自己的 worktree（現有 `worktree.new`）。
2. **Agent 還在工作時 commit**：允許，但顯示警告；commit 後 Agent 繼續改的檔案會出現在下一次清單裡。
3. **遠端機器斷線**：清單保留最後一次的結果並標「Machine unreachable」；Commit 停用。
4. **很大的 diff 或很多檔案**：沿用現有上限（diff 1500 列、清單的 `changes_more`），並明確寫出被截斷。
5. **二進位檔**：清單寫「binary」，diff 區寫「Binary file — no preview」。
6. **很小的視窗**（右欄寬 < 420）：版面引擎依 `min` 讓左欄縮小；整個視窗寬 < 860 時，兩欄都到最小值仍放不下，右欄會被壓縮，清單改成只顯示檔名。是否要在這時自動收起右欄，待確認。
7. **沒有任何分頁**：左欄顯示現有的 `draw_no_tab`；右欄顯示「Select an agent…」。
8. **子 Agent**：子 Agent 通常和父 Agent 在同一個資料夾；目標跟焦點走，peek 視窗不改變目標。
9. **高 DPI**：diff 的字固定 12 設計 px；行號欄寬依字寬計算，不寫死實際像素。

### 8. 驗收標準

1. Given 一個 Agent 改了 3 個檔案，When 按 Ctrl+Shift+R，Then 左邊是它的終端機，右邊列出 3 個檔案並自動顯示第一個的 diff。
2. Given Review 顯示中，When 按 Alt+↓ 換到另一個在不同 repo 的 Agent，Then 右邊在 1 秒內換成那個 repo 的改動（本機）。
3. Given 目標 Agent 從 Working 變成 Done，When 狀態改變，Then 清單立刻重讀，不用等 10 秒。
4. Given 兩個執行中的 Agent 在同一個 checkout，When 看其中一個，Then head 顯示「2 agents in this repo」。
5. Given 有改動且輸入了 commit 訊息，When 按 Commit，Then 執行 `git add -A` 與 `git commit`，清單變成「No uncommitted changes」，提示列顯示 commit 結果。
6. Given 在 Review 版面、焦點在終端機，When 按 F8，Then 換到下一個檔案；在 workspace 版面按 F8，F8 送給終端機。

### 9. 測試方式

- **單元測試**（`tests/unit/review_test.cpp`）：`next_change` 在頭尾、空清單時的行為；`middle_ellipsis` 的中英文寬度；`review_split` 在 420、759、760、1400 寬的結果。keymap 測試：`layout:review` 的預設綁定存在，而且 `global` 段沒有 F8。
- **截圖**：`screenshot_ui --cwd <測試 repo> --run layout:review --out review.png`。測試 repo 由測試前用 git 建立並放幾個改動（另寫一個小腳本，或擴充 `--cwd`，待確認）；不是 repo 的資料夾、沒有改動各一張；`--input "key:f8;shot:next.png"`。
- **人工**：用真的 Claude Code 改幾個檔案，確認 diff 看得清楚；兩個 Agent 共用 checkout 時警告有出現；遠端機器上 commit 一次。

### 10. 工作量與風險

- **大小：M，約 3～4 天**（版面檔與三個區塊 1.5、目標追蹤與重讀 0.5、重用函式整理 0.5、keymap 版面區段預設 0.5、測試 0.5～1）。
- **主要風險**：「這個 Agent 改了什麼」和「這個資料夾有哪些未提交的改動」不一樣。多個 Agent 共用 checkout，或使用者自己也在改時，清單會混在一起。這版只提示不解決；若要精確，得在每輪開始時記下基準（例如 `git stash create` 的結果），屬於之後的工作（待確認）。
- **依賴**：共用元件 3、4；不依賴其他構想。

---

## A4. Timeline（時間軸）

### 1. 目的與情境

給讓 Agent 跑長時間任務、中途離開的人。回來時，側欄只看得到每個 Agent 現在的狀態；剛才誰卡住了多久、誰先做完，都看不出來。Timeline 是橫向的時間軸，每個 Agent 一列，標出工作中、需要你、完成、閒置的時間段，加上「你離開的這段時間」的底色，一眼看出剛才發生了什麼。Timeline 是一個從任何版面都能打開的大型浮層（像 peek 視窗），按 Esc 關閉；它也註冊成 `timeline` 區塊，使用者可以把它放進自己的版面。

### 2. 畫面草圖

浮層 1152×656（視窗的 90%×80%），置中：

```
x=64                                                                                         x=1216
┌────────────────────────────────────────────────────────────────────────────────────────────┐ y=82
│ Timeline   [15m|1h|4h|24h]   ☑ Since you left               6 agents · 2 need you     Esc │ header 48
├──────────────────────┬─────────────────────────────────────────────────────────────────────┤
│                      │ 13:00        13:15        13:30        13:45        14:00     now │ │ axis 28
├──────────────────────┼─────────────────────────────────────────────────────────────────────┤
│ ▣ Claude Code        │ ▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░░[!!!!!!!!!!!!!!!!!]▓▓▓▓▓▓▓▓▓▓▓▓▓▓✓            │ │ row 32
│   api · fix-auth     │                        ┊ you left 13:32 ┊▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒│ │
│ ▣ Codex              │       ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓      │ │
│   web · login        │                                                                 │ │
│   ↳ ▣ sub-agent      │                ▓▓▓▓▓▓▓▓▓✓                                       │ │ 子列 24
│ ▣ Claude Code        │ ▓▓▓▓▓▓▓▓✓ ·········································×           │ │ × = 程序結束
│   docs · readme      │                                                                 │ │
│ ...                  │                                                                 │ │ 垂直捲動
├──────────────────────┴─────────────────────────────────────────────────────────────────────┤
│ ▓ Working   ! Needs you   ✓ Done   · Idle   × Exited      Hover a bar for times          │ legend 28
└────────────────────────────────────────────────────────────────────────────────────────────┘ y=738
```

圖例：`▓` 工作中（`working`）；`[!!!]` 需要你（`needs_you`，段落寬 ≥ 14 px 時中間畫「!」）；`✓` 完成的時間點（`done`）；`·` 閒置（`idle`，細線）；`▒` 離開期間的底色；`×` 程序結束。標籤欄 220 寬。

### 3. 元件規格

| 部分 | 規格 |
|---|---|
| 浮層外框 | 沿用 `push_overlay_style()`；圓角 12、底 `card`、1 px `border`；標題用 `overlay_title(s, "Timeline")`（含 Esc 膠囊） |
| 範圍切換 | `segmented()` 四段：15m、1h、4h、24h；高 26 |
| Since you left | 勾選框，Geist 12.5 `text`；勾起時畫離開期間的底色 |
| 軸 28 | 刻度線 1 px `border`；時間標籤 Geist 11 `faint`，本機時間 24 小時制；刻度間距依範圍：15m 每 2 分、1h 每 15 分、4h 每 30 分、24h 每 3 小時；最右是「now」與 1 px `focus` 豎線（往下貫穿所有列） |
| 標籤欄 220 | 程式圖示 16、名稱 Geist 12.5 medium `text`、第二行 space · 工作 Geist 11 `dim`；子 Agent 縮排 16，前面畫「↳」`faint` |
| 列 | Agent 列高 32、子 Agent 24；奇數列底 `alpha(hover, 0x40)` |
| 段落 | 高 12（子列 8）、圓角 3、垂直置中；顏色見圖例；最小寬 2 px，比這更短的相鄰段落合併成最緊急的那種（`attention_rank`） |
| 形狀（不只靠顏色） | 需要你的段落 ≥ 14 px 時中央畫「!」（Geist bold 10，白字或 `bg`，取對比高者）；完成畫在時間點上的「✓」；Error 段落用 `needs_you` 加 45° 斜線紋；Unknown 不畫段落，只有 1 px `faint` 基線 |
| 離開底色 | `alpha(focus, 0x14)`，起點上方標「you left 13:32」Geist 11 `dim` |
| hover 段落 | 段落外加 1 px `text` 框；tooltip：「Needs you · 13:32–13:51 (19m) · osc7501」 |
| 點段落或標籤 | `agent.focus:<id>`，並關閉浮層 |
| selected（鍵盤） | 標籤欄底 `selected`、左緣 2 px `focus` |
| legend 28 | Geist 11.5 `dim`，每項前面畫和段落相同的小樣本 |

**畫面狀態**

| 狀態 | 畫面 |
|---|---|
| empty（沒有任何紀錄） | 中央：「No agent activity in the last hour」＋「Agents report their state through OSC 7501 or agentctl report-state.」`dim`；範圍 < 24h 時加「Show 24h」 |
| loading | 第一次向 host 要資料時：標籤欄與軌道各 4 列骨架，軸照常畫 |
| error | 本機 host 沒連上：中央「agent-host not connected」`needs_you`。遠端機器沒回應或版本太舊（回 unknown method）：那台機器的群組標頭下寫「Timeline needs a newer agent-host on this machine」或「Machine unreachable since 14:02」，已取得的段落保留，之後畫斜線紋 |

### 4. 互動

| 操作 | 結果 |
|---|---|
| Ctrl+Shift+H | `timeline.toggle`：開或關浮層（H = history；目前沒有綁定） |
| Esc | 關閉浮層（浮層裡沒有終端機，所以 Esc 可以用） |
| ↑↓ | 移動選取的 Agent；Enter：`agent.focus` 並關閉 |
| ←→ | 往前、往後平移半個範圍；End 回到「現在」 |
| 1～4 | 切換範圍 15m／1h／4h／24h |
| 滾輪 | 垂直捲動；Ctrl+滾輪縮放範圍（以滑鼠位置為中心） |
| 點段落或標籤 | `agent.focus:<id>` 並關閉 |

**新命令**：`timeline.toggle`、`timeline.range:<15m|1h|4h|24h>`。

與現有命令的關係：`agent.focus` 原樣使用；浮層開著時其他快捷鍵照常（`run_global_shortcuts` 在有 popup 時本來就不跑，所以 Ctrl+Shift+H 要由浮層自己處理關閉）。

### 5. 資料與架構

**現有資料**

| 資料 | 位置 | 夠不夠 |
|---|---|---|
| 目前狀態與上次變動時間 | host：`AgentStatus::state`、`updated_at`（`libs/host/include/agentdesk/host/session.hpp`） | 只有最新一筆 |
| 上一個值 | `AgentStatus::last_state`（紀錄被丟棄前的值） | 只有一筆 |
| UI 端的上次變動 | `State::changed`、`ui.json` 的 `agents_changed`（`{s, at}`） | 只有最新一筆，而且 UI 沒開時不會記錄 |

所以要在 **host** 補狀態變化的歷史：狀態屬於執行狀態，host 擁有它；UI 關著的時候也要記錄，這正是「離開一陣子回來」的情境。

**要新增的東西**

1. **host（`libs/host`）**
   - `Session` 在 actor 執行緒每次套用新的 agent 狀態時（`handle(ReportCmd&)`、`on_engine_status`、紀錄被丟棄時），以及程序狀態變化時，追加一筆 `StateMark { std::int64_t at_ms; AgentState agent; ProcessState process; }` 到有界的 ring（每個 session 最多 1,000 筆；超過 24 小時的丟掉，但保留範圍開始前的最後一筆，作為起點的狀態）。只記時間與狀態，**不記訊息、不記畫面內容**。
   - 在 actor 裡記錄而不是在 `SessionManager` 的 info 回呼裡記，原因是 info 的發布可能把連續的變化合併成一次（是否會合併待確認），在 actor 裡記才不會漏掉很短的 Blocked。
   - `Session::timeline()`：在鎖內複製 ring 回傳。
   - `SessionManager`：session 被 `prune_exited` 釋放時，把它的 marks、名稱、程式、parent 搬到 `ended_` 清單（最多 200 筆、最多 24 小時），Timeline 才看得到已結束的 Agent。
   - 換版交接（`handover.cpp`）：要一起搬 marks，否則更新 agent-host 後歷史會從頭開始。第一版可以先不搬，在軸上標「History starts 14:05 (agent-host updated)」（待確認）。
2. **協定（v1，新欄位都是選填）**
   - 新方法 `session.timeline`，參數 `{ "session"?: id, "since_ms"?: int }`，回傳 `{ "now_ms": int, "sessions": [ { "id", "name", "program", "parent", "created_at", "ended": bool, "truncated": bool, "marks": [ [at_ms, "Working", "Running"], ... ] } ] }`。加進 `SessionManager::dispatch` 的 `kReadOnly`（交接期間也能讀）。
   - `session.state` 事件加選填欄位 `at`（毫秒），UI 開著時可以直接把新的一筆接上，不必再問一次。
   - `now_ms` 讓 UI 對齊 host 的時鐘（遠端機器的時鐘可能不同）。
3. **agentctl**（選做）：`agentctl timeline [--json] [--since 1h]`，方便測試與自動化。
4. **UI**
   - `HostClient::request_timeline(since_ms, callback)`；`App::State` 加 `struct TimelineState { bool open = false; std::string range = "1h"; std::map<std::string, MachineTimeline> by_machine; Clock::time_point fetched{}; std::optional<long long> left_at_ms; } timeline;`。打開時向每台機器要一次，之後每 15 秒補 `since_ms` 之後的部分；同時接 `session.state` 的 `at`。浮層關著時不要求資料。
   - 「你離開的時間」：`App::on_focus_changed(false)` 或視窗最小化時記下 `left_at_ms`；回到前景時保留到下一次離開。只放在記憶體，不存檔（離開時間只對這次執行有意義）。
   - `apps/desktop/timeline.cpp`：浮層與 `timeline` 區塊的繪製；`draw_dialogs` 加一行；`draw_layout_block` 加 `timeline` 分支。
   - 純邏輯 `libs/ui/include/agentdesk/ui/timeline.hpp`：
     - `std::vector<Segment> segments(const std::vector<Mark>&, long long from_ms, long long to_ms)`：轉成時間段、裁切到範圍、處理時間倒退（系統時間被調整）的資料。
     - `std::vector<Segment> pack(const std::vector<Segment>&, double px_per_ms, float min_px)`：太短的段落合併成最緊急的那種。
     - `std::vector<Tick> ticks(long long from_ms, long long to_ms, float width)`：刻度與標籤，依範圍選間距。
     - `std::vector<TimelineRow> rows(...)`：依最近變動排序、子 Agent 收在父 Agent 下面。

**版面區塊**：`{ "id": "timeline", "type": "block", "content": { "mod": "timeline" } }` 可以放進任何版面，例如 workspace 的 `panel-body` stack 多一個子節點；區塊版本沒有 header 與 Esc，範圍沿用 `timeline_range`。

**效能**：資料量很小（每個 session 最多 1,000 筆 × 16 bytes，30 個 Agent 約 0.5 MB 以下）；查詢只在浮層開著時進行，每 15 秒一次。繪製時每列先 `pack()`，一列最多畫約「寬度 ÷ 2」個段落。浮層開著時不需要比平常更頻繁的重畫，只有「now」線每 5 秒往右移一次（`next_wakeup_ms` 加 5 秒的期限）。

### 6. 設定

| `ui.json` 鍵 | 預設 | 說明 |
|---|---|---|
| `timeline_range` | `"1h"` | 上次選的範圍 |
| `timeline_since_left` | `true` | 是否畫離開期間的底色 |

host 端的保留期限（24 小時、每個 session 1,000 筆、已結束 200 筆）先寫成常數，不開放設定。

### 7. 邊界情況

1. **0 個 Agent／沒有紀錄**：empty 畫面；範圍小於 24h 時提供「Show 24h」。
2. **30 個 Agent**：列表垂直捲動，標籤欄固定；依最近變動排序，需要你的排在最上面。
3. **agent-host 重新啟動**：舊的 session 變成 Lost，歷史只從新 host 開始；軸上標「agent-host restarted 14:05」。換版交接同上（是否搬 marks 待確認）。
4. **遠端機器斷線**：保留已取得的資料；從最後一次回應到現在畫斜線紋，標「Machine unreachable since …」。遠端的 agent-host 太舊、不認得 `session.timeline` 時，那台機器的群組寫明需要更新。
5. **系統時間被調整或兩台機器時鐘不同**：以各機器回傳的 `now_ms` 換算成本機時間；同一個 session 裡時間倒退的 mark 丟掉並記進診斷 log（只記數量）。
6. **子 Agent**：列在父 Agent 下面（`agent_tree` 的順序），列高 24；父 Agent 已結束時子 Agent 仍掛在它下面，父列顯示「×」。
7. **紀錄被丟棄（Unknown）**：照設計文件 3.6，不推測成 Idle，那段只畫基線；tooltip 寫「No status (last: Done)」，用 `last_agent_state`。
8. **很長的任務**（超過 24 小時）：最舊的部分被丟掉，該列左端標「earlier history dropped」（`truncated`）。
9. **很小的視窗**：浮層最小 640×400；更小時佔滿視窗，標籤欄縮成 140，第二行隱藏。
10. **高 DPI**：段落最小寬是 2 設計 px，在 150% 是 3 實際像素；刻度數量依實際寬度計算，不會擠在一起。

### 8. 驗收標準

1. Given 一個 Agent 在 13:00～13:20 工作、13:20～13:35 Blocked、13:35～13:50 工作、13:50 完成，When 在 14:00 打開 Timeline（1h），Then 那一列依序畫出工作中、需要你（中間有「!」）、工作中三段，13:50 有「✓」，hover 需要你的段落顯示「13:20–13:35 (15m)」。
2. Given agent-ui 在 13:30 關閉、14:00 重開，When 打開 Timeline，Then 13:30～14:00 之間的狀態變化仍完整顯示（host 有記錄）。
3. Given 視窗在 13:32 失去焦點、14:00 回來，When 打開 Timeline 並勾選 Since you left，Then 13:32～now 有底色並標「you left 13:32」。
4. Given host 對同一個 session 收到 2,000 次狀態變化，When 查詢 `session.timeline`，Then 只回傳最新 1,000 筆並標 `truncated: true`，host 記憶體不隨變化次數無限增加。
5. Given 點某個 Agent 的段落，When 點下去，Then 浮層關閉，焦點移到那個 Agent 的面板。
6. Given 遠端機器在 Timeline 開著時斷線，When 15 秒後更新，Then 那台機器的列保留已取得的段落，之後畫斜線紋並寫「Machine unreachable since …」。

### 9. 測試方式

- **單元測試**
  - `tests/unit/timeline_test.cpp`（libs/ui）：`segments` 的裁切、範圍前的起點狀態、時間倒退；`pack` 的合併規則（最緊急優先）；`ticks` 在四種範圍與 320～2560 寬的刻度數；`rows` 的排序與子 Agent 位置。
  - host：`Session` 的 ring 上限與 24 小時淘汰（用注入的時鐘，待確認現有程式有沒有可注入時鐘的地方）；`protocol_test.cpp` 加 `session.timeline` 回應與 `session.state` 的 `at` 欄位解析，以及舊 client 忽略新欄位。
- **整合測試**（`tests/integration/windows`）：開一個 `vt_probe report` session，確認 `session.timeline` 至少有 working 與 blocked 兩筆，順序與時間正確；`agentctl report-state` 連送 1,100 次後檢查上限。
- **截圖**：`screenshot_ui --profile chinese --run timeline.toggle --out timeline.png`，搭配建議新增的 `--state`（連續送幾種狀態，中間 `wait`）；`--input "key:4;shot:24h.png"` 檢查範圍切換；沒有 Agent 時的 empty 畫面一張。
- **人工**：讓 Claude Code 跑 30 分鐘以上的任務並離開座位，回來看時間軸是否和實際一致；更新 agent-host 一次，確認標示。

### 10. 工作量與風險

- **大小：L，約 5～7 天**（host ring 與協定 1.5、交接是否搬 marks 0.5～1、UI 浮層與繪製 2、純邏輯與測試 1.5、agentctl 與整合測試 0.5）。
- **主要風險**：要改 host 與協定，牽動換版交接（ADR-19）與遠端機器的版本相容；交接的資料格式一改，就要重測整個交接流程。另外記錄的精確度取決於狀態來源，Codex 只靠 hook，中斷之後可能停在 Working（設計文件 3.6 已提到），時間軸會照實畫出這段「看起來還在工作」的時間。
- **依賴**：共用元件 1、2、4；host 端的 ring 也可以給 A1 的 tile 顯示「Blocked 了多久」更準確的時間。

---

## 共用元件

這四個設計重疊的部分，做一次給大家用：

| # | 元件 | 給誰用 | 內容 |
|---|---|---|---|
| 1 | 狀態的形狀與小樣本 | A1、A2、A4（以及主題 T1） | `status_mark` 之外加 `status_dot(dl, center, attention, size)`：需要你實心、完成實心、工作中空心、閒置小點；Timeline 的段落樣本也用同一組定義。形狀和顏色一起出現，不只靠顏色 |
| 2 | Agent 清單與排序 | A1、A2、A4 | 從 `sidebar.cpp` 的 `AgentList` 抽出 `collect_agents(s, options)`：哪些算 Agent（`is_agent`、`live_agent`）、子 Agent 樹、`priority_before` 排序、每個的 attention 與上次變動時間。側欄、牆面、strip、時間軸用同一份結果，順序才會一致 |
| 3 | 切到某個版面再切回來 | A1、A3 | `toggle_layout(s, name)`：記住切過去之前的版面（`ui.json` 的 `layout_return`），再按一次回去；`mission.toggle`、`review.toggle` 都用它 |
| 4 | 空、載入、錯誤狀態卡 | 全部 | 把 `draw_no_tab` 的排法抽成 `draw_state_card(s, area, kind, title, hint, buttons)` 與骨架灰條 `draw_skeleton_rows(...)`；四個設計的 empty、loading、error 都用它，字級與按鈕一致 |
| 5 | 「目前畫面上看得到哪些終端機」 | A1、A2（也修正現有行為） | 現在 `update_attention` 自己用 `ws.active_tab()` 推算誰被看見。改成繪製時把真正畫出來的終端機記進 `State::visible_sessions`，下一幀的注意力計算用它。監看牆的小格子不算、Focus 只算焦點面板、Mac 版面沒有視窗時也正確 |
| 6 | 快照尾端行 | A1（也可給 `report_failed_start`） | `ui::screen_tail()`，見 A1 第 5 節 |
| 7 | 命令登錄 | 全部 | 新命令同時加進 `kCommands`、`command_names.cpp`、說明視窗；加一個單元測試確認新版面裡每個按鈕的命令都是已知命令（照 `MacLayout.EveryMenuRowNamesAKnownCommand` 的做法） |
| 8 | keymap 的版面區段預設 | A3（A1 之後也可能用） | `for_each_default` 支援 `layout:<名稱>` 段的預設綁定 |
| 9 | 截圖工具的狀態注入 | 全部的截圖測試 | `screenshot_ui --state <名稱>:<state>[:<kind>]`，對隔離的 host 送 `report-state`；`--input` 的 `key:` 支援組合鍵（例如 `key:ctrl+shift+m`） |
