---
sprint: 7
start_date: 2026-11-17
complexity: M
execution_mode: standard
estimated_sprints: 9
qa_agent_enabled: true
sprint_phase: S6_deliver
status: done
---

# Sprint 7：Agent 互相協作

> 使用者要求：功能與 herdr 一樣好用，外觀不同（設計文件 v2.1、ADR-13）。

| ID | Agent | 產出 | 狀態 |
|----|-------|------|------|
| S7-1 | RD | session parent、labels、task；session.wait／label／report | ✅ |
| S7-2 | RD | agentctl run 自動 parent、--task、wait、report、label、--skill | ✅ |
| S7-3 | RD | UI：子 Agent 縮排、peek、Open as pane、Details 列出 | ✅ |
| S7-4 | QA | 整合測試 SubAgentTaskReportAndWait；截圖驗證 | ✅ |
