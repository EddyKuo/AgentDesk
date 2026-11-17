---
contract_type: PRD
version: 1.1.0
status: approved
author_agent: PM
sprint: 0
created_at: 2026-11-16T00:00:00+08:00
approved_at: 2026-11-16T00:00:00+08:00
skills_used:
  - shared/domain-coding-agents@1.0.0
upstream_deps:
  - raw_requirement@2026-11-16
  - docs/design/AgentDesk_Design.md@1.7
downstream_consumers:
  - RD
  - QA
change_summary: |
  1.1.0（Minor）：跟進設計文件 v1.7。2.4 補紀錄被丟棄後顯示 Unknown 並保留 last_agent_state、report-state 新增 --kind／--message；2.7 驗收加退出碼（QA-13）。
  1.0.0：初版。M 模式精簡 PRD：需求本文以 docs/design/AgentDesk_Design.md v1.6 為單一真相來源，
  本契約固定版本並補齊 agents/pm.md 要求的結構。Orchestrator 依 M 模式輕量審核通過。
---

# Product Requirements Document：AgentDesk

> 本契約鎖定設計文件 **v1.7**。設計文件之後的變更若影響第 2、3、4 節，必須依 CLAUDE.md Section 6.3 將本契約升版，下游才能採用。

## 1. 產品概述

### 1.1 背景與問題陳述
見設計文件 1.1。同時跑多個 coding agent CLI 時，難以掌握哪個在等輸入、哪個已結束；一般桌面 UI 持有執行程序，關掉視窗工作就跟著中斷。

### 1.2 目標使用者（Persona）
在 Windows 11 x64 上，以一般使用者權限同時操作兩個以上 coding agent CLI（首發為 Claude Code 與 Codex CLI）的開發者。使用者自行安裝並登入這些 CLI（設計文件 1.4）。

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

## 2. 功能需求

每項的驗收標準（Given／When／Then）在設計文件 **1.2.1**，詳細規則在對應章節。

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
**邊界條件**: 亂序事件依 seq 處理，OSC 7501 的 seq 由 host 指派（QA-08）；紀錄被丟棄或清除後顯示 Unknown，原值保留在 `last_agent_state`；report-state 可帶 `--kind`、`--message`；OSC 7501 查詢回覆須在 DA1 回覆之前（QA-21）；程序結束時清除 working／blocked（QA-22）。

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
| 安全 | Named Pipe 只允許目前使用者、拒絕遠端；輸出不觸發命令或寫剪貼簿 | 設計文件 3.7、3.9；QA-16、QA-17 |
| 隱私 | 診斷 log 不記錄輸入文字或完整環境變數 | 設計文件 3.9 |
| 平台 | Windows 11 x64、一般使用者權限；100%／150%／200% DPI；繁中 IME | 設計文件 1.4；QA-15 |
| 授權 | 本身 MIT；依賴須與 MIT 相容 | ADR-10 |

## 4. 不在範圍內（Out of Scope）
設計文件 1.3 的 P2、P3 項目，以及：HTTP backend、Docker、資料庫、遠端控制、自動多 Agent 排程、跨重開機續跑（ADR-07）、畫面文字推斷 Agent 狀態（ADR-09）、Windows 10 保證支援、WSL 支援承諾。

## 5. 假設與限制
- 使用者預先安裝並登入 Claude Code（v2.1.295 以上才有 OSC 7501 狀態）與 Codex CLI；精確版本由 M0 凍結。
- 開發只用已安裝的 VS 2022 C++ 工具鏈，不另外安裝工具（ADR-11）。
- 內建 ConPTY 是否足夠由 M0 決定（ADR-08）；若需隨附版本，發行包會多兩個檔案。
- 同一目錄的多個 Agent 會改到同一批檔案，只顯示提示，不提供檔案隔離。

## 6. 術語表
以設計文件 7.3 為單一真相來源。

## 7. Aesthetic Refs
不適用（本專案為 M 模式、standard 執行模式）。
