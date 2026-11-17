---
contract_type: TestReport
version: 1.0.0
status: draft
author_agent: QA
sprint: 1
created_at: 2026-11-17T00:00:00+08:00
approved_at: ~
skills_used:
  - qa/framework-gtest@1.0.0
  - qa/strategy-security@1.0.0
upstream_deps:
  - PRD@1.1.0
  - docs/design/AgentDesk_Design.md@1.7
downstream_consumers:
  - Orchestrator
change_summary: |
  Sprint 1（M0 技術 POC）測試報告。判定依 .decisions/ADR_001 替代門檻。
---

# TestReport：Sprint 1（M0 技術 POC）

## 執行摘要

| 指標 | 數值 |
|---|---|
| 測試總數／通過／失敗／跳過 | 89／89／0／0（unit 63、windows_integration 26） |
| 執行環境 | Windows 10.0.26300、MSVC 14.44.35207、Debug，隨附 ConPTY 1.24.260710001 |
| Unit Test Coverage | 未量測（工具未選定，ADR_001） |
| 編譯警告 | 0（`/W4 /permissive- /WX`） |
| 格式 | clang-format 19.1.5 已套用 |
| **整體結論** | ✅ M0 範圍內自動化測試全數通過；IME 與 DPI 手動驗證尚未執行（見「未涵蓋項目」） |

重現指令：`scripts\build.cmd msvc-x64-debug all`（在 repo 根目錄）。

## 效能測試結果

Release 建置，證據在 `docs/poc/evidence/`。

| 場景 | p50 | p95 | p99 | 錯誤率 | 結論 |
|---|---|---|---|---|---|
| 按鍵 → 回顯出現在快照（cmd.exe，100 次） | 15.5 ms | 16.1 ms | 16.6 ms | 0% | ✅ 目標 p95 < 100 ms（不含 UI 繪製，UI 約再加一個 vsync） |
| 1 MiB/s 持續 60 秒 | 實測 0.99 MiB/s | — | — | 0 行遺失（982,800 行依序到達） | ✅ host 持續消費，未觸發背壓 |
| 120×40 快照建立 | 平均 249～277 µs | — | — | — | ✅ |
| 100 次建立／停止 | 平均 43 ms／次 | — | — | — | ✅ handle 88、執行緒 4，全程不成長 |
| UI 閒置 10 秒 | CPU 31 ms | — | — | — | ✅ 接近 0% |
| 峰值記憶體（bench 程序） | 42 MB 工作集 | — | — | — | ✅ 目標 < 500 MB（UI 另計 73 MB） |

## 失敗案例詳情

本 Sprint 沒有未修復的失敗。開發過程中發現並已修正的問題：

### BUG-1：UTF-8 字元被切在兩次讀取之間時出現 U+FFFD
**嚴重程度**: P1
**重現步驟**: 1. 送出 `x50` 加上 `行` 的第一個位元組 2. 下一段送其餘兩個位元組與 `\r\n` 3. 讀畫面
**預期結果 vs 實際結果**: 預期 `x50行`；實際出現 U+FFFD，並污染下一行
**可能原因**: libvterm 0.3.3 本身的行為（直接呼叫 libvterm 可重現）
**處理**: RD 在 VtEngine 暫存尾端不完整序列；回歸測試 `VtEngine.Utf8SplitBeforeControlCharsIsReassembled`

### BUG-2：關閉時可能在 WriteFile 使用中釋放輸入 handle
**嚴重程度**: P1
**重現步驟**: 寫入執行緒寫入的同時呼叫 `PtyProcess::close()`
**預期結果 vs 實際結果**: 預期寫入失敗後安全返回；實際可能使用已關閉的 handle
**處理**: RD 加 write mutex，關閉 pseudoconsole 後才釋放 handle

## 驗收矩陣對照（設計文件 6.2）

M0 只涵蓋 host 端與原型；完整 UI 驗收在 M2～M4。

| QA ID | 狀態 | 證據 |
|---|---|---|
| QA-01 | ✅ host 端 | `ConPty.CwdWithSpaceAndChineseIsUsed` |
| QA-02 | ✅ host 端 | `ConPty.RejectsBatchFilesAndMissingPaths`、`Session.StartFailureIsReported` |
| QA-03 | ⬜ | 需多 session UI（M2） |
| QA-04 | 🟡 部分 | 單次 resize 測試；快速 100 次待 M2 |
| QA-05 | ⬜ | 需 host 背景程序（M1） |
| QA-06 | ⬜ | 需 attach 協定（M1） |
| QA-07 | ✅ | `Session.SilenceWithoutSourceStaysUnknown`（靜默 1.5 秒；60 秒版本併入 M4 長測） |
| QA-08 | ✅ | `ProgramStatusStore.*`、`Session.ReportStateIgnoresStaleSeq` |
| QA-09 | ✅ | `Session.InterruptGracePeriodThenForceStop`、`ConPty.TerminateKillsWholeTree` |
| QA-10 | ⬜ | 需 host（M1） |
| QA-11 | ✅ 邏輯 | `InputMapping.SelectionTextHandlesWideAndCombining` |
| QA-12 | ✅ | `VtEngine.HistoryIsBoundedByLinesAndAltScreenIsExcluded`、`HistoryIsBoundedByBytes` |
| QA-13 | ⬜ | 需 agentctl（M1） |
| QA-14 | ⬜ | 需多 client（M1／M2） |
| QA-15 | ⬜ | 手動：繁中 IME 與 DPI，待使用者驗證 |
| QA-16 | ✅ | OSC 52 未註冊回呼；標題控制字元移除測試 |
| QA-17 | 🟡 部分 | 分幀上限與 UTF-8 檢查（`Frame.*`）；管道 ACL 待 M1 |
| QA-18 | ✅ host 端 | 背壓測試 `Session.BackpressureNeverDropsOutput` |
| QA-19 | 🟡 部分 | host 端 seq 去重；CLI 部分待 M1 |
| QA-20 | ⬜ | 需 host 背景程序（M1） |
| QA-21 | ✅ | `ConPtyCompareTest.*/bundled`、實際 Claude Code 2.1.326 回報 idle |
| QA-22 | ✅ | `Session.Osc7501StateThenUnknownAfterExitKeepsLastState`、`VtEngine.ShellPromptOsc133ClearsWorking` |

## 通過的核心業務流程

- [x] US-01：在指定 cwd 啟動 Agent（host 端與原型 UI）
- [ ] US-02：兩個 Agent 同時互動（原型可開多個 session，正式驗收待 M2）
- [ ] US-03：關閉視窗後續跑（M1 host）
- [x] US-04：Claude Code 經 OSC 7501 顯示狀態；無來源時顯示 Unknown
- [x] US-05：中斷、寬限、強制終止整個程序樹
- [x] US-06：歷史上限與選取文字邏輯（UI 捲動待 M2）
- [ ] US-07：CLI（M1）

## 未涵蓋項目

- 繁中注音輸入、候選視窗位置、Enter 不重複送出、100%／150%／200% DPI（QA-15）：需要人工操作，自動化無法替代。
- Codex hook 的狀態轉換：需修改使用者的 `~/.codex` 設定並送出 prompt，未經同意不執行。
