# QA-05 (design doc 6.2): close the UI, reopen it, and check that the session survived with the
# same PID and an unbroken counter. Uses an isolated pipe and data dir, so it never touches the
# user's real host. Writes evidence JSON to docs\poc\evidence\qa05-ui-detach.json.
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\..\build\msvc-x64-debug'),
    [int]$GapSeconds = 3
)
$ErrorActionPreference = 'Stop'
$ui = Join-Path $BuildDir 'apps\desktop\agent-ui.exe'
$ctl = Join-Path $BuildDir 'apps\desktop\agentctl.exe'
$probe = Join-Path $BuildDir 'tools\vt_probe.exe'
$pipe = '\\.\pipe\AgentDeskQa05-' + $PID
$data = Join-Path $env:TEMP ('agentdesk-qa05-' + $PID)
New-Item -ItemType Directory -Force $data | Out-Null
@{ schema_version = 1; profiles = @(@{ id = 'counter'; name = 'counter'; executable = (Resolve-Path $probe).Path; args = @('counter') }) } |
    ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (Join-Path $data 'profiles.json')
$env:AGENTDESK_DATA_DIR = $data
$r1 = Join-Path $data 'run1.json'
$r2 = Join-Path $data 'run2.json'

# Run 1: the UI starts the host, creates the session, then closes after 4 s (detach).
$p = Start-Process $ui -ArgumentList @('--pipe', $pipe, '--start', 'counter', '--exit-after-ms', '4000', '--report', "`"$r1`"") -PassThru
if (-not $p.WaitForExit(30000)) { $p.Kill(); throw 'UI run 1 did not exit' }
Start-Sleep -Seconds $GapSeconds
# Run 2: reopen the UI without creating anything.
$p = Start-Process $ui -ArgumentList @('--pipe', $pipe, '--exit-after-ms', '3000', '--report', "`"$r2`"") -PassThru
if (-not $p.WaitForExit(30000)) { $p.Kill(); throw 'UI run 2 did not exit' }

$a = Get-Content $r1 -Raw -Encoding UTF8 | ConvertFrom-Json
$b = Get-Content $r2 -Raw -Encoding UTF8 | ConvertFrom-Json
$history = & $ctl --pipe $pipe read counter --lines 200 --json | ConvertFrom-Json
$numbers = @($history.lines | Where-Object { $_ -match '^COUNT (\d+)$' } | ForEach-Object { [int]($_ -replace 'COUNT ', '') })
$gaps = 0
for ($i = 1; $i -lt $numbers.Count; $i++) { if ($numbers[$i] -ne $numbers[$i - 1] + 1) { $gaps++ } }
& $ctl --pipe $pipe shutdown --stop-sessions | Out-Null

$s1 = $a.sessions[0]; $s2 = $b.sessions[0]
$result = [ordered]@{
    checked_at        = (Get-Date).ToUniversalTime().ToString('o')
    same_session_id   = ($s1.id -eq $s2.id)
    same_pid          = ($s1.pid -eq $s2.pid)
    pid               = $s2.pid
    state_after_reopen = $s2.process_state
    same_host_instance = ($a.host_instance_id -eq $b.host_instance_id)
    reconnected_ui_controls_input = $s2.is_controller
    counter_lines_checked = $numbers.Count
    counter_first     = ($numbers | Select-Object -First 1)
    counter_last      = ($numbers | Select-Object -Last 1)
    counter_gaps      = $gaps
}
$result.pass = $result.same_session_id -and $result.same_pid -and $result.state_after_reopen -eq 'Running' -and $gaps -eq 0 -and $numbers.Count -gt 20
$out = Join-Path $PSScriptRoot '..\..\docs\poc\evidence\qa05-ui-detach.json'
$result | ConvertTo-Json | Set-Content -Encoding UTF8 $out
$result | ConvertTo-Json
Remove-Item -Recurse -Force $data
if (-not $result.pass) { exit 1 }
