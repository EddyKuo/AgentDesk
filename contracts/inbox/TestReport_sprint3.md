---
contract_type: TestReport
version: 1.0.0
status: draft
author_agent: QA
sprint: 3
created_at: 2026-11-17T00:00:00+08:00
approved_at: ~
skills_used:
  - qa/framework-gtest@1.0.0
upstream_deps:
  - PRD@1.1.0
  - docs/design/AgentDesk_Design.md@1.9
downstream_consumers:
  - Orchestrator
change_summary: |
  Sprint 3（M2 桌面整合）測試報告。判定依 .decisions/ADR_001 替代門檻。
---

# TestReport：Sprint 3（M2 桌面整合）

## 執行摘要

| 指標 | 數值 |
|---|---|
| 測試總數／通過／失敗／跳過 | 109／109／0／0（unit 66、windows_integration 43） |
| UI 端到端腳本 | `tools/qa/qa05_ui_detach.ps1` 通過；Claude Code 經 host→UI 顯示 Idle（osc7501） |
| Unit Test Coverage | 未量測（工具未選定，ADR_001） |
| 編譯警告 | 0 |
| **整體結論** | ✅ S3-M1（UI 是 host client）、S3-M2（多 session 可操作）達成；S3-M3（150% DPI）待人工驗證 |

## 效能測試結果

| 場景 | 結果 | 結論 |
|---|---|---|
| UI 關閉 3 秒後重開到可操作 | 3 秒內完成（腳本以 3 秒退出計時，報告時已連線並取得控制） | ✅ 目標 p95 < 2 秒的正式量測移到 M3（S4-6） |
| UI 閒置 | 事件驅動，1 秒心跳；未另行量測 | — |

## 失敗案例詳情

### BUG-6：捲動時歷史內容不更新
**嚴重程度**: P2
**重現步驟**: 1. 捲進歷史 2. 程式持續輸出
**預期結果 vs 實際結果**: 預期歷史持續更新；實際停在第一次抓到的內容
**處理**: 超過 1 秒或需要更多行時重新抓取

## 驗收矩陣對照（設計文件 6.2）

| QA ID | 狀態 | 證據 |
|---|---|---|
| QA-03 | ✅ | host 端測試；UI 可同時開多個 session 面板 |
| QA-05 | ✅ | `docs/poc/evidence/qa05-ui-detach.json`：同一 session id 與 PID（40880）、重開後 Running、計數 1～48 無缺號、重開的 UI 取得控制 |
| QA-06 | ✅ | `RemoteScreen.*`（缺號重新同步）＋ host 端連續增量測試 |
| QA-14 | ✅ | host 端測試；UI 顯示唯讀橫幅與「Take control」 |
| QA-20 | ✅ | `HostLaunch.HostBreaksAwayFromLauncherJob`、`HostLaunch.WarnsWhenHostCannotLeaveKillOnCloseJob` |
| QA-04 | ⬜ | 快速 resize 100 次：S4 |
| QA-10 | 🟡 | host 端已驗；UI 端（host 被殺 → Disconnected → 新 host → Lost）：S4 |
| QA-15 | ⏸ | 等使用者手動驗證 |

## 通過的核心業務流程

- [x] US-02：多個 session 面板同時開啟，每個面板有獨立輸入與尺寸
- [x] US-03：關閉視窗只 detach，重開接回原畫面
- [x] US-06：歷史捲動（文字，最多最新 200 行）、畫面與整行選取、複製
- [ ] US-02 正式驗收（兩個實際 Agent 同時互動）：需人工操作，列入 M4 驗收

## 未涵蓋項目

- 繁中 IME 與 DPI（等使用者）
- 歷史捲動目前為純文字（不含顏色），上限 200 行；記入限制清單
