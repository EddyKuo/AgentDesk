---
contract_type: TestReport
version: 1.0.0
status: draft
author_agent: QA
sprint: 5
created_at: 2026-11-17T00:00:00+08:00
approved_at: ~
skills_used:
  - qa/framework-gtest@1.0.0
  - qa/strategy-chaos-engineering@1.0.0
  - qa/strategy-security@1.0.0
upstream_deps:
  - PRD@1.1.0
  - docs/design/AgentDesk_Design.md@2.0
downstream_consumers:
  - Orchestrator
change_summary: |
  Sprint 5（M4 驗收發布）測試報告，也是 MVP 的最終驗收矩陣。判定依 .decisions/ADR_001 替代門檻。
---

# TestReport：Sprint 5（M4 驗收發布）

## 執行摘要

| 指標 | 數值 |
|---|---|
| 測試總數／通過／失敗／跳過 | 120／120／0／0（Debug 與 Release 各跑一次） |
| UI 與發布腳本 | QA-05、QA-07、QA-10、zip 冒煙測試全部通過 |
| 穩定性 | 30 分鐘與 8 小時 soak 都通過 |
| Unit Test Coverage | 未量測（工具未選定，ADR_001） |
| 編譯警告 | 0（`/W4 /WX`） |
| **整體結論** | ✅ S5-M1 可發布；✅ S5-M2 驗收矩陣完成：20 項通過、2 項部分通過並列出待人工項目 |

## 穩定性（soak）

`tools/soak`，Release，4 個持續輸出的 session，每 5 秒 attach／detach，每 20 秒建立並停止一個 session：

| 指標 | 中段平均 | 末段平均 |
|---|---|---|
| handle | 204.5 | 204.8 |
| thread | 32.7 | 32.5 |
| 私有記憶體 | 170.1 MB | 150.0 MB |

30 分鐘內 attach 356 次、建立／停止 90 次、重開 flood 88 次，沒有持續成長。證據：`docs/poc/evidence/m4-soak-30min.json`。

1 分鐘的試跑發現 BUG-9：已結束的 session 一直保留執行緒、引擎和歷史，建立／停止越多，執行緒和記憶體就越多。修正後只保留最新 8 個已結束 session 的完整畫面，較舊的只留清單紀錄（`HostFixture.ExitedSessionsAreReleasedBeyondRetention`）。

8 小時版（2026-11-18 04:40～12:41，同樣的負載，`soak.exe --probe <vt_probe.exe> --host <agent-host.exe> --minutes 480 --sample-seconds 60`）：

| 指標 | 中段平均 | 末段平均 |
|---|---|---|
| handle | 205.5 | 205.2 |
| thread | 33.5 | 33.5 |
| 私有記憶體 | 153.8 MB | 154.0 MB |

8 小時內 attach 5,679 次、建立／停止 1,420 次、重開 flood 1,418 次，沒有持續成長。證據：`docs/poc/evidence/m4-soak-8h.json`。

## 發布包

| 項目 | 結果 |
|---|---|
| `dist\AgentDesk-0.1.0-win-x64.zip` | 2.3 MB；三個執行檔、隨附 ConPTY（含 LICENSE、NOTICE）、LICENSE、THIRD_PARTY_NOTICES.md、hook 腳本、使用說明、相容性矩陣 |
| 符號檔 | 另外打包成 `-symbols.zip` |
| 乾淨目錄冒煙 | host 自動啟動且來自 zip 目錄、使用隨附 ConPTY、OSC 7501 由 host 接收、UI 連線並顯示 2 個分頁、UI 結束後工作繼續、notices 齊備（`docs/poc/evidence/m4-smoke-zip.json`） |

## 本 Sprint 修正的問題

### BUG-9：已結束的 session 不釋放資源
**嚴重程度**: P1（長時間使用時記憶體與執行緒持續成長）
**處理**: 保留最新 8 個，較舊的釋放

### BUG-10：中文與寬字元顯示錯位（使用者回報）
**嚴重程度**: P1
**原因**: 格寬取自 `CalcTextSize("M")`，它會無條件進位（12.09 px 算成 13 px），游標和選取越往右越偏；中文字形靠左畫在兩格寬的位置裡，看起來字字分開；⚠、✓、emoji 等符號沒有字型，顯示成「?」
**處理**: 改用字型的真實寬度並對齊整數像素；每格各自定位；寬字元放大到填滿並置中；加上 Segoe UI Symbol／Emoji 當備援字型，ImGui 改用 32 位元 `ImWchar`
**證據**: 截圖工具 `tools/qa/screenshot_ui.ps1`；中文、方框字元、BMP 外的 emoji 皆正確

### 觀察：一次性的 Big5 亂碼
有一次截圖看到 cmd 的中文變成 Big5 位元組，之後連續 6 次重現不出來。新增 `HostFixture.LocalizedOutputSurvivesResize`（縮放後中文仍是合法 UTF-8），目前通過。列為 P3 觀察項。

## 驗收矩陣（設計文件 6.2）

| QA ID | 狀態 | 證據 |
|---|---|---|
| QA-01 | ✅ | `ConPty.CwdWithSpaceAndChineseIsUsed` |
| QA-02 | ✅ | `ConPty.RejectsBatchFilesAndMissingPaths`、`Session.StartFailureIsReported` |
| QA-03 | ✅ | `HostFixture.TwoSessionsAreIndependent`；UI 多分頁、分割面板 |
| QA-04 | ✅ | `HostFixture.RapidResizesEndConsistent` |
| QA-05 | ✅ | `docs/poc/evidence/qa05-ui-detach.json` |
| QA-06 | ✅ | `HostFixture.AttachSnapshotThenContiguousDeltas`、`RemoteScreen.*` |
| QA-07 | ✅ | 60 秒實測：沒有來源維持 Unknown，可信的 Working 維持 Working（`docs/poc/evidence/qa07-silence-60s.json`） |
| QA-08 | ✅ | `ProgramStatusStore.*`、`Session.ReportStateIgnoresStaleSeq` |
| QA-09 | ✅ | `Session.InterruptGracePeriodThenForceStop`、`ConPty.TerminateKillsWholeTree` |
| QA-10 | ✅ | `HostFixture.HostCrashMarksSessionsLost`＋`docs/poc/evidence/qa10-ui-host-crash.json` |
| QA-11 | ✅ | `InputMapping.SelectionTextHandlesWideAndCombining`；UI 中文顯示修正（BUG-10） |
| QA-12 | ✅ | `VtEngine.HistoryIsBoundedBy*` |
| QA-13 | ✅ | `HostFixture.CliContractAndExitCodes` |
| QA-14 | ✅ | `HostFixture.SingleInputController`；UI 唯讀提示條與 Take control |
| QA-15 | ⏸ | 人工：100%／150%／200% DPI、繁中注音輸入。開發機截圖為 150%，版面正常 |
| QA-16 | ✅ | `VtEngineSecurity.Osc52AndHostileOutputProduceNoInput` |
| QA-17 | 🟡 | 從實際 pipe 讀回 DACL，只有 SYSTEM 與目前使用者（`HostFixture.PipeDaclAllowsOnlySystemAndCurrentUser`）；超長 frame 會斷線；以另一個 Windows 帳號實際連線未測 |
| QA-18 | ✅ | `Session.BackpressureNeverDropsOutput`、`HostFixture.SlowClientDoesNotStallHost` |
| QA-19 | ✅ | `HostFixture.ReportStateViaCli`、`Session.ReportStateIgnoresStaleSeq` |
| QA-20 | ✅ | `HostLaunch.HostBreaksAwayFromLauncherJob`、`HostLaunch.WarnsWhenHostCannotLeaveKillOnCloseJob` |
| QA-21 | 🟡 | 回覆順序：`ConPtyCompareTest.*/bundled`；實際 Claude Code 2.1.326 在 AgentDesk 中回報 idle；working／blocked／done 需要送 prompt，等使用者同意後人工驗證 |
| QA-22 | ✅ | `Session.Osc7501StateThenUnknownAfterExitKeepsLastState`、`VtEngine.ShellPromptOsc133ClearsWorking` |

## 安全

| 項目 | 結果 |
|---|---|
| OSC 52 寫入／讀取、OSC 8 javascript 連結、DCS 查詢、看起來像命令的輸出 | 都不產生終端機輸入（QA-16） |
| pipe ACL | 讀回只有 `S-1-5-18` 與目前使用者 SID（QA-17） |
| 診斷 log | 送出的輸入文字不會出現在 `host.log`（`HostFixture.HostLogNeverContainsInput`） |

## 待人工驗收（S5-8）

1. 繁中注音：候選視窗位置、Enter 只送出一次（QA-15）
2. 100%／150%／200% DPI（QA-15）
3. 兩個實際 Agent 同時互動，Claude Code 回報 working、blocked、done（QA-21）
4. Codex hook 實機狀態轉換
5. 8 小時 soak：已於 2026-11-18 通過，見上面「穩定性」
6. 以另一個 Windows 帳號連線被拒（QA-17）
