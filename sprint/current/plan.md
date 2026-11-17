---
sprint: 1
start_date: 2026-11-17
complexity: M
execution_mode: standard
estimated_sprints: 5
qa_agent_enabled: true
sprint_phase: S_feature
status: active
---

# Sprint 1：M0 技術 POC

> 對應設計文件 5.1 的 M0 與 5.2 的九項必要決策。粗估 8～12 人日。本 Sprint 先驗證終端機核心可行，介面只做到能操作即可；任一關鍵項失敗，先提交替代方案與修正估工（設計文件 5.2）。

## 目標

- 一個真實 Agent（Claude Code）能在 AgentDesk 原型中輸入、縮放、顯示，輸出與停止不死鎖（M0 退出條件）。
- S1-1 凍結 OQ-05（含 JSON 套件）與 OQ-07；M0 結束時凍結 OQ-03、OQ-04、OQ-06，並在 M0 報告提出 M1～M4 的修正估工。
- 以實測證據決定 ADR-08（是否隨附 ConPTY）。

## 開工前提（皆已滿足）

- PRD@1.1.0 approved；設計文件 v1.7
- 開發環境：VS 2022 Build Tools 17.14、CMake 4.3.1、Ninja 1.13.2、`C:\vcpkg`（ADR-11，不另外安裝）
- skill 已就緒（見 CLAUDE.md「對應的 Skill」）

## 任務清單

| ID | Agent | 依賴 | 產出 | Skills | 狀態 |
|-----|-------|------|------|--------|------|
| S1-1 | RD | — | 建置骨架：git repo、CMakePresets（vcvars x64＋Ninja）、vcpkg manifest（imgui docking、gtest）、`third_party/libvterm` 0.3.3 與 notices、空的 gtest 目標可以 `ctest` 通過；凍結 OQ-05（選定 JSON 套件）與 OQ-07，結果寫入 status | rd/lang-cpp、qa/framework-gtest | ✅ |
| S1-2 | RD | — | 凍結 CLI：記錄 Claude Code（需 ≥ v2.1.295）與 Codex CLI 的精確版本、安裝路徑、原生 exe 或 cmd 啟動器、登入方式（5.2 第 1 項） | shared/domain-coding-agents | ✅ |
| S1-3 | RD | S1-1 | `libs/platform/windows` 最小版：ConPTY 建立／讀寫／resize／關閉、Job Object、argv quoting；能跑 pwsh 並正確關閉 | rd/platform-windows-conpty | ✅ |
| S1-4 | RD | S1-1 | `libs/terminal` 最小版：libvterm 封裝、UTF-8 分段、DA／DSR 與 `OSC 7501 ; ?` 回覆、快照；逐位元組切分 fixture 測試通過（5.2 第 2 項）；按鍵編碼檢查（5.2 第 8 項）；字寬與 emoji 限制實測並列表（5.2 第 6 項後半） | rd/engine-terminal-vt、qa/framework-gtest | ✅ |
| S1-5 | RD | S1-3、S1-4 | ConPTY 比較實驗：內建 vs 隨附 Microsoft.Windows.Console.ConPTY，測 DA／DSR 回覆、OSC 7501 查詢與回報能否通過、Ctrl+C、ClosePseudoConsole 行為（5.2 第 7 項，決定 ADR-08） | rd/platform-windows-conpty、rd/engine-terminal-vt | ✅ |
| S1-6 | RD | S1-4 | ImGui 原型：Win32＋DX11＋docking、單一 TerminalView 繪製字元格、游標、縮放換算 cols/rows、閒置不空轉 | rd/frontend-imgui | ⬜ |
| S1-7 | RD | S1-6 | 繁中 IME 與字型：組字、候選視窗定位、Enter 不重複送出；100%／150%／200% DPI（5.2 第 3 項） | rd/frontend-imgui | ⬜ |
| S1-8 | RD | S1-5、S1-6、S1-2 | 端到端：在原型中執行 Claude Code，驗證輸入、縮放、OSC 7501 狀態（5.2 第 4、9 項）；在原型中執行 Codex，記錄其 hook 能回報的轉換 | shared/domain-coding-agents | 🔄 |
| S1-9 | RD | S1-3、S1-4 | 無 UI 量測：合成 1 MiB/s 輸出 60 秒、停止與重新 attach（以測試程式模擬 client）；選定過載政策（5.2 第 5 項、第 6 項前半） | rd/platform-windows-conpty | ⬜ |
| S1-10 | QA | S1-4 | M0 測試集：VT fixtures、OSC 7501 查詢順序（QA-21 的合成部分）、ConPTY 整合測試的資源洩漏檢查 | qa/framework-gtest、qa/strategy-security | ⬜ |
| S1-11 | QA | S1-5～S1-10 | M0 TestReport（`contracts/inbox/TestReport_sprint1.md`，寫入權限見 ADR_002），依 ADR_001 替代門檻判定 | qa/framework-gtest | ⬜ |
| S1-12 | Orchestrator | S1-11 | M0 報告（`docs/poc/M0-report.md`，依 `docs/poc/M0-report-template.md`）：凍結 OQ-03～OQ-07、ADR-08 結論、修正估工；設計文件升版（Orchestrator，ADR_002）；需求受影響時由 PM 升版 PRD | — | ⬜ |

狀態：⬜ 未開始｜🔄 進行中｜⏸ BLOCKED｜✅ 完成

## 並行執行組

| 組別 | 任務 | 前提 |
|------|------|------|
| G1 | S1-1 ∥ S1-2 | 無 |
| G2 | S1-3 ∥ S1-4 | S1-1 完成 |
| G3 | S1-5 ∥ S1-6 ∥ S1-9 ∥ S1-10 | S1-3、S1-4 完成（S1-6、S1-10 只需 S1-4） |
| G4 | S1-7 ∥ S1-8 | 兩者都需 S1-6；S1-8 另需 S1-2、S1-5 |

## 里程碑

| ID | 項目 | 退出條件 | 狀態 |
|----|------|---------|------|
| S1-M1 | 建置可重現 | 乾淨 clone 在 vcvars x64 中 `cmake --preset` → `cmake --build` → `ctest` 全綠，沒有安裝任何新工具 | ✅ |
| S1-M2 | 終端機核心可用 | VT fixture 逐位元組切分全部一致；DA 與 OSC 7501 回覆順序正確 | ✅ |
| S1-M3 | ConPTY 決策 | ADR-08 有實測證據並定案 | ✅ |
| S1-M4 | 真實 Agent 可操作 | Claude Code 在原型中可輸入、縮放、顯示；停止不死鎖 | ⬜ |
| S1-M5 | 繁中可用 | IME 組字與候選視窗在 150% DPI 可用，Enter 不重複送出 | ⬜ |
| S1-M6 | M0 報告 | OQ-03、OQ-04、OQ-06 凍結（OQ-05、OQ-07 已於 S1-1 凍結），修正估工提交使用者 | ⬜ |

## 失敗時的處理

| 情況 | 處理 |
|---|---|
| libvterm 無法通過關鍵 fixture | 記錄缺口；備案 libghostty-vt 需要 Zig，違反 ADR-11，必須先問使用者 |
| 內建與隨附 ConPTY 都擋住 OSC 7501 | Claude Code 改用 hook＋report-state，並在報告列出缺口（中斷後不會觸發 Stop） |
| IME 無法在 ImGui 內正確定位 | 提交替代方案（例如原生 Win32 IME 視窗）與估工，不先擴大 UI |
| Codex 只能在 WSL 運作 | 列為阻擋，調整首發目標，不暗中承諾 WSL 支援（設計文件 6.3） |

## 開放例外

無。
