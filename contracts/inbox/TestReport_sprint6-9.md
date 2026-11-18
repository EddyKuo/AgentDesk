---
contract_type: TestReport
version: 1.0.0
status: draft
author_agent: QA
sprint: 9
created_at: 2026-11-17T00:00:00+08:00
approved_at: ~
skills_used:
  - qa/framework-gtest@1.0.0
upstream_deps:
  - PRD@1.2.0
  - docs/design/AgentDesk_Design.md@2.1
downstream_consumers:
  - Orchestrator
change_summary: |
  Sprint 6～9（herdr 功能對齊）測試報告。判定依 .decisions/ADR_001 替代門檻。
---

# TestReport：Sprint 6～9（herdr 功能對齊）

## 執行摘要

| 指標 | 數值 |
|---|---|
| 測試總數／通過／失敗 | 145／145／0（unit 86、windows_integration 59） |
| UI 端到端腳本 | QA-05、QA-10 通過 |
| 編譯警告 | 0（`/W4 /WX`） |
| **整體結論** | ✅ FR-20～FR-27 有自動化證據；FR-26 的實際 ssh 連線待人工 |

## 新增測試

| 需求 | 測試 |
|---|---|
| FR-20 版面 | `Workspace.*`（10 項：分割、方向焦點、移除收合、清理、交換、調整、放大、JSON、命名、邊界）、`HostFixture.UiStateIsSharedAndPersisted` |
| FR-21 注意力 | `Attention.DoneOnlyWhenFinishedUnseenAndClearsWhenSeen`、`Attention.BlockedNotifiesOnceAndRanksFirst` |
| FR-22 快捷鍵 | `Keymap.ParsesChordsAndAppliesOverrides` |
| 程式偵測 | `HostFixture.ForegroundProgramFollowsChild` |
| FR-24 worktree | `HostFixture.WorktreeAddCreatesCheckout` |
| FR-25 子 Agent | `HostFixture.SubAgentTaskReportAndWait`（task 在就緒後送出、回報帶來源、父 Agent 忙碌時排隊、wait 成功與逾時、無 parent 時拒絕） |
| FR-26 遠端 | `HostFixture.ProtocolOverBridgeCommand`（完整協定走 agentctl bridge 的 stdin/stdout）、`RemoteTransport.FailingCommandExplainsWhy` |
| FR-27 mod | `ModsTest.*`（6 項：只執行啟用的 mod、沙盒、事件與效果、面板與點擊、裝飾、失控迴圈與錯誤） |

## 截圖驗證（`build\<preset>\tools\qa\screenshot_ui.exe`）

分割面板（往右、往下）、命令面板、快捷鍵說明、啟動選單、Ember 主題、子 Agent 縮排與 Details、遠端機器分組（`pipe:` 目標）、board 版面、grid 版面。截圖過程中發現並修正：

| 問題 | 原因 | 處理 |
|---|---|---|
| UI 建立的新面板一出現就消失 | create 的回覆早於 session 清單更新，被當成已消失而清掉 | 新建的面板等到 host 列出 session 為止（最多 15 秒） |
| 窄面板的標題列按鈕重疊 | `CalcTextSize` 預設把 `##id` 也算進寬度 | 改用隱藏 `##` 的寬度，窄面板依序隱藏次要按鈕 |
| 透過 bridge 連線沒有回應 | 子程序的 stdin 與 stdout 共用一個同步 handle，讀取時寫入被卡住 | 改為兩條 pipe（與 ssh 相同） |

## 待人工驗收

1. 實際 ssh 連到另一台安裝 AgentDesk 的 Windows（FR-26）
2. 實際 Claude Code 與 Codex 搭配子 Agent 流程（需要送 prompt）
3. ntfy-notify 推播到手機（需要使用者的 topic）
4. 先前的人工項目（繁中注音、DPI、8 小時 soak 結果、另一個帳號連線被拒）
