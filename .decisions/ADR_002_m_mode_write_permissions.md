---
adr: 2
title: M 模式下 docs/ 與 TestReport 的寫入權限
status: accepted
date: 2026-11-16
deciders: [Orchestrator]
related_contracts: [PRD@1.1.0]
related_exceptions: []
audit_events: []
supersedes: ~
superseded_by: ~
---

# ADR_002: M 模式下 docs/ 與 TestReport 的寫入權限

## 背景

上層憲法 Section 4 的目錄權限表以 L 模式的八個角色為前提：`docs/` 只有 SA、DBA 可寫，QA 只能寫 `tests/` 與 `exceptions/`。本專案是 M 模式，只啟用 Orchestrator、PM、RD、QA，因此出現兩個缺口：

1. 設計文件 `docs/design/AgentDesk_Design.md` 與 M0 報告 `docs/poc/` 沒有任何已啟用角色可以寫。
2. 憲法 6.1 規定 QA 產出 TestReport、M 模式也要求 TestReport，但 Section 4 沒有授權 QA 寫 `contracts/inbox/`，憲法自身互相矛盾。

一致性審查（2026-11-16）指出 Sprint 1 的 S1-11、S1-12 因此沒有合法的執行者。

## 決策

| 路徑 | 寫入者 | 說明 |
|---|---|---|
| `docs/design/`、`docs/poc/`、`docs/research/` | Orchestrator | 代行 M 模式未啟用的 SA。產品需求的變更仍須由 PM 升版 PRD 契約才生效 |
| `contracts/inbox/TestReport_*.md` | QA | 只限 TestReport；其他契約仍依憲法 Section 4 |
| `contracts/inbox/PRD*.md` | PM | 依憲法 |
| `contracts/approved/`、`contracts/deprecated/` | Orchestrator | 依 `contracts/approved/README` 的規定移動與升版 |

其餘權限照憲法 Section 4。

## 考慮方案

| 方案 | 優點 | 缺點 |
|---|---|---|
| 升為 L 模式以啟用 SA | 完全照憲法 | 使用者已選 M；為了權限表多開四個角色不划算 |
| 讓 RD 寫設計文件 | RD 最熟實作 | 違反「RD 嚴格依契約實作，不改上游」的角色定位 |
| Orchestrator 代行 SA 的文件權限（採用） | 每個路徑只有一個寫入角色，與 Sprint plan 一致 | Orchestrator 同時寫設計與做審核，需靠 PRD 升版流程與使用者把關 |

## 後果與 Trade-off

- Sprint 1 的 S1-11（QA 寫 TestReport）與 S1-12（Orchestrator 寫 M0 報告、升版設計文件）依本表執行。
- 若日後升為 L 模式，本 ADR 改為 `superseded`，回歸憲法權限表。
- 框架層 ADR_004 若被接受，可考慮把「M 模式權限補充」併入憲法，屆時本 ADR 一併 supersede。
