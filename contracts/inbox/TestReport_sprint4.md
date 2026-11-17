---
contract_type: TestReport
version: 1.0.0
status: draft
author_agent: QA
sprint: 4
created_at: 2026-11-17T00:00:00+08:00
approved_at: ~
skills_used:
  - qa/framework-gtest@1.0.0
  - qa/strategy-chaos-engineering@1.0.0
upstream_deps:
  - PRD@1.1.0
  - docs/design/AgentDesk_Design.md@1.9
downstream_consumers:
  - Orchestrator
change_summary: |
  Sprint 4（M3 可靠性）測試報告。判定依 .decisions/ADR_001 替代門檻。
---

# TestReport：Sprint 4（M3 可靠性）

## 執行摘要

| 指標 | 數值 |
|---|---|
| 測試總數／通過／失敗／跳過 | 115／115／0／0（unit 66、windows_integration 49） |
| UI 端到端腳本 | QA-05、QA-10 腳本皆通過 |
| Unit Test Coverage | 未量測（工具未選定，ADR_001） |
| 編譯警告 | 0 |
| **整體結論** | ✅ S4-M1（故障有明確狀態）、S4-M2（續接效能）達成 |

## 效能測試結果

| 場景 | p50 | p95 | 最大 | 結論 |
|---|---|---|---|---|
| 4 個 120×40 session 重新 attach 到快照完整（Release，20 輪） | 40.7 ms | 44.3 ms | 44.6 ms | ✅ 目標 < 2 秒 |
| 同上（Debug） | 647.7 ms | 657.6 ms | 661.5 ms | ✅ |
| 1 MiB/s × 3 秒，同時有不讀資料的 client | FLOOD-DONE 在 10 秒內到達 | — | — | ✅ host 消費未被拖慢，健康 client 持續收到增量，host 私有記憶體 < 500 MB |

## 故障情境

| 情境 | 結果 | 證據 |
|---|---|---|
| host 被強制終止（UI 開著） | 子程序隨 Job 結束；UI 不 crash，自動重連新 host，session 顯示 Lost、不可控制 | `docs/poc/evidence/qa10-ui-host-crash.json` |
| 子程序崩潰 | Exited，結束碼 0xC0000005，Agent 狀態不是 Done | `HostFixture.CrashingChildReportsExitCode` |
| 設定檔損毀且無備份 | host 照常啟動，損毀檔保留為 `.corrupt-<時間>`，profiles 改用偵測到的預設值 | `HostFixture.CorruptSettingsDoNotPreventStartup` |
| sessions.json 無法寫入 | session 建立、輸入、讀取照常 | `HostFixture.PersistFailureKeepsHostRunning` |
| 快速 resize 100 次 | host、引擎與子程序看到的尺寸一致 | `HostFixture.RapidResizesEndConsistent` |
| 磁碟不足 | 未實測（需要真的填滿磁碟）；寫入失敗路徑已由上一列涵蓋 | — |

## 失敗案例詳情

### BUG-7：設定檔損毀且沒有備份時 host 無法啟動
**嚴重程度**: P1
**處理**: 保留損毀檔並以預設值啟動

### BUG-8：Lost session 在 UI 仍顯示為控制者
**嚴重程度**: P3（實際無法輸入）
**處理**: Lost 時清除控制者旗標

## 驗收矩陣對照（設計文件 6.2）

| QA ID | 狀態 | 證據 |
|---|---|---|
| QA-04 | ✅ | `HostFixture.RapidResizesEndConsistent` |
| QA-10 | ✅ | host 端測試＋UI 腳本 |
| QA-18 | ✅ | host 端背壓測試＋慢 client 測試；UI 凍結情境由「client 不讀取」等效涵蓋 |
| QA-15 | ⏸ | 等使用者手動驗證 |
