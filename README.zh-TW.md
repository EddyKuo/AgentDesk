# AgentDesk

[English](README.md) | 繁體中文

AgentDesk 是 Windows 11 x64 的原生 AI coding agent 工作台。你可以在可分割的終端機面板裡同時跑多個 Agent CLI（首發支援 Claude Code 與 Codex CLI，其他命令列程式也能跑），關掉視窗後工作照常執行，重開時接回原本的畫面與版面。哪個 Agent 在等你、哪個已經做完，側欄都看得到。介面與行為依 [herdr-app](https://github.com/herdrdev/herdr-app) 的原始碼複刻，用 C++ 重寫，不追求協定相容。

目前版本 0.2.0。MVP（M0～M4）與 herdr-app 的移植都已完成，2026-11-19 完成整體驗收；還要人工驗收的項目列在設計文件 5.4、5.5。

## 功能

- 背景執行：終端機、子程序與畫面狀態都在背景程序 `agent-host.exe` 裡。關掉視窗只會中斷連線，重開時以快照加增量接回。
- space、分頁、分割：一個 space 是一個工作地點（通常是一個 repo），底下有多個分頁，每個分頁可以往右或往下分割、放大、拖曳交換位置。版面由 host 保存，重開視窗後還原。
- Agent 狀態：程序狀態與 Agent 狀態分開顯示。Agent 狀態只來自結構化回報：Claude Code 2.1.295 以上用 OSC 7501（不必設定），Codex CLI 用 hook 呼叫 `agentctl report-state`。沒有可信來源就顯示 Unknown，不從畫面文字猜。Agent 結束、shell 回到提示時，狀態會清掉。
- 注意力與通知：Agent 需要你時一定播放提示音；你沒在看的 Agent 完成時標為 Done，右下角跳出通知，點一下就跳過去。
- 選擇 shell：側欄的「+」列出這台電腦偵測到的 PowerShell 7、Windows PowerShell、Command Prompt 與各個 WSL 發行版，選哪個就用哪個開新 space；分頁列的「+」也能直接開成新分頁。
- WSL：已安裝的發行版自動列為 shell；在 bash 裡 `cd` 之後，space 名稱、分支與 Files／Changes 會跟著變。
- Files／Changes：右側面板顯示目前資料夾的檔案樹與 git 變更，可以看差異、直接 commit。
- 命令面板與快捷鍵：Ctrl+Shift+P 開命令面板，Ctrl+Shift+/ 列出所有快捷鍵，`keys.json` 可以改鍵。
- git worktree：輸入分支名稱就建立 checkout，開成新的 space。
- 子 Agent：在 AgentDesk 的終端機裡執行 `agentctl run --task ...` 會開出子 Agent；`agentctl wait` 等它，`agentctl report` 把結果送回父 Agent。
- 遠端機器：透過 `ssh <目標> agentctl bridge`，另一台 Windows 電腦的 space 與 Agent 顯示在同一個側欄。
- 版面：內建 8 種，也能用 JSON 自訂。AgentDesk（預設的 workspace）、Terminals、AgentDesk Mac（選單列、浮動視窗與 Dock）、Mission Control（所有 Agent 排成一面牆，需要你的排最前面）、Review（終端機旁邊就是這個 Agent 的 diff 與 commit 區）、AgentDesk Tiling（直角面板、以鍵盤操作）、AgentDesk Cockpit（儀表、Agent 清單與狀態紀錄）、AgentDesk Classic（整個視窗用字元畫成單色終端機）。畫法都是 AgentDesk 自己的。
- 專注模式：Ctrl+Shift+. 只留目前的終端機，底下一條細列用小點顯示其他 Agent；有 Agent 需要你時視窗邊緣會亮，也照樣播放提示音。
- 時間軸：Ctrl+Shift+H 看每個 Agent 什麼時候在工作、在等你、做完。agent-host 只記時間與狀態（最多 24 小時），關掉視窗也繼續記；`agentctl timeline` 可以在命令列看同樣的紀錄。
- 主題與 mod：AgentDesk 自己的主題、herdr 的主題，以及為版面配好的主題。外觀可以依時間切換淺色與深色（固定時間，或依你輸入的緯度算日出日落；不連網、不用定位）。主題編輯器可以複製主題、即時預覽並檢查對比。mod 是 C++ DLL，可以加命令、面板與通知。
- 更新：AgentDesk 與 agent-host 都能更新；agent-host 單獨更新時由新版接手，終端機裡的程式不會中斷。AgentDesk 沒有內建伺服器，要設定 `AGENTDESK_RELEASES_URL` 才會檢查。
- 命令列 `agentctl`：`run`、`list`、`read`、`send`、`stop`、`wait` 等指令，`--json` 輸出穩定，退出碼固定。

完整操作說明見 [docs/user-guide.md](docs/user-guide.md)。

## 系統需求

- Windows 11 x64，一般使用者權限即可（Windows 10 未驗證）。
- 要用的 Agent CLI 自行安裝並登入，AgentDesk 不處理登入，也不複製任何 token。Claude Code 要 2.1.295 以上才會回報狀態。
- 從原始碼建置另外需要 Visual Studio 2022 Build Tools，見「從原始碼建置」。

## 安裝與執行

1. 把 `AgentDesk-<版本>-win-x64.zip` 解壓到任意資料夾，例如 `%LOCALAPPDATA%\Programs\AgentDesk`。不需要系統管理員權限，也不寫登錄檔。
2. 確認 `conpty\conpty.dll` 與 `conpty\x64\OpenConsole.exe` 跟執行檔放在一起。這是隨附的 ConPTY：Windows 內建的 ConPTY 會丟掉狀態查詢的回覆，少了它，Claude Code 不會回報狀態，agent-host 也無法單獨更新。
3. 執行 `agent-ui.exe`。它會在背景啟動 `agent-host.exe`，第一次啟動會開歡迎頁，可以選大小與主題。

第一次執行時，Windows 可能顯示「Windows 已保護您的電腦」（SmartScreen）。AgentDesk 的執行檔目前沒有程式碼簽章，新下載、還沒累積信譽的程式都會出現這個畫面。確認 zip 是從本專案的 GitHub Releases 下載的，按「其他資訊」→「仍要執行」即可。也可以從原始碼自己建置。

zip 裡還有 `agentctl.exe`、`LICENSE`、`THIRD_PARTY_NOTICES.md`、`docs\`（使用說明、Agent hook 設定、相容性矩陣）、範例 mod，以及寫 mod 用的 `sdk\`。資料與設定放在 `%LOCALAPPDATA%\AgentDesk`。

## 快速上手

1. 啟動後會先開一個 shell。用 `cd` 切到專案資料夾，space 名稱與分支會跟著變。
2. 輸入 `claude` 或 `codex` 啟動 Agent。它需要你時，側欄 Agents 區塊會標出來，並播放提示音。
3. Ctrl+Shift+O、Ctrl+Shift+E 往右、往下分割，在另一個面板再開一個 Agent；Ctrl+Shift+T 開新分頁，Ctrl+Shift+N 開新 space。
4. 關掉視窗後工作繼續跑，再執行一次 `agent-ui.exe` 就接回來。
5. Codex CLI 的狀態回報要手動安裝 hook，步驟在 [docs/integrations/agent-hooks.md](docs/integrations/agent-hooks.md)。

## 從原始碼建置

只用以下工具，不需要另外安裝（ADR-11）：Visual Studio 2022 Build Tools 17.14（MSVC 14.44、Windows SDK 10.0.26100.0）、CMake、Ninja、vcpkg（manifest 模式，預設在 `C:\vcpkg`，triplet `x64-windows-static-md`）、Git for Windows。技術基線是 C++20、Dear ImGui（docking）＋Win32＋DirectX 11、ConPTY、libvterm 0.3.3。

`scripts\build.cmd` 會自己進入 VS 2022 x64 vcvars 環境、設定 `VCPKG_ROOT` 與英文編譯訊息（`VSLANG=1033`），再執行 CMake preset：

```bat
scripts\build.cmd msvc-x64-debug all          :: configure + build + ctest
scripts\build.cmd msvc-x64-debug build        :: 只建置（preset 已 configure 過）
scripts\build.cmd msvc-x64-release configure  :: 新 preset 第一次要先 configure
```

新 clone 要先取得隨附 ConPTY：建置一次產生工具，執行它，再重新 configure。

```bat
scripts\build.cmd msvc-x64-debug all
build\msvc-x64-debug\tools\fetch_conpty.exe
scripts\build.cmd msvc-x64-debug configure
```

`build.cmd` 在建置前會結束從這個建置資料夾（`build\<preset>\`）啟動的 `agent-ui` 與 `agent-host`，因為它們鎖住了執行檔與 `conpty.dll`；其他位置安裝的 AgentDesk 不受影響。結束 agent-host 也會結束它跑的終端機，不想這樣時先設定 `AGENTDESK_KEEP_HOST=1`。

測試用 GoogleTest，經由 ctest 執行（先建置過一次）：

```bat
ctest --test-dir build\msvc-x64-debug -L unit          :: 依 label：unit、windows_integration
build\msvc-x64-debug\tests\unit_tests.exe --gtest_filter=VtEngine.Utf8SplitBeforeControlCharsIsReassembled
build\msvc-x64-debug\tests\win_integration_tests.exe --gtest_filter=Session.*
```

發布用的 zip 由 `agentdesk_package.exe` 產生，要先建置 Release（`scripts\build.cmd msvc-x64-release all`），輸出在 `dist\`，完整步驟見 [docs/release.md](docs/release.md)。每次推送到 main 或送出 pull request，GitHub Actions（`.github/workflows/ci.yml`）會在 Windows 上建置 Debug 與 Release 並跑所有測試。編譯一律 `/W4 /permissive- /WX`；格式與靜態分析用 VS 內附的 clang-format、clang-tidy，規則在根目錄的 `.clang-format`、`.clang-tidy`。

## 專案結構

| 路徑 | 內容 |
|---|---|
| `apps/desktop` | `agent-ui.exe`：Win32／DX11 外殼與整個介面 |
| `apps/host` | `agent-host.exe`：擁有 ConPTY、子程序、終端機引擎與歷史的背景程序 |
| `apps/cli` | `agentctl.exe`：命令列 client |
| `libs/platform/windows` | `PtyProcess`（ConPTY＋Job＋reader 執行緒）、argv quoting、ConPTY 載入 |
| `libs/terminal` | `VtEngine`（libvterm 封裝、快照、歷史、按鍵編碼）、OSC 7501 解析 |
| `libs/protocol`、`libs/ipc` | IPC 分幀、訊息封包、錯誤碼、畫面編碼；Named Pipe |
| `libs/host` | `Session` actor（程序＋引擎＋狀態） |
| `libs/ui` | `TerminalView`、`Workspace`（space／分頁／分割樹、注意力）、`Keymap`、`ModHost`、版面引擎 |
| `libs/modapi`、`mods/` | mod 介面（純 C 的 `mod_api.h` 與 header-only 的 `mod_sdk.hpp`）與三個範例 mod |
| `tools/` | 開發、量測與打包工具（不出貨） |
| `tests/` | GoogleTest 單元與 Windows 整合測試、fixture |
| `third_party/` | libvterm 0.3.3、隨附 ConPTY 的授權與 NOTICE、字型、lunasvg 說明、herdr 的授權與資源（版面、主題、程式表、提示音） |
| `scripts/build.cmd` | 建置腳本 |
| `docs/`、`contracts/`、`sprint/`、`.decisions/` | 文件、契約、Sprint 紀錄、治理 ADR |

## 文件

| 文件 | 內容 |
|---|---|
| [docs/user-guide.md](docs/user-guide.md) | 使用說明：畫面、快捷鍵、子 Agent、遠端機器、WSL、版面、設定、mod、agentctl、限制 |
| [docs/mods.md](docs/mods.md) | 寫 mod：介面、相容性、安全（英文） |
| [docs/release.md](docs/release.md) | 發布流程：打包、GitHub Release、簽章與更新伺服器（英文） |
| [docs/design/AgentDesk_Design.md](docs/design/AgentDesk_Design.md) | 唯一權威的產品需求與系統設計，產品與技術決策在 7.1 |
| [docs/design/use-cases.md](docs/design/use-cases.md) | 使用情境 |
| [docs/design/design-concept.md](docs/design/design-concept.md) | 設計理念 |
| [contracts/approved/PRD.md](contracts/approved/PRD.md) | PRD 契約 |
| [docs/integrations/agent-hooks.md](docs/integrations/agent-hooks.md) | Agent 狀態 hook 的手動安裝 |
| [docs/compatibility.md](docs/compatibility.md) | 相容性矩陣 |
| [docs/research/](docs/research/) | herdr 與 herdr-app 的研究筆記、移植規格 |
| [CLAUDE.md](CLAUDE.md) | 給 Claude Code 的專案指引：指令、架構重點、不可違反的規則 |

## 授權

AgentDesk 以 MIT License 釋出，見 [LICENSE](LICENSE)。

收錄或隨附的第三方元件（libvterm、ConPTY、字型、lunasvg），來源、版本、雜湊與授權寫在各自的 `third_party/*/README.agentdesk.md`；發布包的 `THIRD_PARTY_NOTICES.md` 列出所有隨附元件的授權聲明。

## 致謝

AgentDesk 的介面與行為依 [herdr-app](https://github.com/herdrdev/herdr-app)（Apache-2.0）的原始碼移植成 C++。herdr-app 的授權在 [third_party/herdr/LICENSE](third_party/herdr/LICENSE)，發布包的 THIRD_PARTY_NOTICES 也列出了它。
