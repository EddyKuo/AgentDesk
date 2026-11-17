# S5-4: smoke test of the portable zip in a clean directory. Uses only the zip's executables
# (vt_probe from the build is the fake agent that reports OSC 7501). Isolated pipe and data dir.
# Evidence: docs\poc\evidence\m4-smoke-zip.json
param([string]$Zip = '', [string]$Probe = '')
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = (Resolve-Path (Join-Path $here '..\..')).Path
if (-not $Zip) { $Zip = Join-Path $root 'dist\AgentDesk-0.1.0-win-x64.zip' }
if (-not $Probe) { $Probe = Join-Path $root 'build\msvc-x64-release\tools\vt_probe.exe' }
$work = Join-Path ([IO.Path]::GetTempPath()) ('agentdesk-smoke-' + $PID)
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory($Zip, $work)
$app = Join-Path $work 'AgentDesk-0.1.0-win-x64'
$data = Join-Path $work 'data'
New-Item -ItemType Directory -Force $data | Out-Null
$cmd = Join-Path ([Environment]::GetFolderPath('System')) 'cmd.exe'
$json = @{ schema_version = 1; profiles = @(
        @{ id = 'probe'; name = 'probe'; executable = $Probe; args = @('report') },
        @{ id = 'cmd'; name = 'cmd'; executable = $cmd; args = @() }) } | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText((Join-Path $data 'profiles.json'), $json, (New-Object Text.UTF8Encoding($false)))
$env:AGENTDESK_DATA_DIR = $data
$pipe = '\\.\pipe\AgentDeskSmoke-' + $PID
$ctl = Join-Path $app 'agentctl.exe'

$r = [ordered]@{ checked_at = (Get-Date).ToUniversalTime().ToString('o'); zip = (Split-Path -Leaf $Zip) }
& $ctl --pipe $pipe run --profile cmd --name shell --cwd $work | Out-Null
$r.host_autostarted = ($LASTEXITCODE -eq 0)
& $ctl --pipe $pipe run --profile probe --name agent --cwd $work | Out-Null
Start-Sleep -Seconds 3
$list = & $ctl --pipe $pipe list --json | ConvertFrom-Json
$agent = $list.sessions | Where-Object { $_.name -eq 'agent' }
$r.osc7501_state = $agent.agent_state
$r.osc7501_last_state = $agent.last_agent_state  # probe exits after reporting: state is Unknown, last kept
$r.osc7501_source = $agent.agent_source
$hostLog = Get-Content (Join-Path $data 'host.log') -Raw
$r.bundled_conpty = $hostLog -match 'conpty=bundled'
$hostProc = Get-CimInstance Win32_Process -Filter "Name='agent-host.exe'" | Where-Object { $_.CommandLine -like "*$pipe*" }
$r.host_from_zip = $hostProc.ExecutablePath -like "$app*"

$report = Join-Path $work 'ui.json'
$p = Start-Process (Join-Path $app 'agent-ui.exe') -ArgumentList @('--pipe', $pipe, '--exit-after-ms', '4000', '--report', "`"$report`"") -PassThru
$p.WaitForExit(30000) | Out-Null
$ui = Get-Content $report -Raw -Encoding UTF8 | ConvertFrom-Json
$r.ui_exit_code = $p.ExitCode
$r.ui_connected = $ui.connected
$r.ui_sessions = @($ui.sessions).Count
$r.ui_tabs = $ui.tabs
$r.sessions_survive_ui_exit = @((& $ctl --pipe $pipe list --json | ConvertFrom-Json).sessions | Where-Object { $_.process_state -eq 'Running' }).Count
& $ctl --pipe $pipe shutdown --stop-sessions | Out-Null
Start-Sleep -Seconds 1
$r.notices_present = (Test-Path (Join-Path $app 'THIRD_PARTY_NOTICES.md')) -and (Test-Path (Join-Path $app 'LICENSE')) -and (Test-Path (Join-Path $app 'conpty\NOTICE.md'))
$r.pass = $r.host_autostarted -and $r.host_from_zip -and $r.bundled_conpty -and ($r.osc7501_last_state -eq 'Blocked') -and ($r.osc7501_source -eq 'osc7501') -and
    ($r.ui_exit_code -eq 0) -and $r.ui_connected -and ($r.ui_sessions -eq 2) -and ($r.sessions_survive_ui_exit -ge 1) -and $r.notices_present
$out = Join-Path $root 'docs\poc\evidence\m4-smoke-zip.json'
[IO.File]::WriteAllText($out, ($r | ConvertTo-Json), (New-Object Text.UTF8Encoding($false)))
$r | ConvertTo-Json
Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
if (-not $r.pass) { exit 1 }
