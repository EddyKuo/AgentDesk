# QA-10 (design doc 6.2), UI side: agent-host dies while the UI is open. The UI must show the
# session as disconnected, then, once a new host is running, as Lost; nothing is re-run.
# Isolated pipe and data dir. Evidence: docs\poc\evidence\qa10-ui-host-crash.json
param([string]$BuildDir = (Join-Path $PSScriptRoot '..\..\build\msvc-x64-debug'))
$ErrorActionPreference = 'Stop'
$dir = Join-Path $BuildDir 'apps\desktop'
$ui = Join-Path $dir 'agent-ui.exe'
$ctl = Join-Path $dir 'agentctl.exe'
$probe = (Resolve-Path (Join-Path $BuildDir 'tools\vt_probe.exe')).Path
$pipe = '\\.\pipe\AgentDeskQa10-' + $PID
$data = Join-Path $env:TEMP ('agentdesk-qa10-' + $PID)
New-Item -ItemType Directory -Force $data | Out-Null
@{ schema_version = 1; profiles = @(@{ id = 'counter'; name = 'counter'; executable = $probe; args = @('counter') }) } |
    ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (Join-Path $data 'profiles.json')
$env:AGENTDESK_DATA_DIR = $data
$report = Join-Path $data 'ui.json'

$sid = (& $ctl --pipe $pipe run --profile counter --name victim --cwd $env:TEMP).Trim()
$before = (& $ctl --pipe $pipe list --json | ConvertFrom-Json)
$oldInstance = $before.host_instance_id
$childPid = $before.sessions[0].pid

# UI open for 9 s; the host is killed at ~2 s and a new one started at ~4 s.
$p = Start-Process $ui -ArgumentList @('--pipe', $pipe, '--exit-after-ms', '9000', '--report', "`"$report`"") -PassThru
Start-Sleep -Seconds 2
$hostProc = Get-CimInstance Win32_Process -Filter "Name='agent-host.exe'" | Where-Object { $_.CommandLine -like "*$pipe*" }
Stop-Process -Id $hostProc.ProcessId -Force
Start-Sleep -Seconds 2
$childAlive = $null -ne (Get-Process -Id $childPid -ErrorAction SilentlyContinue)
& $ctl --pipe $pipe list | Out-Null          # auto-starts a new host
if (-not $p.WaitForExit(30000)) { $p.Kill(); throw 'UI did not exit' }
$uiExit = $p.ExitCode
$after = Get-Content $report -Raw -Encoding UTF8 | ConvertFrom-Json
$s = $after.sessions | Where-Object { $_.id -eq $sid }
& $ctl --pipe $pipe shutdown --stop-sessions | Out-Null

$result = [ordered]@{
    checked_at             = (Get-Date).ToUniversalTime().ToString('o')
    ui_exit_code           = $uiExit
    child_killed_with_host = (-not $childAlive)
    ui_reconnected         = $after.connected
    new_host_instance      = ($after.host_instance_id -ne $oldInstance)
    session_marked_lost    = [bool]$s.lost
    session_state          = $s.process_state
    session_not_controller = (-not $s.is_controller)
}
$result.pass = ($uiExit -eq 0) -and $result.child_killed_with_host -and $result.ui_reconnected -and $result.new_host_instance -and $result.session_marked_lost
$out = Join-Path $PSScriptRoot '..\..\docs\poc\evidence\qa10-ui-host-crash.json'
$result | ConvertTo-Json | Set-Content -Encoding UTF8 $out
$result | ConvertTo-Json
Remove-Item -Recurse -Force $data
if (-not $result.pass) { exit 1 }
