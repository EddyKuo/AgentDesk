# AgentDesk

[English](README.md) | 繁體中文

[![CI](https://github.com/EddyKuo/AgentDesk/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/EddyKuo/AgentDesk/actions/workflows/ci.yml)
[![Release](https://github.com/EddyKuo/AgentDesk/actions/workflows/release.yml/badge.svg)](https://github.com/EddyKuo/AgentDesk/actions/workflows/release.yml)
[![Latest release](https://img.shields.io/github/v/release/EddyKuo/AgentDesk?include_prereleases&sort=semver)](https://github.com/EddyKuo/AgentDesk/releases)
[![Downloads](https://img.shields.io/github/downloads/EddyKuo/AgentDesk/total)](https://github.com/EddyKuo/AgentDesk/releases)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![Platform: macOS](https://img.shields.io/badge/platform-macOS%20%28Apple%20Silicon%29-lightgrey?logo=apple)
![Platform: Windows](https://img.shields.io/badge/platform-Windows%2011%20x64-0078D6?logo=windows)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)](https://en.cppreference.com/w/cpp/20)
[![Dear ImGui](https://img.shields.io/badge/UI-Dear%20ImGui-5c6bc0)](https://github.com/ocornut/imgui)
[![Tests](https://img.shields.io/badge/tests-GoogleTest-brightgreen)](https://github.com/EddyKuo/AgentDesk/actions/workflows/ci.yml)

AgentDesk 是 Windows 11 與 macOS 上的原生 AI coding agent 工作台。你可以在可分割的終端機面板裡同時跑多個 Agent CLI（首發支援 Claude Code 與 Codex CLI，其他命令列程式也能跑），關掉視窗後工作照常執行，重開時接回原本的畫面與版面。哪個 Agent 在等你、哪個已經做完，側欄都看得到。

介面與行為依 [herdr-app](https://github.com/herdrdev/herdr-app) 的原始碼複刻，用 C++ 重寫，不追求與 herdr 的協定相容。

目前版本 0.2.0。支援平台：Windows 11 x64，以及 Apple Silicon 的 macOS 13 Ventura 以上。

## 運作方式

AgentDesk 由三個程式組成：

| 程式 | 角色 |
|---|---|
| `agent-host` | 每位使用者只有一個的背景程序，擁有所有終端機：偽終端機、子程序、終端機引擎（libvterm）與捲動歷史 |
| `agent-ui` | 視窗（macOS 上是 `AgentDesk.app`）。它只是 agent-host 的 client：畫出快照與即時更新，送出你的輸入 |
| `agentctl` | 命令列 client，也負責 hook 的狀態回報、透過 ssh 轉接協定，以及更新 agent-host |

三者透過只開放給你自己帳號的本機通道溝通：Windows 用 Named Pipe，macOS 用 Unix socket。終端機和裡面的程式都在 host 裡，所以關掉視窗只是中斷連線。

## 功能

- **背景執行**：終端機在 agent-host 裡繼續跑，重開視窗時以快照加即時更新接回。
- **space、分頁、分割**：一個 space 是一個工作地點，通常是一個 repo。底下有多個分頁，每個分頁可以往右或往下分割、放大、拖曳交換面板。版面由 host 保存，重開後還原；同時開幾個視窗，看到的都是同一份版面。
- **可信的 Agent 狀態**：程序狀態與 Agent 狀態分開顯示。Agent 狀態只來自結構化回報：Claude Code 2.1.295 以上的 OSC 7501（不用設定），以及 Codex CLI 透過 `agentctl hook`。沒有可信來源時顯示 Unknown，AgentDesk 不從畫面文字猜。shell 重新顯示提示字元時，Agent 的狀態會被清掉。
- **提醒與通知**：Agent 需要你時一定會響；你沒在看的 Agent 做完時標成 Done 並跳出通知，點一下就跳過去。提示音與通知可以分別關掉。
- **選擇 shell**：側欄的「+」列出這台電腦找到的 shell。Windows 是 PowerShell 7、Windows PowerShell、命令提示字元與各個 WSL 發行版；macOS 是登入 shell 與 `/etc/shells` 列出的 shell。
- **資料夾追蹤**：AgentDesk 替 PowerShell、命令提示字元、WSL 的 bash、zsh 與 bash 加上提示字元鉤子，在 shell 裡 `cd` 之後，space 名稱、分支與 Files／Changes 面板都會跟著變。
- **Files 與 Changes 面板**：檔案樹加 git 狀態與唯讀預覽；未提交的變更、差異與 commit。
- **命令面板與快捷鍵**：模糊搜尋的命令面板、所有快捷鍵的清單、跳到 space／分頁／Agent 的清單，以及用 `keys.json` 改鍵。macOS 有以 Cmd 為主的鍵表，以及 Option 鍵當 Alt 的設定。
- **git worktree**：輸入分支名稱就能建立 checkout，並開成新的 space。
- **子 Agent**：在 AgentDesk 的終端機裡執行 `agentctl run --task ...` 開子 Agent；`agentctl wait` 等它，`agentctl report` 把結果送回父 Agent。
- **遠端機器**：透過 `ssh <目標> agentctl bridge`，另一台機器的 space 與 Agent 會出現在同一個側欄。
- **版面**：內建 7 種，也能用 JSON 自訂。AgentDesk（預設的 workspace）、Terminals（可改成直角、調整間距、用鍵盤調整面板大小）、Stage（一個 Agent 在大的主舞台上，其他的在下方排成即時縮圖；Ctrl+Shift+M）、Inbox（需要你的 Agent 排成卡片，可以直接回答、延後或打開；Ctrl+Shift+I）、Review（終端機旁邊就是這個 Agent 的 diff 與 commit 區）、Pair（兩個 Agent 各用自己的 worktree 並排，共用一個輸入列，比誰先做完；Ctrl+Shift+2）、AgentDesk Mac（選單列、浮動視窗與 Dock）。在設定 › Look 打開「Fun layouts」後，還有兩個好玩的版面：AgentDesk Classic（整個視窗用字元畫成單色終端機）與 Garden（每個 Agent 是一隻自己畫的小動物，動作代表它的狀態）。畫法都是 AgentDesk 自己的，共用同一套間距、字級、陰影與動畫規則，並遵守 Windows 的「減少動畫」設定。
- **專注模式**：Ctrl+Shift+. 只留目前的終端機，底下一條細列用小點顯示其他 Agent；有 Agent 需要你時視窗邊緣會亮，也照樣播放提示音。
- **時間軸**：Ctrl+Shift+H 看每個 Agent 什麼時候在工作、在等你、做完。agent-host 只記時間與狀態（最多 24 小時），關掉視窗也繼續記；`agentctl timeline` 可以在命令列看同樣的紀錄。
- **主題與 mod**：AgentDesk 自己的主題、herdr 的主題，以及為版面配好的主題。外觀可以依時間切換淺色與深色（固定時間，或依你輸入的緯度算日出日落；不連網、不用定位）。主題編輯器可以複製主題、即時預覽並檢查對比。mod 用 C++ 寫（Windows 是 `mod.dll`，macOS 是 `mod.dylib`），可以加命令、面板與通知。
  上面的快捷鍵是 Windows 的；macOS 把 Ctrl 換成 Cmd。
- **更新不中斷終端機**：AgentDesk 可以自我更新，agent-host 也可以單獨更新：新的 host 會接手所有終端機，裡面的程式繼續執行。沒有內建的更新伺服器，要設定 `AGENTDESK_RELEASES_URL` 才會檢查。

完整的使用說明見 [docs/user-guide.md](docs/user-guide.md)。

## 系統需求

- Windows 11 x64，或 Apple Silicon 的 macOS 13 Ventura 以上。一般使用者權限即可。
- 自行安裝並登入要用的 Agent CLI。AgentDesk 不處理登入，也不會複製 token。Claude Code 從 2.1.295 起會回報狀態；Codex CLI 需要設定 hook（見 [docs/integrations/agent-hooks.md](docs/integrations/agent-hooks.md)）。

## 安裝

### Windows

1. 把 `AgentDesk-<版本>-win-x64.zip` 解壓到任何地方，例如 `%LOCALAPPDATA%\Programs\AgentDesk`。不需要系統管理員權限，也不寫登錄檔。
2. `conpty\conpty.dll` 與 `conpty\x64\OpenConsole.exe` 要留在執行檔旁邊。這是隨附的 ConPTY：Windows 內建的版本會丟掉狀態查詢的回覆，少了它 Claude Code 就不會回報狀態，agent-host 也無法單獨更新。
3. 執行 `agent-ui.exe`。它會在背景啟動 `agent-host.exe`；第一次啟動會打開歡迎頁。

執行檔還沒有程式碼簽章，第一次執行可能出現「Windows 已保護您的電腦」。請確認 zip 來自本專案的 GitHub Releases，再選「其他資訊」→「仍要執行」。

### macOS

1. 解壓 `AgentDesk-<版本>-macos-arm64.zip`，把 `AgentDesk.app` 移到「應用程式」。
2. app 是 ad-hoc 簽章，不是 Developer ID，所以第一次開啟要按住 Control 點選 → 打開（或到「系統設定」→「隱私權與安全性」→「仍要打開」）。
3. 打開 AgentDesk。終端機會取得登入 shell 的 `PATH`，所以即使從 Finder 啟動，也找得到用 Homebrew 或 npm 裝的程式。

### 檔案位置

| | Windows | macOS |
|---|---|---|
| 設定與資料 | `%LOCALAPPDATA%\AgentDesk` | `~/Library/Application Support/AgentDesk` |
| 字型、音效、主題、範例 mod | 執行檔旁邊 | `AgentDesk.app/Contents/Resources` |
| git worktree | `%USERPROFILE%\.agentdesk\worktrees` | `~/.agentdesk/worktrees` |

`AGENTDESK_DATA_DIR` 可以改變資料資料夾（可攜用途與測試）。

## 快速上手

1. 新開的面板一開始是 shell。`cd` 到專案資料夾，space 名稱與分支會跟著變。
2. 輸入 `claude` 或 `codex` 啟動 Agent。它需要你的時候，側欄的 Agents 區會標出來並播放提示音。
3. 分割、開分頁與 space：

   | | Windows | macOS |
   |---|---|---|
   | 往右／往下分割 | Ctrl+Shift+O／Ctrl+Shift+E | ⌘D／⇧⌘D |
   | 新分頁／新 space | Ctrl+Shift+T／Ctrl+Shift+N | ⌘T／⌘N |
   | 命令面板 | Ctrl+Shift+P | ⇧⌘P |
   | 跳到 space、分頁或 Agent | Ctrl+Shift+K | ⌘P |
   | 所有快捷鍵 | Ctrl+Shift+/ | ⌘/ |
   | 設定 | Ctrl+, | ⌘, |

4. 關掉視窗，工作照常執行。再打開 AgentDesk 就會接回來。

## agentctl

```sh
agentctl run --profile claude-native --name review --cwd ~/src/app
agentctl list --json
agentctl read review --lines 100
agentctl send review --text "run the tests" --enter
agentctl wait review --until idle
agentctl stop review --interrupt        # 停不下來再用 --force
agentctl update                         # 更換 agent-host，終端機繼續執行
agentctl --skill                        # 給在 AgentDesk 裡執行的 Agent 看的說明
```

加上 `--json` 時，stdout 只有結果。退出碼：0 成功、2 參數錯誤、3 連不上 host、4 找不到或名稱歧義、5 被拒絕、6 啟動失敗、7 逾時。`send` 沒加 `--enter` 就不會按 Enter。

## 從原始碼建置

### Windows

工具：Visual Studio 2022 Build Tools 17.14（MSVC 14.44、Windows SDK 10.0.26100.0）、CMake 3.25 以上、Ninja、manifest 模式的 vcpkg（`C:\vcpkg`，triplet `x64-windows-static-md`）與 Git for Windows。

`scripts\build.cmd` 會進入 VS 2022 x64 環境、設定 `VCPKG_ROOT` 與英文編譯訊息，再執行 CMake preset：

```bat
scripts\build.cmd msvc-x64-debug all          :: configure + build + ctest
scripts\build.cmd msvc-x64-debug build        :: 只建置
scripts\build.cmd msvc-x64-release configure  :: 新的 preset 要先 configure
```

新 clone 的專案需要隨附 ConPTY：先建置一次，執行 `build\msvc-x64-debug\tools\fetch_conpty.exe`（它下載固定版本並核對 SHA-256），再重新 configure。建置前 `build.cmd` 會結束從該建置資料夾啟動的 `agent-ui` 與 `agent-host`，因為它們鎖住執行檔；設定 `AGENTDESK_KEEP_HOST=1` 可略過。發布用的 zip 在 Release 建置後由 `agentdesk_package.exe` 產生（[docs/release.md](docs/release.md)）。

### macOS

工具：Xcode Command Line Tools、CMake 3.25 以上、Ninja，以及 clone 到 `~/vcpkg` 並 bootstrap 過的 vcpkg。preset 使用 overlay triplet `cmake/triplets/arm64-osx-agentdesk.cmake`（最低 macOS 13）。不需要隨附 ConPTY：Unix pty 會直接放行狀態查詢的回覆。

```sh
scripts/build.sh clang-macos-arm64-debug all     # configure + build + ctest
scripts/build.sh clang-macos-arm64-debug build
scripts/package_macos.sh                         # Release 建置、AgentDesk.app 與 dist/*.zip
```

`agent-ui --screenshot out.png --exit-after-ms N` 會寫出最後一幀，不需要螢幕錄製權限就能自動檢查畫面。

### 測試

透過 ctest 執行 GoogleTest。label：兩個平台都有 `unit`，Windows 另有 `windows_integration`，macOS 另有 `posix_integration`。

```sh
ctest --test-dir build/<preset> -L unit
build/<preset>/tests/unit_tests --gtest_filter='VtEngine.*'
```

所有程式都把警告當錯誤編譯（`/W4 /permissive- /WX`，或 `-Wall -Wextra -Wpedantic -Werror`）。格式與靜態分析使用 `.clang-format` 與 `.clang-tidy`。

## 原始碼結構

兩個平台共用一份原始碼。共用的程式沒有平台 `#ifdef`：不同的部分放在小的內部介面（`*_os.hpp`）後面，Windows 的實作在 `*_win.cpp` 或 `win/`，macOS 的在 `*_posix.cpp` 或 `macos/`，由各資料夾的 CMakeLists 挑選。系統能力走 `libs/platform` 的共用介面，ctest 會檢查共用檔案不依賴任何一個系統。

| 路徑 | 內容 |
|---|---|
| `apps/desktop` | agent-ui：共用的視窗內容；Win32／DX11 外殼（`win/`）與 AppKit／Metal 外殼（`macos/`） |
| `apps/host` | agent-host 的入口（`main_win.cpp`、`main_posix.cpp`） |
| `apps/cli` | agentctl：共用的指令，加上 `cli_win.cpp`／`cli_posix.cpp` |
| `libs/platform` | 平台層：共用介面、Windows 實作（ConPTY、Job、Named Pipe）與 POSIX 實作（pty、posix_spawn、Unix socket） |
| `libs/terminal` | `VtEngine`（libvterm 封裝、快照、歷史、按鍵編碼）、OSC 7501 解析 |
| `libs/protocol`、`libs/ipc` | IPC 分幀、訊息、錯誤碼、畫面編碼；本機通道與 ssh 轉接 |
| `libs/host` | `Session` actor、`SessionManager`（協定）、資料保存、git 工具、agent-host 交接 |
| `libs/ui` | `TerminalView`、`Workspace`、版面引擎、`Keymap`、`ModHost`、字型、連結、更新、使用統計 |
| `libs/modapi`、`mods/` | mod 介面（純 C 的 `mod_api.h`、header-only 的 `mod_sdk.hpp`）與三個範例 mod |
| `tools/` | 開發、量測與打包工具（不出貨） |
| `tests/` | GoogleTest：共用的單元測試、各平台的單元測試、Windows 與 POSIX 整合測試、fixture |
| `third_party/` | libvterm、隨附 ConPTY 的授權、字型、修改過的 ImGui macOS backend、herdr-app 的授權與素材 |
| `docs/`、`contracts/`、`sprint/`、`.decisions/` | 文件、契約、Sprint 紀錄與流程決策 |

## 文件

| 文件 | 內容 |
|---|---|
| [docs/user-guide.md](docs/user-guide.md) | 使用說明 |
| [contracts/approved/PRD.md](contracts/approved/PRD.md) | 產品需求與驗收條件 |
| [docs/design/AgentDesk_Design.md](docs/design/AgentDesk_Design.md) | 詳細設計：架構、協定、host、UI、平台差異；決策紀錄在第 11 章 |
| [docs/design/platform-abstraction.md](docs/design/platform-abstraction.md) | 平台層與 macOS 移植 |
| [docs/mods.md](docs/mods.md) | 撰寫 mod：介面、相容性、安全（英文） |
| [docs/integrations/agent-hooks.md](docs/integrations/agent-hooks.md) | 安裝 Agent 狀態 hook |
| [docs/release.md](docs/release.md) | 發布流程 |
| [docs/compatibility.md](docs/compatibility.md) | 相容性矩陣 |
| [CLAUDE.md](CLAUDE.md) | 給 Claude Code 的專案指南：指令、架構、不變式 |

## 隱私與網路

AgentDesk 沒有預設伺服器。只有設定 `AGENTDESK_RELEASES_URL` 才會檢查更新；只有設定 `AGENTDESK_TELEMETRY_URL` 才會送出匿名的每日使用統計（設定 › Privacy 會逐字顯示要送的內容，也可以關閉；設定 `DO_NOT_TRACK` 同樣會關閉）。意見回饋只能複製或存成檔案，不會送出。診斷紀錄不會包含你輸入的內容。

## 授權

AgentDesk 以 MIT License 釋出，見 [LICENSE](LICENSE)。

第三方元件（libvterm、ConPTY、字型、lunasvg、ImGui macOS backend）的來源、版本與授權寫在各自的 `third_party/*/README.agentdesk.md`。發布包裡的 `THIRD_PARTY_NOTICES.md` 收錄所有隨附元件的授權聲明。

## 致謝

AgentDesk 的介面與行為依 [herdr-app](https://github.com/herdrdev/herdr-app)（Apache-2.0）的原始碼移植成 C++。它的授權見 [third_party/herdr/LICENSE](third_party/herdr/LICENSE) 與發布包的 THIRD_PARTY_NOTICES。AgentDesk 是獨立專案，與 herdr 沒有隸屬或合作關係。
