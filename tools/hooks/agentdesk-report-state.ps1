# AgentDesk status hook for agent CLIs (Codex CLI, Claude Code).
# Maps hook events to `agentctl report-state` (design doc 3.6). Install it yourself; AgentDesk
# never edits ~/.codex or ~/.claude for you. See docs/integrations/agent-hooks.md.
#
# Contract with the CLI: read the event JSON from stdin, print NOTHING, always exit 0.
# Printing would be parsed by the CLI (Codex Stop/Interrupt only accept JSON) and a decision
# on PermissionRequest would bypass the user's approval prompt, which AgentDesk must never do.
$ErrorActionPreference = 'SilentlyContinue'
if (-not $env:AGENTDESK_CTL -or -not $env:AGENTDESK_SESSION_ID -or -not $env:AGENTDESK_PIPE) { exit 0 }
if (-not (Test-Path -LiteralPath $env:AGENTDESK_CTL)) { exit 0 }

try { $evt = [Console]::In.ReadToEnd() | ConvertFrom-Json } catch { exit 0 }

$kind = $null
$state = switch ($evt.hook_event_name) {
    'UserPromptSubmit'  { 'working' }
    'PostToolUse'       { 'working' }
    'PermissionRequest' { $kind = 'permission'; 'blocked' }
    'Stop'              { 'idle' }
    'Interrupt'         { 'idle' }      # Codex: the user interrupted the turn
    default             { $null }
}
if (-not $state) { exit 0 }

# seq must grow across restarts: milliseconds since the epoch (design doc 3.6).
$seq = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
$cliArgs = @('report-state', $env:AGENTDESK_SESSION_ID, '--source', 'agentdesk-hook', '--state', $state, '--seq', "$seq")
if ($kind) { $cliArgs += @('--kind', $kind) }

# Never slow the agent down: short timeout, ignore failures (host may be restarting).
# System.Diagnostics.Process directly: Start-Process cannot send stdout and stderr to the same
# NUL target, and anything agentctl prints must stay out of the CLI's view.
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $env:AGENTDESK_CTL
$psi.Arguments = ($cliArgs | ForEach-Object { '"' + ($_ -replace '"', '\"') + '"' }) -join ' '
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
try {
    $p = [System.Diagnostics.Process]::Start($psi)
    if (-not $p.WaitForExit(1500)) { $p.Kill() }
} catch { }
exit 0
