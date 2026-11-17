# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 專案現況

AgentDesk 是 Windows 11 x64 的原生 AI Agent 工作台：在可分割的終端機面板裡跑多個 coding agent CLI，關掉視窗後工作繼續跑，重開時接回原畫面。設計參考 herdr（https://github.com/herdrdev），但是獨立實作，不追求相容。

**寫程式之前的作業已全部完成，但還沒有任何程式碼。** Sprint 0 已結束，Sprint 1（M0 技術 POC）計畫在 `sprint/current/plan.md`，狀態 `ready`。在使用者明確下令開工之前，不要建立 `apps/`、`libs/`、CMakeLists.txt、vcpkg.json 或任何原始碼；若需求不清楚，列進設計文件 7.4 待決事項，不要自己補。開工後依 plan 的任務順序（S1-1 起）進行，並在 `sprint/current/status.md` 追加狀態列。

## 文件地圖

| 檔案 | 內容 |
|---|---|
| `docs/design/AgentDesk_Design.md` | 唯一權威的 PRD＋系統設計（目前 v1.7）。改設計就改這份，並在檔頭版本紀錄加一列 |
| `contracts/approved/PRD.md` | PRD 契約 v1.1.0，鎖定設計文件 v1.7。設計變更影響需求時要依憲法 6.3 升版此契約 |
| `sprint/current/` | 目前 Sprint 的 plan 與 status；`sprint/archive/` 為已結束的 Sprint |
| `.decisions/` | 治理與流程 ADR（`ADR_NNN`；ADR_002 定義 M 模式下 `docs/` 由 Orchestrator 寫、TestReport 由 QA 寫入 `contracts/inbox/`）。產品與技術決策是設計文件 7.1 的 `ADR-NN`，兩者不要混用 |
| `docs/poc/M0-report-template.md` | M0 結束時要填的報告範本 |
| `docs/research/herdr-reference.md` | 從 herdr 原始碼查到的事實，以及哪些已經反映到設計 |
| `docs/archive/` | 舊版設計，唯讀 |

設計文件裡的數字（佇列 8 MiB／4 MiB、frame 上限 4 MiB、歷史 10,000 行／16 MiB、停止等待 5 秒、RPC 逾時 5 秒、request_id 保留 10 分鐘）是驗收依據，引用時照抄，不要改寫成近似值。

## 架構重點（需要讀好幾節才拼得出來的部分）

三個執行檔：`agent-ui.exe`（ImGui＋Win32＋DX11）、`agent-host.exe`（背景程序）、`agentctl.exe`（CLI）。UI 和 CLI 都只是 host 的 client，透過 Named Pipe 溝通（4-byte LE 長度＋UTF-8 JSON，先 `hello` 協商 v=1）。

**host 擁有一切執行狀態。** ConPTY、子程序、Job Object、VT 引擎、歷史全部在 host。UI 只拿畫面快照和增量來畫，不持有 HPCON 或程序 handle。所以終端機查詢（DA／DSR）由 host 回覆，UI 不在時程式照樣能跑。改任何功能前先判斷它屬於 host 還是 UI，不要讓 UI 直接碰 ConPTY。

**不可違反的不變式**（散在設計文件 2.2、3.3、3.6、3.7）：

- host 必須完整依序解析所有 VT bytes；只有送給 UI 的畫面更新可以合併或丟棄（丟了就設 `needs_snapshot`）。
- 每個 TerminalState 只由自己的 session actor 寫入；renderer 只讀不可變快照；ImGui／DX11 只在 UI thread 呼叫。
- process_state 和 agent_state 是兩個獨立欄位。程序 Running 不代表 Agent Working，輸出沉默不代表 Done。沒有可信來源就是 Unknown。
- agent_state 只接受程式自己送出的結構化回報：主要是 OSC 7501（公開規格 Revision 0.4，Claude Code v2.1.295 起原生支援），其次是 `agentctl report-state`，依 (session, source) 內單調遞增的 seq 去重；OSC 7501 的 seq 由 host 指派。紀錄被丟棄或清除後顯示 Unknown，原值存 `last_agent_state`。MVP 不從畫面文字推斷狀態（ADR-09）。
- host 必須回覆 `OSC 7501 ; ?` 查詢，而且回覆要排在 DA1 回覆之前寫回 PTY，否則 Claude Code 會判定終端機不支援、不送狀態。
- 新 host 啟動時把舊的 Running 紀錄標成 Lost，絕不憑舊 PID 接管程序。
- attach 時先在 session actor 內取 seq=S 的快照並註冊訂閱，再送快照，之後只送 seq>S 的增量。generation 不同或 base_seq 不連續時，UI 丟掉增量，重新要求快照。
- 一個 session 同時只有一個輸入控制者，只有控制者能決定 ConPTY 尺寸。
- executable 與 argv 分開存，不把 prompt 拼進 shell 字串；`.cmd` 啟動器走獨立 adapter。
- 輸出內容不可觸發命令或寫剪貼簿（OSC 52 預設關閉）。

**Windows 細節**（設計文件 2.2、3.2）：host 以 `DETACHED_PROCESS`＋`CREATE_BREAKAWAY_FROM_JOB` 啟動，不開新 process group；子程序放進 KILL_ON_JOB_CLOSE 的 Job，以 CREATE_SUSPENDED 建立、指派 Job 後才 Resume。ClosePseudoConsole 可能等輸出被讀完，關閉時 reader 必須持續 drain。內建 ConPTY 可能吞掉 DA 與 OSC 7501，是否改用隨附的 Microsoft.Windows.Console.ConPTY 由 M0 決定（ADR-08）。

## 開發環境（不可另外安裝工具）

使用者要求只用開發機上已安裝的工具（ADR-11），不要安裝 Zig、其他編譯器或建置工具；需要新工具時先問使用者。已確認可用：

- Visual Studio 2022 Build Tools 17.14：MSVC 14.44.35207、Windows SDK 10.0.26100.0、`vcvars64.bat`，以及內附的 clang-format／clang-tidy（`VC\Tools\Llvm\x64\bin`）
- CMake 4.3.1（`C:\Program Files\CMake`）、Ninja 1.13.2
- vcpkg：`C:\vcpkg`（第三方函式庫用 manifest 模式取得）

建置要在 x64 vcvars 環境中執行 CMake（MSVC 不在預設 PATH）。

## 規劃中的程式結構與建置

尚未建立，以下取自設計文件 3.1、3.10，實作開始後要以實際檔案為準更新本節：

- C++20、CMake＋MSVC，固定工具鏈並使用 CMakePresets；Dear ImGui 以 vcpkg `imgui` 1.93.0（docking-experimental feature）並用 builtin-baseline＋overrides 固定版本；所有第三方套件固定版本並保留 license 與 notices。本專案採 MIT License（`LICENSE`）；新增依賴前確認授權與 MIT 相容，herdr（Apache-2.0）只參考設計，不複製程式碼。
- 目錄：`apps/{desktop,host,cli}`、`libs/{protocol,host,platform/windows,terminal,ui,adapters}`、`tests/{unit,integration,fixtures}`、`third_party/`。
- 依賴：Dear ImGui 1.93.0 docking 與 GoogleTest 1.18.0 走 vcpkg manifest；libvterm 0.3.3 沒有 vcpkg port，原始碼放 `third_party/libvterm`（產生的 `.inc` 一併提交，建置時不呼叫 perl）。
- 測試：GoogleTest＋CTest（OQ-05 傾向，M0 凍結）；fixture 放 `tests/fixtures/`，`.gitattributes` 已設為不轉換換行。
- 格式與靜態分析：根目錄 `.clang-format`、`.clang-tidy` 已建立並驗證，命名規則對齊 `rd/lang-cpp`（函式 snake_case、私有成員尾底線、常數 `k` 前綴）。工具用 VS 內附版本：
  ```powershell
  $llvm = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin"
  & "$llvm\clang-format.exe" --dry-run --Werror <檔案>      # 檢查格式
  & "$llvm\clang-tidy.exe" -p build <檔案>                   # 需要 compile_commands.json
  ```
- 編譯警告：MSVC `/W4 /permissive- /WX`，另以 `/analyze` 取代未安裝的 cppcheck（`.decisions/ADR_001`）。
- 執行期資料放在 `%LOCALAPPDATA%\AgentDesk`；host 持有 `profiles.json`、`sessions.json`，UI 持有 `ui.json`、`imgui.ini`，都用暫存檔再替換的方式寫入。

## 對應的 Skill

依上層憲法 IL-6，實作前先載入對應 skill（位於 `..\.claude\skills`，2026-11-16 為本專案建立）：

| Skill | 何時載入 |
|---|---|
| `rd/lang-cpp`、`rd/lang-c` | 所有 C／C++ 程式 |
| `rd/platform-windows-conpty` | ConPTY、Job Object、host 啟動、Named Pipe、argv quoting |
| `rd/engine-terminal-vt` | libvterm 封裝、快照／增量、OSC 7501、終端機查詢回覆 |
| `rd/frontend-imgui` | UI、TerminalView、DPI、字型、IME |
| `qa/framework-gtest` | 所有測試、CMakePresets 的 test preset |
| `shared/domain-coding-agents` | Agent 狀態模型、Claude Code／Codex 整合、PM 與 QA 的相容性需求 |

skill 與設計文件衝突時，以設計文件的產品決策為準，並回報衝突。

## 開發順序

M0 技術 POC（ConPTY 內建與隨附比較、libvterm 驗證、繁中 IME、Claude Code 端到端）→ M1 host 與協定 → M2 桌面整合 → M3 可靠性 → M4 驗收。設計文件 5.2 規定：關鍵 POC 項目沒通過前，不擴大 UI 功能，也不要先做漂亮介面。

## 與上層團隊憲法（`..\.claude\CLAUDE.md`）的關係

上層憲法也會載入。Sprint 0 已完成（`sprint/archive/sprint-0/`），設定如下；名稱、複雜度、授權、首發 CLI 由使用者確認（2026-11-16）：

```yaml
project:
  name: "AgentDesk"          # 已確認（OQ-01）
  type: "desktop_app"        # 憲法列舉值沒有桌面應用，暫用此值
language:
  backend: "C++20"           # host 與 CLI
  frontend: "C++20 (Dear ImGui)"
framework:
  backend: "Win32 + ConPTY"
  frontend: "Dear ImGui docking + DirectX 11"
infrastructure:
  database_primary: "none"   # JSON 設定檔
  message_queue: "none"      # 本機 Named Pipe
  cloud_provider: "none"
deployment:
  target_env: "Windows 11 x64，portable zip"
compliance: "none"
project_complexity: "M"
execution_mode:
  base: "standard"
```

憲法裡針對 Web 服務的條目（JWT、CORS、Rate Limiting、HTTP 回應格式、API p95、禁用 any）不適用，替代門檻見 `.decisions/ADR_001_constitution_tailoring.md`；每個 Sprint 的 TestReport 依該表判定。`project.type: desktop_app` 不在憲法列舉值內，框架層已起草 `..\.claude\.decisions\ADR_004`（proposed）。Git 分支與 Conventional Commits 照常適用；repo 已 `git init`（main），尚無 commit，commit 前先問使用者。

## 文件撰寫

中文文件用台灣繁體與全形標點，完成前依使用者全域設定跑一次 `speak-human-tw`。引用 herdr 時寫明是觀察結果、出處檔案與查閱日期，不要寫成 AgentDesk 已驗證的事實。
