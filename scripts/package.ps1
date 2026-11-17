# Builds the portable zip (design doc 5.1 M4): dist\AgentDesk-<version>-win-x64.zip
#   agent-ui.exe, agent-host.exe, agentctl.exe, conpty\ (bundled ConPTY, ADR-08),
#   LICENSE, THIRD_PARTY_NOTICES.md, hooks\, docs\
# Symbols go to a separate ...-symbols.zip. Run after: scripts\build.cmd msvc-x64-release all
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build\msvc-x64-release'),
    [string]$OutDir = (Join-Path $PSScriptRoot '..\dist'),
    [string]$Version = '0.1.0'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$bin = Join-Path $BuildDir 'apps\desktop'
foreach ($f in @('agent-ui.exe', 'agent-host.exe', 'agentctl.exe', 'conpty\conpty.dll', 'conpty\x64\OpenConsole.exe')) {
    if (-not (Test-Path (Join-Path $bin $f))) { throw "missing $f in $bin (build Release and run scripts\fetch-conpty.ps1)" }
}

$name = "AgentDesk-$Version-win-x64"
$stage = Join-Path $OutDir $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage, (Join-Path $stage 'conpty\x64'), (Join-Path $stage 'hooks'), (Join-Path $stage 'docs') | Out-Null

foreach ($f in @('agent-ui.exe', 'agent-host.exe', 'agentctl.exe')) { Copy-Item (Join-Path $bin $f) $stage }
Copy-Item (Join-Path $bin 'conpty\conpty.dll') (Join-Path $stage 'conpty')
Copy-Item (Join-Path $bin 'conpty\x64\OpenConsole.exe') (Join-Path $stage 'conpty\x64')
Copy-Item (Join-Path $root 'third_party\conpty\LICENSE') (Join-Path $stage 'conpty\LICENSE')
Copy-Item (Join-Path $root 'third_party\conpty\NOTICE.md') (Join-Path $stage 'conpty\NOTICE.md')
Copy-Item (Join-Path $root 'LICENSE') $stage
Copy-Item (Join-Path $root 'tools\hooks\agentdesk-report-state.ps1') (Join-Path $stage 'hooks')
Copy-Item (Join-Path $root 'docs\user-guide.md') (Join-Path $stage 'docs')
Copy-Item (Join-Path $root 'docs\integrations\agent-hooks.md') (Join-Path $stage 'docs')
Copy-Item (Join-Path $root 'docs\compatibility.md') (Join-Path $stage 'docs')

# Third-party notices are assembled from the shipped dependencies' own license files.
$share = Join-Path $BuildDir 'vcpkg_installed\x64-windows-static-md\share'
$notices = New-Object System.Text.StringBuilder
[void]$notices.AppendLine('# Third-party notices')
[void]$notices.AppendLine()
[void]$notices.AppendLine('AgentDesk (MIT, see LICENSE) includes the following components. GoogleTest is used for testing only and is not shipped.')
$components = @(
    @{ Name = 'libvterm 0.3.3'; Url = 'https://www.leonerd.org.uk/code/libvterm/'; File = (Join-Path $root 'third_party\libvterm\LICENSE') },
    @{ Name = 'Dear ImGui 1.93.0 (docking)'; Url = 'https://github.com/ocornut/imgui'; File = (Join-Path $share 'imgui\copyright') },
    @{ Name = 'nlohmann/json 3.12.0'; Url = 'https://github.com/nlohmann/json'; File = (Join-Path $share 'nlohmann-json\copyright') },
    @{ Name = 'Microsoft.Windows.Console.ConPTY 1.24.260710001'; Url = 'https://github.com/microsoft/terminal'; File = (Join-Path $root 'third_party\conpty\LICENSE') }
)
foreach ($c in $components) {
    [void]$notices.AppendLine()
    [void]$notices.AppendLine("## $($c.Name)")
    [void]$notices.AppendLine()
    [void]$notices.AppendLine("Source: $($c.Url)")
    [void]$notices.AppendLine()
    [void]$notices.AppendLine('```')
    [void]$notices.AppendLine((Get-Content -Raw -Encoding UTF8 $c.File).TrimEnd())
    [void]$notices.AppendLine('```')
}
[void]$notices.AppendLine()
[void]$notices.AppendLine('The ConPTY package also carries Microsoft''s third-party notices: see conpty\NOTICE.md.')
[System.IO.File]::WriteAllText((Join-Path $stage 'THIRD_PARTY_NOTICES.md'), $notices.ToString(), (New-Object System.Text.UTF8Encoding($false)))

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = Join-Path $OutDir "$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [IO.Compression.CompressionLevel]::Optimal, $true)

$symStage = Join-Path $OutDir "$name-symbols"
if (Test-Path $symStage) { Remove-Item -Recurse -Force $symStage }
New-Item -ItemType Directory -Force $symStage | Out-Null
Get-ChildItem $bin -Filter '*.pdb' | Where-Object { $_.Name -match '^(agent-ui|agent-host|agentctl)\.pdb$' } | Copy-Item -Destination $symStage
$symZip = Join-Path $OutDir "$name-symbols.zip"
if (Test-Path $symZip) { Remove-Item -Force $symZip }
[IO.Compression.ZipFile]::CreateFromDirectory($symStage, $symZip)
Remove-Item -Recurse -Force $symStage

$hash = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
"$zip"
"sha256 $hash"
"size $([math]::Round((Get-Item $zip).Length / 1MB, 2)) MB"
