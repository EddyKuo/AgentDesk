# AgentDesk 使用說明

AgentDesk 是 Windows 11 x64 的 AI coding agent 工作台：在可分割的終端機面板裡同時跑 Claude Code、Codex CLI 或任何命令列程式，關掉視窗後工作繼續跑，重開時接回原來的版面，並告訴你哪個 Agent 在等你。

## 安裝

1. 解壓 `AgentDesk-<版本>-win-x64.zip` 到任意資料夾，例如 `%LOCALAPPDATA%\Programs\AgentDesk`。不需要系統管理員權限，也不寫登錄檔。
2. 確認 `conpty\conpty.dll` 與 `conpty\x64\OpenConsole.exe` 跟執行檔放在一起。少了它們，Claude Code 的狀態回報會失效（見「限制」）。
3. 先自行安裝並登入要用的 Agent CLI。AgentDesk 不處理登入，也不複製任何 token。

## 畫面

執行 `agent-ui.exe`。它會在背景啟動 `agent-host.exe`，所有工作都跑在 host 裡。

- **Space**：一個工作地點，通常是一個 repo 或資料夾。側欄上半部列出 space，旁邊是 git 分支和最需要注意的狀態。沒取名的 space 以第一個終端機的資料夾命名；雙擊可改名。
- **分頁**：space 裡的一件工作。上方分頁列前面是 space 名稱，每個分頁顯示最緊急的 Agent 狀態。雙擊分頁改名，中鍵關閉。
- **面板**：分頁裡的終端機，可以往右或往下分割、放大成單一面板、拖曳中間的邊界調整大小。標題列顯示程式、資料夾、git 分支與狀態。
- **Agents**：側欄下半部依狀態分組：Needs you、Done、Working、Idle，以及 Shells & other、Stopped。子 Agent 縮排列在啟動它的 Agent 底下。
- **Details**（分頁列右上可開關）：Agent 狀態與來源、訊息、程序與結束碼、資料夾、分支、機器、子 Agent，以及停止與重新啟動按鈕。
- **狀態列**：host 與遠端機器的連線、工作數、正在工作與需要你的數量、終端機大小。

狀態一律以文字加形狀表示，不只靠顏色。沒有可信的狀態來源時顯示 Unknown，不會猜成 Done。**Done** 的意思是「在你沒看的時候完成了」，看過之後變回 Idle。

## 通知

Agent 需要你，或在你沒看的時候完成時，AgentDesk 會播放提示音、在右下角顯示通知；視窗不在前景時，工作列圖示會閃爍。點通知就跳到那個 Agent。可在「設定 > Alerts」關閉音效或通知。

程式剛啟動就以錯誤結束時，通知會附上結束碼和最後幾行輸出。

## 新增工作

按側欄或分頁列的「+」、Ctrl+Shift+T，或用命令面板。選單列出偵測到的 profile（Claude Code、Codex CLI、PowerShell 7、Windows PowerShell、Command Prompt），可直接按數字鍵選擇。新工作預設在目前面板的資料夾；資料夾不存在時會留在選單並顯示原因，不會自動改用別的資料夾。

- 新 space：Ctrl+Shift+N
- 往右或往下分割：Ctrl+Shift+O、Ctrl+Shift+E，或面板標題列的分割按鈕
- git worktree：Ctrl+Shift+G。輸入分支名稱後，AgentDesk 會在 `%USERPROFILE%\.agentdesk\worktrees\<repo>\<分支>` 建立 checkout（可在設定中更改），開成新的 space，並在裡面啟動你選的 Agent。這樣多個 Agent 可以同時改同一個 repo，又不會動到彼此的檔案。

## 快捷鍵

Ctrl+Shift+/ 顯示完整清單，Ctrl+Shift+P 開啟命令面板（命令、Agent、space、主題、版面、mod 命令都能搜尋）。常用的：

| 快捷鍵 | 動作 |
|---|---|
| Ctrl+Shift+T／Ctrl+Shift+N | 新分頁／新 space |
| Ctrl+Shift+O／Ctrl+Shift+E | 往右／往下分割 |
| Ctrl+Shift+Z | 放大或還原面板 |
| Ctrl+Shift+X／Ctrl+Shift+W | 關閉面板／分頁（工作繼續執行） |
| Ctrl+Tab、Ctrl+1～9 | 切換分頁 |
| Alt+1～9、Ctrl+Shift+PgUp/PgDn | 切換 space |
| Alt+H/J/K/L 或 Alt+方向鍵 | 移到左下上右的面板 |
| Alt+Shift+H/J/K/L | 與該方向的面板交換位置 |
| Ctrl+Alt+方向鍵 | 調整面板大小 |
| Alt+↑／Alt+↓ | 上一個／下一個 Agent |
| F2／Shift+F2 | 分頁／space 改名 |
| Ctrl+Shift+F | 在終端機裡搜尋 |
| Ctrl+Shift+L | 切換版面 |
| Ctrl+= ／Ctrl+- ／Ctrl+0 | 放大／縮小／重設整個視窗 |
| Ctrl+, | 設定 |

這些按鍵由 AgentDesk 使用，不會送進終端機裡的程式。要改鍵位，在命令面板執行「Edit keys.json」，例如：

```json
{ "global": { "ctrl-shift-t": "", "ctrl-alt-n": "tab.new" } }
```

值為空字串表示取消綁定。重新啟動 AgentDesk 後生效。

## 終端機操作

- 輸入：一般按鍵、方向鍵、F1～F12、注音等輸入法組字後的文字。
- Ctrl+C：有選取時複製，沒有選取時送中斷。Ctrl+Shift+C 固定複製，Ctrl+Shift+V 貼上。
- 滑鼠：拖曳選取、雙擊選字、三擊選整行，放開就複製（可在設定關閉）；右鍵選單有複製、貼上、全選、搜尋、分割、放大、改名、關閉。
- 捲動：滑鼠滾輪或 Shift+PgUp/PgDn 捲進歷史（最多 5000 行），右側有捲軸可以拖曳。一開始打字就回到即時畫面。
- 搜尋：Ctrl+Shift+F，搜尋畫面與歷史，Enter 往前找，Shift+Enter 往後找，Esc 關閉。
- 貼上多行文字前會先顯示預覽，確認後才送出；含 NUL 或超過 1 MiB 的內容會被拒絕。
- 同一個工作同時只有一個視窗能輸入。其他視窗顯示 Read-only，按「Take control」可明確接手。

## 關閉與停止

| 動作 | 結果 |
|---|---|
| 視窗右上角 X | 只關閉介面，工作繼續在背景執行。第一次會顯示一次說明 |
| 面板或分頁的 × | 只關閉面板，工作不停止，仍列在側欄 Agents；點一下可重新打開 |
| Details > Stop | 送出 Ctrl+C。5 秒後仍未結束會出現「Force stop」 |
| Force stop | 結束整個受管理的程序樹 |
| 側欄下方「Exit and stop all...」 | 列出仍在執行的工作，確認後全部停止並結束 host |

登出、重新開機或 host 異常結束後，原本的工作無法續接，會顯示為 Lost；AgentDesk 不會自動重跑任何 prompt。Lost 或已結束的面板有「Restart here」按鈕，可在同一個資料夾重新啟動同一個 profile。

## 子 Agent

在 AgentDesk 的終端機裡執行 `agentctl run` 時，新的 session 會成為目前 session 的子 Agent：它列在父 Agent 底下，不會自動開成面板。點它可以浮動預覽（peek），按「Open as pane」改開在父 Agent 旁邊。給 Agent 看的使用說明：`agentctl --skill`。

```bat
agentctl run --profile claude-native --name reviewer --cwd C:\work\demo --task "review the diff and report problems"
agentctl wait reviewer --until idle --timeout-ms 600000
agentctl read reviewer --lines 80
:: 在子 Agent 裡回報給父 Agent：文字會送進父 Agent 的輸入，開頭加上 [from reviewer ...]
agentctl report "found 3 problems: ..."
```

`--task` 在子 Agent 就緒（回報 Idle）後才送出；沒有狀態回報的程式則等 8 秒。回報在父 Agent 工作中或等待使用者時先排隊，等它閒下來才送。

## 遠端機器

在「設定 > Machines」加入可用 ssh 連線的電腦（名稱、`user@host` 或 ssh 設定裡的別名、遠端 agentctl 路徑）。AgentDesk 會執行 `ssh <目標> agentctl bridge`，把那台電腦的 space 和 Agent 顯示在同一個側欄，它們在那台電腦上執行，這台電腦休眠時也繼續跑。

需求：遠端是 Windows 並安裝了 AgentDesk，`agentctl` 在 PATH 上（或填完整路徑），並設定好金鑰登入。AgentDesk 不會在背景跳出密碼輸入；需要密碼或要確認主機金鑰時，設定頁會顯示 ssh 的訊息，請先在自己的終端機執行一次 `ssh <目標>`。

## 版面

Ctrl+Shift+L 或命令面板切換：

- **workspace**：預設，側欄＋分頁＋分割面板。
- **board**：Agent 依狀態排成四欄卡片（Needs you、Done、Working、Idle & shells），卡片顯示最後一行輸出。點卡片預覽，雙擊開到 workspace。
- **grid**：目前 space 的所有工作平鋪成即時終端機。
- **focus**：只有終端機，沒有側欄、分頁列和狀態列。

自訂版面放在 `%LOCALAPPDATA%\AgentDesk\layouts\<名稱>.json`：

```json
{ "name": "wall", "main": "grid", "sidebar": false, "tab_bar": false, "status_bar": true, "theme": "ember" }
```

## Mod

Mod 是放在 `%LOCALAPPDATA%\AgentDesk\mods\<名稱>\main.lua` 的 Lua 小程式，可以對 Agent 狀態做出反應、加命令面板項目、在側欄或 Details 加面板、在分頁和 Agent 列加文字。在「設定 > Mods」啟用或停用、看錯誤、重新載入。第一次啟動會寫入三個範例：

- `tab-timer`（預設啟用）：在分頁顯示 Agent 已工作多久。
- `ntfy-notify`：Agent 需要你時推播到手機。在它的 `settings.json` 填入 ntfy.sh 的 topic 後啟用。
- `agent-summary`：側欄面板，顯示需要你、工作中、閒置的數量。

範例：

```lua
-- 有 Agent 需要我時跳通知並切過去
agentdesk.on("agent_state", function(ev)
  if ev.state == "Blocked" then
    agentdesk.notify(ev.title .. " needs you")
    agentdesk.run("agent.focus:" .. ev.session)
  end
end)

agentdesk.panel{ id = "hello", title = "Hello", where = "sidebar", draw = function(ui)
  if ui:button("Count agents") then agentdesk.notify(#agentdesk.sessions() .. " sessions") end
end }
```

可用的函式：`on(事件, fn)`（agent_state、session_added、session_exited、tick）、`sessions()`、`focused()`、`run(命令)`、`send(session, 文字, 是否按 Enter)`、`notify(標題, 內文)`、`exec({程式, 參數...})`、`setting(鍵, 預設值)`、`command(id, 名稱, fn)`、`panel{...}`（`ui:text`、`ui:button`、`ui:separator`、`ui:progress`、`ui:row`、`ui:badge`）、`decorate("tab"|"agent", fn)`。

Mod 在沙盒中執行：不能讀寫檔案、不能執行 shell、不能載入其他程式碼；`exec` 直接啟動程式，不經過 shell。執行太久或反覆出錯的 mod 會被停用，錯誤顯示在設定頁。

## agentctl（命令列）

```bat
agentctl run --profile codex-native --name review --cwd C:\work\demo
agentctl list --json
agentctl read review --lines 100
agentctl send review --text "summarize the diff"          :: 不會自動按 Enter
agentctl send review --enter
agentctl send review --file C:\work\prompt.txt --enter
agentctl stop review --interrupt
agentctl stop review --force
agentctl wait review --until idle|blocked|exited|change --timeout-ms 600000
agentctl label review role=review issue=123
agentctl report "text"                                     :: 在子 Agent 裡回報給父 Agent
agentctl shutdown --stop-sessions
```

- 名稱有重複時會拒絕執行，請改用 `list` 顯示的 session id。
- `--json` 時 stdout 只有結果 JSON，診斷訊息在 stderr。
- 退出碼：0 成功、2 參數錯誤、3 連不到 host、4 找不到或名稱重複、5 操作被拒（例如別的視窗持有輸入、仍有工作在跑時要求 shutdown）、6 啟動失敗、7 逾時（逾時不代表沒執行，請用 `list` 確認）。
- `send` 只是模擬鍵盤輸入。Agent 正在等你確認時送字，可能會被當成回答；自動化請優先使用 Agent 自己的 headless 模式。

## Agent 狀態

| Agent | 狀態來源 | 設定 |
|---|---|---|
| Claude Code 2.1.295 以上 | 終端機狀態協定 OSC 7501 | 不需設定 |
| Codex CLI | hook 呼叫 `agentctl report-state` | 依 `docs\agent-hooks.md` 手動安裝 hook |
| 其他程式 | 無 | 顯示 Unknown |

## 資料位置

`%LOCALAPPDATA%\AgentDesk`：

- `profiles.json`：可自行編輯，新增 profile。
- `sessions.json`：工作紀錄。
- `ui-state.json`：space、分頁與分割，由 host 保存。
- `ui.json`：設定、機器、啟用的 mod 與版面。
- `keys.json`：自訂快捷鍵。
- `mods\`、`layouts\`：mod 與自訂版面。
- `host.log`：只記錄 ID、事件與錯誤碼，不記錄輸入內容。
- `ui-crash.txt`：介面崩潰時的呼叫堆疊；回報問題時請附上。

設定檔損毀時會改用備份；備份也不能用時，損毀檔會改名為 `*.corrupt-<時間>` 保留，並以預設值啟動。

## 限制

- 只支援 Windows 11 x64；Windows 10 未驗證。不支援 WSL 專用整合、多人協作；沒有手機 app。
- 重新開機、登出或 host 異常結束後無法續接原工作（顯示 Lost）。
- 歷史捲動為純文字，最多 5000 行；每個工作的歷史上限 10,000 行或 16 MiB，超過時捨棄最舊的部分。
- 字寬依 libvterm 0.3.3：東亞「模糊寬度」字元一律 1 欄；由零寬連接字組成的 emoji（例如家庭 emoji）不合併，會占多欄；emoji 以單色顯示；不支援 kitty keyboard protocol 與終端機圖片。
- 狀態協定 OSC 7501 的「RIS 清除全部紀錄」規則尚未實作（libvterm 不通知 RIS）。
- 沒有隨附 ConPTY 時，Windows 內建 ConPTY 會吞掉狀態查詢的回覆，Claude Code 因此不會回報狀態。
- 已結束的工作超過 8 個時，較舊的只保留清單紀錄，最後畫面與歷史不再保留。
- 遠端機器需要遠端也是 Windows 並安裝 AgentDesk；Linux／macOS 的 host 尚未提供。
- 介面沒有螢幕閱讀器支援（Dear ImGui 的限制）。
