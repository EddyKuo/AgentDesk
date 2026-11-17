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
| 2026-11-17 | S1-2 | ⬜ | ✅ | 凍結 CLI：Claude Code 2.1.326（`%USERPROFILE%\.local\bin\claude.exe`，原生 exe，≥2.1.295 支援 OSC 7501）；Codex CLI 0.155.1（`%LOCALAPPDATA%\Programs\OpenAI\Codex\bin\codex.exe`，原生 exe）。兩者皆不需 cmd 啟動器。登入由 CLI 自行處理，未檢查帳號內容 |
| 2026-11-17 | S1-8 | ⬜ | 🔄 | 無 UI 端到端（tools/pty_run，只啟動與 Ctrl+C 退出，未送 prompt）：Claude Code 在隨附 ConPTY 下送出 7501 查詢、2.3 秒回報 idle、退出時 clear；內建 ConPTY 下無任何狀態。Codex 不送 7501 查詢，可正常啟動與退出。證據：docs/poc/evidence/s1-8-*.json。UI 內操作待 S1-6；Codex hook 實測需修改使用者 ~/.codex 設定，留待使用者同意 |
| 2026-11-17 | — | — | — | 新增 libs/host Session actor（M1 沿用）：引擎只在 actor 執行緒、快照合併發布、中斷 5 秒寬限後提示強制終止、OSC 7501 與 report-state 兩種來源、seq 去重。測試 75/75 |
| 2026-11-17 | S1-6 | ⬜ | ✅ | agent-ui.exe 原型（Win32＋DX11＋docking、TerminalView、事件驅動迴圈）。閒置 10 秒 CPU 31 ms、工作集 73 MB。Claude Code 在原型內正確繪製並經 osc7501 顯示 Idle（docs/poc/evidence/s1-8-claude-in-ui.json） |
| 2026-11-17 | S1-8 | 🔄 | ✅ | UI 內端到端完成。Codex hook 實測移到 M1（需使用者同意修改 ~/.codex） |
| 2026-11-17 | S1-9 | ⬜ | ✅ | Release 量測（docs/poc/evidence/s1-9-session-bench.json）：1 MiB/s×60 s 共 982,800 行全數依序到達、歷史 0 缺口、實測 0.99 MiB/s、峰值工作集 42 MB；100 次建立／停止 handle 88、執行緒 4 皆不成長；120×40 快照 277 µs。**凍結 OQ-06：背壓**（佇列超過 8 MiB 時 reader 暫停讀取，不丟位元組；4 KiB 上限測試確認觸發且無缺漏） |
| 2026-11-17 | — | — | — | 里程碑 S1-M4（真實 Agent 可操作）達成：Claude Code 可在原型內顯示、縮放、以 Ctrl+C 停止，無死結 |
| 2026-11-17 | — | — | — | 提前開始 M1：libs/protocol（分幀、訊息、錯誤碼、畫面編碼）＋6 個單元測試。測試 88/88 |
| 2026-11-17 | S1-10 | ⬜ | ✅ | M0 測試集：VT fixtures（逐位元組＋隨機切分）、OSC 7501 查詢順序、ConPTY 洩漏、背壓、QA-07 靜默測試。共 89 個 |
| 2026-11-17 | S1-11 | ⬜ | ✅ | `contracts/inbox/TestReport_sprint1.md`：89/89，互動延遲 p95 16.1 ms |
| 2026-11-17 | S1-12 | ⬜ | ✅ | `docs/poc/M0-report.md`；設計文件 v1.8（ADR-08 定案、OQ-03～OQ-07 凍結、背壓、libvterm 限制、修正估工）；CLAUDE.md 改為反映實際程式 |
| 2026-11-17 | S1-7 | ⬜ | ⏸ | 程式端已完成（PlatformImeData、字元佇列、CJK 字型），但注音組字、候選窗位置與 DPI 只能人工驗證，轉入 Sprint 2 的 S2-9，等使用者操作 |
| 2026-11-17 | — | — | — | 里程碑：S1-M1～S1-M4、S1-M6 達成；S1-M5（繁中可用）待人工驗證 |

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

- [x] 所有任務為 ✅ 或已轉入下個 Sprint（附理由：S1-7 需人工驗證，轉入 S2-9）
- [x] 無未解決的 P0/P1 例外
- [x] 升級偵測已執行（CLAUDE.md Section 7.1），結果：維持 M（仍為單機程序間 Named Pipe，無 gRPC／MQ，compliance none）
- [x] plan.md、status.md 已移至 `sprint/archive/sprint-1/`
