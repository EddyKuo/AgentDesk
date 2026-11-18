# herdr-app 原始碼審閱

審閱日期：2026-11-18。對象是 https://github.com/herdrdev/herdr-app 的 commit `321c9a8`（2026-11-16），終端機核心部分對照 herdr-core 的 commit `c751780`。逐項規格（英文，附原始碼行號）在 `docs/research/herdr-app/`：

| 檔案 | 範圍 |
|---|---|
| `spec-sidebar-layout.md` | 視窗版面、左側 Spaces／Agents、底部、狀態標記、字型與配色 |
| `spec-tabs-panes-menus.md` | 分頁列、面板外框與拖曳、New 選單、picker、命令面板、peek、toast、worktree、machine-add、命令清單 |
| `spec-files-changes.md` | 右側 Files、Changes 面板 |
| `spec-settings-fonts.md` | 設定檔與設定視窗、字型、快捷鍵、程式辨識、shell 目錄追蹤、音效、首次啟動 |

之前的 `herdr-reference.md` 讀的是 herdr-core 和 herdr 主產品，介面部分只看了說明文件和網頁；這份補上 herdr-app 介面的原始碼。兩者衝突時以這份為準。

## 授權

三個 repo 都是 Apache-2.0。使用者在 2026-11-18 同意可以把 herdr 的程式碼轉寫成 C++。條件：轉寫的檔案保留 Apache-2.0 授權聲明並註明改過，發布包的 THIRD_PARTY_NOTICES 列出 herdr-app；AgentDesk 其餘部分仍是 MIT。herdr-app 附的字型 Geist、Geist Mono、JetBrains Mono 是 OFL 1.1，Symbols Nerd Font 是 MIT，都可以隨附。

## 架構

herdr-app 是 Rust 寫的視窗程式（約 1.5 MB 原始碼），終端機用 libghostty。畫面上幾乎每一塊都是一個 JavaScript mod（跑在內建的 QuickJS），由 `defaults/layouts/main.json` 排版：spaces、agents、tabs、pane、files、changes、picker、new-menu、peek、toast、worktree、machine-add、settings 等。mod 回傳一棵元件樹，Rust 端負責畫。

AgentDesk 不搬 JS 執行環境，改用 C++／ImGui 照同樣的行為重寫；版面數字、互動規則、狀態判斷照 herdr。

## 視窗版面

- 系統標題列隱藏。整個視窗是一列：左側欄（預設 268 px，可拖曳 44～480 px）＋ 1 px 分隔線＋工作區。
- 左側欄由上而下：品牌列 46 px（logo、名稱、搜尋、收合鈕）、Spaces 與 Agents 兩個列表（各占一半，中間分界可拖曳）、底部 69 px（Settings、Add machine）。側欄窄於 160 px 時每個區塊改成只剩圖示的窄欄；Ctrl+Shift+B 完全隱藏。
- 工作區由上而下：分頁列 42 px、內容區（邊距 8、間距 8）、狀態列 28 px。內容區是面板，右側可再開 368 px 的 Files 或 Changes 面板（可拖曳 280～900 px，起始隱藏）。
- 所有尺寸是設計 px，乘上 zoom。

## 左側 Spaces

- 每列 30 px：18 px 彩色字母頭像（固定六色，不跟主題變）、名稱，靠右是 `repo · branch`，後面接綠色 ↑N、紅色 ↓N（領先／落後遠端）。main、master 和名稱已經表達的部分會省略。
- worktree 的 space 以 `├─`／`└─` 掛在主 repo 的 space 下面，列高 28 px。
- 有遠端機器時才依機器分組，組標題可收合。
- 放開滑鼠才算選取；拖曳可在同一台機器內調整順序，以 2 px 強調色線標示落點。
- 右鍵選單：Rename、New worktree、Open worktree、Remove worktree、Close（會先問「Close space?」）。沒有雙擊、釘選、封存。
- 展開狀態不顯示 Agent 狀態，只有窄欄才用 `●`／`·` 表示。
- 新 space 不跳對話框：開在目前面板的資料夾，以資料夾名稱命名；有 git 時用 repo 頂層資料夾名稱，家目錄顯示 `~`。

## 左側 Agents

- 兩種排序：預設依優先度分段「Needs you／Done／Working／Idle」，同段內最近變動的在前；或依 space 分組。釘選的永遠在最前面。
- 每列 30 px：程式圖示、space 名稱與說明（使用者取的分頁名稱，否則是整理過的終端機標題）、遠端機器名稱、子 Agent 的收合箭頭、距離上次變動的時間（`<1m`、`5m`、`2h`，滑鼠移上去變成釘選鈕）、狀態標記。點一下跳到該 Agent 的面板。子 Agent 列 26 px，有樹狀線。
- 狀態標記：Working 是每 900 ms 轉一圈的 spinner；Blocked 是「Needs you」膠囊（段落標題已寫時改成 `!`）；Done 是 7 px 實心點；Idle 是 7 px 空心圈。
- Done 的意思是「在你沒看的時候完成」。視窗在前景、而且該終端機出現在畫面上，就變回 Idle。進入 Blocked 一定提醒；完成只在你沒看的時候提醒。

## 分頁列

- 分頁是 29 px 高的膠囊：程式圖示、最多 170 px 的名稱（沒命名時 Agent 跟著它的即時標題，shell 顯示程式名稱）、狀態 spinner 或點，關閉鈕只出現在目前分頁。
- 雙擊改名；放開滑鼠才選取，拖到別的分頁上放開就移動順序（沒有拖曳預覽）；滾輪切換分頁；放不下時出現水平捲軸並保持目前分頁可見。
- 新分頁插在目前分頁後面。關分頁不詢問，除非是最後一個，這時問要不要關 space。
- Files、Changes 兩個按鈕在分頁列右側：按目前開著的那個會關掉，按另一個會切換，開關狀態依 space 記住，沒有預設快捷鍵。
- Windows 上，分頁列右端畫 26 px 的最小化、最大化／還原、關閉；拖曳分頁列會送 `SC_MOVE`，所以能用系統貼齊；雙擊切換最大化。

## 面板

- 標題列 30 px：圖示、名稱、`~` 縮寫的資料夾、狀態（Needs you 膠囊）。分割右／下、放大、關閉只在滑鼠移上去時出現。
- 目前面板有 1 px 強調色外框；圓角 10、間距 8；只有一個面板也畫外框。
- 標題列上方有 28×18 的拖曳把手，移動超過 4 px 才開始拖。放到別的面板外圈 25% 的位置，就移到最近那一側；放在中間則互換。
- 分隔線的可點範圍是 6 px，兩側最小 24 px。

## New 選單、picker、命令面板

- New 選單：Terminal（開 shell 分頁）、已安裝的 Agent（數字鍵 1～9，最多再列 2 個「not installed」）、New space、New worktree…、Open worktree…。選單裡沒有機器選項；遠端機器在 picker 裡以「new space on <machine>」出現。
- 沒帶參數的新分頁、新 space、分割都開 shell。新分頁的資料夾依序取：目前面板的即時資料夾、space 的資料夾、家目錄；分割用被分割面板的資料夾。Windows 上預設 shell 是 PATH 上的 pwsh.exe，沒有才用 powershell.exe。
- picker 用子字串搜尋（不是模糊比對），忙碌的 space 排前面（Blocked 最前），安靜的收進「earlier」；上下鍵循環，Ctrl+D 關掉選到的分頁，Esc 關閉。
- 命令面板用模糊比對，有「Recent」群組（最近 5 個），群組順序固定，顯示快捷鍵，上下鍵到頭就停。
- toast 一般 5 秒，錯誤和警告 8 秒。

## Files 與 Changes

兩者共用右側面板，跟著目前面板的即時資料夾走（其次是 space 資料夾），遠端 space 就在那台機器上執行。每 10 秒更新一次，本機資料夾另外監看檔案變動（150 ms 合併）。

**Files**：
- 每層資料夾展開時才列出內容。Windows 上用 PowerShell `Get-ChildItem -Force`，顯示隱藏檔，只略過 `.git`，不套用 .gitignore。
- 資料夾在前，再依名稱排序。上限是每層 500 項、總共 5000 項、深度 40；沒有搜尋。
- 每列 28 px，縮排 8＋每層 18。
- 檔案旁標示 git 的 A／M／D／U，資料來源是 `git status --porcelain=v1 -z --untracked-files=all`。herdr 用字串直接比對路徑，Windows 上 `C:/` 與 `C:\` 可能對不起來，我們轉寫時要先統一路徑格式。
- 點檔案會在面板裡開唯讀預覽，有語法上色和 ← 返回。超過 1 MiB、二進位檔、讀不到的檔案改顯示說明；文字超過 400k 字元截斷。點檔案不會開外部編輯器，也不會把路徑貼進終端機。

**Changes**：
- 依序執行 `git status`、`git diff HEAD --numstat`（repo 還沒有 HEAD 時改用 `--cached` 加工作區），點進單一檔案時執行 `git diff [HEAD] -- <path>`。每個讀取命令都加 `--no-optional-locks`、`core.fsmonitor=false`、`diff.autoRefreshIndex=false`。未追蹤檔直接讀出來，整份當新增。
- 清單把已 stage、未 stage、未追蹤的檔案混成一張，上限 1000 個。每列顯示狀態字母、淡色的目錄、檔名、+／− 行數；標頭顯示檔案數和總 +／−。不是 git repo 時顯示「Not a git repository」。
- 差異只有合併成一欄的顯示方式：一欄行號（刪除行用舊行號，其餘用新行號），+／− 符號拿掉。新增、刪除行用 9% 的綠、紅底色，hunk 標頭用 raised 底色，tab 展開成 4 格。每行 20 px，字型是終端機字型 11 px。只畫看得到的行，上限 1500 行、每行 4000 字元。第一個檔案自動打開。
- 唯一的動作是 commit：`git add -A` 再 `git commit -m`，在輸入框按 Enter 也會 commit。沒有 stage、discard、送給 Agent 這類動作。

## 設定、字型、快捷鍵

- 設定檔是 `%USERPROFILE%\.config\herdr-app\settings.json`，共 20 個鍵。主要範圍：zoom 0.5～3；終端機字型預設 JetBrains Mono 14，字重 500，粗體 700；行高與字寬倍率 0.8～2；捲動速度 0.1～5。
- 套用順序：版面自己的外觀設定 ＞ settings.json ＞ 預設值。檔案改了會即時套用（監看檔案，另每 400 ms 輪詢一次）。寫入時保留它不認得的鍵；檔案解析失敗時不覆寫，並跳錯誤 toast。
- 設定視窗分頁依序是 Look、Keyboard（只有 macOS）、Machines、Alerts、Updates、Privacy。沒有儲存鈕，每次點選就重新讀檔，確認檔案沒被別人改過才寫入（最多重試 3 次）。選主題會存在目前的版面，不寫進 settings.json。
- 字型：介面 Geist，固定 13 px × zoom（只能靠 zoom 調整大小）；終端機 JetBrains Mono；Geist Mono 和 Symbols Nerd Font 也隨附。指定的終端機字型沒有安裝時，整個退回 JetBrains Mono。Nerd Font 圖示直接用隨附的符號字型；其他後備字型的字太寬時縮小到格子裡；框線字元用畫的，不用字型。
- zoom 每次 ±0.1：Ctrl+=、Ctrl++、Ctrl+-、Ctrl+0。第一次啟動時讀 Windows 的文字大小設定當 zoom，螢幕寬度 2400 px 以上再乘 1.15。
- 快捷鍵完整表在 `spec-settings-fonts.md` 第 3 節。`ctrl-alt-shift-escape` 保留給「重設版面與快捷鍵」。沒改過的舊 keys.json 會自動升級到新預設；改過的就保留。

## 程式辨識與資料夾追蹤

- 內建 124 個程式的對照表（圖示、名稱、顏色），使用者的 programs.json 逐欄覆蓋。終端機顯示什麼，優先用 Agent 自己回報的，否則看前景程式；會拆開包裝層（例如 `pwsh -NoExit -Command "& '…'"`）、去掉 `.exe`，直譯器顯示它執行的腳本。不認得的程式依名稱雜湊配一個顏色。
- 前景程式的判斷：根程序是 shell 時，取它最新的直接子程序（略過 conhost、OpenConsole），否則就是根程序本身；孫程序不算，所以 Agent 呼叫的輔助程式不會取代它。每 500 ms 檢查一次。
- 資料夾讀取：從前景程序的 PEB 讀 `CurrentDirectory`（x64 偏移 0x38），32 位元程序略過，herdr 不解析 OSC 7。PowerShell 的 `cd` 不會改變程序的工作目錄，所以 herdr 啟動 pwsh 時，在使用者的提示字元函式外面包一層，每次出現提示字元就把目前位置寫進 `[Environment]::CurrentDirectory`；cmd 的 `cd` 本來就會改，不需要處理。
- 資料夾變動會更新：沒命名的 space 名稱、側欄分支、面板標題的資料夾、新分頁與分割的預設資料夾、終端機輸出裡相對路徑連結的解析。git 只在資料夾變了、一個指令或 Agent 回合結束、或視窗在前景時每 60 秒，才重新查詢（`git -C <cwd> rev-parse --show-toplevel --abbrev-ref HEAD`）。

## 提醒與首次啟動

- 音效預設開啟，可換成自己的 MP3，Windows 上用 MCI 播放。「Needs you」一定響；「完成」只在沒看的時候響並跳 toast。沒有找到系統通知或工作列閃爍。
- 第一次啟動（`shared.json` 沒有 `frame.welcomed`）開 4 頁歡迎視窗：外觀與 zoom、加機器、開啟面板、命令面板快捷鍵。按 Esc 記為略過；完成時若沒有任何 space 就開一個。

## 跟 AgentDesk 目前的差異

| 項目 | herdr-app | AgentDesk 現況 |
|---|---|---|
| 標題列 | 自繪，視窗按鈕在分頁列 | 系統標題列 |
| 側欄寬度 | 268 px，可拖曳，窄欄模式 | 固定 256 px，只能整個隱藏 |
| Space 列 | 字母頭像、`repo · branch`、↑↓、worktree 樹狀 | 頭像、分支，沒有 ↑↓、沒有 worktree 樹狀 |
| Agents 列 | 四段優先度、釘選、時間、spinner | 已有分段與釘選，列內容與時間格式待逐項對照 |
| 底部 | Settings、Add machine | Settings、Exit and stop all |
| 分頁 | 膠囊、拖曳排序、滾輪切換 | 有分頁，缺拖曳與滾輪 |
| Files／Changes | 右側面板 | 沒有 |
| 面板 | 滑鼠移上才出現按鈕、拖曳重排 | 按鈕常駐，只能用快捷鍵互換 |
| New 選單 | Terminal、Agent、space、worktree | 新 space、分頁、分割直接開 shell，Run... 選程式 |
| 資料夾追蹤 | PEB＋PowerShell 提示字元 hook | 2026-11-18 已在 host 加上 `live_cwd`，UI 尚未使用 |
| 字型 | Geist 13、JetBrains Mono 14／500 | Segoe UI、Cascadia Mono，字級可調 |
| 設定分頁 | Look、Machines、Alerts、Updates、Privacy | Look、Terminal、Alerts、Mods、Machines、Worktrees |
| 首次啟動 | 4 頁歡迎視窗 | 沒有 |
| 配色 | graphite 等主題 | midnight、ember、moss、paper（使用者同意配色可以不同） |

## 實作順序

1. UI 使用 `live_cwd`：space 名稱、分支、面板標題、新分頁資料夾。
2. 視窗版面：自繪標題列、側欄拖曳寬度與窄欄、分頁列膠囊與 Files／Changes 按鈕、狀態列。
3. 左側 Spaces 與 Agents 照上面的列內容和互動重做。
4. 面板外框：滑鼠移上才出現按鈕、拖曳重排。
5. Files、Changes 面板。
6. New 選單、picker、命令面板的細節。
7. 字型改用隨附的 Geist 與 JetBrains Mono，zoom 照 herdr 的規則。
8. 設定視窗分頁、首次啟動歡迎頁。
