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

pushd "%~dp0.."
if /i "%STEP%"=="configure" goto configure
if /i "%STEP%"=="build" goto build
if /i "%STEP%"=="test" goto test

:configure
cmake --preset %PRESET% || goto fail
if /i "%STEP%"=="configure" goto done
:build
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
