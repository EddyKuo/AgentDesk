---
contract_type: PRD
version: 2.0.1
status: approved
author_agent: PM
sprint: 11
created_at: 2026-11-16T00:00:00+08:00
approved_at: 2026-11-19T00:00:00+08:00
skills_used:
  - shared/domain-coding-agents@1.0.0
upstream_deps:
  - raw_requirement@2026-11-16
  - docs/design/AgentDesk_Design.md@2.11
  - sprint/current/status.md@2026-11-19
downstream_consumers:
  - RD
  - QA
change_summary: |
  2.0.1（Patch）：FR-21 引用的測試改名為 `Attention.AlertRules`（2026-11-20 重構時拿掉測試名稱裡的 herdr），需求不變。
  2.0.0（Major）：FR-27 刪掉了看板與格狀兩種版面，屬於刪除需求內容，依憲法 6.3 升主版號（下游 RD、QA 需重新確認 FR-27）。其餘：跟上出貨的產品版本 0.2.0，鎖定設計文件 v2.11。新增第 9 節 FR-29～FR-37，都是 Sprint 10 已實作、使用者在 2026-11-19 驗收過的功能：
  依 herdr-app 複刻的介面與行為（ADR-15）、Files／Changes 面板、Agent 狀態的生命週期（回到 shell 提示時清除）、新增 space 或分頁時選擇 shell（ADR-20）、
  程式裡只出現 AgentDesk（ADR-21）、主題、AgentDesk 線上更新與使用統計（ADR-18）、agent-host 單獨更新時接手終端機（ADR-19）。
  第 8 節（原本誤標為第二個「6.」，改為 8）的 FR-20～FR-28 補上 Given／When／Then；FR-27 的版面依 ADR-18 更正為 workspace、terminals、AgentDesk 98 與自訂 JSON（原文的看板、格狀已由使用者決定移除）。
  第 3、4、5 節隨 ADR-08、ADR-15～ADR-19 更新：隨附 ConPTY 已定案；SSH 遠端、mod、自動更新、host 升級不中斷已不屬於範圍外。
  原有需求 ID 與數字不變。
  1.3.1（Patch）：FR-27 的 mod 由 Lua 改為 C++ DLL（設計文件 v2.5、ADR-17），需求不變。
  1.3.0（Minor）：使用者要求接入 WSL（設計文件 v2.3、ADR-16）。新增 FR-28：WSL 發行版作為本機 shell，資料夾追蹤跟著 Linux 的 cd。不在範圍內的「WSL 支援承諾」改為「WSL 作為遠端機器（Linux 版 host）」。原有需求不變。
  1.2.0（Minor）：使用者要求功能與 herdr 一樣好用、外觀不同（設計文件 v2.1、ADR-13）。新增需求：space／分頁／分割、注意力與通知、命令面板與快捷鍵、終端機搜尋與選取、worktree、子 Agent 協作、遠端機器、版面與 mod。原有需求不變。
  1.1.0（Minor）：跟進設計文件 v1.7。2.4 補紀錄被丟棄後顯示 Unknown 並保留 last_agent_state、report-state 新增 --kind／--message；2.7 驗收加退出碼（QA-13）。
  1.0.0：初版。M 模式精簡 PRD：需求本文以 docs/design/AgentDesk_Design.md v1.6 為單一真相來源，
  本契約固定版本並補齊 agents/pm.md 要求的結構。Orchestrator 依 M 模式輕量審核通過。
---

# Product Requirements Document：AgentDesk

> 本契約鎖定設計文件 **v2.11**，對應出貨的產品版本 0.2.0（1.3.1 鎖定 v2.5，1.1.0 鎖定 v1.7）。設計文件之後的變更若影響第 2、3、4、8、9 節，必須依 CLAUDE.md Section 6.3 將本契約升版，下游才能採用。上一版保存在 `contracts/deprecated/PRD_v1.3.1.md`。

## 1. 產品概述

### 1.1 背景與問題陳述
見設計文件 1.1。同時跑多個 coding agent CLI 時，很難掌握哪個在等輸入、哪個已經結束；一般桌面 UI 自己持有執行中的程序，關掉視窗，工作就跟著中斷。

AgentDesk 的介面與行為依 herdr-app 的原始碼複刻（ADR-15），用 C++ 重寫，不追求協定相容。

### 1.2 目標使用者（Persona）
在 Windows 11 x64 上以一般使用者權限，同時操作兩個以上 coding agent CLI（首發為 Claude Code 與 Codex CLI）的開發者。這些 CLI 由使用者自行安裝並登入（設計文件 1.4）。

### 1.3 產品目標（可量化）
依設計文件 1.5 的基準機：

| 目標 | 數值 |
|---|---|
| 並行 | 4 個 shell 穩定，其中 2 個實際 Agent 同時互動 |
| 互動延遲 | 本機 echo p95 < 100 ms |
| UI 續接 | 4 個 session、120×40，p95 < 2 秒至可操作 |
| 記憶體 | host + UI 額外私有記憶體 < 500 MB（不含子程序） |
| 穩定性 | 8 小時 soak、100 次建立／停止，無持續 handle 或 thread 增長 |

### 1.4 成功指標（KPI）
- MVP 交付時，設計文件 6.2 驗收矩陣（QA-01～QA-22）全數通過，或未通過項目已寫入限制清單並經使用者同意。
- 沒有任何一次把未知的 Agent 狀態標成 Done（設計文件 5.3）。
- 第 1.3 節的數值目標全數達成，並附測試報告證據（設計文件 6.3）。
- 第 8、9 節的需求（FR-20～FR-37）都有自動化測試或實機證據。使用者已在 2026-11-19 完成整體驗收（`sprint/current/status.md`）；還要人工驗收的項目列在設計文件 5.4、5.5。

## 2. 功能需求

每項的驗收標準（Given／When／Then）在設計文件 **1.2.1**，詳細規則在對應章節。v1.2.0 之後新增的需求在第 8、9 節。

### 2.1 啟動工作（US-01）
**優先級**: P1
**描述**: 依 profile、cwd、名稱啟動 Agent，顯示工作目錄、命令與可互動終端機。設計文件 3.2、4.2。
**驗收標準**: US-01a、US-01b
**邊界條件**: cwd 含空白與中文字；executable 或 cwd 不存在；`.cmd` 啟動器需獨立 adapter；不默默改用其他目錄。

### 2.2 多工作並行（US-02）
**優先級**: P1
**描述**: 每個工作有獨立輸入、畫面、尺寸與程序狀態；一個 session 同時只有一個輸入控制者。設計文件 3.3、3.7。
**驗收標準**: US-02a、US-02b
**邊界條件**: 快速 resize 100 次後尺寸一致（QA-04）；第二個 UI 唯讀。

### 2.3 背景續跑與重新連線（US-03）
**優先級**: P1
**描述**: UI 關閉只 detach，host 繼續執行；重開後以快照加增量接回。設計文件 2.2、3.7、4.3。
**驗收標準**: US-03a、US-03b
**邊界條件**: attach 與輸出同時發生（QA-06）；host 崩潰後標記 Lost，不重送 prompt（QA-10）；登出與重開機不保證續跑。

### 2.4 Agent 狀態提示（US-04）
**優先級**: P1
**描述**: 程序狀態與 Agent 狀態分開；Agent 狀態只來自結構化回報（OSC 7501 為主、`agentctl report-state` 為輔），沒有來源就顯示 Unknown。設計文件 3.6。
**驗收標準**: US-04a、US-04b
**邊界條件**: 亂序事件依 seq 處理，OSC 7501 的 seq 由 host 指派（QA-08）；紀錄被丟棄或清除後顯示 Unknown，原值保留在 `last_agent_state`；report-state 可帶 `--kind`、`--message`；OSC 7501 查詢回覆須在 DA1 回覆之前（QA-21）；程序結束時清除 working／blocked（QA-22）。回到 shell 提示時的清除規則見 FR-31。

### 2.5 停止工作（US-05）
**優先級**: P1
**描述**: 先中斷、最多等待 5 秒，再由使用者明確選擇強制終止整個 Job。設計文件 3.2。
**驗收標準**: US-05a
**邊界條件**: 子程序忽略 Ctrl+C；根程序已結束但子程序仍在時顯示「正在清理子程序」。

### 2.6 捲動、選取與複製（US-06）
**優先級**: P1
**描述**: 歷史每工作 10,000 行、16 MiB 上限；選取以 line_id 為準。設計文件 3.4、3.5。
**驗收標準**: US-06a、US-06b
**邊界條件**: 歷史淘汰時取消受影響的選取；OSC 52 預設不執行（QA-16）。

### 2.7 CLI 自動化（US-07）
**優先級**: P1
**描述**: `agentctl` 提供穩定 JSON 輸出與退出碼；`send` 預設不送 Enter。設計文件 3.8。
**驗收標準**: US-07a、US-07b
**邊界條件**: 退出碼 0、2～7 各有對應情境（QA-13）；name 歧義時拒絕；RPC 逾時不代表未執行，需依 request_id 查詢。

## 3. 非功能需求

| 類別 | 要求 | 備註 |
|---|---|---|
| 效能 | 本契約 1.3 | 設計文件 1.5 |
| 輸出壓力 | 合成 1 MiB/s 持續 60 秒，host 持續消費 | 設計文件 1.5、3.3 |
| 安全 | Named Pipe 只允許目前使用者、拒絕遠端；輸出不觸發命令或寫剪貼簿（herdr 預設允許 OSC 52，AgentDesk 不開放） | 設計文件 3.7、3.9、5.6；QA-16、QA-17 |
| 安全（mod） | 只載入已啟用的 mod；`LoadLibraryExW` 只搜尋 DLL 所在資料夾與 System32；mod 當掉就停用，AgentDesk 照常執行。mod 沒有沙盒，以使用者權限執行 | ADR-17 |
| 隱私 | 診斷 log 不記錄輸入文字或完整環境變數 | 設計文件 3.9 |
| 網路 | 沒有預設伺服器：設定 `AGENTDESK_RELEASES_URL` 才檢查更新，設定 `AGENTDESK_TELEMETRY_URL` 才送使用統計；意見回饋只能複製或存檔，不自動送出 | ADR-18；FR-35、FR-37 |
| 平台 | Windows 11 x64、一般使用者權限；100%／150%／200% DPI；繁中 IME | 設計文件 1.4；QA-15 |
| 介面 | 介面文字預設 15 px（herdr 為 13），可在設定調整 | ADR-15 |
| 授權 | 本身 MIT；依賴須與 MIT 相容。herdr-app（Apache-2.0）列在發布包的 THIRD_PARTY_NOTICES 並附授權；隨附字型 Geist、JetBrains Mono（OFL）與 Symbols Nerd Font（MIT） | ADR-10、ADR-15 |

## 4. 不在範圍內（Out of Scope）
設計文件 1.3 的 P2、P3 項目，但已由本契約納入的除外：SSH 遠端（FR-26）、插件執行環境（FR-27 的 C++ DLL mod）、自動更新（FR-35），以及 P2 的 host 升級不中斷工作（FR-36）。

另外不做：HTTP backend、Docker、資料庫、遠端控制、自動多 Agent 排程、跨重開機續跑（ADR-07）、從畫面文字推斷 Agent 狀態（ADR-09）、保證支援 Windows 10、WSL 作為遠端機器（需要 Linux 版 agent-host，ADR-16）、手機 app（ADR-13）、內建的更新與使用統計伺服器（ADR-18）、讓程式透過 OSC 52 寫入剪貼簿、使用系統內建 ConPTY 時的 agent-host 交接（ADR-19）。

## 5. 假設與限制
- 使用者預先安裝並登入 Claude Code（v2.1.295 以上才有 OSC 7501 狀態）與 Codex CLI；精確版本由 M0 凍結。
- 開發只用已安裝的 VS 2022 C++ 工具鏈，不另外安裝工具（ADR-11）。
- 一律使用隨附的 ConPTY（ADR-08 已定案：M0 實測內建版本會丟掉 OSC 7501 查詢回覆），發行包因此多兩個檔案。
- 同一目錄的多個 Agent 會改到同一批檔案，只顯示提示，不提供檔案隔離。
- 遠端機器必須是 Windows、已安裝 AgentDesk，並設好 ssh 金鑰登入（FR-26）。
- WSL 的資料夾追蹤只支援 bash（ADR-16）。

## 6. 術語表
以設計文件 7.3 為單一真相來源。第 8、9 節另外用到的名詞：

| 名詞 | 說明 |
|---|---|
| space | 一個工作地點，通常是一個 repo 或資料夾，可以在遠端機器上；底下有多個分頁 |
| 分頁 | space 裡的一棵分割面板樹 |
| 面板 | 分頁裡的一個終端機 |
| 版面 | 決定顯示哪些區塊的設定（workspace、terminals、AgentDesk 98 與自訂 JSON） |
| mod | 放在 `mods\<名稱>\mod.dll` 的 C++ DLL 擴充（ADR-17） |
| 機器 | 透過 ssh＋`agentctl bridge` 連上的另一台電腦 |

## 7. Aesthetic Refs
不適用（本專案為 M 模式、standard 執行模式）。

## 8. 功能對齊 herdr（v1.2.0 新增，設計文件 5.5）

v2.0.0 補上 Given／When／Then；需求欄除了 FR-27 的版面更正之外，維持原文。v2.11 的設計文件把遠端機器、版面、mod、AgentDesk 98 分別寫在 2.6、2.4、2.5、2.8。

| ID | 需求 | 驗收 | 驗證 |
|---|---|---|---|
| FR-20 | 工作依 space（工作地點）→ 分頁 → 分割面板組織；可往右或往下分割、放大、用鍵盤切換與交換、拖曳調整大小、改名；重開視窗後還原 | Given 兩個分頁與一個分割，When 關閉並重開 UI，Then 版面相同 | `HostFixture.UiStateIsSharedAndPersisted`、`Workspace.*` |
| FR-21 | Agent 在沒被看時完成標為 done，看過才變 idle；需要使用者或完成時發出音效與通知，點通知跳到該 Agent | Given 使用者沒在看某個 Agent，When 它完成，Then 標為 Done、播放提示音並在右下角通知，點通知跳到該面板，看過後變成 Idle。Given 任一 Agent，When 它需要使用者（error 視同需要使用者），Then 一定播放提示音；只有沒在看它時才跳出通知 | `Attention.*`（含 `Attention.AlertRules`） |
| FR-22 | 命令面板、快捷鍵說明，快捷鍵可用 `keys.json` 修改，不送進終端機程式 | Given `keys.json` 把某個命令綁到一組按鍵，When 終端機有焦點時按下這組鍵，Then 執行該命令，按鍵不送進終端機程式 | `Keymap.*` |
| FR-23 | 終端機可搜尋畫面與歷史、雙擊選字、三擊選行、放開即複製 | Given 畫面與歷史裡有某段文字，When 使用者搜尋它，Then 標出畫面與歷史裡的符合處（歷史分頁讀取，最多 5000 行）。Given 開啟放開即複製，When 雙擊一個字或三擊一行後放開滑鼠，Then 選取該字或該行並複製到剪貼簿 | 截圖驗證 |
| FR-24 | 可為 repo 建立 git worktree 並開成新 space | Given 一個 git repo 的 space，When 使用者執行 New worktree 並輸入分支名稱，Then 在 `%USERPROFILE%\.agentdesk\worktrees\<repo>\<branch>` 建立 checkout，並開成新 space | `HostFixture.WorktreeAddCreatesCheckout` |
| FR-25 | Agent 可開子 Agent、給第一則任務、等待它、接收它的回報 | Given 一個在 AgentDesk 終端機裡執行的 Agent，When 它執行 `agentctl run --task "<任務>"`，Then 新 session 的 parent 是它，任務在子 Agent 就緒（回報 Idle，或沒有狀態來源時等 8 秒）後送出；子 Agent 執行 `agentctl report` 時，文字加上 `[from <名稱> <id>]` 送進父 Agent 的輸入，父 Agent 正在工作或等待使用者時先排隊 | `HostFixture.SubAgentTaskReportAndWait` |
| FR-26 | 可加入 ssh 可連線的機器，遠端的 space 與 Agent 顯示在同一個側欄 | Given 遠端是 Windows、已安裝 AgentDesk、`agentctl` 在 PATH 上且設好金鑰登入，When 使用者在 Add machine 選這台主機，Then AgentDesk 執行 `ssh <目標> agentctl bridge`，遠端的 space 與 Agent 列在同一個側欄 | `HostFixture.ProtocolOverBridgeCommand`；實際 ssh 待人工（設計文件 5.5） |
| FR-27 | 可切換版面（workspace、terminals、AgentDesk 98 與自訂 JSON；v2.0.0 依 ADR-18 更正，原文的看板與格狀已移除），可用 mod（C++ DLL，ADR-17）加命令、面板與通知 | Given 目前是 workspace 版面，When 使用者按 Ctrl+Shift+L 切到 terminals，Then 側欄與狀態列消失，space 改在分頁列左邊選。Given 已啟用一個 mod，When 它的 DLL 當掉，Then 該 mod 被停用、錯誤顯示在設定頁，AgentDesk 照常執行 | `Layout.*`、`ModsTest.*` |
| FR-28 | 已安裝的 WSL 發行版自動列為 shell，在目前資料夾開啟；在 Linux 裡 cd 後，space 名稱、分支與 Files／Changes 跟著變（v1.3.0，ADR-16） | Given 已安裝 Ubuntu（WSL），When 使用者從新增選單開「Ubuntu (WSL)」並在 bash 裡 `cd /mnt/d/src`，Then space 名稱、分支與 Files／Changes 跟著換到 `D:\src` | `CwdUri.*`；實機 Ubuntu 驗證見 TestReport |

## 9. v2.0.0 新增需求（Sprint 10，設計文件 2.4～2.8、5.6、ADR-15～ADR-21）

### FR-29 介面與行為依 herdr-app 複刻（ADR-15）
**優先級**: P1
**描述**: 版面尺寸、互動、狀態與提醒規則、picker、命令面板、設定、歡迎頁、worktree 對話框都照 herdr-app（commit 321c9a8），以 C++／ImGui 重寫。範圍包括：自繪標題列（視窗按鈕在分頁列）、側欄 268 px 可拖曳並有窄欄模式、Spaces（字母頭像、`repo · branch`、↑↓、worktree 樹狀、拖曳排序）、Agents（優先度分段或依 space 分組、釘選、子 Agent 樹）、分頁膠囊與收起（`tab.settle`）、面板外框與拖曳把手、滑鼠回報、程式表與圖示、What's new 與 Send feedback、框線與方塊字元依格子繪製。設計文件 5.6 逐項列出。
**驗收標準**:
- Given 一個分頁有兩個面板，When 使用者拖曳其中一個面板標題列的把手、放在另一個面板中間，Then 兩個面板互換位置；放在邊緣時移到那一側。
- Given 一個已收起（settled）的分頁，When 裡面的 Agent 需要使用者，Then 分頁自動回到分頁列。
- Given 程式開啟了滑鼠回報（DECSET 1000／1002／1003），When 使用者在面板裡點擊，Then 事件以 SGR 編碼送給程式；按住 Shift 時改為選取文字。
- Given 使用者在 `%LOCALAPPDATA%\AgentDesk\programs.json` 改了某個程式的名稱或圖示，When 存檔，Then 立刻套用；檔案有錯時原因寫在 `programs.error`。
- Given 程式輸出框線（U+2500–257F）、方塊（U+2580–259F）或點字（U+2800–28FF）字元，When 終端機繪製，Then 依格子的像素邊界畫成圖形，上下列之間不留縫。
- Given 使用者在 Send feedback 寫好草稿，When 按 Copy 或 Save to file，Then 內容被複製或存到 `%LOCALAPPDATA%\AgentDesk\feedback\`，不會自動送出。
**驗證**: `Workspace.SettledTabsAreSkippedUntilShown`、`HostFixture.MouseEventsReachAProgramThatAskedForThem`、`Programs.*`、`Sprites.*`；使用者整體驗收（2026-11-19）。

### FR-30 Files／Changes 面板
**優先級**: P1
**描述**: 右側面板跟著目前面板 shell 所在的資料夾。Files 是檔案樹，檔名旁有 git 狀態，可唯讀預覽；Changes 列出未提交的檔案與差異，可以 commit。遠端機器也可用。shell 所在資料夾靠 PEB 與 PowerShell 提示字元 hook 追蹤，任何 shell 送出的 OSC 7 優先。設計文件 5.6、ADR-15、ADR-16。
**驗收標準**:
- Given 目前面板的 shell 在一個有未提交變更的 repo 裡，When 使用者打開 Changes，Then 列出所有未提交的檔案與 +／− 行數，點檔案顯示差異，輸入訊息可以 commit（先執行 `git add -A`）。
- Given 一個在遠端機器上的 space，When 使用者打開 Files 或 Changes，Then 內容經該機器的 agent-host（`fs.list`、`fs.read`、`git.run`）取得，每 10 秒更新。
**驗證**: 設計文件 5.6；使用者整體驗收（2026-11-19）。

### FR-31 Agent 狀態的生命週期
**優先級**: P1
**描述**: 使用者決定（2026-11-19）：Agent 結束就刪除狀態，space 關閉才全部關掉。shell 顯示新提示（OSC 133 A）表示 Agent 已經結束，host 丟掉該 session 全部的 OSC 7501 紀錄並立刻更新。PowerShell、CMD、WSL 每次顯示提示都會送出 OSC 133 A（host 啟動 shell 時加上的提示鉤子，CMD 保留使用者原本的 PROMPT）。Agents 清單只列出各 space 面板裡、正在執行而且現在有狀態的終端機。設計文件 2.2、3.6、QA-22。
**驗收標準**:
- Given Claude Code 在 PowerShell 或 CMD 的面板裡回報 Working，When 它結束、shell 顯示提示，Then 該 session 的 OSC 7501 紀錄全部被丟掉，agent_state 變成 Unknown，原值存進 `last_agent_state`；Agents 清單、分頁與 space 的狀態點一起清掉。
- Given 一個終端機已結束（exited、killed）或遺失（Lost），When 使用者重開 UI，Then 它的面板被移除，分頁空了就關閉分頁，space 空了就關閉 space；已結束的 session 不會被開成 space。
**驗證**: `VtEngine.ShellPromptOsc133DropsEveryRecord`、`VtEngine.ShellPromptOsc133ClearsWorking`；實測 PowerShell 與 CMD；使用者實機驗收（2026-11-19）。

### FR-32 新增 space 或分頁時選擇 shell（ADR-20）
**優先級**: P1
**描述**: AgentDesk 有、herdr 沒有的功能。側欄的「+」列出程式啟動時在這台電腦偵測到的 shell（PowerShell 7、Windows PowerShell、Command Prompt、各個 WSL 發行版），選哪個就用哪個開新 space；分頁列的「+」選單也列出預設以外的 shell，直接開成新分頁。
**驗收標準**:
- Given 這台電腦有 PowerShell 7、Windows PowerShell、Command Prompt 與一個 WSL 發行版，When 使用者按側欄 Spaces 的「+」並選 Command Prompt，Then 用 Command Prompt 開一個新 space。
- Given 同上，When 使用者打開分頁列的「+」選單，Then 預設以外的 shell 都列出來，選了就開成新分頁。
- Given 同上，When 使用者按 Ctrl+Shift+N 或 Ctrl+Shift+T，Then 直接用預設 shell 開新 space 或新分頁。
**驗證**: 使用者要求並驗收（2026-11-19）。

### FR-33 程式裡只出現 AgentDesk（ADR-21）
**優先級**: P2
**描述**: 執行檔、畫面與跟著發布的檔案只寫 AgentDesk。win98 版面改名 AgentDesk 98，開機、關於與開始選單側邊的圖片改由程式用像素字型畫出；Start 與 Update 圖示換成 AgentDesk 的標誌；預設主題 id 改為 `preset-<名稱>`。保留：THIRD_PARTY_NOTICES 裡 herdr-app 的授權聲明（Apache-2.0 第 4 條要求）、原始碼註解裡的出處、版本庫的 `third_party/herdr/`。
**驗收標準**:
- Given Release 建置輸出，When 在三個執行檔與建置輸出裡搜尋 herdr，Then 找不到（發布包的 THIRD_PARTY_NOTICES 授權聲明依授權規定保留）。
- Given 一份用舊主題 id 的設定檔，When AgentDesk 啟動，Then 照樣讀得到該主題。
**驗證**: `sprint/current/status.md`（2026-11-19「程式裡不出現 herdr 的名稱」）。

### FR-34 主題
**優先級**: P2
**描述**: 除了 herdr 的主題（graphite、ink、moss、paper 與隨附的 atom-one-dark），另外保留 AgentDesk 自己的四個主題（Midnight、Ember、Moss、Paper）；使用者放在 `themes\` 資料夾、格式相同的主題檔也會列出。介面顏色照 herdr 從主題推算。每個版面各自記住一組深色與淺色主題。設計文件 2.4、5.6、ADR-15。
**驗收標準**:
- Given 使用者把一個主題檔放進 `%LOCALAPPDATA%\AgentDesk\themes\`，When 打開設定 › Look，Then 它和內建主題列在一起。
- Given 目前版面是 workspace、系統為深色，When 使用者選了一個主題，Then 只換掉 workspace 的深色主題，其他版面與淺色主題不變。
**驗證**: 使用者決定（2026-11-19，status.md「加入 herdr 的主題」「主題跟著版面」）；使用者整體驗收（2026-11-19）。

### FR-35 AgentDesk 線上更新（ADR-18）
**優先級**: P2
**描述**: 照 herdr 的更新流程，但沒有預設伺服器：設定 `AGENTDESK_RELEASES_URL` 才檢查。啟動 10 秒後檢查，之後每小時一次，失敗 30 分鐘後重試；讀 `app/latest.json` 取 `windows-x86_64` 的 url 與 sha256，下載後比對 sha256 並確認 `agent-ui --version`。按 Restart to update、下次結束程式，或上次沒正常結束時在下次啟動前換版。更新後第一次啟動顯示 What's new。設計文件 2.7。
**驗收標準**:
- Given 沒有設定 `AGENTDESK_RELEASES_URL`，When AgentDesk 執行，Then 不連線檢查更新。
- Given 已設定 `AGENTDESK_RELEASES_URL`，伺服器上有較新版本，When 檢查完成，Then 下載、驗證後在右下角提示「AgentDesk X is ready」；按 Restart to update 換成新版並重開。
- Given 下載檔的 sha256 與 `latest.json` 不符，When 驗證，Then 不暫存任何檔案。
- Given 換版途中失敗，When 處理錯誤，Then 還原成原本的版本。
**驗證**: `Updates.*`；本機假伺服器實測（status.md 2026-11-19「驗證」）。

### FR-36 agent-host 單獨更新時接手終端機（ADR-19）
**優先級**: P2
**描述**: 使用者決定照 herdr 移植（2026-11-19，OQ-09）。舊 host 啟動 `agent-host --takeover`，凍結每個 session（解析器回到 ground 才停），把 ConPTY、子程序與 Job 的 handle 複製給新 host，並送出完整的終端機狀態（含歷史、OSC 7501 紀錄、seq）；新 host 逐一核對 handle 後回報 ready，舊 host 才放手。只支援隨附 ConPTY。觸發方式：`agentctl update`、設定 › Updates 的 agent-host 列，以及 app 更新後第一次啟動。設計文件 2.7。
**驗收標準**:
- Given host 正在跑幾個終端機，使用隨附 ConPTY，When 使用者執行 `agentctl update`，Then 新 host 接手所有 session，終端機裡的程式不中斷；連線會斷，client 重新連上後取得新快照，不把 session 標成 Lost。
- Given 新 host 在回報 ready 前出錯或逾時（30 秒），When 交接失敗，Then 舊 host 結束新程序並恢復所有 session 照常執行。
- Given 使用系統內建 ConPTY，When 要求更新 agent-host，Then 回報錯誤，終端機不受影響。
**驗證**: `HostUpdate.TerminalsKeepRunningThroughAnUpdate`、`HostUpdate.FailedTakeoverKeepsEverything`、`CoreUpdate.*`。

### FR-37 使用統計（ADR-18）
**優先級**: P3
**描述**: 照 herdr 收集使用統計，但沒有預設伺服器：設定 `AGENTDESK_TELEMETRY_URL` 才送出。每個 UTC 日一份報告，7 天後丟棄，使用隨機 id；報告不含自由文字。設定 › Privacy 可以關閉，並逐字顯示會送出的內容。設計文件 2.7。
**驗收標準**:
- Given 沒有設定 `AGENTDESK_TELEMETRY_URL`，When AgentDesk 執行，Then 不送出任何統計。
- Given 已設定 URL，但設了 `DO_NOT_TRACK`、`AGENTDESK_TELEMETRY=0` 或在 CI 裡，When 這次執行，Then 不送出。
- Given 使用者在設定 › Privacy 按「What we send」的 Show，When 畫面打開，Then 顯示待送的報告、今天的草稿與上次送出的內容。
- Given 使用者在 Privacy 關閉使用統計，When 關閉，Then 清空待送報告並換一個新的 id。
**驗證**: `Telemetry.*`；本機接收端實測（status.md 2026-11-19「驗證」）。
