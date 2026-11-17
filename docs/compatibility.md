# 相容性矩陣

依設計文件 6.3：每次 release 記錄環境、Agent CLI 版本與測試結果。不以 shell 通過取代 Agent 驗收。

## 0.1.0（2026-11-17）

| 項目 | 值 |
|---|---|
| OS | Windows 11，build 10.0.26300 |
| 建置 | MSVC 14.44.35207、Windows SDK 10.0.26100.0、CMake 4.3.1、Ninja 1.13.2 |
| 依賴 | libvterm 0.3.3、Dear ImGui 1.93.0 docking、nlohmann-json 3.12.0、GoogleTest 1.18.0（測試）、Microsoft.Windows.Console.ConPTY 1.24.260710001 |
| GPU／驅動、DPI、IME | 未記錄；DPI 與繁中 IME 待人工驗證 |

| 程式 | 版本 | 啟動方式 | 結果 | 證據 |
|---|---|---|---|---|
| Command Prompt | 系統內建 | 原生 exe | ✅ 輸入、輸出、resize、中斷、停止 | 整合測試 |
| PING.EXE | 系統內建 | 原生 exe | ✅ 寫入 0x03 可中斷（內建與隨附 ConPTY 皆是） | `ConPty.CtrlCByteInterruptsPing` |
| Claude Code | 2.1.326 | 原生 exe | ✅ 顯示、縮放、Ctrl+C 結束；OSC 7501 回報 idle（僅隨附 ConPTY） | `docs/poc/evidence/s1-8-*.json`、`s3-claude-via-host-ui.json` |
| Codex CLI | 0.155.1 | 原生 exe | ✅ 啟動、結束；不送 OSC 7501；hook 腳本以模擬事件驗證 | `docs/poc/evidence/s1-8-codex-bundled.json` |
| PowerShell 7、Windows PowerShell 5.1 | — | 原生 exe | 🟡 profile 已偵測；未跑專屬測試 | — |

| ConPTY | DA1 回覆 | OSC 7501 查詢回覆 | 狀態回報 | 鍵盤位元組 |
|---|---|---|---|---|
| 內建（10.0.26300） | 由 conhost 自行回覆 | ❌ 被丟棄 | ✅ | ✅ |
| 隨附 1.24.260710001 | host 回覆 | ✅ 早於 DA1 | ✅ | ✅ |

## 尚未驗證

- 繁中注音輸入、候選視窗位置、Enter 不重複送出
- 100%／150%／200% DPI
- 兩個實際 Agent 同時互動並各自送出 prompt
- Codex hook 在實際 Codex 工作中的狀態轉換
- 8 小時 soak（已跑 30 分鐘，見 `docs/poc/evidence/m4-soak-30min.json`）
- 以另一個 Windows 帳號連線被拒
