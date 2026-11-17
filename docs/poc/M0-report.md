# M0 技術 POC 報告

日期：2026-11-17｜分支：`feature/sprint-1/s1-1-build-skeleton`｜依 `docs/poc/M0-report-template.md` 填寫。證據檔在 `docs/poc/evidence/`，測試結果在 `contracts/inbox/TestReport_sprint1.md`。

## 1. 環境

| 項目 | 值 |
|---|---|
| OS build | Windows 10.0.26300（Windows 11） |
| CPU／GPU／驅動 | 未記錄（量測未依賴 GPU） |
| DPI 設定 | 未手動測試（見第 3 節第 3 項） |
| IME | 未手動測試 |
| MSVC／Windows SDK | 14.44.35207／10.0.26100.0 |
| CMake／Ninja | 4.3.1／1.13.2 |
| AgentDesk commit | `feature/sprint-1/s1-1-build-skeleton` 最新 |
| 字型與授權 | 執行期讀取 `%WINDIR%\Fonts` 的 Cascadia Mono（或 Consolas）＋微軟正黑體，不隨附 |

## 2. 凍結的 Agent CLI（S1-2，OQ-02）

| CLI | 版本 | 安裝路徑 | 啟動方式 | 登入方式 | 備註 |
|---|---|---|---|---|---|
| Claude Code | 2.1.326 | `%USERPROFILE%\.local\bin\claude.exe` | 原生 exe | CLI 自行處理 | ≥ 2.1.295，支援 OSC 7501 |
| Codex CLI | 0.155.1 | `%LOCALAPPDATA%\Programs\OpenAI\Codex\bin\codex.exe` | 原生 exe | CLI 自行處理 | 不送 OSC 7501 查詢 |

兩者都不需要 `.cmd` 啟動器 adapter。

## 3. 必要決策的結果（設計文件 5.2；第 1 項見第 2 節）

| # | 項目 | 結果 | 證據 | 後續 |
|---|---|---|---|---|
| 2 | libvterm 以 MSVC 建置並通過 VT fixture | 通過 | 官方 tarball（SHA-256 已記錄）以 CMake＋MSVC 建置；3 個 fixture 逐位元組切分全數一致 | 發現 libvterm 0.3.3 的 UTF-8 切分 bug，已在 VtEngine 繞過 |
| 3 | 繁中 IME：組字、候選視窗、Enter 不重複 | **未驗證** | 程式已依 skill 設定 `PlatformImeData`、只從字元佇列取文字 | 需要使用者手動測試（第 10 節） |
| 4 | OSC 7501 端到端 | 通過 | Claude Code 2.1.326 在隨附 ConPTY 下 2.3 秒回報 idle、退出時 clear；UI 原型內顯示 Idle（`s1-8-*.json`） | — |
| 5 | 無 UI 的輸出消費、停止、重新 attach | 部分 | 消費與停止通過（`s1-9-session-bench.json`）；重新 attach 需 M1 host | M1 |
| 6 | 過載政策、字寬與 emoji 限制 | 通過 | 背壓（OQ-06）；字寬見第 8 節 | — |
| 7 | 內建 vs 隨附 ConPTY | 通過 | 第 4 節 | ADR-08 定案 |
| 8 | libvterm 按鍵編碼 | 通過 | 方向鍵依 DECCKM 切換、Ctrl+C=0x03、Enter=CR、bracketed paste（`VtEngine.*` 測試） | kitty keyboard protocol 不支援，列入限制 |
| 9 | Claude Code／Codex 狀態轉換涵蓋度 | 部分 | Claude Code：idle、clear 已實測；Codex：無 OSC 7501 | 送 prompt 的轉換與 Codex hook 需使用者同意後實測 |

## 4. ConPTY 比較（S1-5，ADR-08）

| 測項 | 內建 ConPTY | 隨附 ConPTY 1.24.260710001 |
|---|---|---|
| DA1 回覆到達程式 | 是，但由 conhost 自行回覆 `ESC[?61;6;7;21;22;23;24;28;32;42c` | 是，為 host 的回覆 |
| `OSC 7501 ; ?` 從程式到 host | 是 | 是 |
| 7501 回覆從 host 到程式，且早於 DA1 | **否，被丟掉** | 是 |
| 7501 狀態回報到 host | 是 | 是 |
| 寫入 0x03 中斷 PING | 是 | 是（整合測試在兩者下皆通過） |
| ClosePseudoConsole 在持續 drain 下不阻塞 | 是 | 是 |

結論：**ADR-08 定案，隨附 ConPTY**。內建版本會讓 Claude Code 判定終端機不支援狀態回報（實測：內建下 0 筆狀態）。內建行為保留為特性描述測試，Windows 改變時會提醒重新評估。

## 5. 狀態轉換涵蓋度（S1-8）

| 轉換 | Claude Code（OSC 7501） | Codex（hook） |
|---|---|---|
| 送出 prompt → working | 未測（需送 prompt） | 未測 |
| 等待權限 → blocked(permission) | 未測 | 未測 |
| 提問 → blocked(question) | 未測 | 未測 |
| 完成 → done／idle | 啟動後 idle：已測 | 未測 |
| 使用者中斷 | 未測 | 未測 |
| 拒絕權限後 | 未測 | 未測 |
| 錯誤 | 未測 | 未測 |
| 程序結束 | clear：已測；host 依規格轉 Unknown 並保留 last_agent_state | 不送狀態，維持 Unknown |

送 prompt 會消耗使用者的模型用量，Codex hook 需要修改 `~/.codex/hooks.json`，兩者都等使用者同意。

## 6. 量測（S1-9，Release）

| 項目 | 目標 | 實測 |
|---|---|---|
| 1 MiB/s × 60 秒，host 持續消費 | 不中斷、不丟 VT bytes | 982,800 行全數依序到達，0.99 MiB/s，歷史 0 缺口 |
| 停止（interrupt → force） | 5 秒內提示 | 寬限期到即標記 `still_running_after_interrupt`，force 後整個 Job 清空 |
| 互動延遲（host 端） | p95 < 100 ms | p50 15.5 ms、p95 16.1 ms、p99 16.6 ms |
| 重新 attach 至可操作 | p95 < 2 秒（M3 驗收） | 需 M1 host；快照建立 120×40 約 0.25 ms |
| 100 次建立／停止的 handle、thread 趨勢 | 不持續增長 | handle 88、thread 4，全程不變 |
| 記憶體 | host + UI < 500 MB | bench 峰值工作集 42 MB；UI 原型 73 MB |

選定的過載政策：**背壓**。佇列超過 8 MiB 時 reader 暫停讀取 PTY，子程序的寫入隨之阻塞，不丟任何位元組；不需要磁碟暫存與清理。4 KiB 上限的測試確認背壓確實觸發且歷史無缺漏。

## 7. 凍結的待決事項

| ID | 結論 | 依據 |
|---|---|---|
| OQ-03 VT 引擎 | libvterm 0.3.3 | 通過 fixture 與按鍵編碼；需搭配 UTF-8 切分繞過 |
| OQ-04 是否隨附 ConPTY | 隨附 1.24.260710001 | 第 4 節 |
| OQ-05 測試框架與 JSON 套件 | GoogleTest 1.18.0、nlohmann-json 3.12.0（S1-1 凍結） | vcpkg baseline `afa249f` |
| OQ-06 過載政策 | 背壓 | 第 6 節 |
| OQ-07 第三方套件取得方式 | vcpkg manifest；libvterm 原始碼收錄（S1-1 凍結） | vcpkg 無 libvterm port |

## 8. 已知限制與阻擋

- libvterm 0.3.3：寬度表把 Ambiguous 一律當 1 欄；ZWJ emoji 序列不合併（例如家庭 emoji 佔 4 欄）；不支援 kitty keyboard protocol。
- libvterm 不對外通知 RIS，因此 OSC 7501 規格「RIS 清除全部紀錄」目前做不到；紀錄仍會在程序結束、OSC 133 A 與 `clear` 時清除。
- 從 Claude Code 內啟動 AgentDesk 時，子程序會繼承 `CLAUDE_CODE_*` 環境變數（實測 Claude Code 顯示「Transcript saving is off」）。一般使用者從桌面啟動不受影響；是否由 host 清除這類變數，列為 M1 待決。
- UI 原型的捲動歷史、line_id 選取、多 session 同時輸入的正式驗收在 M2。

## 9. 修正估工

M0 原估 8–12 人日。M1 的核心元件（Session actor、背壓、協定分幀與訊息）已在 M0 一併完成並有測試，以下為依此調整的建議：

| 里程碑 | 原估（人日） | 修正（人日） | 理由 |
|---|---|---|---|
| M1 Host 基礎 | 8–12 | 6–9 | Session、協定層已完成；剩 pipe server、單一實例、agentctl、持久化 |
| M2 桌面整合 | 10–15 | 10–15 | 不變；TerminalView 已有雛形，但 IPC 化、捲動、控制權與 IME 驗收仍完整 |
| M3 可靠性 | 8–12 | 8–12 | 不變 |
| M4 驗收發布 | 8–12 | 8–12 | 不變；8 小時 soak、DPI 與 IME 矩陣需人工 |

## 10. 需要使用者決定或執行的事項

1. **繁中 IME 與 DPI 手動驗證（QA-15、S1-7）**：開啟 `build\msvc-x64-debug\apps\desktop\agent-ui.exe`，新增 PowerShell session，用注音輸入「測試中文」，確認候選視窗貼在游標、Enter 只送出一次；在 100%／150%／200% 縮放各看一次。
2. **是否允許送 prompt 實測狀態轉換**：會消耗 Claude Code 與 Codex 的用量。
3. **是否允許安裝 Codex hook**：修改 `~/.codex/hooks.json` 以呼叫 `agentctl report-state`（M1 提供）。
4. **框架 ADR_004**：是否核准修改上層憲法。
