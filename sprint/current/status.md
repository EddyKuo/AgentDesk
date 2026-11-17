---
sprint: 1
updated_at: 2026-11-17T00:00:00+08:00
plan: sprint/current/plan.md
---

# Sprint 1：狀態

> 由 Orchestrator 維護，只追加不改寫歷史列。

## 任務狀態快照

| 時間 | 任務 ID | 舊狀態 | 新狀態 | 說明 |
|------|--------|--------|--------|------|
| 2026-11-16 | S1-1～S1-12 | — | ⬜ | 計畫建立，等使用者下令開工 |
| 2026-11-16 | — | — | — | 一致性審查後修正 plan：依賴、G4、里程碑改名 S1-M1～M6、OQ-05／OQ-07 提前到 S1-1 凍結 |
| 2026-11-17 | S1-1 | ⬜ | ✅ | 使用者下令開工。CMake＋Ninja＋MSVC 14.44＋vcpkg（baseline afa249f，triplet x64-windows-static-md）；libvterm 0.3.3 收錄於 third_party（官方 tarball 已含 .inc，不需 perl）。冒煙測試 3/3 通過。**凍結 OQ-05**：GoogleTest 1.18.0＋nlohmann-json 3.12.0；**凍結 OQ-07**：vcpkg manifest，libvterm 原始碼收錄 |
| 2026-11-17 | — | — | — | 里程碑 S1-M1（建置可重現）達成 |
| 2026-11-17 | S1-3 | ⬜ | ✅ | libs/platform/windows：PtyProcess（ConPTY＋CREATE_SUSPENDED＋KILL_ON_JOB_CLOSE Job，無 breakaway）、argv quoting、批次檔拒絕。11 個 Windows 整合測試通過；實測寫入 0x03 可中斷 PING（skill 中「待實機驗證」項已驗證） |
| 2026-11-17 | S1-4 | ⬜ | ✅ | libs/terminal：VtEngine（libvterm）、OSC 7501 rev 0.4 解析與紀錄、歷史上限、bracketed paste。發現並繞過 libvterm 0.3.3 bug：UTF-8 被切在兩次寫入之間且後接控制字元時產生 U+FFFD，改為暫存尾端不完整序列（不丟資料）。逐位元組切分測試通過 |
| 2026-11-17 | S1-5 | ⬜ | ✅ | 內建 vs 隨附 ConPTY 實測（Windows 10.0.26300）：內建版本自行回 DA1 並丟掉 OSC 7501 查詢回覆；隨附 1.24.260710001 正確轉送且順序在 DA1 之前。兩者都轉送 7501 狀態回報與鍵盤位元組。**ADR-08 定案：隨附 ConPTY；凍結 OQ-04** |
| 2026-11-17 | — | — | — | 里程碑 S1-M3（ConPTY 決策）達成；S1-M2 中「DA 與 OSC 7501 回覆順序正確」已由單元與整合測試涵蓋 |

## 契約狀態

| 契約 | 版本 | 狀態 | 審核時間 | 下游通知 |
|------|------|------|---------|---------|
| PRD | 1.0.0 | approved | 2026-11-16 | RD、QA |
| PRD | 1.1.0 | approved（1.0.0 移至 deprecated） | 2026-11-16 | RD、QA：Minor，需 review 2.4、2.7 |

## 例外摘要

無。

## Skill 衝突紀錄

沿用 Sprint 0 紀錄（cppcheck、覆蓋率工具）。

| 時間 | Skill A@版本 | Skill B@版本 | 衝突點 | 採用 |
|------|-------------|-------------|-------|------|
| 2026-11-17 | rd/platform-windows-conpty@1.0.0 | 設計文件 v1.7 §3.2 | skill 範例的 Job 設 `JOB_OBJECT_LIMIT_BREAKAWAY_OK`；設計文件規定不允許刻意 breakaway | 依設計文件，不設此旗標 |

## 估算調整

| 時間 | 欄位 | 舊值 | 新值 | 理由 |
|------|------|------|------|------|

## Sprint 結束檢查

- [ ] 所有任務為 ✅ 或已轉入下個 Sprint（附理由）
- [ ] 無未解決的 P0/P1 例外
- [ ] 升級偵測已執行（CLAUDE.md Section 7.1），結果：維持 / 建議升級至 __
- [ ] plan.md、status.md 已移至 `sprint/archive/sprint-1/`
