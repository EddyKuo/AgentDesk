# Captures a screenshot of agent-ui.exe running against an isolated host, with one session that
# prints mixed Chinese/ASCII text. Used to check rendering (fonts, CJK widths) without a human.
#   screenshot_ui.ps1 -Out <png> [-BuildDir ...] [-Profile chinese|claude]
param(
    [Parameter(Mandatory)][string]$Out,
    [string]$BuildDir = '',
    [string]$Profile = 'chinese',
    [int]$WaitSeconds = 6,
    [string]$Run = '',
    [string]$Child = ''   # profile:name started as a sub-agent of the first session
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $BuildDir) { $BuildDir = Join-Path $here '..\..\build\msvc-x64-debug' }
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Win {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
}
'@
[Win]::SetProcessDPIAware() | Out-Null
$dir = Join-Path $BuildDir 'apps\desktop'
$cmd = Join-Path ([Environment]::GetFolderPath('System')) 'cmd.exe'
$data = Join-Path ([IO.Path]::GetTempPath()) ('agentdesk-shot-' + $PID)
New-Item -ItemType Directory -Force $data | Out-Null
$claude = (Get-Command claude.exe -ErrorAction SilentlyContinue).Source
$text = 'chcp 65001>nul & echo 中文測試：寬字元 ABC 混排 & echo 第二行 第三欄位 OK & echo ｜全形標點，句號。 & dir /w'
$profiles = @(
    @{ id = 'chinese'; name = '中文測試'; executable = $cmd; args = @() }
)
if ($claude) { $profiles += @{ id = 'claude'; name = 'Claude Code'; executable = $claude; args = @() } }
$ps = Join-Path ([Environment]::GetFolderPath('System')) 'WindowsPowerShell\v1.0\powershell.exe'
$symbols = '[Console]::OutputEncoding = [Text.Encoding]::UTF8; ' +
    "'中文測試：寬字元 ABC 混排，全形標點。'; " +
    "'symbols: ' + [char]0x26A0 + ' ' + [char]0x2387 + ' ' + [char]0x2713 + ' ' + [char]0x23F5 + [char]0x23F5 + ' ' + [char]0x25D0 + ' ' + [char]::ConvertFromUtf32(0x1F500) + ' ' + [char]::ConvertFromUtf32(0x1F9E0); " +
    "'box: ' + [char]0x250C + [string][char]0x2500 * 6 + [char]0x2510 + ' ' + [char]0x2588 + [char]0x2593 + [char]0x2592 + [char]0x2591"
$profiles += @{ id = 'symbols'; name = 'symbols'; executable = $ps; args = @('-NoLogo', '-NoProfile', '-NoExit', '-Command', $symbols) }
# Windows PowerShell 5.1 writes a BOM with -Encoding UTF8; write BOM-less UTF-8 explicitly.
$json = @{ schema_version = 1; profiles = $profiles } | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText((Join-Path $data 'profiles.json'), $json, (New-Object Text.UTF8Encoding($false)))
$env:AGENTDESK_DATA_DIR = $data
$pipe = '\\.\pipe\AgentDeskShot-' + $PID
$ui = Join-Path $dir 'agent-ui.exe'
$report = [IO.Path]::ChangeExtension($Out, '.json')
if ($env:SHOT_CTL_HOST) { & (Join-Path $dir 'agentctl.exe') --pipe $pipe list | Out-Null }
if ($env:SHOT_EXTRA) {  # extra sessions created before the UI starts: "profile:name,profile:name"
    foreach ($e in $env:SHOT_EXTRA.Split(',')) {
        $pn = $e.Split(':')
        & (Join-Path $dir 'agentctl.exe') --pipe $pipe run --profile $pn[0] --name $pn[1] --cwd ([IO.Path]::GetTempPath()) | Out-Null
    }
}
$uiArgs = @('--pipe', $pipe, '--start', $Profile, '--exit-after-ms', (($WaitSeconds + 1) * 1000), '--report', "`"$report`"")
if ($Run) { $uiArgs += @('--run', "`"$Run`"") }
$p = Start-Process $ui -ArgumentList $uiArgs -PassThru -WorkingDirectory $here
if ($Child) {
    Start-Sleep -Seconds 4
    $first = (& (Join-Path $dir 'agentctl.exe') --pipe $pipe list --json | ConvertFrom-Json).sessions[0].id
    $pn = $Child.Split(':')
    & (Join-Path $dir 'agentctl.exe') --pipe $pipe run --profile $pn[0] --name $pn[1] --cwd ([IO.Path]::GetTempPath()) --parent $first | Out-Null
    Start-Sleep -Seconds ([Math]::Max(1, $WaitSeconds - 4))
} else {
    Start-Sleep -Seconds $WaitSeconds
}
$p.Refresh()
$h = $p.MainWindowHandle
[Win]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 500
$r = New-Object Win+RECT
[Win]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.R - $r.L; $hgt = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $hgt
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[Win]::PrintWindow($h, $dc, 2) | Out-Null   # PW_RENDERFULLCONTENT: works when covered, and for DX
$g.ReleaseHdc($dc)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
$p.WaitForExit(15000) | Out-Null
if ($env:SHOT_DEBUG) { $sid = (& (Join-Path $dir 'agentctl.exe') --pipe $pipe list --json | ConvertFrom-Json).sessions[0].id; $rb = [IO.Path]::ChangeExtension($Out, '.read.bin'); cmd /c "`"$(Join-Path $dir 'agentctl.exe')`" --pipe $pipe read $sid --lines 3 > `"$rb`""; Copy-Item (Join-Path $data 'host.log') ([IO.Path]::ChangeExtension($Out, '.host.log')) }
& (Join-Path $dir 'agentctl.exe') --pipe $pipe shutdown --stop-sessions | Out-Null
Start-Sleep -Seconds 1; Remove-Item -Recurse -Force $data -ErrorAction SilentlyContinue
"saved $Out ($w x $hgt)"
