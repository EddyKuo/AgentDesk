---
sprint: 5
start_date: 2026-11-17
complexity: M
execution_mode: standard
estimated_sprints: 5
qa_agent_enabled: true
sprint_phase: S6_deliver
status: active
---

# Sprint 5：M4 驗收發布

> 對應設計文件 5.1 的 M4（8–12 人日）：adapter、測試、效能、portable zip 與文件。退出條件：完成第 6 章矩陣，限制清單與 notices 齊備。

## 任務清單

| ID | Agent | 依賴 | 產出 | Skills | 狀態 |
|-----|-------|------|------|--------|------|
| S5-1 | QA | — | 穩定性：soak 工具（多 session 持續輸出＋反覆 attach／detach，取樣 host 的 handle、thread、記憶體）；開發機跑 30 分鐘並附 8 小時版的執行方式 | qa/strategy-chaos-engineering | ⬜ |
| S5-2 | RD | — | `scripts/package.ps1`：Release 建置 → portable zip（三個執行檔、隨附 ConPTY、LICENSE、THIRD_PARTY_NOTICES、hook 腳本、使用說明） | rd/lang-cpp | ⬜ |
| S5-3 | Orchestrator | — | 文件：使用說明（安裝、操作、CLI）、限制清單、相容性矩陣（設計文件 6.3） | — | ⬜ |
| S5-4 | QA | S5-2 | Release 建置全部測試通過；zip 解壓後在乾淨目錄冒煙（host 自動啟動、Claude Code 顯示狀態） | qa/framework-gtest | ⬜ |
| S5-5 | QA | — | 安全檢查：QA-16（OSC 52、輸出不觸發命令）、QA-17（pipe ACL 設定實際讀回）、診斷 log 不含輸入文字 | qa/strategy-security | ⬜ |
| S5-6 | QA | S5-1～S5-5 | 最終驗收報告：QA-01～QA-22 逐項狀態與證據、未通過項目的限制或阻擋結論 | qa/framework-gtest | ⬜ |
| S5-7 | Orchestrator | S5-6 | 設計文件 2.0（MVP 完成狀態、限制、後續）、CLAUDE.md、M4 報告 | — | ⬜ |
| S5-8 | 使用者 | — | 人工驗收清單：繁中 IME、DPI、兩個實際 Agent 同時互動、Codex hook 實機、8 小時 soak、另一個 Windows 帳號連線被拒 | — | ⏸ |

## 里程碑

| ID | 項目 | 退出條件 | 狀態 |
|----|------|---------|------|
| S5-M1 | 可發布 | portable zip 可在乾淨目錄執行；notices 齊備 | ⬜ |
| S5-M2 | 驗收矩陣完成 | QA-01～QA-22 每項都有通過證據，或寫明限制／待人工 | ⬜ |

## 開放例外

無。
