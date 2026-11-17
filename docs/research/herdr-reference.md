# herdr 參考筆記

查閱日期：2026-11-16。來源是 herdrdev 組織的公開 repo（全部 Apache-2.0，Rust）。AgentDesk 只參考 herdr 的設計，不複製程式碼，也不宣稱相容。

## 三個 repo 的分工

| Repo | 角色 | 跟 AgentDesk 的對應 |
|---|---|---|
| herdr | 主產品：TUI 工作台、背景 server、CLI、socket API、插件 | 整體產品形態 |
| herdr-core（`hc`） | 無畫面的核心：持有終端機、OSC 7501 狀態、OpenRPC API、多機 SSH | agent-host.exe |
| herdr-app | 桌面視窗（spaces／tabs／panes、Agents 側欄），終端機跑在 core | agent-ui.exe |

herdr-app 關掉視窗時終端機繼續跑在 core 裡；重開機或 core 崩潰後，保留的 pane 會在原目錄開新 shell，不會恢復原程序。這跟 AgentDesk ADR-07 的立場一致。

## 對設計有直接影響的發現

**1. Windows 內建 ConPTY 不夠用。** herdr-core 的 `scripts/fetch-conpty.ps1` 註解寫明：Windows 內建 ConPTY 會吞掉 hc 回給程式的應答（7501、DA），所以它隨附 Microsoft.Windows.Console.ConPTY 1.24.260710001（`conpty\conpty.dll`、`conpty\x64\OpenConsole.exe`），下載後驗 SHA-256。`platform/windows.rs` 用 LoadLibraryW 載入，找不到才退回系統版本，載入失敗只讓該次啟動失敗。→ 設計文件 3.2、ADR-08。

**2. VT 引擎用 libghostty-vt。** herdr-core 透過 git submodule 引入 ghostty，用 Zig 0.16 建出 libghostty-vt 靜態庫，再以 FFI 呼叫。按鍵編碼（cursor keys、kitty keyboard、modifyOtherKeys）也交給它處理。→ 原本列為第二候選；因使用者要求不另外安裝工具（ADR-11），開發機沒有 Zig，降為備案，只有取得現成建好的庫時才考慮。

**3. OSC 7501 的實際格式。**（2026-11-16 更正：後來查到 Ghostty 原始碼指向公開規格 OSC 7501 Program Status Protocol Revision 0.4，https://www.superlogical.com/rex/docs/build/program-status ，Claude Code v2.1.295 起也原生支援。下面這段是最初從 herdr-core 讀到的內容，規格細節以設計文件 3.6 為準。） 從 `status.rs` 的 encode 函式看到：`ESC ] 7501 ; state=…:id=…:kind=…:progress=…:app=…:title=<base64>:msg=<base64> ESC \`。state 有 idle、working、done、blocked、error，另有 clear。kind 是 permission、question、auth。core 只存程式自己回報的狀態，不從畫面推斷。（原本寫「不是公開規格」，已更正如上。）→ 設計文件 3.6。

**4. 狀態偵測其實大多靠畫面。** herdr（主產品）對 Claude Code、Codex 等多數 Agent 是用「screen manifest」規則比對畫面底部來判斷 idle／working／blocked；它們的 hook 只回報 session 身分，用來重開後續接。原因是這些 hook 沒有涵蓋每一種轉換（中斷、拒絕權限後）。blocked 判斷刻意從嚴，比對不到就退回 idle，文件也提醒 idle 不等於可以代按同意。→ AgentDesk MVP 不做畫面推斷（ADR-09），但要接受 hook 會漏報。

**5. 第三方回報走環境變數＋CLI。** pane 內的程序會繼承 `HERDR_ENV`、`HERDR_PANE_ID`、`HERDR_BIN_PATH`、`HERDR_SOCKET_PATH`。整合方呼叫 `"$HERDR_BIN_PATH" pane report-agent <pane> --source --agent --state --seq`。seq 必須跨重啟遞增（建議用時間戳），herdr 忽略不大於上次的報告。另有 `release-agent` 與 resume 命令回報。→ AgentDesk 的 `AGENTDESK_*` 與 `agentctl report-state`。

**6. 背景程序的啟動方式。** `spawn_detached` 用 `DETACHED_PROCESS` 加 `CREATE_BREAKAWAY_FROM_JOB`，刻意不開新 process group（註解說明這樣會讓它啟動的程式收不到 Ctrl+C），並用 `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` 只繼承 NUL 與 log。breakaway 被拒時留在原 Job。→ 設計文件 2.2。

**7. Named Pipe 設定。** overlapped、`PIPE_REJECT_REMOTE_CLIENTS`、第一個 instance 用 `FILE_FLAG_FIRST_PIPE_INSTANCE` 防搶名，另外設 security attributes。跟設計文件 3.7 一致。

**8. Core 與 holder 分離。** herdr-core 另有 holder 元件持有程式，因此 core 重啟或更新時終端機不中斷。AgentDesk MVP 沒有這層，列入 P2。

## 沒有採用的部分

- SSH 多機、插件／mods、layout 檔、tmux 式 prefix 鍵：不在 MVP 範圍（設計文件 1.3 P3）。
- 畫面規則推斷狀態：見 ADR-09。
- Agent 間互相派工與 wait：P2。

## 原始碼位置（供日後查證）

- herdr-core：`scripts/fetch-conpty.ps1`、`crates/herdr-core/src/platform/windows.rs`、`crates/herdr-core/src/status.rs`、`crates/ghostty-vt/src/lib.rs`、`crates/ghostty-vt/build.rs`、`schema/README.md`
- herdr：`docs/next/website/src/content/docs/add-herdr-support.mdx`、`agents.mdx`、`integrations.mdx`、`concepts.mdx`
