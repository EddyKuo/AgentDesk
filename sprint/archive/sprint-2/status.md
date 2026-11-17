---
sprint: 2
updated_at: 2026-11-17T00:00:00+08:00
plan: sprint/current/plan.md
---

# Sprint 2：狀態

> 由 Orchestrator 維護，只追加不改寫歷史列。

## 任務狀態快照

| 時間 | 任務 ID | 舊狀態 | 新狀態 | 說明 |
|------|--------|--------|--------|------|
| 2026-11-17 | S2-1～S2-8 | — | ⬜ | 計畫建立 |
| 2026-11-17 | S2-9 | — | ⏸ | 自 S1-7 轉入，等使用者手動驗證 |
| 2026-11-17 | S2-1 | ⬜ | ✅ | libs/ipc：Named Pipe server／client，DACL、拒絕遠端、首實例、雙向 SID 比對、4 MiB 送出預算。7 個測試 |
| 2026-11-17 | S2-2 | ⬜ | ✅ | SessionManager：協定 v1 全部方法、attach 快照＋增量、分段快照、單一控制者、request id 去重、profiles／sessions.json、Lost 標記 |
| 2026-11-17 | S2-3 | ⬜ | ✅ | agent-host.exe：單一實例 mutex、pipe server、AGENTDESK_* 環境變數、host.log |
| 2026-11-17 | S2-4 | ⬜ | ✅ | agentctl.exe：run／list／read／send／stop／report-state／shutdown、`--json`、退出碼 0、2～7、自動啟動 host（breakaway） |
| 2026-11-17 | S2-5 | ⬜ | ✅ | 8 個 host 端到端測試；修正 request id 撞號（BUG-3）與 client close 未釋放 handle（BUG-4）。104/104，整合測試平行重複 3 輪通過 |
| 2026-11-17 | S2-6 | ⬜ | ✅ | hook 腳本與整合文件；實機驗證 Blocked／Idle 與 last_agent_state |
| 2026-11-17 | S2-7 | ⬜ | ✅ | `contracts/inbox/TestReport_sprint2.md` |
| 2026-11-17 | S2-8 | ⬜ | ✅ | 設計文件 v1.9（3.7.1 協定補充、3.8 CLI 選項） |
| 2026-11-17 | — | — | — | 里程碑 S2-M1、S2-M2、S2-M3 達成 |

## 契約狀態

| 契約 | 版本 | 狀態 | 審核時間 | 下游通知 |
|------|------|------|---------|---------|
| PRD | 1.1.0 | approved | 2026-11-16 | RD、QA |
| TestReport（Sprint 1） | 1.0.0 | draft（contracts/inbox） | — | Orchestrator |

## 例外摘要

無。

## Skill 衝突紀錄

沿用 Sprint 0、1 紀錄。

## 估算調整

| 時間 | 欄位 | 舊值 | 新值 | 理由 |
|------|------|------|------|------|
| 2026-11-17 | M1 人日 | 8–12 | 6–9 | M0 已完成 Session、背壓、協定分幀 |

## Sprint 結束檢查

- [x] 所有任務為 ✅ 或已轉入下個 Sprint（附理由：S2-9 需使用者手動驗證 IME／DPI，轉入 S3-10）
- [x] 無未解決的 P0/P1 例外
- [x] 升級偵測已執行（CLAUDE.md Section 7.1），結果：維持 M
- [x] plan.md、status.md 已移至 `sprint/archive/sprint-2/`
