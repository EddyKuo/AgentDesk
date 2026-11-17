# QA-07 (design doc 6.2): 60 s of silence never turns a state into Done. One session without any
# status source must stay Unknown; one with a trusted Working report must stay Working.
# Evidence: docs\poc\evidence\qa07-silence-60s.json
param([string]$BuildDir = '', [int]$Seconds = 60)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $BuildDir) { $BuildDir = Join-Path $here '..\..\build\msvc-x64-release' }
$ctl = Join-Path $BuildDir 'apps\desktop\agentctl.exe'
$data = Join-Path ([IO.Path]::GetTempPath()) ('agentdesk-qa07-' + $PID)
New-Item -ItemType Directory -Force $data | Out-Null
$cmd = Join-Path ([Environment]::GetFolderPath('System')) 'cmd.exe'
$json = @{ schema_version = 1; profiles = @(@{ id = 'cmd'; name = 'cmd'; executable = $cmd; args = @() }) } | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText((Join-Path $data 'profiles.json'), $json, (New-Object Text.UTF8Encoding($false)))
$env:AGENTDESK_DATA_DIR = $data
$pipe = '\\.\pipe\AgentDeskQa07-' + $PID
$cwd = [IO.Path]::GetTempPath()
& $ctl --pipe $pipe run --profile cmd --name silent --cwd $cwd | Out-Null
& $ctl --pipe $pipe run --profile cmd --name working --cwd $cwd | Out-Null
& $ctl --pipe $pipe report-state working --source qa07 --state working --seq 1 | Out-Null
Start-Sleep -Seconds $Seconds
$list = & $ctl --pipe $pipe list --json | ConvertFrom-Json
$silent = $list.sessions | Where-Object { $_.name -eq 'silent' }
$working = $list.sessions | Where-Object { $_.name -eq 'working' }
& $ctl --pipe $pipe shutdown --stop-sessions | Out-Null
Start-Sleep -Seconds 1
Remove-Item -Recurse -Force $data -ErrorAction SilentlyContinue
$r = [ordered]@{
    checked_at = (Get-Date).ToUniversalTime().ToString('o')
    silence_seconds = $Seconds
    no_source_state = $silent.agent_state
    trusted_state = $working.agent_state
    trusted_source = $working.agent_source
}
$r.pass = ($r.no_source_state -eq 'Unknown') -and ($r.trusted_state -eq 'Working')
$out = Join-Path $here '..\..\docs\poc\evidence\qa07-silence-60s.json'
[IO.File]::WriteAllText($out, ($r | ConvertTo-Json), (New-Object Text.UTF8Encoding($false)))
$r | ConvertTo-Json
if (-not $r.pass) { exit 1 }
