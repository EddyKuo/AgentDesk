---
sprint: 2
start_date: 2026-11-17
complexity: M
execution_mode: standard
estimated_sprints: 5
qa_agent_enabled: true
sprint_phase: S_feature
status: done
---

# Sprint 2：M1 Host 基礎

> 對應設計文件 5.1 的 M1（修正估工 6–9 人日，M0 報告第 9 節）。Session actor、背壓、協定分幀已在 M0 完成。

## 目標

- `agent-host.exe` 背景程序：單一實例、Named Pipe IPC v1、管理多個 session，UI 不在也能讀寫與停止。
- `agentctl.exe`：run／list／read／send／stop／report-state，穩定 JSON 輸出與退出碼（設計文件 3.8）。
- 退出條件（設計文件 5.1）：2 個工作互不干擾；UI 不在也可讀寫與停止。

## 任務清單

| ID | Agent | 依賴 | 產出 | Skills | 狀態 |
|-----|-------|------|------|--------|------|
| S2-1 | RD | — | `libs/ipc`：Named Pipe 伺服器與客戶端（自訂 DACL、拒絕遠端、首實例旗標、client SID 驗證、分幀、每 client 4 MiB 送出預算） | rd/platform-windows-conpty | ✅ |
| S2-2 | RD | S2-1 | `libs/host` SessionManager：session.create／list／get／attach／detach／input／resize／stop、screen.snapshot、history.read、state.report、host.shutdown；request_id 去重 10 分鐘；單一輸入控制者；attach 快照＋增量（seq 不漏） | rd/platform-windows-conpty、rd/engine-terminal-vt | ✅ |
| S2-3 | RD | S2-2 | `apps/host` agent-host.exe：每使用者每登入 session 單一實例、`DETACHED_PROCESS`＋breakaway 啟動、環境變數 `AGENTDESK_SESSION_ID`／`AGENTDESK_CTL`／`AGENTDESK_PIPE`、profiles.json／sessions.json（原子寫入、Lost 標記） | rd/platform-windows-conpty | ✅ |
| S2-4 | RD | S2-1 | `apps/cli` agentctl.exe：run／list／read／send（預設不送 Enter）／stop（interrupt／force）／report-state；name 歧義拒絕；`--json` 只輸出結果；退出碼 0、2～7；必要時自動啟動 host | rd/lang-cpp | ✅ |
| S2-5 | QA | S2-2～S2-4 | 整合測試：兩 session 互不干擾（QA-03 host 端）、UI 不在仍可讀寫停止、退出碼（QA-13）、report-state（QA-19）、pipe 拒絕其他使用者與超長 frame（QA-17）、host 重啟標記 Lost（QA-10）、第二 client 唯讀（QA-14 host 端） | qa/framework-gtest、qa/strategy-security | ✅ |
| S2-6 | RD | S2-4 | Codex hook 範例腳本（不自動安裝）：PowerShell hook 呼叫 `agentctl report-state` | shared/domain-coding-agents | ✅ |
| S2-7 | QA | S2-5 | `contracts/inbox/TestReport_sprint2.md` | qa/framework-gtest | ✅ |
| S2-8 | Orchestrator | S2-7 | 設計文件升版（若協定細節有變）、status 收尾 | — | ✅ |
| S2-9 | 使用者 | — | 自 S1-7 轉入：繁中 IME 與 DPI 手動驗證（M0 報告第 10 節步驟） | rd/frontend-imgui | ⏸ |

狀態：⬜ 未開始｜🔄 進行中｜⏸ BLOCKED｜✅ 完成

## 並行執行組

| 組別 | 任務 | 前提 |
|------|------|------|
| G1 | S2-1 | — |
| G2 | S2-2 ∥ S2-4（client 端） | S2-1 |
| G3 | S2-3 → S2-5 → S2-7 | S2-2、S2-4 |

## 里程碑

| ID | 項目 | 退出條件 | 狀態 |
|----|------|---------|------|
| S2-M1 | host 可用 | `agentctl run` 自動啟動 host 並建立 session；關掉 agentctl 後工作持續 | ✅ |
| S2-M2 | 兩工作互不干擾 | 兩個 session 同時輸出，對其中一個輸入不影響另一個 | ✅ |
| S2-M3 | CLI 契約 | 退出碼與 `--json` 行為符合設計文件 3.8 | ✅ |

## 開放例外

無。
