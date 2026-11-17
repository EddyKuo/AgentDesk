---
sprint: 8
start_date: 2026-11-17
complexity: M
execution_mode: standard
estimated_sprints: 9
qa_agent_enabled: true
sprint_phase: S6_deliver
status: done
---

# Sprint 8：遠端機器（SSH）

> 使用者要求：功能與 herdr 一樣好用，外觀不同（設計文件 v2.1、ADR-13）。

| ID | Agent | 產出 | 狀態 |
|----|-------|------|------|
| S8-1 | RD | agentctl bridge、open_raw_pipe、PipeClient::adopt（讀寫分開） | ✅ |
| S8-2 | RD | connect_over_command（ssh -T -o BatchMode=yes … agentctl bridge）、錯誤顯示 ssh 訊息 | ✅ |
| S8-3 | RD | UI：設定 Machines、依機器分組的 space、啟動選單選機器、離線機器保留面板 | ✅ |
| S8-4 | RD | ui-crash.txt 崩潰堆疊 | ✅ |
| S8-5 | QA | 整合測試 ProtocolOverBridgeCommand、RemoteTransport.FailingCommandExplainsWhy；pipe: 目標截圖驗證 | ✅ |
