---
contract_type: TestReport
version: 1.0.0
status: draft
author_agent: QA
sprint: 2
created_at: 2026-11-17T00:00:00+08:00
approved_at: ~
skills_used:
  - qa/framework-gtest@1.0.0
  - qa/strategy-security@1.0.0
upstream_deps:
  - PRD@1.1.0
  - docs/design/AgentDesk_Design.md@1.9
downstream_consumers:
  - Orchestrator
change_summary: |
  Sprint 2（M1 Host 基礎）測試報告。判定依 .decisions/ADR_001 替代門檻。
---

# TestReport：Sprint 2（M1 Host 基礎）

## 執行摘要

| 指標 | 數值 |
|---|---|
| 測試總數／通過／失敗／跳過 | 104／104／0／0（unit 63、windows_integration 41） |
| 穩定性 | windows_integration 41 個以 `--repeat until-fail:3 -j 4` 平行重複 3 輪全數通過 |
| Unit Test Coverage | 未量測（工具未選定，ADR_001） |
| 編譯警告 | 0（`/W4 /permissive- /WX`） |
| **整體結論** | ✅ M1 退出條件達成：兩個工作互不干擾；UI 不在也可讀寫與停止 |

重現：`scripts\build.cmd msvc-x64-debug all`。

## 效能測試結果

本 Sprint 未新增效能量測；M0 的 Session 量測（`docs/poc/evidence/`）仍適用。pipe 往返在 8 個 client 並行、各 20 次請求的測試中全數於逾時內完成。

## 失敗案例詳情

開發中發現並已修正：

### BUG-3：不同 agentctl 程序的 request id 相同，被 host 誤判為重送
**嚴重程度**: P1
**重現步驟**: 1. `agentctl run --profile echo` 2. 另一個 `agentctl run --profile broken`
**預期結果 vs 實際結果**: 預期第二個回 PROFILE_INVALID（退出碼 6）；實際回傳第一個 session 的結果（退出碼 0）
**可能原因**: 每個程序的 id 都從 `c-1` 起算，host 依 request id 去重 10 分鐘
**處理**: RD 改為 `c-<pid>-<計時器>-<序號>`；設計文件 3.7.1 記錄此要求

### BUG-4：PipeClient::close() 未關閉 pipe handle
**嚴重程度**: P2
**重現步驟**: 控制者 client 呼叫 close() 後，另一 client 嘗試輸入
**預期結果 vs 實際結果**: 預期控制權釋放；實際 host 未察覺斷線，仍回 NOT_CONTROLLER
**處理**: close() 釋放 handle；測試 `HostFixture.SingleInputController` 涵蓋

### BUG-5：測試程序 crash 時遺留 agent-host.exe
**嚴重程度**: P2（只影響測試環境）
**處理**: 修正測試的生命週期錯誤；fixture 將 host 放入 kill-on-close Job

## 驗收矩陣對照（設計文件 6.2）

| QA ID | 狀態 | 證據 |
|---|---|---|
| QA-03 | ✅ host 端 | `HostFixture.TwoSessionsAreIndependent` |
| QA-05 | 🟡 | host 背景續跑已具備（UI 關閉不影響 host）；UI 重新連線驗收在 M2 |
| QA-06 | ✅ | `HostFixture.AttachSnapshotThenContiguousDeltas`（輸出持續時 attach，增量 base_seq 連續） |
| QA-10 | ✅ | `HostFixture.HostCrashMarksSessionsLost`（子程序隨 Job 結束、Lost、不接受輸入、instance id 改變） |
| QA-13 | ✅ | `HostFixture.CliContractAndExitCodes`（`--json` 可解析、未加 `--enter` 不送 Enter、退出碼 0、2～7） |
| QA-14 | ✅ host 端 | `HostFixture.SingleInputController` |
| QA-17 | 🟡 | 超長 frame 斷線、遠端拒絕旗標、SID 比對已實作並測試；以「另一個 Windows 使用者」實際連線未測（需第二個帳號） |
| QA-19 | ✅ | `HostFixture.ReportStateViaCli`、`Session.ReportStateIgnoresStaleSeq`；hook 腳本實機驗證 |
| QA-20 | 🟡 | breakaway 與 Job 偵測已實作；「關閉啟動 UI 的終端機」需 M2 UI 連 host 後驗證 |

## 通過的核心業務流程

- [x] US-03（host 端）：工作在 host 內持續，與 client 生命週期無關
- [x] US-05：`agentctl stop --interrupt／--force`
- [x] US-07：agentctl 全部指令、JSON 輸出、退出碼
- [ ] US-02、US-03（UI 端）：M2

## 未涵蓋項目

- 以另一個 Windows 帳號連線的 ACL 實測（QA-17）
- 繁中 IME 與 DPI（S2-9，等使用者）
