# AgentDesk 使用說明

AgentDesk 是 Windows 11 x64 的 AI coding agent 工作台：在可分割的終端機面板裡同時跑 Claude Code、Codex CLI 或任何命令列程式，關掉視窗後工作繼續跑，重開時接回原畫面，並顯示每個 Agent 是否在等你。

## 安裝

1. 解壓 `AgentDesk-<版本>-win-x64.zip` 到任意資料夾，例如 `%LOCALAPPDATA%\Programs\AgentDesk`。不需要系統管理員權限，也不寫登錄檔。
2. 確認 `conpty\conpty.dll` 與 `conpty\x64\OpenConsole.exe` 跟執行檔放在一起。少了它們，Claude Code 的狀態回報會失效（見「限制」）。
3. 先自行安裝並登入要用的 Agent CLI。AgentDesk 不處理登入，也不複製任何 token。

## 第一次使用

執行 `agent-ui.exe`。它會在背景啟動 `agent-host.exe`；之後所有工作都跑在 host 裡。

- **File > New session**：選 profile（Claude Code、Codex CLI、PowerShell 7、Windows PowerShell、Command Prompt，依 PATH 自動偵測）、工作目錄與名稱。工作目錄不存在時會留在表單並顯示原因，不會自動改用別的目錄。
- 左側 **Sessions**：每個工作的程序狀態與 Agent 狀態。狀態一律以文字＋符號表示（`! Blocked` 需要你處理、`* Working`、`- Idle`、`v Done`、`x Error`、`? Unknown`）。沒有可信來源時顯示 Unknown，不會猜測成 Done。
- 點清單裡的名稱可重新打開已關閉的面板。
- 右側 **Details**：程序與結束碼、Agent 狀態來源、訊息、停止按鈕。
- 底部狀態列：host 連線、輸入控制權、終端機大小、歷史截斷、輸出節流。

## 關閉與停止

| 動作 | 結果 |
|---|---|
| 視窗右上角 X | 只關閉介面，工作繼續在背景執行。第一次會顯示一次說明 |
| 面板 X | 只關閉該面板，工作不停止 |
| Details > Stop (interrupt) | 送出 Ctrl+C。5 秒後仍未結束會出現「Force stop」 |
| Force stop | 結束整個受管理的程序樹 |
| File > Exit and stop all sessions | 列出仍在執行的工作，確認後全部停止並結束 host |

登出、重新開機或 host 異常結束後，原本的工作無法續接，會顯示為 Lost；AgentDesk 不會自動重跑任何 prompt。

## 終端機操作

- 輸入：一般按鍵、方向鍵、F1～F12、注音等輸入法組字後的文字。
- Ctrl+C：有選取時複製，沒有選取時送中斷。Ctrl+Shift+C 固定複製，Ctrl+Shift+V 貼上。
- 貼上多行文字前會先顯示預覽，確認後才送出；含 NUL 或超過 1 MiB 的內容會被拒絕。
- 滑鼠滾輪：往上捲進歷史（最新 200 行，純文字）。捲動時以整行為單位選取；一開始打字就回到即時畫面。
- 同一個工作同時只有一個視窗能輸入。其他視窗顯示「Read-only」，按「Take control」可明確接手。

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

`%LOCALAPPDATA%\AgentDesk`：`profiles.json`（可自行編輯，增加 profile）、`sessions.json`（工作紀錄）、`ui.json`、`imgui.ini`、`host.log`（只記錄 ID、事件與錯誤碼，不記錄輸入內容）。設定檔損毀時會改用備份；備份也不能用時，損毀檔會改名為 `*.corrupt-<時間>` 保留，並以預設值啟動。

## 限制

- 只支援 Windows 11 x64；Windows 10 未驗證。不支援 WSL 專用整合、SSH 遠端、多人協作。
- 重新開機、登出或 host 異常結束後無法續接原工作（顯示 Lost）。
- 歷史捲動為純文字、最多最新 200 行；每個工作的歷史上限 10,000 行或 16 MiB，超過時捨棄最舊的部分。
- 字寬依 libvterm 0.3.3：東亞「模糊寬度」字元一律 1 欄；由零寬連接字組成的 emoji（例如家庭 emoji）不合併，會佔多欄；不支援 kitty keyboard protocol。
- 狀態協定 OSC 7501 的「RIS 清除全部紀錄」規則尚未實作（libvterm 不通知 RIS）。
- 沒有隨附 ConPTY 時，Windows 內建 ConPTY 會吞掉狀態查詢的回覆，Claude Code 因此不會回報狀態。
- 同一目錄的多個 Agent 會改到同一批檔案；需要隔離時請使用不同的 git worktree。
- 介面沒有螢幕閱讀器支援（Dear ImGui 的限制）。
- 結束超過 8 個的工作，只保留清單紀錄，最後畫面與歷史會被釋放。
