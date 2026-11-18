# Agent 狀態 hook 整合

AgentDesk 不會替你修改 `~/.codex` 或 `~/.claude`。以下是手動安裝方式，安裝前請先備份原設定，並與既有的 hooks 合併，不要覆寫。

## 何時需要

| Agent | 狀態來源 | 需要 hook？ |
|---|---|---|
| Claude Code 2.1.295 以上 | OSC 7501（AgentDesk 隨附 ConPTY 時自動生效） | 不需要。hook 只在 OSC 7501 不可用時當備援 |
| Codex CLI 0.155.1 | 不支援 OSC 7501 | 需要，否則狀態一律顯示 Unknown |

AgentDesk 在每個 session 注入 `AGENTDESK_SESSION_ID`、`AGENTDESK_CTL`、`AGENTDESK_PIPE`。hook 只在 `AGENTDESK_SESSION_ID` 與 `AGENTDESK_PIPE` 都存在時回報，所以在 AgentDesk 外面執行同一個 Agent 時不會有任何作用。

## hook 指令

hook 指令是 `agentctl.exe hook`，直接寫在 agentctl 裡（C++），不需要另外的腳本。它從 stdin 讀 Agent 傳來的事件 JSON，透過 named pipe 回報給 agent-host。它不會啟動 agent-host，連不上時 1.5 秒內放棄。下面的設定要填 AgentDesk 解壓縮後的完整路徑，例如 `C:\Tools\AgentDesk\agentctl.exe`。

事件對應：

| Hook 事件 | 回報狀態 |
|---|---|
| `UserPromptSubmit`、`PostToolUse` | working |
| `PermissionRequest` | blocked（kind=permission） |
| `Stop` | idle |
| `Interrupt`（Codex） | idle |

`agentctl hook` 不輸出任何內容、永遠以 0 結束，也不對 `PermissionRequest` 做出核准或拒絕的決定，原本的核准畫面照常出現。

## Codex CLI：`~/.codex/hooks.json`

格式依 Codex 官方 hooks 文件（https://learn.chatgpt.com/docs/hooks ，查閱日期 2026-11-17）。`Interrupt` 預設逾時 1 秒、最多 3 秒。

```json
{
  "hooks": {
    "UserPromptSubmit": [{ "hooks": [{ "type": "command", "commandWindows": "C:\\Tools\\AgentDesk\\agentctl.exe hook", "command": "true", "timeout": 5 }] }],
    "PermissionRequest": [{ "hooks": [{ "type": "command", "commandWindows": "C:\\Tools\\AgentDesk\\agentctl.exe hook", "command": "true", "timeout": 5 }] }],
    "PostToolUse": [{ "hooks": [{ "type": "command", "commandWindows": "C:\\Tools\\AgentDesk\\agentctl.exe hook", "command": "true", "timeout": 5 }] }],
    "Stop": [{ "hooks": [{ "type": "command", "commandWindows": "C:\\Tools\\AgentDesk\\agentctl.exe hook", "command": "true", "timeout": 5 }] }],
    "Interrupt": [{ "hooks": [{ "type": "command", "commandWindows": "C:\\Tools\\AgentDesk\\agentctl.exe hook", "command": "true", "timeout": 3 }] }]
  }
}
```

Codex 會要求在 `/hooks` 檢視並同意非託管的 hook 之後才執行。

## Claude Code：`~/.claude/settings.json`（備援）

只有在 OSC 7501 無法使用時才需要。格式依 skill `shared/domain-coding-agents`；`Stop` 在使用者中斷時不會觸發，這是 Claude Code 官方文件寫明的限制。

```json
{
  "hooks": {
    "UserPromptSubmit": [{ "hooks": [{ "type": "command", "command": "C:/Tools/AgentDesk/agentctl.exe", "args": ["hook"], "timeout": 5, "async": true }] }],
    "PermissionRequest": [{ "hooks": [{ "type": "command", "command": "C:/Tools/AgentDesk/agentctl.exe", "args": ["hook"], "timeout": 5, "async": true }] }]
  }
}
```

`PostToolUse`、`Stop` 依相同格式加入。

## 從舊版的腳本換過來

舊版附的是 `hooks\agentdesk-report-state.ps1`（PowerShell 腳本）。換成上面的 `agentctl.exe hook` 之後，可以刪掉 `%USERPROFILE%\.agentdesk\hooks\agentdesk-report-state.ps1`。

## 驗證

在 AgentDesk 裡啟動 Agent 後執行 `agentctl list --json`，`agent_source` 應為 `report:agentdesk-hook`（Codex）或 `osc7501`（Claude Code）。
