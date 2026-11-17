---
sprint: 3
start_date: 2026-11-17
complexity: M
execution_mode: standard
estimated_sprints: 5
qa_agent_enabled: true
sprint_phase: S_feature
status: active
---

# Sprint 3：M2 桌面整合

> 對應設計文件 5.1 的 M2（10–15 人日）。UI 由 M0 的「程序內 Session」改為 agent-host 的 client。

## 目標

- agent-ui.exe 透過 Named Pipe 連 host：關閉視窗只 detach，重開接回原畫面（US-03）。
- 兩個 Agent 可在 UI 同時操作；docking、歷史捲動、選取、控制權（US-02、US-06）。
- 退出條件（設計文件 5.1）：兩個 Agent 可操作，150% DPI 可用。

## 任務清單

| ID | Agent | 依賴 | 產出 | Skills | 狀態 |
|-----|-------|------|------|--------|------|
| S3-1 | RD | — | `libs/ui` RemoteSource：attach、分段快照組裝、增量套用（generation／base_seq 不連續就重要快照）、輸入與 resize 經背景送出執行緒（UI 執行緒不阻塞） | rd/frontend-imgui、rd/engine-terminal-vt | ⬜ |
| S3-2 | RD | S3-1 | UI 改連 host：session 清單（`session.list`＋`session.state`）、以 profile 建立、停止／強制停止、host 斷線覆蓋 Disconnected、instance id 改變時標記 Lost、自動重連 | rd/frontend-imgui | ⬜ |
| S3-3 | RD | S3-1 | TerminalView 歷史捲動（滑鼠滾輪以 `history.read` 分頁）與以 line_id 為準的選取 | rd/frontend-imgui | ⬜ |
| S3-4 | RD | S3-2 | 控制權：唯讀標示、「取得控制」按鈕、唯讀面板不改尺寸 | rd/frontend-imgui | ⬜ |
| S3-5 | RD | S3-2 | 關閉語意：視窗 X＝detach（首次說明一次）、面板 X 不停止 session、「退出並停止所有工作」列出執行中工作後才 shutdown | rd/frontend-imgui | ⬜ |
| S3-6 | RD | S3-2 | UI 自動啟動 host；breakaway 被拒且父 Job 會連帶結束時提示（QA-20） | rd/platform-windows-conpty | ⬜ |
| S3-7 | RD | S3-2 | 狀態列：host 連線、控制權、字元尺寸、截斷與背壓提示 | rd/frontend-imgui | ⬜ |
| S3-8 | QA | S3-1～S3-7 | 測試：RemoteSource 增量／缺號重新同步單元測試、UI 經 host 啟動冒煙、QA-05（關 UI 後再開，PID 與 session 不變、計數連續） | qa/framework-gtest | ⬜ |
| S3-9 | QA | S3-8 | `contracts/inbox/TestReport_sprint3.md` | qa/framework-gtest | ⬜ |
| S3-10 | 使用者 | — | 自 S2-9 轉入：繁中 IME、DPI 手動驗證 | rd/frontend-imgui | ⏸ |

## 里程碑

| ID | 項目 | 退出條件 | 狀態 |
|----|------|---------|------|
| S3-M1 | UI 是 host client | UI 關閉後 session 持續；重開後接回原畫面 | ⬜ |
| S3-M2 | 兩 Agent 可操作 | 兩個 session 同時在 UI 中可輸入、各自尺寸 | ⬜ |
| S3-M3 | 150% DPI 可用 | 需使用者手動確認（S3-10） | ⏸ |

## 開放例外

無。
