@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title Thermalright Mini Display - C++ Toolchain Check
set "VSWHERE="
set "VSFOUND="

echo ============================================================
echo  THERMALRIGHT MINI DISPLAY - C++ TOOLCHAIN DIAGNOSTICS
echo ============================================================
echo.

if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined VSWHERE if exist "%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

if defined VSWHERE (
  echo [Visual Studio installations]
  "%VSWHERE%" -all -products * -property installationPath
  for /f "usebackq tokens=*" %%I in (\`"%VSWHERE%" -all -products * -property installationPath\`) do call :TryVisualStudio "%%I"
) else (
  echo [vswhere.exe] NOT FOUND
)
echo.

echo [MSVC compiler]
where cl.exe 2>nul
if errorlevel 1 echo NOT FOUND after Visual Studio environment initialization
echo.

echo [Linker]
where link.exe 2>nul
if errorlevel 1 echo NOT FOUND in initialized environment
echo.
echo [Windows Resource Compiler]
where rc.exe 2>nul
if errorlevel 1 echo NOT FOUND in initialized environment
echo.

echo [Windows SDK environment]
echo WindowsSdkDir=%WindowsSdkDir%
echo WindowsSDKVersion=%WindowsSDKVersion%
if defined WindowsSdkDir if defined WindowsSDKVersion (
  set "SDKVER=%WindowsSDKVersion%"
  if not "%SDKVER:~-1%"=="\" set "SDKVER=%SDKVER%\"
  if exist "%WindowsSdkDir%Include\%SDKVER%um\Windows.h" (echo Windows.h: FOUND) else (echo Windows.h: NOT FOUND)
  if exist "%WindowsSdkDir%Lib\%SDKVER%um\x64\kernel32.lib" (echo kernel32.lib: FOUND) else (echo kernel32.lib: NOT FOUND)
) else (
  echo SDK environment could not be initialized from any discovered Visual Studio installation.
)
echo.
echo Results indicate whether MSVC and the SDK are both available to the build script.
echo.
pause
endlocal
exit /b 0

:TryVisualStudio
if defined VSFOUND exit /b 0
if exist "%~1\VC\Auxiliary\Build\vcvars64.bat" (
  call "%~1\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
  where cl.exe >nul 2>nul
  if not errorlevel 1 set "VSFOUND=1"
)
exit /b 0
