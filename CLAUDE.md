# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 專案現況

AgentDesk 是 Windows 11 x64 的原生 AI Agent 工作台：在可分割的終端機面板裡跑多個 coding agent CLI（首發 Claude Code 與 Codex CLI），關掉視窗後工作繼續跑，重開時接回原畫面。介面與行為依 herdr 桌面版（https://github.com/herdrdev/herdr-app）的原始碼複刻（ADR-15），用 C++ 重寫，不追求協定相容。

MVP（M0～M4，Sprint 1～5）已完成：結論在 `docs/poc/M0-report.md`、`docs/poc/M4-report.md`，最終驗收矩陣在 `contracts/inbox/TestReport_sprint5.md`。Sprint 6～9 依使用者要求補齊 herdr 的主要功能，外觀自訂（設計文件 5.5、ADR-13、ADR-14）：space／分頁／分割、注意力與通知、命令面板與 keys.json、子 Agent 協作、遠端機器（ssh＋`agentctl bridge`）、版面與 mod（C++ DLL，ADR-17）。Sprint 10 依使用者要求依 herdr-app 原始碼完整複刻介面與行為（設計文件 5.6、ADR-15）：有疑問就查 herdr 原始碼，逐項規格在 `docs/research/herdr-app/`。剩下的是人工驗收項目（設計文件 5.4、5.5）。之後的工作依 `sprint/current/plan.md` 的任務順序進行，每完成一項就在 `sprint/current/status.md` 追加一列（只追加不改寫）。需求不清楚時列進設計文件 7.4，不要自己補。

## 常用指令

建置都要在 VS 2022 x64 vcvars 環境裡跑；`scripts\build.cmd` 會自己進入 vcvars、設 `VCPKG_ROOT` 與英文編譯訊息（`VSLANG=1033`）。建置前它會先結束從 `build\<preset>\` 啟動的 agent-ui 與 agent-host（它們鎖住執行檔與 `conpty.dll`；host 裡的終端機會一起結束），其他位置的 AgentDesk 不受影響；設 `AGENTDESK_KEEP_HOST=1` 可略過。

```bat
scripts\build.cmd msvc-x64-debug all          :: configure + build + ctest
scripts\build.cmd msvc-x64-debug build        :: 只建置（preset 已 configure 過）
scripts\build.cmd msvc-x64-release configure  :: 新 preset 第一次要先 configure
build\msvc-x64-debug\tools\fetch_conpty.exe   :: 取得隨附 ConPTY（新 clone 必做：先建置一次產生這個工具，執行後再重新 configure）
```

單一測試（先跑過一次建置）：

```bat
build\msvc-x64-debug\tests\unit_tests.exe --gtest_filter=VtEngine.Utf8SplitBeforeControlCharsIsReassembled
build\msvc-x64-debug\tests\win_integration_tests.exe --gtest_filter=Session.*
ctest --test-dir build\msvc-x64-debug -L unit          :: 依 label：unit、windows_integration
```

格式與靜態分析用 VS 內附的 LLVM（`VC\Tools\Llvm\x64\bin`）：`clang-format -i <檔案>`、`clang-tidy -p build\msvc-x64-debug <檔案>`。規則在根目錄 `.clang-format`、`.clang-tidy`（函式 snake_case、私有成員尾底線、常數 `k` 前綴）。編譯一律 `/W4 /permissive- /WX`；`cmake -DAGENTDESK_ANALYZE=ON` 加上 `/analyze`（取代未安裝的 cppcheck，`.decisions/ADR_001`）。

開發工具（在 `build\<preset>\tools\`）：
- `pty_run.exe [--send-after MS TEXT]... -- <程式>`：無 UI 在隨附 ConPTY 下跑程式，輸出畫面與 OSC 7501 時間線的 JSON
- `session_bench.exe --probe <vt_probe.exe>`：吞吐、延遲、生命週期洩漏量測（Release 才有意義）
- `vt_probe.exe query|report|echo|flood|counter|size|crash`：測試用的假 Agent
- `soak.exe --probe <vt_probe.exe> --host <agent-host.exe> [--minutes N]`：host 長時間穩定性（handle、執行緒、記憶體趨勢）
- `apps\desktop\agent-ui.exe [--pipe NAME] [--start "<profile>"] [--exit-after-ms N] [--report file.json]`：UI；參數供自動化驗證
- `qa\*.exe`：QA-05、QA-07、QA-10（`qa05_ui_detach.exe`、`qa07_silence.exe`、`qa10_ui_host_crash.exe`）、`screenshot_ui.exe`（隔離的 host 加 PrintWindow 截圖與 report，用來檢查渲染）、`smoke_zip.exe`；發布用 `agentdesk_package.exe`（先建 Release）
- `fetch_conpty.exe`（隨附 ConPTY）、`make_fixtures.exe`（重產 `tests\fixtures\bytes`）

macOS（ADR-30，設計文件 3.11）：`scripts/build.sh clang-macos-arm64-debug all`（vcpkg 在 `~/vcpkg`，triplet `arm64-osx-agentdesk`）、`scripts/package_macos.sh`（`.app` 與 zip）。macOS 的程式在 `libs/platform/posix/`（含過渡用的 `compat/`）、`*_posix.cpp`、`apps/desktop/macos/`；共用程式的 Win32 段落用 `#ifdef _WIN32` 隔開，Windows 的寫法不動。截圖用 `agent-ui --screenshot <png> --exit-after-ms N`。

不要用自動化腳本反覆啟動又強制結束實際的 Claude Code：它會把這當成全螢幕渲染器故障，自行關掉該模式。渲染檢查用 PowerShell 或 vt_probe。

## 文件地圖

| 檔案 | 內容 |
|---|---|
| `docs/design/AgentDesk_Design.md` | 唯一權威的 PRD＋系統設計（目前 v2.21）。改設計就改這份，並在檔頭版本紀錄加一列 |
| `contracts/approved/PRD.md` | PRD 契約 v3.2.0（鎖定設計文件 v2.20；v2.21 只加實作說明）。需求變更要依憲法 6.3 升版 |
| `docs/design/use-cases.md`、`docs/design/design-concept.md` | Use case（UC-01～UC-21）與設計概念（給新加入的開發者） |
| `contracts/inbox/TestReport_sprint*.md` | 每個 Sprint 的測試報告（QA，依 ADR_001 判定） |
| `docs/poc/M0-report.md`、`docs/poc/evidence/` | M0 結論與實測 JSON |
| `sprint/current/`、`sprint/archive/` | Sprint 計畫與狀態 |
| `.decisions/` | 治理與流程 ADR（`ADR_NNN`）。產品與技術決策是設計文件 7.1 的 `ADR-NN`，兩者不要混用 |
| `docs/research/herdr-reference.md` | herdr 研究筆記（core 與主產品） |
| `docs/research/herdr-app-review.md`、`docs/research/herdr-app/` | herdr-app 原始碼審閱與移植規格（版面、側欄、分頁、面板、選單、Files／Changes、設定、字型） |
| `third_party/*/README.agentdesk.md` | 每個收錄依賴的來源、版本、雜湊與授權 |

設計文件裡的數字（佇列 8 MiB／4 MiB、frame 上限 4 MiB、歷史 10,000 行／16 MiB、停止等待 5 秒、RPC 逾時 5 秒、request_id 保留 10 分鐘）是驗收依據，引用時照抄。

## 程式結構

| 路徑 | 內容 |
|---|---|
| `libs/platform/windows` | `PtyProcess`（ConPTY＋Job＋reader 執行緒）、argv quoting、ConPTY 載入、RAII handle |
| `libs/terminal` | `VtEngine`（libvterm 封裝、快照、歷史、按鍵編碼）、OSC 7501 解析與紀錄 |
| `libs/protocol` | IPC 分幀（4-byte LE＋UTF-8 JSON）、訊息封包、錯誤碼、畫面編碼 |
| `libs/host` | `Session` actor（程序＋引擎＋狀態），M1 的 host 由它組成 |
| `libs/ui` | `TerminalView`（字元格、輸入、IME、選取、搜尋、捲動）、`Workspace`（space／分頁／分割樹、注意力）、`Keymap`、`ModHost`（載入 C++ DLL mod，ImGui 無關）、`git_info`、`HostClient`、唯一碰 `imgui_internal.h` 的 `imgui_adapter.cpp` |
| `apps/desktop` | `agent-ui.exe`：`main.cpp` 是 Win32／DX11 外殼（自繪標題列：`WM_NCCALCSIZE`／`WM_NCHITTEST`）與崩潰記錄，`app.cpp` 是 `App` 的建構與對外介面；介面依功能分檔（`sidebar.cpp`、`tab_bar.cpp`、`panes.cpp`、`files_changes.cpp`、`commands.cpp`、`settings_dialog.cpp`、`welcome.cpp`、`frame.cpp` 等，清單見設計文件 3.10）。共用宣告在 `app_internal.hpp`（`detail` 命名空間） |
| `third_party/fonts`、`third_party/herdr` | 隨附字型（Geist、JetBrains Mono、Symbols Nerd Font）與 herdr 的授權、提示音；建置時複製到執行檔旁的 `fonts\`、`sounds\` |
| `libs/modapi`、`mods/` | mod 介面（純 C 的 `mod_api.h` 與 header-only 的 `mod_sdk.hpp`，ADR-17）與三個範例 mod（各自編成 `mods\<名稱>\mod.dll`，隨 zip 附上 SDK） |
| `tools/` | 開發與量測工具（不出貨） |
| `tests/unit`、`tests/integration/windows`、`tests/fixtures` | GoogleTest；fixture 是原始位元組檔，由 `build\<preset>\tools\make_fixtures.exe` 產生 |
| `third_party/libvterm`、`third_party/conpty` | 收錄的 libvterm 0.3.3（加了換版用的狀態存取，ADR-19）；隨附 ConPTY 的授權與 NOTICE（二進位由腳本下載到 `bin/`，不進版控） |

## 架構重點（需要讀好幾個檔案才拼得出來的部分）

三個執行檔：`agent-ui.exe`、`agent-host.exe`（背景程序）、`agentctl.exe`（CLI）。UI 和 CLI 都只是 host 的 client，透過 Named Pipe 溝通（先 `hello` 協商 v=1）。

**host 擁有一切執行狀態。** ConPTY、子程序、Job、VtEngine、歷史全部在 host。UI 只拿快照和增量來畫，不持有 HPCON 或程序 handle。改功能前先判斷它屬於 host 還是 UI。`ui::TerminalSource` 是兩者之間的接縫。

**執行緒模型**（`libs/host/src/session.cpp`）：每個 Session 有 reader 執行緒（`PtyProcess` 內）、actor 執行緒（唯一能碰 VtEngine 的地方）、writer 執行緒（寫 PTY）。reader 只把位元組丟進 actor 佇列；actor 解析、產生查詢回覆、以最多每 8 ms 一次發布不可變快照（`shared_ptr<const ScreenSnapshot>`）。輸出佇列超過 8 MiB 時 reader 暫停讀取（背壓），不丟任何位元組；關閉前要先解除背壓，否則 join reader 會死結。

**不可違反的不變式**：
- host 必須完整依序解析所有 VT bytes；只有送給 UI 的畫面更新可以合併或丟棄（丟了就設 `needs_snapshot`）。
- process_state 和 agent_state 是兩個獨立欄位。程序 Running 不代表 Agent Working，輸出沉默不代表 Done。沒有可信來源就是 Unknown。
- agent_state 只接受結構化回報：主要是 OSC 7501（公開規格 Revision 0.4，Claude Code ≥ 2.1.295 原生支援），其次是 `agentctl report-state`，依 (session, source) 內遞增的 seq 去重。紀錄被丟棄或清除後顯示 Unknown，原值存 `last_agent_state`。不從畫面文字推斷狀態（ADR-09）。
- host 必須回覆 `OSC 7501 ; ?`，而且回覆要排在 DA1 回覆之前。**內建 ConPTY 會丟掉這個回覆**（M0 實測），所以一律用隨附的 ConPTY（ADR-08）；沒有 `conpty\conpty.dll` 時 Claude Code 不會送任何狀態。
- 新 host 啟動時把舊的 Running 紀錄標成 Lost，絕不憑舊 PID 接管程序。
- attach 時先在 session actor 內取 seq=S 的快照並註冊訂閱，再送快照，之後只送 seq>S 的增量。
- 一個 session 同時只有一個輸入控制者，只有控制者能決定 ConPTY 尺寸。
- executable 與 argv 分開存，不把 prompt 拼進 shell 字串；`PtyProcess::spawn` 拒絕 `.cmd`／`.bat`。
- 輸出內容不可觸發命令或寫剪貼簿（OSC 52 不註冊回呼）。

**libvterm 0.3.3 的坑**：UTF-8 序列被切在兩次寫入之間時會產生 U+FFFD，`VtEngine::feed` 會暫存尾端不完整的序列（只延後、不丟），程序結束時 `flush()`。預設色的格子 RGB 一律為 0（標準形式），比較快照或走協定時才會相等。

**終端機繪製**（`libs/ui/src/terminal_view.cpp`）：格寬取字型的真實 advance（mono 字型開 PixelSnapH），不可用 `CalcTextSize`，它會無條件進位，游標會越往右越偏。每格各自定位；寬字元放大填滿兩格並置中。字型（`libs/ui/src/fonts.cpp`）：介面用隨附的 Geist，終端機用隨附的 JetBrains Mono Medium，缺字由微軟正黑體、Symbols Nerd Font、Segoe UI Symbol／Emoji 補；ImGui 用 vcpkg 的 `wchar32` 與 `freetype` feature，由 FreeType 繪字。

**Windows 細節**：子程序以 CREATE_SUSPENDED 建立、指派 KILL_ON_JOB_CLOSE Job 後才 Resume，Job 不設 BREAKAWAY_OK（設計文件優先於 skill 範例）。`PtyProcess::close()` 順序：ClosePseudoConsole → join reader → 釋放 handle（write mutex 保護）→ TerminateJobObject。中斷就是寫入 0x03（M0 實測對 PING、cmd 有效）。

## 開發環境（不可另外安裝工具）

使用者要求只用開發機上已安裝的工具（ADR-11），不要安裝 Zig、其他編譯器或建置工具；需要新工具時先問使用者。可用：VS 2022 Build Tools 17.14（MSVC 14.44.35207、Windows SDK 10.0.26100.0）、CMake 4.3.1、Ninja 1.13.2、vcpkg（`C:\vcpkg`，manifest 模式，baseline `afa249f`，triplet `x64-windows-static-md`）、Git for Windows（含 perl）。函式庫走 vcpkg 或收錄原始碼；新依賴要先確認授權與 MIT 相容。herdr（Apache-2.0）的程式碼使用者同意可以轉寫成 C++（2026-11-18，ADR-15）：轉寫時保留出處註明，發布包的 THIRD_PARTY_NOTICES 已列出 herdr-app 與其授權（`third_party/herdr/LICENSE`）。

## 對應的 Skill

依上層憲法 IL-6，實作前先載入對應 skill（位於 `..\.claude\skills`）：

| Skill | 何時載入 |
|---|---|
| `rd/lang-cpp`、`rd/lang-c` | 所有 C／C++ 程式 |
| `rd/platform-windows-conpty` | ConPTY、Job、host 啟動、Named Pipe、argv quoting |
| `rd/engine-terminal-vt` | libvterm 封裝、快照／增量、OSC 7501、查詢回覆 |
| `rd/frontend-imgui` | UI、TerminalView、DPI、字型、IME |
| `qa/framework-gtest` | 所有測試、CMakePresets 的 test preset |
| `shared/domain-coding-agents` | Agent 狀態模型、Claude Code／Codex 整合 |

skill 與設計文件衝突時，以設計文件為準，並記入 status 的「Skill 衝突紀錄」。

## 與上層團隊憲法（`..\.claude\CLAUDE.md`）的關係

M 模式、standard 執行模式。Web 專用條目（JWT、CORS、HTTP 格式、API p95、禁用 any）不適用，替代門檻見 `.decisions/ADR_001_constitution_tailoring.md`；M 模式的寫入權限補充見 `ADR_002`。`project.type: desktop_app` 的框架層 ADR_004 仍是 proposed。

Git：Conventional Commits；分支 `feature/sprint-N/...`；不要直接在 main 上開發。commit 訊息結尾加 Co-Authored-By 行。

## 程式註解

註解用英文、用自己的話寫，說明為什麼這樣做、不明顯的限制與單位，不寫 herdr 的出處或行號（使用者要求，2026-11-20，ADR-22）。要查某段程式當初照 herdr 哪裡寫，看 `docs/research/herdr-app/code-map.md`。例外：有轉寫 herdr 程式的檔案開頭保留一行 `// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.`，這是授權要求，不可刪。

## 回覆語言

回覆使用者一律用中文（台灣繁體），包括解釋、進度回報與問題說明；程式碼、指令、識別字與 commit message 照原樣（使用者要求，2026-11-19）。

## 文件撰寫

中文文件用台灣繁體與全形標點，完成前依使用者全域設定跑 `speak-human-tw`（它要求先列清單、等使用者確認才改）。引用 herdr 時寫明是觀察結果與出處。
