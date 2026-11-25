---
contract_type: PRD
version: 5.0.1
status: approved
author_agent: PM
sprint: 11
created_at: 2026-11-16T00:00:00+08:00
approved_at: 2026-11-25T00:00:00+08:00
skills_used:
  - shared/domain-coding-agents@1.0.0
upstream_deps:
  - raw_requirement@2026-11-16
  - docs/design/AgentDesk_Design.md@3.1
  - sprint/current/status.md@2026-11-25
downstream_consumers:
  - RD
  - QA
change_summary: |
  5.0.1（Patch）：更新 macOS 的驗證狀態（使用者 2026-11-25 要求），需求、驗收條文與數字不變，仍鎖定設計文件 v3.1。
  第 7 節與 2.11 開頭改為已在 macOS 建置並通過自動測試、人工驗收未完成，並列出 FR-43 的兩項落差。
  上一版保存在 `contracts/deprecated/PRD_v5.0.0.md`。
  5.0.0（Major）：合併兩份同為 4.0.0 的契約（Windows 端鎖定設計文件 v2.27、macOS 端鎖定 v3.0；使用者 2026-11-24 要求合併）。
  以 macOS 端的重寫版為底，Windows 端的版面與主題需求原文放進 2.11，需求內容與數字不變。
  編號相撞的需求改號，屬於重大變更，所以升主版號：Windows 端的 FR-39「Mac 風格的視窗按鈕」改為 FR-58，
  FR-40「Mission Control」（已移除）改為 FR-59；macOS 端的 FR-39、FR-40 不變。FR-58 只適用 Windows（ADR-44）。
  FR-27 的內建版面改為 2.11 的九個。下游 RD、QA 需重新確認 FR-27、FR-39、FR-40、FR-58、FR-59。
  合併前的兩份保存在 `contracts/deprecated/PRD_v4.0.0-windows.md`、`PRD_v4.0.0-macos.md`。
  4.0.0（Major）：使用者要求依照現有程式重寫一份完整的 PRD（2026-11-24）。改寫方式：
  需求本文不再分散在「v1.2.0 新增」「v2.0.0 新增」「v3.2.0 新增」各節，改依功能領域整理成一份；
  每項需求的驗收直接寫在本契約，不再引用設計文件 1.2.1（設計文件 v3.0 已不含 PRD 章節）。
  需求 ID（US-01～US-07、FR-20～FR-40）全部沿用，數字不變。
  內容依 0.2.0 的程式校正：FR-24 的 worktree 位置寫成兩個平台的路徑；FR-26 的遠端機器不再限定 Windows，
  改為「對方裝了 AgentDesk、agentctl 可執行」，跨平台互連是否保證仍待 OQ-11；FR-32 加上 macOS 的 shell；
  FR-36 併入 macOS 的熱更新；FR-29 列出實作的介面元件。新增第 7 節「已知落差」，列出程式與需求不一致、
  尚未修正的地方（寫成當天已全部修正）。刪除了已不適用的敘述（AgentDesk 98 的歷史、Lua mod），所以升主版號。
  上一版保存在 `contracts/deprecated/PRD_v3.2.0.md`，更早的版本紀錄見該檔。
---

# Product Requirements Document：AgentDesk

> 本契約鎖定設計文件 **v3.1**（`docs/design/AgentDesk_Design.md`），對應產品版本 0.2.0。設計文件之後的變更若影響第 2～5 節，必須依憲法 6.3 將本契約升版，下游才能採用。各需求對應的設計文件章節見第 8 節。

## 1. 產品概述

### 1.1 背景與問題

開發者同時在多個終端機跑 coding agent CLI（Claude Code、Codex CLI 等）時，很難掌握哪個在等輸入、哪個已經結束、各自在改哪個專案。一般桌面終端機自己持有執行中的程序，視窗一關，工作就跟著中斷。

AgentDesk 是原生的 AI Agent 工作台：在可分割的終端機面板裡跑多個 Agent，側欄顯示哪個需要你、哪個做完了；終端機與子程序放在背景程序 agent-host 裡，關掉視窗工作照常進行，重開時接回原畫面。它管理的是實際的 CLI 工作階段，不代理模型 API，也不處理 Agent 的登入與訂閱。

介面與行為依 herdr-app 的原始碼複刻（commit 321c9a8，ADR-15），全部用 C++ 重寫，不追求與 herdr 的協定相容。

### 1.2 目標使用者

同時操作兩個以上 coding agent CLI 的開發者，以一般使用者權限使用以下任一平台：

- Windows 11 x64。
- Apple Silicon 的 macOS 13 Ventura 以上（ADR-30）。

首發支援 Claude Code 與 Codex CLI，其他命令列程式也能在面板裡執行。這些 CLI 由使用者自行安裝與登入。

### 1.3 產品目標

| 目標 | 數值 | 量測 |
|---|---|---|
| 並行 | 4 個 shell 穩定，其中 2 個實際 Agent 同時互動 | 實機 |
| 互動延遲 | 本機 echo，從按鍵到畫面更新 p95 < 100 ms（不含模型回應） | `session_bench` |
| UI 續接 | 4 個 session、每個 120×40，p95 < 2 秒到可操作 | QA-05 |
| 記憶體 | host＋UI 額外私有記憶體 < 500 MB（不含子程序） | `session_bench`、soak |
| 輸出壓力 | 合成 1 MiB/s 持續 60 秒，host 持續消費 | `session_bench` |
| 穩定性 | 8 小時 soak、100 次建立／停止，handle 與執行緒沒有持續增長 | `soak` |

基準機：Windows 11 x64、8 邏輯核心以上、16 GB RAM、1080p、150% DPI。macOS 的基準機在 macOS 版正式發布前由使用者指定。

### 1.4 成功指標

- 設計文件第 10 章的驗收矩陣（QA-01～QA-22）全數通過，或未通過的項目已寫進限制清單並經使用者同意。
- 沒有任何一次把未知的 Agent 狀態標成 Done。
- 第 1.3 節的數值目標全數達成，並附測試報告。
- 第 2 節每項需求都有自動化測試或實機證據（欄位「驗證」）。

## 2. 功能需求

格式：每項需求有優先級、描述、驗收（Given／When／Then）與驗證。P1 沒有就不能發布；P2 首發應該要有，必要時可以延到下一版；P3 可有可無。

### 2.1 終端機工作階段（核心）

#### US-01 啟動工作
**優先級**：P1
**描述**：依 profile、工作目錄與名稱啟動程式（Agent 或 shell），顯示工作目錄、命令與可互動的終端機。executable 與參數分開保存，不把 prompt 拼進 shell 字串；Windows 拒絕直接啟動 `.cmd`／`.bat`。
**驗收**：
- Given 有效的 profile，cwd 含空白與中文字，When 使用者啟動，Then 面板從 Starting 變成 Running，程式的實際工作目錄等於指定的 cwd，可以輸入。（QA-01）
- Given profile 的 executable 或 cwd 不存在，When 使用者啟動，Then 回報原因（`PROFILE_INVALID` 或 `SPAWN_FAILED`），沒有殘留程序。（QA-02）

**驗證**：`Session.*`、`HostFixture.*`、QA-01、QA-02。

#### US-02 多工作並行
**優先級**：P1
**描述**：每個工作有獨立的輸入、畫面、尺寸與程序狀態。一個 session 同時只有一個輸入控制者，只有控制者能改變終端機尺寸；其他視窗唯讀，明確接手後才能操作。
**驗收**：
- Given 兩個工作都在持續輸出，When 使用者在其中一個輸入，Then 只有它收到輸入，另一個的畫面與尺寸不變。（QA-03）
- Given 快速調整尺寸 100 次，When 停止調整，Then host、引擎與 UI 的尺寸一致。（QA-04）
- Given 第二個視窗 attach 到同一個工作，When 它嘗試輸入或改尺寸，Then 被拒（`NOT_CONTROLLER`）；按「Take control」後才能操作。（QA-14）

**驗證**：QA-03、QA-04、QA-14。

#### US-03 背景續跑與重新連線
**優先級**：P1
**描述**：關閉視窗只會中斷連線，agent-host 繼續執行所有工作；重開後以快照加增量接回原畫面與版面。host 以背景程序啟動，脫離啟動它的終端機。
**驗收**：
- Given 工作每秒輸出一行計數，When 使用者關閉視窗 30 秒後重開，Then PID 與 session 不變，計數連續，2 秒內可以操作。（QA-05）
- Given attach 與輸出同時發生，When 接回，Then 畫面沒有缺漏；增量缺號時重新取得快照。（QA-06）
- Given 從終端機啟動 UI，When 使用者關閉那個終端機，Then host 與工作仍在；Windows 上無法脫離啟動者的 Job 時，狀態列事先提示。（QA-20）
- Given host 異常結束，When 使用者重開，Then 原本的工作標成 Lost，不憑舊 PID 接管程序，也不重送 prompt。（QA-10）

**邊界**：登出、重開機、host 異常結束後不保證續跑（ADR-07）。

**驗證**：QA-05、QA-06、QA-10、QA-20。

#### US-04 Agent 狀態提示
**優先級**：P1
**描述**：程序狀態（Starting、Running、Stopping、Exited、Failed、Lost）與 Agent 狀態（Unknown、Idle、Working、Blocked、Done、Error）分開保存與顯示。Agent 狀態只接受結構化回報：OSC 7501（Program Status Protocol，Claude Code 2.1.295 起原生支援）為主，`agentctl report-state`／`agentctl hook` 為輔，沒有可信來源就是 Unknown，不從畫面文字推斷（ADR-09）。
**驗收**：
- Given Claude Code 在工作中執行，When 它等待權限確認，Then 清單顯示 Blocked（Needs you）、來源與更新時間。（QA-21）
- Given 工作沒有可信的狀態來源，When 輸出靜默 60 秒，Then 仍顯示 Unknown，不會變成 Done。（QA-07）
- Given 同一來源送出亂序的回報，When host 處理，Then 依 seq 只接受較新的；OSC 7501 的 seq 由 host 依收到順序指派。（QA-08、QA-19）
- Given 程式送出 `OSC 7501 ; ?` 再送 `CSI c`，When host 回覆，Then OSC 7501 的回覆排在 DA1 回覆之前。（QA-21）
- Given OSC 7501 的根紀錄被清除或丟棄，When 顯示，Then agent_state 是 Unknown，原值保存在 `last_agent_state`。（QA-08）
- Given 程式結束，When host 處理，Then working 與 blocked 紀錄被丟棄，done 與 error 保留。（QA-22）

**驗證**：`VtEngine.*`、`ProgramStatus.*`、`Session.*`、QA-07、QA-08、QA-19、QA-21、QA-22。

#### US-05 停止工作
**優先級**：P1
**描述**：停止先送中斷（Ctrl+C，0x03），最多等 5 秒；仍在執行時由使用者明確選擇強制終止，結束整個受管理的程序樹（Windows：Job；macOS：該終端機的所有程序）。
**驗收**：
- Given 子程序忽略 Ctrl+C，When 使用者停止並在 5 秒後選擇強制終止，Then 整個程序樹結束，沒有殘留。（QA-09）
- Given 根程序已結束但子程序還在，When 顯示，Then 標出還在執行的程序數。

**驗證**：QA-09、`PosixPty.CloseEndsJobsInOtherProcessGroups`。

#### US-06 捲動、選取與複製
**優先級**：P1
**描述**：每個工作的歷史保留 10,000 行、上限 16 MiB，先達到者截斷最舊的資料。可捲動、選取、複製畫面與歷史的文字。程式輸出不能寫入剪貼簿（不支援 OSC 52）。
**驗收**：
- Given 畫面含中文、組合字元與雙欄字元，When 使用者選取並複製，Then 剪貼簿內容欄位正確、不含控制碼。（QA-11）
- Given 歷史已達上限，When 使用者往上捲，Then 顯示已截斷，記憶體維持在預算內。（QA-12）
- Given 程式輸出 OSC 52，When host 處理，Then 剪貼簿不變。（QA-16）

**驗證**：QA-11、QA-12、QA-16。

#### US-07 CLI 自動化
**優先級**：P1
**描述**：`agentctl` 提供 `run`、`list`、`read`、`send`、`stop`、`wait`、`report-state`、`report`、`label`、`shutdown`、`hook`、`bridge`、`update`。加上 `--json` 時 stdout 只有結果，診斷寫到 stderr。退出碼固定：0 成功、2 參數錯誤、3 host 無法連線、4 session 不存在或名稱歧義、5 操作遭拒、6 啟動失敗、7 逾時。
**驗收**：
- Given 腳本以 `--json` 呼叫 `agentctl list`，When 指令完成，Then stdout 只有 JSON，退出碼符合上述定義。（QA-13）
- Given 腳本呼叫 `agentctl send` 而沒有 `--enter`，When 指令完成，Then 只送出文字，不送 Enter。（QA-13）
- Given RPC 逾時，When 回報，Then 退出碼 7，訊息說明操作可能已經執行。

**邊界**：名稱對到多個 session 時拒絕（`AMBIGUOUS`）；穩定的自動化應使用 session UUID。

**驗證**：QA-13、`HostFixture.*`。

### 2.2 工作區與介面

#### FR-20 space、分頁與分割
**優先級**：P1
**描述**：工作依 space（工作地點，通常是一個 repo，可以在遠端機器上）→ 分頁 → 分割面板組織。可往右或往下分割、放大單一面板、用鍵盤切換與交換、拖曳分隔線調整大小、拖曳面板換位置、改名。版面存在 agent-host（`ui.state`），所有視窗共用，重開後還原。
**驗收**：
- Given 兩個分頁與一個分割，When 關閉並重開 UI，Then 版面相同。
- Given 一個分頁有兩個面板，When 使用者拖曳其中一個的把手放到另一個面板中間，Then 兩個面板互換；放在邊緣時移到那一側。

**驗證**：`HostFixture.UiStateIsSharedAndPersisted`、`Workspace.*`。

#### FR-29 介面與行為依 herdr-app 複刻
**優先級**：P1
**描述**：版面尺寸、互動、狀態與提醒規則、picker、命令面板、設定、歡迎頁、worktree 對話框都照 herdr-app 的行為。實作的元件：
- 側欄：品牌列、Spaces（字母頭像、`repo · branch`、↑N／↓N、worktree 樹狀、拖曳排序、遠端機器標頭）、Agents（依優先度或依 space 分組、釘選、子 Agent 樹、上次變動時間）、底部的 Settings 與 Add machine。側欄預設 268 px，可拖曳在 44～480 px 之間，窄於 160 px 改成窄欄。
- 分頁列：膠囊分頁、新增選單、Files／Changes 按鈕；Windows 的視窗按鈕畫在分頁列右端，macOS 保留系統紅綠燈。
- 面板：圓角外框、標題列（程式圖示、名稱、資料夾、狀態）、滑鼠移上才出現的分割／放大／關閉按鈕、拖曳把手。
- 狀態列：機器、分支、Agent 數與需要你的數量、命令面板提示。
- 其他：Details 欄、peek（預覽子 Agent）、What's new、Send feedback、程式表與圖示、框線與方塊字元依格子繪製、分頁收起（`tab.settle`）。

**驗收**：
- Given 一個已收起的分頁，When 裡面的 Agent 需要使用者，Then 分頁自動回到分頁列。
- Given 程式開啟滑鼠回報（DECSET 1000／1002／1003），When 使用者在面板裡點擊，Then 事件以 SGR 編碼送給程式；按住 Shift 時改為選取。
- Given 使用者在資料夾的 `programs.json` 改了某個程式的名稱或圖示，When 存檔，Then 立刻套用；檔案有錯時原因寫在 `programs.error`。
- Given 程式輸出框線（U+2500–257F）、方塊（U+2580–259F）或點字（U+2800–28FF），When 終端機繪製，Then 依格子的像素邊界畫成圖形，上下列之間不留縫。
- Given 使用者寫好 Send feedback 的草稿，When 按 Copy 或 Save to file，Then 內容被複製或存到資料夾的 `feedback/`，不會自動送出。

**驗證**：`Workspace.SettledTabsAreSkippedUntilShown`、`HostFixture.MouseEventsReachAProgramThatAskedForThem`、`Programs.*`、`Sprites.*`；使用者整體驗收（2026-11-19）。

#### FR-32 新增 space 或分頁時選擇 shell
**優先級**：P1
**描述**：新 space、新分頁與分割一律先開 shell，位置是目前面板 shell 所在的資料夾。側欄 Spaces 的「+」列出這台電腦偵測到的 shell，選哪個就用哪個開新 space；分頁列「+」選單列出預設以外的 shell。偵測到的 shell：Windows 是 PowerShell 7、Windows PowerShell、Command Prompt 與各 WSL 發行版；macOS 是登入 shell 與 `/etc/shells` 列出的 shell。預設 shell 可在設定 › Look 指定；沒指定時，Windows 依序找 PowerShell 7、Windows PowerShell、Command Prompt，macOS 用登入 shell（ADR-20）。
**驗收**：
- Given 這台電腦有多個 shell，When 使用者按側欄 Spaces 的「+」並選其中一個，Then 用它開一個新 space。
- Given 同上，When 使用者打開分頁列的「+」選單，Then 預設以外的 shell 都列出來，選了就開成新分頁，同一個 shell 不會出現兩次。
- Given 同上，When 使用者按新 space 或新分頁的快捷鍵，Then 直接用預設 shell 開啟。

**驗證**：使用者驗收（2026-11-19）；macOS 實機（2026-11-24）。

#### FR-38 視窗開啟的位置與大小
**優先級**：P2
**描述**：視窗記住上次的螢幕、位置、大小與是否最大化（`window.json`，以邏輯像素保存）。沒有紀錄時，大小是 1280×820 邏輯像素乘上 zoom，限制在螢幕可用範圍的 50%～90% 並置中（ADR-23）。
**驗收**：
- Given 第一次啟動、Full HD 100%、zoom 1.0，When 視窗打開，Then 大小 1280×820，置中。
- Given 第一次啟動、4K 150%、zoom 1.2，When 視窗打開，Then 大小 1536×984 邏輯像素。
- Given 使用者移動並調整視窗後關閉，When 再次開啟，Then 回到同一個螢幕、位置與大小；關閉時是最大化就以最大化開啟。
- Given 上次的螢幕已經不在，When 開啟，Then 以上次的大小置中於滑鼠所在的螢幕。

**驗證**：`WindowPlace.*`。

### 2.3 注意力與狀態生命週期

#### FR-21 注意力與通知
**優先級**：P1
**描述**：Agent 在沒被看時完成標為 Done，看過才變 Idle（這個已讀狀態屬於 UI，不回寫 host）；分頁與 space 顯示其中最緊急的狀態，排序是 Needs you > Done > Working > Idle。需要使用者（含 Error）時一定播放提示音；只有沒在看它時才在右下角跳通知，點通知跳到該面板。提示音與通知可在設定 › Alerts 分別關閉，提示音可換成自己的 MP3。
**驗收**：
- Given 使用者沒在看某個 Agent，When 它完成，Then 標為 Done、播放提示音並跳出通知，點通知跳到該面板，看過後變成 Idle。
- Given 任一 Agent，When 它需要使用者，Then 一定播放提示音；只有沒在看它時才跳出通知。
- Given UI 剛啟動、第一次取得清單，When 顯示，Then 不發出任何提醒。
- Given 使用者在設定 › Alerts 關閉「Show notes」，When 沒在看的 Agent 完成，Then 照樣播放提示音，但不跳出通知。

**驗證**：`Attention.*`（含 `Attention.AlertRules`）。

#### FR-31 Agent 狀態的生命週期
**優先級**：P1
**描述**：Agent 結束時只清掉它的狀態，終端機（shell）留著；space 關閉時，裡面的終端機才會被結束。shell 顯示新提示（OSC 133 A）表示前一個程式已經結束，host 丟掉該 session 的全部 OSC 7501 紀錄。host 啟動 shell 時加上提示鉤子：Windows 是 PowerShell、Command Prompt、WSL（bash）；macOS 是 zsh 與 bash，同時送出 OSC 7 回報資料夾。Agents 清單只列出各 space 面板裡、正在執行而且現在有狀態的終端機。已結束或 Lost 的終端機，面板會被移除，分頁空了就關閉分頁，space 空了就關閉 space。
**驗收**：
- Given Claude Code 在 shell 裡回報 Working，When 它結束、shell 顯示提示，Then 該 session 的 OSC 7501 紀錄全部被丟掉，agent_state 變成 Unknown，原值存進 `last_agent_state`，Agents 清單與狀態點一起清掉。
- Given 一個終端機已結束或 Lost，When 使用者重開 UI，Then 它的面板被移除，已結束的 session 不會被開成 space。

**驗證**：`VtEngine.ShellPromptOsc133DropsEveryRecord`、`VtEngine.ShellPromptOsc133ClearsWorking`；實機（2026-11-19）。

### 2.4 終端機操作與快捷鍵

#### FR-22 命令面板與快捷鍵
**優先級**：P1
**描述**：命令面板（模糊搜尋、最近用過的排前面）、快捷鍵說明（列出所有按鍵、可篩選）、跳轉清單（picker，搜尋 space、分頁與 Agent）。快捷鍵可用資料夾裡的 `keys.json` 修改：區段 `global`、`terminal`、`layout:<版面>`、`mod:<名稱>`；動作可以是命令、`命令:值`、`{"command","args"}`、`send:<文字>` 或 `terminal`（交給程式）。檔案有錯時整份不套用，原因寫在 `keys.error` 並顯示在設定頁；存檔後立刻生效。綁定的按鍵不送進終端機程式。
**驗收**：
- Given `keys.json` 把某個命令綁到一組按鍵，When 終端機有焦點時按下，Then 執行該命令，按鍵不送進程式。
- Given `keys.json` 有一項寫錯，When 存檔，Then 整份不套用，錯誤顯示在設定 › Look，原本的按鍵照常可用。

**驗證**：`Keymap.*`。

#### FR-23 搜尋與選取
**優先級**：P1
**描述**：終端機可搜尋畫面與歷史（最多 5000 行，不分大小寫），雙擊選字、三擊選行、放開即複製（可關閉），拖曳超出邊緣時自動捲動。多行貼上先確認，單次貼上上限 1 MiB、不得含 NUL。連結要按住 Ctrl（macOS 為 Cmd）點擊才開啟，只開白名單內的 scheme 與存在的檔案；程式與指令檔只在檔案總管或 Finder 中標出，不執行。
**驗收**：
- Given 畫面與歷史裡有某段文字，When 使用者搜尋，Then 從最新的符合處開始標出，Enter 往舊、Shift+Enter 往新。
- Given 開啟放開即複製，When 雙擊一個字或三擊一行後放開，Then 選取並複製到剪貼簿。
- Given 畫面上有一個指向 `.exe`（macOS：`.app`、可執行檔）的檔案路徑，When 使用者 Ctrl＋點擊，Then 在檔案總管（Finder）中標出，不執行。

**驗證**：`Links.*`、`InputMapping.*`；截圖驗證。

#### FR-40 macOS 的快捷鍵與 Option 鍵
**優先級**：P1
**描述**：macOS 用獨立的預設鍵表（照 herdr-app 的 `keys.macos.json`，以 Cmd 為主，`cmd-1..9` 選分頁、`ctrl-1..9` 選 space）。`keys.json` 的和弦多一個 `cmd` 修飾鍵（只在 macOS 有效），`ctrl` 指 Control 鍵；按鍵提示以 ⌃⌥⇧⌘ 顯示。Cmd 只用於 AgentDesk 的命令，不會送進程式；Control 照原樣送給程式。設定 › Keyboard 的 `option_as_alt`：`auto`（美式與 ABC 鍵盤配置時當 Alt）、`on`、`off`、`left`、`right`。
**驗收**：
- Given macOS 版、預設鍵表，When 使用者按 `keys.macos.json` 綁定的組合鍵，Then 執行對應命令，按鍵不送進程式。
- Given 終端機有焦點，When 使用者按 Control+C，Then 0x03 送給程式，不會被當成複製。
- Given `option_as_alt` 設為 `left`，When 使用者按左 Option＋B，Then 送出 ESC b；按右 Option＋B 時照鍵盤配置輸入字元。

**驗證**：`Keymap.*`（含 `KeymapMac.*`）；macOS 實機驗收（待做，見設計文件 12.2）。

### 2.5 專案工具

#### FR-24 git worktree
**優先級**：P2
**描述**：可為 repo 建立 git worktree 並開成新 space，也可開啟或移除既有的 worktree。新 checkout 預設在家目錄的 `.agentdesk/worktrees/<repo>/<分支>`（Windows：`%USERPROFILE%\.agentdesk\worktrees\…`；macOS：`~/.agentdesk/worktrees/…`），位置可在設定 › Look 更改；遠端機器由那台的 agent-host 決定位置。移除會關閉該 checkout 裡的 space 並刪除 checkout 資料夾，分支保留；有未提交的變更或子模組有未推送的 commit 時先警告；主 checkout 不能移除。
**驗收**：
- Given 一個 git repo 的 space，When 使用者執行 New worktree 並輸入分支名稱，Then 在預設位置建立 checkout，並以預設 shell 開成新 space。
- Given 一個有未提交變更的 worktree，When 使用者選擇移除，Then 先警告，確認後才移除。

**驗證**：`HostFixture.WorktreeAddCreatesCheckout`。

#### FR-30 Files／Changes 面板
**優先級**：P1
**描述**：右側面板跟著目前面板 shell 所在的資料夾（shell 送的 OSC 7 優先，其次是程序的工作目錄）。Files 是檔案樹，檔名旁有 git 狀態，可唯讀預覽（1 MiB 以下的文字檔，含語法上色）；Changes 列出未提交的檔案與 +／− 行數、差異，可輸入訊息 commit（先 `git add -A`）。每 10 秒更新一次，本機另有資料夾變更監看。遠端機器也可用。
**驗收**：
- Given 目前面板的 shell 在一個有未提交變更的 repo 裡，When 使用者打開 Changes，Then 列出所有未提交的檔案與 +／− 行數，點檔案顯示差異，輸入訊息可以 commit。
- Given 一個在遠端機器上的 space，When 使用者打開 Files 或 Changes，Then 內容經該機器的 agent-host（`fs.list`、`fs.read`、`git.run`）取得。

**驗證**：使用者整體驗收（2026-11-19）。

### 2.6 協作與自動化

#### FR-25 子 Agent
**優先級**：P2
**描述**：在 AgentDesk 終端機裡執行的 Agent，可以用 `agentctl` 開子 Agent、給第一則任務、等待它、接收它的回報。`agentctl --skill` 印出給 Agent 看的使用說明。子 Agent 不自動開成面板，列在父 Agent 底下，可以預覽（peek）或開成面板。
**驗收**：
- Given 一個在 AgentDesk 終端機裡執行的 Agent，When 它執行 `agentctl run --task "<任務>"`，Then 新 session 的 parent 是它；任務在子 Agent 就緒後送出（回報 Idle，沒有狀態來源時等 8 秒，最晚 24 秒）。
- Given 子 Agent 執行 `agentctl report`，When 父 Agent 不在工作也不在等使用者，Then 文字加上 `[from <名稱> <id>]` 送進父 Agent 的輸入；否則先排隊。
- Given `agentctl wait --until idle|blocked|exited|change`，When 條件在逾時前成立，Then 退出碼 0；逾時退出碼 7。

**驗證**：`HostFixture.SubAgentTaskReportAndWait`。

### 2.7 遠端機器

#### FR-26 遠端機器
**優先級**：P2
**描述**：可加入 ssh 連得上的機器，它的 space 與 Agent 顯示在同一個側欄，程式在那台機器上執行。「Add machine」從 `~/.ssh/config` 列出主機，也可輸入 `user@host`。UI 執行 `ssh <目標> agentctl bridge`，之後走同一套協定。AgentDesk 不在背景要求輸入密碼；需要密碼或確認主機金鑰時，顯示 ssh 的訊息。機器沒連上時，它的面板保留。
**驗收**：
- Given 遠端已安裝 AgentDesk、`agentctl` 在 PATH 上（或填了完整路徑）且設好金鑰登入，When 使用者在 Add machine 選這台主機，Then 遠端的 space 與 Agent 列在同一個側欄。
- Given ssh 需要密碼，When 連線，Then 不卡住，設定頁顯示 ssh 的錯誤訊息。

**邊界**：Windows 與 macOS 能不能互為遠端機器還沒決定，也還沒驗證（OQ-11）。

**驗證**：`HostFixture.ProtocolOverBridgeCommand`；實際 ssh 連線待人工驗收。

### 2.8 擴充與外觀

#### FR-27 版面與 mod
**優先級**：P2
**描述**：
- **版面**：內建九個版面（workspace 為預設，七個列出、兩個好玩的，見 2.11），另可放自訂的 JSON 版面；使用者的拖曳與開關另存，不改版面檔。可複製與重設內建版面。
- **mod**：C++ 動態函式庫（Windows `mods/<名稱>/mod.dll`、macOS `mods/<名稱>/mod.dylib`），透過純 C 介面加入命令、面板、通知，以及分頁與 Agent 旁的裝飾文字（ADR-17）。只載入已啟用的 mod；啟用前先說明權限；mod 當掉就停用，AgentDesk 照常執行。隨附三個範例 mod，預設不啟用。

**驗收**：
- Given 目前是 workspace 版面，When 使用者切到 terminals，Then 側欄與狀態列消失，space 改在上方列選；上方列最左邊的按鈕可切回有側欄的版面。
- Given 一個 mod 是關閉的，When 使用者在設定 › Mods 打開開關，Then 先顯示權限說明，按「Turn on」才啟用，按「Cancel」維持關閉。
- Given 已啟用的 mod，When 它的程式當掉，Then 該 mod 被停用、錯誤顯示在設定頁，AgentDesk 照常執行。
- Given 一個用比 AgentDesk 新的介面版本編譯的 mod，When 載入，Then 拒絕並在設定頁說明。

**驗證**：`Layout.*`、`ModsTest.*`。

#### FR-33 程式裡只出現 AgentDesk
**優先級**：P2
**描述**：執行檔、畫面與跟著發布的檔案只寫 AgentDesk（ADR-21）。保留：THIRD_PARTY_NOTICES 裡 herdr-app 的授權聲明、修改過的檔案開頭的出處行、版本庫的 `third_party/herdr/`。
**驗收**：
- Given Release 建置輸出，When 在執行檔與建置輸出裡搜尋 herdr，Then 找不到（THIRD_PARTY_NOTICES 依授權保留）。
- Given 一份用舊主題 id 的設定檔，When AgentDesk 啟動，Then 照樣讀得到該主題。

**驗證**：`sprint/current/status.md`（2026-11-19）。

#### FR-34 主題
**優先級**：P2
**描述**：內建八個主題：AgentDesk 的 Midnight、Ember、Moss、Paper，以及 graphite、ink、moss、paper；另外列出程式資料夾與使用者資料夾 `themes/` 裡的主題檔（隨附 atom-one-dark）。外觀可跟隨系統、固定淺色或深色。每個版面各自記住一組深色與淺色主題；版面檔指定的主題優先。
**驗收**：
- Given 使用者把一個主題檔放進資料夾的 `themes/`，When 打開設定 › Look，Then 它和內建主題列在一起。
- Given 目前版面是 workspace、系統為深色，When 使用者選了一個深色主題，Then 只換掉 workspace 的深色主題，其他版面與淺色主題不變。

**驗證**：使用者驗收（2026-11-19）。

### 2.9 更新與使用統計

#### FR-35 AgentDesk 線上更新
**優先級**：P2
**描述**：沒有預設伺服器，設了 `AGENTDESK_RELEASES_URL` 才檢查（ADR-18）。只有從發布包安裝的版本才會更新（Windows：資料夾裡有 `THIRD_PARTY_NOTICES.md`；macOS：`AgentDesk.app`）。啟動 10 秒後檢查，之後每小時一次，失敗 30 分鐘後重試；讀 `app/latest.json` 的 `windows-x86_64` 或 `darwin-aarch64`，下載後比對 sha256 並確認 `--version`。按「Restart」、下次結束程式，或上次沒正常結束時的下次啟動前換版，失敗就還原。更新後第一次啟動顯示 What's new。
**驗收**：
- Given 沒有設定 `AGENTDESK_RELEASES_URL`，When AgentDesk 執行，Then 不連線檢查更新。
- Given 伺服器上有較新版本，When 檢查完成，Then 下載、驗證後在右下角提示，按 Restart 換成新版並重開。
- Given 下載檔的 sha256 與 `latest.json` 不符，When 驗證，Then 不暫存任何檔案。
- Given 換版途中失敗，When 處理錯誤，Then 還原成原本的版本。

**驗證**：`Updates.*`；本機假伺服器實測（2026-11-19）。

#### FR-36 agent-host 單獨更新時接手終端機
**優先級**：P2
**描述**：觸發時機：`agentctl update`、設定 › Updates 的 agent-host 列，或 app 更新後第一次啟動。舊 host 啟動新的 agent-host，凍結每個 session（解析器回到 ground 才停），再把終端機連同完整狀態（畫面、歷史、OSC 7501 紀錄與 seq、待送任務）交給新 host。新 host 核對後回報 ready，舊 host 才放手（ADR-19）。Windows 複製 ConPTY、子程序與 Job 的 handle，只支援隨附 ConPTY；macOS 經 Unix socket 以 `SCM_RIGHTS` 傳 pty master（ADR-30）。
**驗收**：
- Given host 正在跑幾個終端機，When 使用者執行 `agentctl update`，Then 新 host 接手所有 session，程式不中斷；連線會斷，client 重新連上後取得新快照，不把 session 標成 Lost。
- Given 新 host 在 30 秒內沒回報 ready 或出錯，When 交接失敗，Then 舊 host 結束新程序並恢復所有 session。
- Given Windows 使用系統內建 ConPTY，When 要求更新 agent-host，Then 回報錯誤，終端機不受影響。
- Given macOS 上接手的程式之後結束，When 記錄，Then exit code 記為未知（新 host 不是它的父程序）。

**驗證**：`HostUpdate.TerminalsKeepRunningThroughAnUpdate`、`HostUpdate.FailedTakeoverKeepsEverything`、`CoreUpdate.*`；macOS 實機（shell pid 不變，2026-11-24）。

#### FR-37 使用統計
**優先級**：P3
**描述**：沒有預設伺服器，設了 `AGENTDESK_TELEMETRY_URL` 才送出（ADR-18）。每個 UTC 日一份報告，7 天後丟棄，用隨機 id，只含分組後的數量與內建名稱，不含自由文字。設定 › Privacy 可以關閉，並逐字顯示會送出的內容。
**驗收**：
- Given 沒有設定 `AGENTDESK_TELEMETRY_URL`，When AgentDesk 執行，Then 不送出任何統計。
- Given 設了 `DO_NOT_TRACK`、`AGENTDESK_TELEMETRY=0` 或在 CI 環境，When 這次執行，Then 不送出。
- Given 使用者在 Privacy 按「What we send」的 Show，When 畫面打開，Then 顯示待送的報告、今天的草稿與上次送出的內容。
- Given 使用者關閉使用統計，When 關閉，Then 清空待送報告並換一個新的 id。

**驗證**：`Telemetry.*`；本機接收端實測（2026-11-19）。

### 2.10 平台

#### FR-28 WSL 發行版（Windows）
**優先級**：P2
**描述**：已安裝的 WSL 發行版（略過 docker-desktop）自動列為 shell，在目前資料夾開啟；在 bash 裡 cd 後，space 名稱、分支與 Files／Changes 跟著變（ADR-16）。
**驗收**：Given 已安裝 Ubuntu（WSL），When 使用者開「Ubuntu (WSL)」並在 bash 裡 `cd /mnt/d/src`，Then space 名稱、分支與 Files／Changes 換到 `D:\src`。
**邊界**：只支援 bash；`~/.bashrc` 自設 `PROMPT_COMMAND`，或使用 zsh、fish 時，資料夾停在啟動位置。
**驗證**：`CwdUri.*`；實機 Ubuntu。

#### FR-39 macOS 版
**優先級**：P1
**描述**：macOS 版包含第 2 節的全部功能，WSL（FR-28）與隨附 ConPTY 除外。只支援 Apple Silicon、macOS 13 以上，以 `AgentDesk.app` 發布。資料夾對應：Windows 的 `%LOCALAPPDATA%\AgentDesk` 在 macOS 是 `~/Library/Application Support/AgentDesk`，`%USERPROFILE%` 是 `~`。從 Finder 啟動時，終端機仍取得登入 shell 的 PATH、`TERM` 與 UTF-8 的 `LANG`。輸入法組字中的文字畫在終端機游標處。
**驗收**：
- Given 一台 Apple Silicon、macOS 13 以上的 Mac，When 使用者從 `.app` 啟動並在 zsh 裡執行 Claude Code，Then Agent 狀態依 OSC 7501 顯示，第 2.1 節的驗收照樣成立。
- Given 另一個使用者帳號的程式，When 它連線到這個使用者的 host socket，Then 連線被拒。
- Given 使用者停止一個工作，When 中斷後 5 秒仍未結束、使用者選擇強制終止，Then 該終端機的所有程序結束（含 shell 開的背景 job）。
- Given 注音輸入法，When 使用者在終端機裡組字，Then 組字文字顯示在游標處，確定後才送進程式。
- Given Retina 螢幕，When 視窗在 Retina 與非 Retina 螢幕之間移動，Then 文字與格子依縮放比例重新繪製，游標不偏移。

**驗證**：POSIX 整合測試（`PosixPty.*`、`PosixProcess.*`、`PosixIpc.*`、`PosixSystem.*`、`Osc7501Posix.*`）、截圖、實機；注音、Retina 等待人工驗收（設計文件 12.2）。

### 2.11 版面與主題

本節是 Windows 端 4.0.0 的 FR-39（改為 FR-58）、第 10 節與第 11 節原文，只改編號。文中「第 8～11 節」指那份契約的章節。文中的按鍵是 Windows 的，macOS 的見設計文件 7.7。macOS 端已建置並通過自動測試，人工驗收未完成（第 7 節、設計文件 12.2）。

#### FR-58 Mac 風格的視窗按鈕（ADR-42）
**優先級**: P2
**描述**: 視窗按鈕預設是左上角三顆圓鈕（關閉、最小化、縮放），位置與周圍留白依版面的 `traffic_lights`；設定 › Look › Window buttons 可改回 Windows 式的右上按鈕。圖形與顏色由 AgentDesk 自己繪製，不使用 Apple 的素材。設計文件 4.1。
**驗收標準**:
- Given 預設設定，When 視窗打開，Then 左上角有三顆圓鈕，右上角沒有 Windows 式按鈕，品牌列、分頁列與 terminals 上方列都不被圓鈕遮住。
- Given 滑鼠移到圓鈕上，When 停留，Then 三顆都顯示符號（×、−、＋）；點擊分別關閉（只關介面）、最小化、切換最大化。
- Given 視窗不在前景，When 畫面更新，Then 圓鈕顯示為灰色。
- Given 使用者在設定選 Windows，When 關閉設定，Then 圓鈕消失，分頁列右端出現最小化、最大化、關閉。
**驗證**: 截圖（status.md 2026-11-20「Mac 風格的視窗按鈕」）。


使用者從 `docs/design/ideas.md` 選定 A1～A4、B1～B3、T2～T5，T1 延後。實作與規格的差異記在 `docs/design/ideas/IMPL-*.md`，本節以實作為準。截圖在各 IMPL 筆記列的 `build\shots\` 資料夾（不進版控）。v4.0.0 起 FR-59、FR-45、FR-46 已移除、FR-44 改為好玩的版面（第 11 節），條文保留作為歷史。

#### FR-59 Mission Control 版面（ADR-32）（v4.0.0 已移除，由 FR-52 Stage 取代，ADR-38）
**狀態**: 已移除。下面的條文保留作為歷史，不再是驗收依據。仍然有效的部分：「看過」以畫面上實際畫出的終端機判斷（FR-21、設計文件 4.1），Esc 照常送給終端機；這兩點由 FR-52 承接。存檔的 `mission` 開成 Stage，`mission.toggle`、`mission.expand`、`mission.filter` 保留為 Stage 的別名。
**優先級**: P2
**描述**: 內建版面 `mission`（Mission Control）。所有 Agent 排成格子，每格是程式、名稱、狀態、變動距今多久、畫面最後幾行與 OSC 7501 訊息；需要你的格子有紅框並排在最前面。點一格以一般面板開啟那個 Agent。格子用快照畫，不建終端機檢視、不搶控制權。「看過」改以畫面上實際畫出的終端機判斷，所以只看牆面不會清掉 Done。設計文件 7.17、4.1。
**驗收標準**:
- Given 三個 Agent，一個需要你、一個完成、一個工作中，When 使用者按 Ctrl+Shift+M，Then 顯示牆面，順序是需要你、完成、工作中，需要你的格子有 `needs_you` 色的框。
- Given 牆面上有一個完成的 Agent，When 使用者只看牆面、沒點開它，Then 它保持 Done；點開之後才變 Idle。
- Given 牆面，When 使用者點一格或按 1～9、方向鍵加 Enter，Then 切到那個 Agent 的分頁，面板取得鍵盤；再按 Ctrl+Shift+M 回牆面，在牆面上再按一次回到原本的版面。
- Given 牆面或展開的 Agent，When 使用者按 Esc，Then Esc 送給終端機，不離開 Mission Control。
- Given 上方「Needs you」計數為 1，When 使用者點它，Then 只顯示需要你的 Agent；再點一次取消篩選。
**驗證**: （v3.5.0 時）`Mission.*`、`Attention.*`、`Layout.ShippedLayoutsParseAndNameEveryNode`、`LayoutStyles.*`；截圖（IMPL-A12）。v4.0.0 起 `ui::mission` 的共用邏輯（`screen_tail`、`wall_order`、`clean_message` 等）仍由 `Mission.*` 測試，版面相關的兩個測試改寫進 `Stage.*`。

#### FR-41 Focus 模式（ADR-32）
**優先級**: P2
**描述**: 任何版面都能切換的模式，只顯示目前的終端機，底部一條細列顯示其他 Agent 的狀態點。分割與放大狀態不變，重開後仍在 Focus。視窗在前景時的提醒依 `focus_alerts`，預設 `edge_sound`（亮視窗邊緣並播放提示音、不跳通知），所以「需要你時一定播放提示音」仍成立。設計文件 7.17。
**驗收標準**:
- Given 一個分頁有兩個面板，When 使用者按 Ctrl+Shift+.，Then 只顯示焦點面板與底部細列；再按一次回到原本的版面，兩個面板與分割比例不變。
- Given Focus 模式、預設設定、視窗在前景，When 另一個 Agent 需要使用者，Then 視窗邊緣亮一下並播放提示音，不跳通知。
- Given `focus_alerts` 設為 `edge`，When 同上，Then 只亮邊緣，不播放提示音。
- Given Focus 模式、視窗在背景，When 一個 Agent 需要使用者，Then 照平常播放提示音、跳通知。
- Given 底部細列有一個 Agent 的狀態點，When 使用者點它，Then 換成顯示那個 Agent，仍在 Focus 模式。
**驗證**: `FocusMode.*`、`Attention.AlertRules`；截圖（IMPL-A12）。

#### FR-42 Review 版面（ADR-33）
**優先級**: P2
**描述**: 內建版面 `review`（Review）。左邊是目前分頁，右邊是目前 Agent 資料夾的未提交改動、diff 與 commit 區。焦點換到另一個 Agent 時跟著換；焦點移到一般 shell 時，`review_follow` 為 `agent`（預設）保留上一個 Agent。F8／Shift+F8 換檔只在這個版面有作用。設計文件 7.18。
**驗收標準**:
- Given 一個 Agent 的資料夾有 4 個未提交的改動，When 使用者按 Ctrl+Shift+R，Then 右邊列出 4 個檔案並顯示第一個的 diff；再按 Ctrl+Shift+R 回到原本的版面。
- Given Review 版面且終端機有焦點，When 使用者按 F8，Then 換到下一個檔案，F8 不送進終端機；在其他版面按 F8 時照常送給程式。
- Given 右邊有改動，When 使用者輸入訊息並按 Commit，Then 提交成功，清單顯示「No uncommitted changes」，提示列顯示「Committed "…"」。
- Given 同一個 checkout 有兩個執行中的 Agent，When 顯示 Review，Then 上方出現「2 agents in this repo」。
- Given 目標 Agent 從 Working 變成其他狀態，When 狀態改變，Then 立刻重讀改動，不等 10 秒。
**驗證**: `Review.*`、`Keymap.*`；截圖（IMPL-A34）。

#### FR-43 Timeline 與 host 狀態歷史（ADR-33）
**優先級**: P2
**描述**: Ctrl+Shift+H 開的時間軸，每個 Agent 一列，畫出工作中、需要你、完成、閒置與結束的時間段；範圍 15m、1h、4h、24h。歷史由 agent-host 記錄（UI 關著也記）：每個 session 最多 1,000 筆、24 小時，只記時間與狀態，不記訊息與畫面內容。新的唯讀方法 `session.timeline`，`session.state` 事件加選填欄位 `at`，協定版本不變。換版交接不搬歷史。`agentctl timeline` 在命令列輸出同樣的紀錄。設計文件 7.18、3.7.3、3.8。
**驗收標準**:
- Given 一個 Agent 依序回報 Working、Blocked、Idle，When 使用者打開 Timeline，Then 那一列依序畫出三段，滑鼠停在段落上顯示狀態與起訖時間；點段落切到那個 Agent 並關閉浮層。
- Given 一個 session 有 1,100 次狀態變化，When 呼叫 `session.timeline`，Then 只回傳 1,000 筆並標 `truncated: true`；回應裡沒有訊息或畫面內容。
- Given host 正在跑有歷史的終端機，When 執行 `agentctl update` 換版交接，Then 新 host 的歷史從接手時開始，`adopted` 為 true，UI 在軸上標「History starts HH:MM (agent-host updated)」。
- Given 一台機器上是沒有 `session.timeline` 的舊 agent-host，When 新 UI 打開 Timeline，Then 那台機器的群組顯示「Timeline needs a newer agent-host on this machine」，其他機器照常顯示。
- Given host 有紀錄，When 執行 `agentctl timeline --since 1h` 或加 `--json`，Then 輸出每個 session 的狀態變化（文字或 host 的原始 JSON）。
**驗證**: `Timeline.*`、`StateTimeline.*`、`HostFixture.TimelineRecordsStateChangesInOrder`、`HostFixture.TimelineIsBoundedAndSaysSo`、`HostFixture.TimelineIsAnOrdinaryReadOnlyMethod`、`HostUpdate.TerminalsKeepRunningThroughAnUpdate`；截圖（IMPL-A34）。

#### FR-44 AgentDesk Classic 版面（ADR-34；v4.0.0 改為好玩的版面，ADR-37）
**v4.0.0 變更**: Classic 不再列在一般版面清單。設定 › Look 的「Fun layouts」關著（預設）時，它不出現在版面卡片、Mac 與 Classic 的 View 選單，Ctrl+Shift+L 也跳過它；命令面板的「Layout: AgentDesk Classic」與 `layout:classic` 一律可用（FR-56）。外觀與下面的驗收標準不變。
**優先級**: P3
**描述**: 內建版面 `classic`（AgentDesk Classic）：整個視窗用字元畫成單色終端機的樣子，選單、分頁、框線與狀態都是文字；狀態一律有字元（`[!]`、`[~]`、`[+]`、`[.]`、`[?]`），不只靠顏色。F10 打開選單列（只在這個版面）。終端機可轉成主題的單色色階；掃描線特效在淺色主題與 Windows 高對比時不畫。設計文件 7.19。
**驗收標準**:
- Given Classic 版面且終端機有焦點，When 使用者按 F10、按右鍵到 File、選 Split right 並按 Enter，Then 目前面板往右分割；在其他版面按 F10 時照常送給程式。
- Given Monochrome terminal 開啟、主題 phosphor-green，When 程式輸出 24-bit 紅字，Then 文字畫成綠色色階，和底色對比不低於 7:1。
- Given 一個 Agent 需要使用者，When 顯示分頁列，Then 那個分頁有反白的 `[!]`。
- Given 淺色主題或 Windows 高對比，When 畫面更新，Then 不畫掃描線與暗角。
**驗證**: `ClassicLayout.*`、`LayoutStyles.*`；截圖（IMPL-B）。

#### FR-45 AgentDesk Tiling 版面（ADR-34）（v4.0.0 已移除，併進 Terminals，見 FR-57，ADR-37）
**狀態**: 已移除。下面的條文保留作為歷史，不再是驗收依據。直角、間距與 Alt+R、Alt+Shift+Enter、Alt+Shift+Q、Alt+Shift+G 改在 Terminals（`layout:terminals`），由 FR-57 驗收；頂端狀態列、Pane titles、Focus follows mouse 沒有保留。存檔的 `tiling` 開成 Terminals 並打開直角。
**優先級**: P3
**描述**: 內建版面 `tiling`（AgentDesk Tiling）：沒有側欄與分頁卡片，頂端一條細狀態列，面板直角並留間距，以鍵盤為主。Alt+R 調整大小模式、Alt+Shift+Enter 往較長的一邊分割、Alt+Shift+Q 關閉面板、Alt+Shift+G 開關間距，這些鍵只在 `layout:tiling` 區段。設計文件 7.19。
**驗收標準**:
- Given Tiling 版面、左右兩個面板，When 使用者按 Alt+R 再按 L 三次，Then 左側面板寬了 120 px，沒有任何字元送進終端機；按 Esc 後打字回到原面板。
- Given 一個寬大於高的面板，When 使用者按 Alt+Shift+Enter，Then 往右分割。
- Given 間距關閉、未聚焦的面板需要使用者，When 畫面更新，Then 那個面板的框是 2 px `needs_you` 色，狀態列顯示 `!1`。
- Given 其他版面，When 使用者按 Alt+R，Then 照常送給程式。
**驗證**: （v3.5.0 時）`TilingLayout.*`（含 `TilingLayout.LayoutKeysDoNotClashWithDefaults`）、`LayoutStyles.*`；截圖（IMPL-B）。v4.0.0 拿掉 `TilingLayout.*`，改由 `TerminalsOptions.*` 驗證（FR-57）。

#### FR-46 AgentDesk Cockpit 版面（ADR-34）（v4.0.0 已移除，ADR-37）
**狀態**: 已移除。下面的條文保留作為歷史，不再是驗收依據。程式、版面檔、`cockpit-night`／`cockpit-day` 主題、`cockpit.reset_today` 與 `cockpit_*` 設定都已刪除；它的用途由 Stage（FR-52）與 Inbox（FR-53）接手。存檔的 `cockpit` 開成 AgentDesk 版面，選了 Cockpit 主題的人退回預設主題。
**優先級**: P3
**描述**: 內建版面 `cockpit`（AgentDesk Cockpit）：左邊三個儀表（工作中、需要你、今天完成），右邊 Agent 清單，下方狀態變化紀錄（最多 200 筆，不存檔），中間是目前分頁。今天完成從本地時間午夜起算，存在 `cockpit_done_today`。窄視窗先藏清單再藏紀錄。設計文件 7.19。
**驗收標準**:
- Given Cockpit 版面、一個 Agent 在 Working，When 它變成 Done 或 Idle，Then DONE TODAY 加一，LOG 最上面多一列 `HH:MM:SS  space/程式  WORKING → …`。
- Given 今天完成數為 3、存檔日期是昨天，When 本地時間換日後畫面更新，Then 歸零。
- Given 窗寬小於 1000 設計 px，When 畫面更新，Then 藏起 Agent 清單；小於 820 再藏紀錄。
- Given 使用者點「NEEDS YOU」儀表，When 有 Agent 需要使用者，Then 跳到第一個需要你的 Agent。
**驗證**: （v3.5.0 時）`Cockpit.*`、`LayoutStyles.*`；截圖（IMPL-B）。v4.0.0 拿掉 `Cockpit.*`，由 `LayoutStyles.RemovedLayoutsHaveReplacements` 檢查存檔轉換。

#### FR-47 依時間切換淺色與深色（ADR-35）
**優先級**: P3
**描述**: 設定 › Look › Theme 的 Appearance 多一個 Schedule：依固定時間，或依日出日落切換淺色與深色。不連網、不使用 Windows 定位 API，緯度與經度由使用者手動輸入，只存到小數一位。`appearance.toggle` 暫時換成另一種，到下一個切換時間為止。設計文件 7.20。
**驗收標準**:
- Given Schedule、Fixed times 07:00／19:00，When 本地時間到 19:00，Then 一秒內換成深色主題。
- Given 淺色 22:00、深色 06:00，When 本地時間 23:00，Then 顯示淺色（跨午夜的排程）。
- Given Sunrise, sunset、緯度 25.0，When 計算台北夏至與冬至，Then 日出日落和 NOAA 的參考值相差不超過 3 分鐘。
- Given 使用者沒填緯度，When 打開 Switch by，Then Sunrise, sunset 為灰色、不能選。
- Given 排程模式顯示淺色，When 執行 `appearance.toggle`，Then 換成深色，到下一個切換時間回到排程。
**驗證**: `Schedule.*`、`ThemeCommands.NamesAreBuiltIn`；截圖（IMPL-T）。

#### FR-48 跟著 Agent 狀態變色（ADR-35）
**優先級**: P3
**描述**: Tint by agent state（預設 Off）設為 Subtle 時，有 Agent 需要你，介面底色緩慢偏向 `needs_you` 的色相、強度很小；Also when done 開啟時，有完成的 Agent 偏向 `done` 色相、強度減半。文字、狀態色、框線與終端機顏色不變，強度受對比保護限制。主題編輯器開著、Windows 對比主題開著時不作用。設計文件 7.20。
**驗收標準**:
- Given Subtle、主題 Midnight，When 一個 Agent 需要使用者，Then 側欄底色在約 1.2 秒內偏暖（例如 `#101521` → `#151320`），終端機顏色不變；問題解決並維持至少 2 秒後淡回原色。
- Given 任何主題，When 套用變色，Then 檢查的文字與圖形配對都不低於變色前與 AA 門檻中較低的那一個。
- Given Windows 關閉動畫效果，When 狀態改變，Then 直接換到目標強度，不漸變。
**驗證**: `AmbientTint.*`、`AmbientFade.*`、`ColorMath.*`、`PaletteCheck.*`。

#### FR-49 版面的建議主題（ADR-35）
**優先級**: P3
**描述**: 版面可以有一組為它配好的深淺主題（版面檔的 `suggested_themes` 或內建表：AgentDesk Mac 配 Dusk／Dawn、Classic 配 Phosphor Green／Paper；v4.0.0 起 Cockpit 與它的 Cockpit Night／Day 已移除）。建議只出現在設定頁，**不會自動改變外觀**（使用者決定）。設計文件 2.4、7.20。
**驗收標準**:
- Given 目前版面是 AgentDesk Mac、主題是 Midnight／Paper，When 打開設定 › Look › Theme，Then 出現「Made for …」列與 Use 按鈕，Dusk 與 Dawn 的主題卡有星號。
- Given 同上，When 使用者按 Use，Then 這個版面的深淺主題改成 Dusk／Dawn，其他版面不變。
- Given 使用者第一次切到 Classic，When 版面顯示，Then 主題不變，只在設定頁建議（v4.0.0 拿掉原本的 Cockpit）。
- Given 版面目前的主題已經是建議值，或版面檔寫死 `theme`，When 打開設定，Then 不顯示建議列。
**驗證**: `ThemeFile.BuiltinSuggestions`、`ThemeFile.SuggestedThemeFilesShip`、`ThemeFile.MacDuskAndDawnPassThePaletteCheck`、`Layout.SuggestedThemesAreReadAndWrittenBack`。

#### FR-50 主題編輯器（ADR-35）
**優先級**: P3
**描述**: 在設定裡複製或新建主題、調整每個顏色並即時預覽整個視窗，存成 `%LOCALAPPDATA%\AgentDesk\themes\<名稱>.json`（和手寫主題檔相同格式，只寫有改的顏色，保留未知的鍵）。顯示每個顏色的對比、可模擬三種色覺的範例與 Checks。刪除是改名成 `.json.old-<時間>`，不真的刪檔。設計文件 7.20。
**驗收標準**:
- Given 使用者在主題卡右鍵選 Edit a copy…，When 改了 accent，Then 整個視窗立刻用新顏色；按 Cancel 回到原本的主題。
- Given 一個合法的名稱，When 按 Save 或 Ctrl+S，Then 寫入 `themes\<名稱>.json`，`colors` 只含改過的值，存完立刻套用，不必重開。
- Given 名稱是空的、含 `<>:"/\|?*`、是保留裝置名稱或內建主題 id，When 輸入，Then Save 停用並顯示原因。
- Given 終端機文字對背景低於 3:1，When 按 Save，Then 多問一次才存。
- Given 一個使用者自己的主題正被某個版面使用，When 使用者選 Delete…並確認，Then 檔案改名為 `<名稱>.json.old-<時間>`，那個版面改回預設主題。
**驗證**: `ThemeFile.*`（含 `ThemeFile.NameRules`、`ThemeFile.WriteKeepsUnknownKeysAndOnlyOverrides`）、`ColorMath.*`、`PaletteCheck.*`、`ThemeCommands.NamesAreBuiltIn`。

#### FR-51 第一次使用提示（ADR-36）
**優先級**: P2
**描述**: 每個內建版面與專注模式第一次出現時，顯示一張提示卡：用途與 3～5 個常用操作，快捷鍵依使用者目前的按鍵設定。之後可以隨時重看，也可以全部關閉。同一批修正：視窗按鈕顏色跟著主題（補充 FR-58）。設計文件 2.4、ADR-36。
**驗收標準**:
- Given 使用者第一次切到某個版面，When 畫面穩定後，Then 中央出現那個版面的提示卡；按「Got it」後，這個版面不會再自動出現提示。
- Given 提示卡開著，When 使用者打字或按 Enter，Then 按鍵不會送進終端機；關閉後打字照常送進終端機。
- Given 使用者在 keys.json 改了某個操作的鍵，When 提示卡顯示，Then 顯示的是改過的鍵。
- Given 使用者按「Don't show tips」，When 之後切到沒看過的版面，Then 不再自動出現提示；執行 `layout.tips` 仍可打開。
- Given 使用者換主題，When 畫面更新，Then 左上角的視窗按鈕改用該主題推算的顏色，三顆仍分得出來，且在所在的列上對比至少 3:1。
**驗證**: `LayoutTips.*`、`WindowButtons.*`；截圖（status.md 2026-11-20「視窗按鈕跟著主題、第一次使用提示」）。v4.0.0 起提示卡換成目前的版面（Mission Control、Tiling、Cockpit 的卡拿掉，加上 Stage、Inbox、Pair、Garden），`LayoutTips.EveryLayoutAndFocusHasACard` 用新的名稱清單。


使用者 2026-11-21 看過 `docs/design/ideas.md` 第二輪構想後說「全部都做」，依建議執行：共用設計規則與整體美化、Tiling 併進 Terminals、拿掉 Cockpit、Classic 改為好玩的版面，以及 C1 Inbox、C2 Stage、C3 Pair、C4 Garden。實作與規格的差異記在 `docs/design/ideas/IMPL-round2-*.md`，本節以實作為準。截圖在各 IMPL 筆記列的 `build\shots\` 資料夾（不進版控）。v2.27 時 Debug 共 477 個測試。

#### FR-52 Stage 版面（ADR-38，取代 FR-59）
**優先級**: P2
**描述**: 內建版面 `stage`（Stage）。一個 Agent 在大的主舞台上，是一般的面板，可以直接打字；其他 Agent 在下方底片條排成即時縮圖（程式、名稱、狀態、多久前變化、畫面最後幾行），縮圖用快照畫，不建終端機檢視、不搶控制權。需要你的縮圖發光並排到最前面（等最久的在前），其他維持固定順序。條上可篩選（All／Needs you／Working）與調整大小（S／M／L，視窗太矮時自動小一號）。Ctrl+Shift+M 切換 Stage，Alt+Left／Alt+Right 只在 Stage 換上一個／下一個 Agent。自動換台預設關閉。存檔的 `mission` 開成 Stage。設計文件 7.17。
**驗收標準**:
- Given 三個 Agent，一個需要你、兩個工作中，When 使用者按 Ctrl+Shift+M，Then 顯示 Stage，需要你的那張縮圖發光並排在底片條最前面；再按一次回到原本的版面。
- Given Stage，When 使用者點一張縮圖，Then 那個 Agent 換上主舞台並取得鍵盤，原本台上的 Agent 回到底片條；頂端列的「←」換回剛才那一個。
- Given Stage 且終端機有焦點，When 使用者按 Alt+Right，Then 換到下一個 Agent，按鍵不送進終端機；在其他版面按 Alt+Right 時照常是 `pane.go:right`。
- Given 底片條上有一個完成的 Agent，When 使用者只看縮圖、沒把它放上台，Then 它保持 Done。
- Given Stage，When 使用者按 Esc，Then Esc 送給終端機，不離開 Stage。
- Given 「Switch to an agent that needs you」打開，When 使用者 4 秒內按過鍵，或有選單開著，或台上的 Agent 也在等使用者，Then 有新的 Agent 需要你時不自動換台；以上都不成立時自動換上台。
- Given `ui.json` 存的是 `"layout": "mission"` 與 `mission_filter`，When AgentDesk 啟動，Then 開成 Stage 並沿用原本的篩選；下次存檔時 `mission_*` 鍵被刪掉。
- Given Windows 關閉動畫效果，When 換台或有 Agent 需要你，Then 不淡入、不呼吸，直接顯示結果。
**驗證**: `Stage.*`（含 `Stage.ShippedLayoutHasBarStageAndStrip`、`Stage.CommandsAndKeys`）、`Mission.*`（共用邏輯）、`LayoutStyles.RemovedLayoutsHaveReplacements`、`LayoutStyles.SavingDropsKeysOfRemovedLayouts`、`Attention.*`；截圖（IMPL-round2-stage）。

#### FR-53 Inbox 版面（ADR-39）
**優先級**: P2
**描述**: 內建版面 `inbox`（Inbox）。左邊是需要你的 Agent 卡片佇列（Agent 名稱、space 與分支、變更檔案數、等了多久、Agent 在結構化回報裡說它要什麼、畫面最後四行的預覽），右邊是目前分頁的面板。卡片上可以快速回答、回覆、Snooze 或 Open；下面的 DONE 區列出沒看過的完成，可 Dismiss；全部處理完顯示 Inbox zero。快速回答依程式設定在 `ui.json` 的 `inbox_answers`；隨附的預設只有 Claude Code（`1`／Esc）與 Codex（`y`／Esc）的 permission 回報，待用真的 CLI 驗證。狀態一律不從畫面文字判斷（ADR-09）。Ctrl+Shift+I 切換 Inbox。設計文件 7.22。
**驗收標準**:
- Given 兩個 Agent 需要使用者，一個 1 分鐘前開始等、一個 5 分鐘前，When 使用者按 Ctrl+Shift+I，Then 顯示兩張卡片，預設 1 分鐘的在前；切到 Oldest 後順序對調；再按 Ctrl+Shift+I 回到原本的版面。
- Given 一張卡片的 Agent 用 OSC 7501 回報了訊息，When 顯示卡片，Then 顯示那段訊息；沒有訊息時顯示淡色提示，不從畫面文字推斷。
- Given 選取的卡片，When 使用者在回覆欄位打字並按 Enter，Then 文字加一個 Enter 送進那個終端機，卡片顯示「✓ Sent」。
- Given `inbox_answers` 給某個程式設了 `{"label": "Continue", "send": "", "enter": true}`，When 那個程式的卡片出現，Then 有一顆「Continue」按鈕，按下或按 1 送出 Enter；寫成空陣列時沒有按鈕；存檔後幾秒內生效，不用重開。
- Given 使用者 Snooze 一張卡片，When 那個 Agent 換了問題，或離開等待後又回來，Then 卡片重新出現；重開 agent-ui 後 Snooze 清掉。
- Given 佇列握有鍵盤，When 使用者按 ↓、R、1～9、S，Then 依序移動選取、回覆、快速回答、Snooze；點一下右邊的終端機後，同樣的鍵照常送給程式。
- Given 所有卡片都處理完，When 佇列空了，Then 顯示 Inbox zero 與「N agents working, M idle」之類的摘要；Windows 關閉動畫效果時直接顯示最後一格。
**驗證**: `Inbox.*`（排序、Snooze 規則、預設快速回答只給已知程式的 permission、自訂覆寫與 `"*"`、格式錯誤、最多 9 個、命令與 Ctrl+Shift+I）；截圖（IMPL-round2-inbox）。隨附的快速回答與真的 Claude Code、Codex 的互動待人工驗收。

#### FR-54 Pair 版面（ADR-40）
**優先級**: P2
**描述**: 內建版面 `pair`（Pair）。兩個 Agent 左右並排，下方一個共用輸入列，可以把同一段文字同時送給兩邊（貼上後補 Enter）。「Separate worktrees」預設打開，資料夾是 git 儲存庫時每一邊在 `pair/` 開頭的新分支有自己的 checkout。誰先完成只依結構化的 agent 狀態判斷，比的是各自的用時；兩邊都完成後顯示結果條，可 Review 或 Keep（只是畫面標記，不做 git 操作）。Pair 開的 worktree 不自動移除。Ctrl+Shift+2 切換 Pair，Alt+P 移到輸入列、Alt+Shift+S 交換左右，這兩個鍵只在 Pair。設計文件 7.23。
**驗收標準**:
- Given 目前資料夾是 git 儲存庫、Separate worktrees 打開，When 使用者在設定卡按 Start pair，Then 建立兩個 `pair/` 開頭分支的 worktree，在目前 space 一個名為「Pair」的新分頁裡左右開出「<profile> (A)」與「(B)」。
- Given 資料夾不是 git 儲存庫，When 顯示設定卡，Then 開關停用並以黃色警告說明兩邊會在同一個資料夾工作，仍可以開始。
- Given 一對 Agent，When 使用者在輸入列打字並按 Enter，Then 同一段文字加 Enter 送給兩邊並開始新的一輪；按 Left only 只送左邊。
- Given 兩邊都回報過 Working，When 左邊先回報 Done，Then 左邊面板頂端出現終點線與「Finished first」；右邊也 Done 後，結果條寫「<名稱> finished first, Ns ahead」（相差不到 1 秒寫「A dead heat」），並顯示兩邊的時間與變更數。
- Given 結果條，When 使用者按 Keep，Then 那一邊標「Kept」，git 沒有任何變化；按 Review 切到 Review 看那一邊的 diff，Ctrl+Shift+R 回到 Pair。
- Given Pair 以外的版面，When 使用者按 Alt+P，Then 照常送進終端機。
- Given 使用者按 Close pair，When 解除配對，Then 兩個終端機照常在「Pair」分頁裡跑，worktree 保留。
**驗證**: `Pair.*`、`PairRace.*`（生命週期、比賽計時與結果、worktree 與 session 名稱、預設程式、命令、`Pair.KeysOnlyWhilePairShows`）；截圖（IMPL-round2-pair）。貼上後補 Enter 對真的 Claude Code 與 Codex 的效果待人工驗收。

#### FR-55 Garden 版面（ADR-41）
**優先級**: P3
**描述**: 內建的好玩版面 `garden`（Garden）。每個 Agent 是一隻 AgentDesk 自己畫的小動物，外觀由 session id 決定，顏色跟著所在 space。姿勢只看結構化狀態：工作中跳步、需要你揮手並有紅色「!」、完成時彈跳後坐下、閒置睡覺、Unknown 半透明。滑鼠停留顯示程式、狀態與最後一則訊息；點一下回到原本的版面並聚焦那個 Agent。深色主題是夜晚、淺色是白天。遵守 Windows 的「減少動畫」，視窗不在前景時降低更新頻率。沒有預設按鍵，在 Fun layouts 打開時才列在版面清單（FR-56）。設計文件 7.24。
**驗收標準**:
- Given 五個 Agent 分別是 Working、Blocked、Done、Idle、Unknown，When 打開 Garden，Then 五隻小動物依序是跳步、揮手加「!」、坐著笑、睡覺加「z z」、半透明；上方計數顯示需要你 1、工作中 1、完成 1。
- Given 同一個 Agent，When 重開 Garden 或 agent-ui，Then 它的小動物長得一樣。
- Given 使用者點一隻小動物，When 點擊，Then 回到原本的版面、聚焦那個 Agent，並跳出說明怎麼回到花園的通知。
- Given Windows 關閉動畫效果，When Garden 顯示，Then 每隻小動物停在定格姿勢，不要求新的畫格。
- Given 視窗不在前景，When 有 Agent 在工作，Then 更新頻率降到每秒約 4 格。
- Given 沒有 Agent，When 打開 Garden，Then 草地中間顯示木牌與「Start a program…」按鈕。
**驗證**: `Garden.*`（外觀由 id 決定、狀態對姿勢、姿勢內插、1～20 個的排列、子 Agent 跟隨、畫格頻率、版面與命令）；截圖（IMPL-round2-garden）。

#### FR-56 好玩的版面（Fun layouts）（ADR-37）
**優先級**: P3
**描述**: 設定 › Look 的「Fun layouts」開關（`ui.json` 的 `fun_layouts`，預設 false）。關著時 AgentDesk Classic 與 Garden 不出現在版面卡片、Mac 與 Classic 的 View 選單，Ctrl+Shift+L 也跳過它們；正在使用的版面一律保留自己的卡片。命令面板的「Layout: AgentDesk Classic」「Layout: Garden」與 `layout:classic`、`layout:garden` 一律可用。設計文件 2.4、7.19。
**驗收標準**:
- Given 預設設定，When 使用者連按 Ctrl+Shift+L，Then 依序輪到 AgentDesk、Terminals、Stage、Inbox、Review、Pair、AgentDesk Mac，不會出現 Classic 或 Garden。
- Given Fun layouts 打開，When 打開設定 › Look，Then 版面卡片多出 AgentDesk Classic 與 Garden，Ctrl+Shift+L 也會輪到它們。
- Given Fun layouts 關著，When 使用者在命令面板執行「Layout: Garden」，Then 切到 Garden，設定頁仍保留 Garden 的卡片直到換走。
**驗證**: `LayoutStyles.FunLayoutsStayOutOfTheListUntilTurnedOn`、`LayoutTips.MergedTerminalsAndFunLayoutsAreExplained`；截圖（IMPL-round2-base、IMPL-round2-garden）。

#### FR-57 Terminals 的直角、間距與鍵盤調整大小（ADR-37，取代 FR-45）
**優先級**: P3
**描述**: 設定 › Look 選到 Terminals 時有 Square corners（`terminals_square`）與 Gaps（`terminals_gaps_on`、`terminals_gaps` 0～24 px，預設 8）。預設值時畫面和原本相同。Alt+R 調整大小模式、Alt+Shift+Enter 往較長的一邊分割、Alt+Shift+Q 關閉面板、Alt+Shift+G 開關間距，只綁在 `layout:terminals`。存檔的 `tiling` 開成 Terminals 並打開直角；`tiling.*` 舊命令仍可用。設計文件 7.19。
**驗收標準**:
- Given Terminals 版面、左右兩個面板，When 使用者按 Alt+R 再按 L 三次，Then 左側面板寬了 120 px，沒有任何字元送進終端機，面板區下方出現調整大小的提示卡；按 Esc 後打字回到原面板。
- Given 一個寬大於高的面板，When 使用者按 Alt+Shift+Enter，Then 往右分割。
- Given 使用者打開 Square corners、關掉 Gaps，When 畫面更新，Then 面板變成直角並彼此相貼，相鄰面板共用一條 1 px 邊線，左上角的視窗按鈕變成小方塊。
- Given 其他版面，When 使用者按 Alt+R，Then 照常送給程式。
- Given `ui.json` 存的是 `"layout": "tiling"` 與 `tiling_gaps: 12`，When AgentDesk 啟動，Then 開成 Terminals、直角打開、間距 12；下次存檔寫 `terminals_*`，刪掉 `tiling_*`。
**驗證**: `TerminalsOptions.*`、`LayoutStyles.RemovedLayoutsHaveReplacements`、`LayoutStyles.SavingDropsKeysOfRemovedLayouts`、`DesignTokens.*`；截圖（IMPL-round2-base 的 `step2\`）。

## 3. 非功能需求

| 類別 | 要求 |
|---|---|
| 效能 | 第 1.3 節 |
| 不遺漏輸出 | host 依序解析所有 VT 位元組；只有送給 UI 的畫面更新可以合併或丟棄（之後以快照補齊）。輸出佇列超過 8 MiB 時暫停讀取（背壓），不丟位元組 |
| 本機通道安全 | 只允許目前使用者。Windows：Named Pipe，DACL 只含 SYSTEM 與使用者，拒絕遠端連線，雙方核對身分。macOS：Unix socket，資料夾 0700、socket 0600，雙方以 `getpeereid` 核對 uid |
| 輸出安全 | 程式輸出不能觸發命令或寫剪貼簿；連結需使用者按住修飾鍵點擊，程式與指令檔只標出不執行 |
| mod 安全 | 只載入已啟用的 mod；Windows 只搜尋 mod 自己的資料夾與 System32，macOS 以絕對路徑載入；mod 沒有沙盒，以使用者權限執行，啟用前說明 |
| 隱私 | 診斷紀錄只記 ID、事件類型、錯誤碼，不記輸入文字或完整環境變數；不複製 Agent 的 token |
| 網路 | 沒有預設伺服器：設 `AGENTDESK_RELEASES_URL` 才檢查更新，設 `AGENTDESK_TELEMETRY_URL` 才送統計；意見回饋只能複製或存檔 |
| 資料可靠性 | JSON 設定以暫存檔加原子替換寫入，保留一份 `.bak`；兩份都壞時把壞檔改名保留、以預設值啟動；不覆寫解析失敗的使用者檔案 |
| 平台（Windows） | Windows 11 x64、一般使用者權限；100%／150%／200% DPI；繁中 IME；一律使用隨附 ConPTY（ADR-08） |
| 平台（macOS） | Apple Silicon、macOS 13 以上、一般使用者權限；Retina 與非 Retina；注音輸入法 |
| 介面 | 介面文字預設 15 px，可調整 11～24；狀態用文字與圖示搭配顏色，不只靠顏色 |
| 授權 | AgentDesk 為 MIT；依賴須與 MIT 相容；發布包的 THIRD_PARTY_NOTICES 列出每個隨附元件的授權（含 herdr-app 的 Apache-2.0） |

## 4. 不在範圍內

- HTTP backend、Docker、資料庫、多人協作、自動多 Agent 排程。
- 跨重開機或登出續跑（ADR-07）。
- 從畫面文字推斷 Agent 狀態（ADR-09）。
- 程式透過 OSC 52 寫入剪貼簿。
- 保證支援 Windows 10、Intel Mac、Linux。
- WSL 作為遠端機器（需要 Linux 版 agent-host，ADR-16）。
- 手機 app（ADR-13）。
- 內建的更新與使用統計伺服器（ADR-18）。
- Windows 使用系統內建 ConPTY 時的 agent-host 交接（ADR-19）。
- 螢幕閱讀器支援。

## 5. 假設與限制

- 使用者自行安裝並登入 Claude Code（2.1.295 以上才有 OSC 7501 狀態）與 Codex CLI。Codex 沒有 OSC 7501，要另外設定 hook 呼叫 `agentctl hook`（`docs/integrations/agent-hooks.md`）。
- Windows 一律使用隨附的 ConPTY：系統內建版本會丟掉 OSC 7501 查詢的回覆（M0 實測，ADR-08）。macOS 的 pty 不攔截，不需要隨附元件。
- 同一資料夾的多個 Agent 會改到同一批檔案；AgentDesk 只提示，需要隔離時由使用者用 git worktree。
- 遠端機器需要設好 ssh 金鑰登入並安裝 AgentDesk；跨平台（Windows 與 macOS 互連）還沒決定（OQ-11）。
- macOS 版目前以 ad-hoc 簽章發布，第一次開啟要在系統設定允許；Developer ID 簽章與公證需要 Apple Developer 帳號（OQ-11）。
- 字寬依 libvterm 0.3.3：模糊寬度字元一律 1 欄，ZWJ emoji 不合併；emoji 為單色。
- 開發環境：Windows 只用已安裝的 VS 2022 工具鏈（ADR-11）；macOS 可用 Xcode Command Line Tools 與 vcpkg（ADR-30）。

## 6. 術語

| 名詞 | 說明 |
|---|---|
| agent-host | 每位使用者一個的背景程序，擁有所有終端機、子程序、終端機引擎與歷史 |
| agent-ui | 桌面視窗，只是 agent-host 的 client |
| agentctl | 命令列 client，也是 Agent 的狀態 hook 與 ssh 的轉接程式 |
| session | agent-host 管理的一個終端機工作階段 |
| space | 一個工作地點，通常是一個 repo 或資料夾，可以在遠端機器上；底下有多個分頁 |
| 分頁、面板 | 分頁是一棵分割樹，每個面板是一個終端機 |
| 版面 | 決定畫面上有哪些區塊與位置的 JSON 樹 |
| mod | 使用者安裝的 C++ 動態函式庫擴充 |
| 控制者 | 一個 session 目前唯一能輸入與改尺寸的 client |
| 交接（handover） | agent-host 換版時把執行中的終端機交給新 host |
| OSC 7501 | Program Status Protocol（Revision 0.4），程式回報自己狀態的終端機控制序列 |
| OSC 133 A、OSC 7 | shell 回報「顯示新提示」與「目前資料夾」的控制序列 |
| picker | 跳轉清單，搜尋 space、分頁與 Agent |
| 機器 | 透過 ssh＋`agentctl bridge` 連上的另一台電腦 |

## 7. 已知落差

程式目前與本契約不一致、尚未修正的地方。修正後從這裡刪除；若決定改需求，則依憲法 6.3 升版。

### 7.1 FR-41～FR-57 在 macOS 上的狀態

不含已移除的 FR-45、FR-46。FR-58 只適用 Windows，FR-59 已移除，都不在此列。

| 項目 | 狀態（2026-11-25） |
|---|---|
| 建置 | Debug、Release 成功，沒有編譯警告 |
| 自動測試 | Debug、Release 各 438／438。這些需求引用的單元測試都有在 macOS 執行 |
| 畫面 | 九個版面與主要對話框以截圖檢查，正常。截圖不含系統的紅綠燈按鈕 |
| Timeline（FR-43） | 在隔離的 host 上，`agentctl report-state` 回報的狀態依序記錄，`agentctl timeline` 與 `--json` 都讀得到。引用的 `HostFixture.Timeline*` 與 `HostUpdate.*` 只在 Windows 執行，macOS 沒有對應的自動測試 |
| 人工驗收 | 未完成：紅綠燈與各版面頂端列的位置（含 Focus）、新版面的快捷鍵、減少動畫與增加對比、Claude Code 與 Codex 的實際狀態與互動（設計文件 12.2） |

人工驗收完成前，FR-41～FR-57 在 macOS 上不算通過驗收。

### 7.2 程式與需求不一致

| 項目 | 現況 | 影響 |
|---|---|---|
| `agentctl timeline --since`（兩個平台共用的程式） | `--since 1h` 的 `1h` 被當成 session 名稱。沒指定 session 時回報 `NOT_FOUND`；有指定時不會出錯，但時間範圍沒有作用。`--since=1h` 也沒有作用 | FR-43：`agentctl timeline --since 1h` 的驗收不成立 |

4.0.0 寫成時列出的五項（macOS 畫面上的 Windows 字樣、使用統計的平台欄位、`toasts` 設定沒有作用、第一次關閉的說明指向不存在的按鈕、What's new 的 WSL 條目）已在 2026-11-24 修正。

## 8. 需求追溯

| 需求 | 設計文件 v3.1 章節 |
|---|---|
| US-01、US-02、US-05 | 3.3 session、3.4 停止、4 協定 |
| US-03 | 3.1 生命週期、4.6 attach、7.12 重新連線 |
| US-04、FR-31 | 3.5 狀態模型、3.6 提示鉤子 |
| US-06、FR-23 | 3.3 歷史、7.9 終端機繪製與輸入 |
| US-07、FR-25 | 6 agentctl、3.7 子 Agent |
| FR-20、FR-29、FR-38 | 7.2 版面引擎、7.3 畫面區域、7.4 工作區模型 |
| FR-21 | 7.5 注意力與提醒 |
| FR-22、FR-40 | 7.7 快捷鍵 |
| FR-24、FR-30 | 7.13 Files／Changes 與 git、4.4 遠端檔案方法 |
| FR-26 | 7.12 遠端機器 |
| FR-27、FR-33、FR-34 | 7.2 版面、7.14 mod、7.10 主題 |
| FR-28、FR-32 | 3.6 提示鉤子、3.2 profile |
| FR-35、FR-36、FR-37 | 5 更新、交接與使用統計 |
| FR-41～FR-59 | 7.16～7.25 版面與主題 |
| FR-39 | 2.3 平台分離、9 平台差異 |
