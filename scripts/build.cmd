@echo off
rem Usage: scripts\build.cmd [preset] [configure|build|test|all]
rem Enters the VS 2022 Build Tools x64 environment, then runs CMake presets.
setlocal
set PRESET=%1
if "%PRESET%"=="" set PRESET=msvc-x64-debug
set STEP=%2
if "%STEP%"=="" set STEP=all

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
  echo [build] Visual Studio C++ tools not found 1>&2
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not defined VCPKG_ROOT set "VCPKG_ROOT=C:\vcpkg"
rem English compiler diagnostics regardless of the OS UI language.
set VSLANG=1033
rem VSLANG only works when VS has the English language pack; otherwise cl prints its
rem "Note: including file:" prefix localized, in the console code page. Ninja matches it byte for
rem byte against the prefix CMake recorded at configure time, so a different code page (PowerShell
rem often runs in 65001) silently drops every header dependency. Always use the system OEM page.
for /f "tokens=3" %%c in ('reg query HKLM\SYSTEM\CurrentControlSet\Control\Nls\CodePage /v OEMCP 2^>nul ^| findstr OEMCP') do chcp %%c >nul

pushd "%~dp0.."
if /i "%STEP%"=="configure" goto configure
if /i "%STEP%"=="build" goto build
if /i "%STEP%"=="test" goto test

:configure
cmake --preset %PRESET% || goto fail
if /i "%STEP%"=="configure" goto done
:build
rem A running agent-host (and agent-ui) from this build folder holds its exe and conpty.dll open,
rem and the build cannot replace them: end those first. Only processes started from
rem build\<preset>\ stop; an installed AgentDesk elsewhere keeps running. Ending agent-host ends
rem the terminals it runs. set AGENTDESK_KEEP_HOST=1 to skip this.
if "%AGENTDESK_KEEP_HOST%"=="1" goto build_run
powershell -NoProfile -NonInteractive -Command "$d = (Join-Path (Get-Location) 'build\%PRESET%') + '\'; $p = Get-Process agent-ui, agent-host -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($d, [StringComparison]::OrdinalIgnoreCase) }; foreach ($x in $p) { Write-Host ('[build] ending ' + $x.Name + ' (pid ' + $x.Id + ')'); Stop-Process -Id $x.Id -Force }; if ($p) { Start-Sleep -Milliseconds 800 }"
:build_run
cmake --build --preset %PRESET% || goto fail
if /i "%STEP%"=="build" goto done
:test
ctest --preset %PRESET% || goto fail
:done
popd
exit /b 0
:fail
set RC=%ERRORLEVEL%
popd
exit /b %RC%
