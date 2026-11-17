# Downloads Microsoft.Windows.Console.ConPTY (MIT, microsoft/terminal) and places
#   <Dest>\conpty\conpty.dll and <Dest>\conpty\x64\OpenConsole.exe
# Version and SHA-256 values are pinned; nothing "latest" is ever fetched.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\fetch-conpty.ps1 [-Dest third_party\conpty\bin]
param([string]$Dest = (Join-Path $PSScriptRoot "..\third_party\conpty\bin"))
$ErrorActionPreference = "Stop"

$Version = "1.24.260710001"
$Url = "https://github.com/microsoft/terminal/releases/download/v1.24.11911.0/Microsoft.Windows.Console.ConPTY.$Version.nupkg"
$Sha = @{
    "nupkg"                                     = "9382ad7becb7e4d84e300578d8e4f4df28f43d979d9055d978c42913c47e0e9d"
    "runtimes/win-x64/native/conpty.dll"        = "39fba2713e2495117b1591ae8c32a3b904bea7aa66069cf7815e2844c76d75d8"
    "build/native/runtimes/x64/OpenConsole.exe" = "b7fd936c2668b87b9ecf7b3366dc6568afc1c6f981874cba3e955a1c35cf8160"
}

function Assert-Sha256([string]$Path, [string]$Expected) {
    $stream = [IO.File]::OpenRead($Path)
    try {
        $hash = [Security.Cryptography.SHA256]::Create().ComputeHash($stream)
    } finally {
        $stream.Close()
    }
    $actual = -join ($hash | ForEach-Object { $_.ToString("x2") })
    if ($actual -ne $Expected) { throw "$Path sha256 $actual, expected $Expected" }
}

$work = Join-Path ([IO.Path]::GetTempPath()) "agentdesk-conpty-$Version"
$zip = "$work.zip"
if (-not (Test-Path $zip)) {
    Invoke-WebRequest -UseBasicParsing $Url -OutFile "$zip.part"
    Move-Item -Force "$zip.part" $zip
}
Assert-Sha256 $zip $Sha["nupkg"]
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
# .NET directly: Expand-Archive fails on some Windows PowerShell 5.1 installs.
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory($zip, $work)

New-Item -ItemType Directory -Force (Join-Path $Dest "conpty\x64") | Out-Null
$map = @(
    @("runtimes/win-x64/native/conpty.dll", "conpty\conpty.dll"),
    @("build/native/runtimes/x64/OpenConsole.exe", "conpty\x64\OpenConsole.exe")
)
foreach ($pair in $map) {
    $src = Join-Path $work $pair[0]
    Assert-Sha256 $src $Sha[$pair[0]]
    Copy-Item -Force $src (Join-Path $Dest $pair[1])
}
foreach ($legal in @("LICENSE", "LICENSE.txt", "NOTICE.md", "NOTICE.txt")) {
    $p = Join-Path $work $legal
    if (Test-Path $p) { Copy-Item -Force $p (Join-Path $Dest "conpty\$legal") }
}
Remove-Item -Recurse -Force $work
"ConPTY $Version -> $(Resolve-Path (Join-Path $Dest 'conpty'))"
