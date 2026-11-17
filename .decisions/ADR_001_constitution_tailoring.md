---
adr: 1
title: 上層團隊憲法在桌面原生專案的適用範圍與替代門檻
status: accepted
date: 2026-11-16
deciders: [Orchestrator]   # 替代門檻由 Orchestrator 依 M 模式提出；背景中的專案設定由使用者確認，使用者可要求改版
related_contracts: [PRD@1.0.0, PRD@1.1.0]   # 決策時為 1.0.0；PRD 已升 1.1.0，本 ADR 內容不受影響
related_exceptions: []
audit_events: []
supersedes: ~
superseded_by: ~
---

# ADR_001: 上層團隊憲法在桌面原生專案的適用範圍與替代門檻

## 背景

上層憲法（`..\.claude\CLAUDE.md` v2.4.0）的品質門檻與安全基準以 Web／API 服務為前提：JWT、CORS、Rate Limiting、HTTP 回應格式、API p95、TypeScript 式的「禁用 any」。AgentDesk 是 Windows 原生 C++ 桌面程式，三個執行檔之間只走本機 Named Pipe，沒有 HTTP 端點。照字面套用會出現無法滿足或沒有意義的門檻。

使用者已確認：`project_complexity: M`、`execution_mode: standard`、名稱 AgentDesk、MIT License、首發支援 Claude Code 與 Codex CLI，且只用已安裝的 VS 2022 C++ 工具鏈。

## 決策

憲法其餘條文照常適用（絕對法則、契約系統、Skill 載入協定、例外處理、Git 規範）。下列條目以替代門檻取代：

| 憲法條目 | 本專案處理 | 替代內容 |
|---|---|---|
| 7 `project.type` 列舉值 | 暫填 `desktop_app` | 框架層 ADR_004（proposed）提議加入列舉值 |
| 10 型別標注 100%、禁用 any | 不適用（C++ 為靜態型別） | MSVC `/W4 /permissive- /WX` 零警告；禁止 C 式轉型與 `reinterpret_cast`（平台邊界的 RAII 封裝除外，需註解理由） |
| 10 Linting 零 error | 適用 | VS 內附 clang-tidy、clang-format，規則檔在 repo 根目錄。`rd/lang-cpp` 另列的 cppcheck 未安裝，依 ADR-11 不安裝，改由 MSVC `/analyze` 補足靜態分析 |
| 10 Unit Test Coverage ≥ 80% | 部分適用 | `libs/protocol`、`libs/terminal`、`libs/host` 以 80% 為目標；覆蓋率工具依 `qa/framework-gtest` 尚未選定，選定前 TestReport 註明「未量測（工具未選定）」 |
| 10 Secret 掃描 | 適用 | 提交前檢查無 token、金鑰；診斷 log 不記錄輸入文字 |
| 10 API p95 | 不適用 | 改用設計文件 1.5 的互動延遲、續接、記憶體、穩定性目標 |
| 11 JWT、Refresh Token、CORS、Rate Limiting | 不適用（無網路端點） | 改用設計文件 3.7：Named Pipe DACL 限目前使用者、拒絕遠端、驗證 client 身分、frame 上限 4 MiB |
| 11 輸入驗證 | 適用 | 所有 IPC frame 先驗長度、UTF-8、JSON、版本；終端輸出中的控制序列有長度上限並安全忽略 |
| 11 SQL、PII 加密儲存 | 不適用（無資料庫、不儲存個資） | 不保存 Agent 登入資訊；transcript 落地為選配且需告知 |
| 12 API 統一回應格式 | 不適用 | 改用設計文件 3.7 的 IPC 格式與 `error.code`／`message`／`retryable` |
| 6 契約清單 | 依 M 模式 | 只產出 PRD（`contracts/approved/PRD.md`）與每個 Sprint 的 TestReport；架構與協定以設計文件為單一真相來源 |

產品與技術層級的決策（ADR-01～ADR-11）記錄在設計文件 7.1；本目錄只放治理與流程決策，兩者編號格式不同（`ADR-NN` 與 `ADR_NNN`）以免混淆。

## 考慮方案

| 方案 | 優點 | 缺點 |
|---|---|---|
| 照字面套用憲法 | 不需例外 | JWT、CORS 等無從實作，TestReport 永遠無法判定通過 |
| 改專案為 L 模式補齊全部契約 | 流程完整 | 單人專案負擔過重；使用者已選 M |
| 本 ADR 的替代門檻（採用） | 門檻可量測、與設計文件一致 | 需維護一份對照表；憲法改版時要重審 |

## 後果與 Trade-off

- 每個 Sprint 的 TestReport 依本表的替代門檻判定通過與否。
- 憲法升版時，Orchestrator 需重審本表；框架層 ADR_004 若被接受，`project.type` 改為正式列舉值。
- 覆蓋率在工具選定前無法量測，屬已知缺口，記在 Sprint status。
