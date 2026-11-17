---
sprint: 4
start_date: 2026-11-17
complexity: M
execution_mode: standard
estimated_sprints: 5
qa_agent_enabled: true
sprint_phase: S_feature
status: done
---

# Sprint 4：M3 可靠性

> 對應設計文件 5.1 的 M3（8–12 人日）。attach 快照、慢 client 預算、設定原子寫入、重連已在 M1／M2 實作；本 Sprint 補故障處理與量測證據。

## 任務清單

| ID | Agent | 依賴 | 產出 | Skills | 狀態 |
|-----|-------|------|------|--------|------|
| S4-1 | QA | — | QA-04：快速 resize 100 次後 host、engine、ConPTY（子程序實際看到的）尺寸一致 | qa/framework-gtest | ✅ |
| S4-2 | QA | — | QA-10（UI 端）：UI 開著時 host 被終止 → Disconnected、禁止輸入；新 host → Lost、不重送 | qa/framework-gtest | ✅ |
| S4-3 | QA | — | QA-18（IPC 端）：不讀資料的 client 不拖慢 host 消費、不影響其他 client，host 記憶體有界 | qa/strategy-chaos-engineering | ✅ |
| S4-4 | RD | — | 設定檔損毀：有備份用備份；無備份時保留損毀檔（`.corrupt-<時間>`）並以預設值啟動，host 不得因此無法啟動 | rd/lang-cpp | ✅ |
| S4-5 | QA | — | 子程序崩潰（非 0 結束碼、例外結束）→ Exited 與結束碼正確 | qa/framework-gtest | ✅ |
| S4-6 | QA | — | UI 續接量測：4 個 120×40 session，attach 到完整快照 p95（目標 < 2 秒） | qa/framework-gtest | ✅ |
| S4-7 | QA | — | 寫入失敗（sessions.json 無法寫入）時 host 持續運作 | qa/strategy-chaos-engineering | ✅ |
| S4-8 | QA | S4-1～S4-7 | `contracts/inbox/TestReport_sprint4.md` | qa/framework-gtest | ✅ |
| S4-9 | 使用者 | — | 自 S3-10 轉入：繁中 IME、DPI 手動驗證 | rd/frontend-imgui | ⏸ |

## 里程碑

| ID | 項目 | 退出條件 | 狀態 |
|----|------|---------|------|
| S4-M1 | 故障有明確狀態 | host 崩潰、子程序崩潰、設定損毀、寫入失敗都有可見狀態且不卡死 | ✅ |
| S4-M2 | 續接效能 | 4 session 續接 p95 < 2 秒 | ✅ |

## 開放例外

無。
