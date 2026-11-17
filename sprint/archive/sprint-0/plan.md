---
sprint: 0
start_date: 2026-11-16
complexity: M
execution_mode: standard
estimated_sprints: 5        # M0～M4 各一個 Sprint（設計文件 5.1）
qa_agent_enabled: true
sprint_phase: S0_init
status: done
---

# Sprint 0：專案初始化

## 目標
完成技術棧與複雜度確認、補齊缺少的 skill、建立契約與決策紀錄，讓 Sprint 1（M0 技術 POC）可以直接開始寫程式。

## 技術棧確認清單

- [x] `project.name`：AgentDesk（使用者決定）
- [x] `project.type`：`desktop_app`（憲法列舉值沒有，見 ADR_001 與框架 ADR_004 draft）
- [x] `project.description`：Windows 原生 AI coding agent 終端機工作台
- [x] `language.backend`／`language.frontend`：C++20
- [x] `framework.backend`：Win32＋ConPTY；`framework.frontend`：Dear ImGui 1.93.0 docking＋DirectX 11
- [x] `infrastructure.database_primary`：none（JSON 設定檔）
- [x] `infrastructure.database_cache`：none
- [x] `infrastructure.message_queue`：none（本機 Named Pipe）
- [x] `infrastructure.container`：none
- [x] `infrastructure.cloud_provider`：none
- [x] `deployment.target_env`：Windows 11 x64，portable zip
- [x] `deployment.ci_cd`：none（MVP 只在開發機建置；之後另議）
- [x] `non_functional_requirements`：設計文件 1.5；compliance none
- [x] `project_complexity`：M（使用者確認）
- [x] `execution_mode.base`：standard
- [x] `estimated_sprints`：5

## 執行模式確認

- [x] `execution_mode.base = standard`，不需要 harness 產物

## Skill 掃描清單

- [x] `skills/shared/domain-*`：無既有匹配，已建立 `shared/domain-coding-agents`
- [x] `skills/shared/compliance-*`：compliance none，不載入
- [x] 各角色技術 skill：沿用 `rd/lang-cpp`、`rd/lang-c`；已建立 `rd/platform-windows-conpty`、`rd/engine-terminal-vt`、`rd/frontend-imgui`、`qa/framework-gtest`
- [x] 缺少的 skill 已由 Skill Manager 建立（2026-11-16）

## 角色啟動清單（M 模式）

- [x] Orchestrator
- [x] PM：載入 `shared/domain-coding-agents`，產出 `contracts/approved/PRD.md`
- [x] RD：Sprint 1 載入 `rd/lang-cpp`、`rd/lang-c`、`rd/platform-windows-conpty`、`rd/engine-terminal-vt`、`rd/frontend-imgui`
- [x] QA：Sprint 1 載入 `qa/framework-gtest`、`qa/strategy-security`、`qa/strategy-chaos-engineering`

## 里程碑

| ID | 項目 | 狀態 |
|----|------|------|
| S0-1 | Section 7 填寫完成（CLAUDE.md「與上層團隊憲法的關係」） | ✅ |
| S0-2 | 複雜度確認：M | ✅ |
| S0-3 | Skill 掃描完成，缺少的 5 個已建立 | ✅ |
| S0-4 | 角色啟動完成 | ✅ |
| S0-5 | 執行模式確認：standard | ✅ |
| S0-6 | PRD 契約 approved（v1.0.0） | ✅ |
| S0-7 | 憲法適用範圍 ADR_001 accepted | ✅ |
| S0-8 | Sprint 1 計畫完成 | ✅ |

## 開放例外

無。
