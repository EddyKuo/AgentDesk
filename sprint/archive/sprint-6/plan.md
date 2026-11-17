---
sprint: 6
start_date: 2026-11-17
complexity: M
execution_mode: standard
estimated_sprints: 9
qa_agent_enabled: true
sprint_phase: S6_deliver
status: done
---

# Sprint 6：herdr 功能對齊：核心

> 使用者要求：功能與 herdr 一樣好用，外觀不同（設計文件 v2.1、ADR-13）。

| ID | Agent | 產出 | 狀態 |
|----|-------|------|------|
| S6-1 | RD | space／分頁／分割樹模型與單元測試（libs/ui/workspace） | ✅ |
| S6-2 | RD | host 保存 UI 文件（ui.state.get/set、ui-state.json、watch 事件） | ✅ |
| S6-3 | RD | 注意力（done＝沒看時完成）、通知音效、角落通知、工作列閃爍 | ✅ |
| S6-4 | RD | keymap 與 keys.json、命令面板、快捷鍵說明、設定（主題、縮放、字級、提醒） | ✅ |
| S6-5 | RD | 終端機搜尋、完整捲動與捲軸、雙擊／三擊選取、放開即複製、右鍵選單 | ✅ |
| S6-6 | RD | 啟動選單、程式偵測、啟動失敗提示、git 分支、worktree.add | ✅ |
| S6-7 | QA | 整合測試 UiStateIsSharedAndPersisted、ForegroundProgramFollowsChild、WorktreeAddCreatesCheckout；截圖驗證 | ✅ |
