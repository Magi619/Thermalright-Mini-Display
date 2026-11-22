@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title Thermalright Mini Display - Native C++ v3.0.8 Build

set "TOOLCHAIN_READY="
set "CL_FOUND="
set "SDK_FOUND="
set "VSWHERE="

echo ============================================================
echo  THERMALRIGHT MINI DISPLAY - NATIVE C++ v3.0.8 BUILD
echo ============================================================
echo.

rem First accept an already-initialized Visual Studio developer prompt.
call :CheckCurrentEnvironment
if defined TOOLCHAIN_READY goto :ToolchainReady

rem Locate vswhere in either standard Installer location.
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined VSWHERE if exist "%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

rem Search all installed VS editions without an overly restrictive component filter.
if defined VSWHERE (
  for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -all -products * -property installationPath`) do call :TryVisualStudio "%%I"
)

rem Check again in case vcvars64 initialized the environment.
call :CheckCurrentEnvironment
if defined TOOLCHAIN_READY goto :ToolchainReady

if not defined CL_FOUND (
  echo ERROR: MSVC C++ compiler cl.exe was not detected.
  echo.
  echo Visual Studio was searched, but no usable x64 C++ toolchain was initialized.
  echo Open Visual Studio Installer ^> Modify ^> Individual components and install:
  echo   MSVC x64/x86 build tools (v143 or newer)
  echo   C++ core desktop features
  echo.
  echo You can also open "x64 Native Tools Command Prompt for VS" and run this file again.
  echo.
  pause
  exit /b 2
)

if not defined SDK_FOUND (
  echo ERROR: MSVC was found, but a usable Windows SDK was not detected.
  echo.
  echo Install a Windows 10 or Windows 11 SDK in Visual Studio Installer.
  echo Required files:
  echo   Include\^<SDK version^>\um\Windows.h
  echo   Lib\^<SDK version^>\um\x64\kernel32.lib
  echo.
  pause
  exit /b 3
)

:ToolchainReady
echo Detected C++ compiler:
where cl.exe
echo.
echo Windows SDK: %WindowsSdkDir% %WindowsSDKVersion%
echo.

if not exist build mkdir build
if not exist publish mkdir publish

echo Extracting the Windows built-in Monitor and Keyboard icon...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "scripts\Extract-WindowsMonitorIcon.ps1" -OutputPath "build\monitor_keyboard.ico"
if errorlevel 1 (
  echo.
  echo ERROR: Could not extract the Windows system icon from Shell32.
  pause
  exit /b 4
)

> build\appicon.rc echo #define IDI_APP_ICON 101
>> build\appicon.rc echo IDI_APP_ICON ICON "build/monitor_keyboard.ico"
rc.exe /nologo /fo build\appicon.res build\appicon.rc
if errorlevel 1 (
  echo.
  echo ERROR: Windows Resource Compiler could not embed the system icon.
  pause
  exit /b 5
)

echo Building optimized x64 native Windows application...
cl.exe /nologo /utf-8 /std:c++17 /EHsc /O2 /GL /Gy /Gw /MT /DNDEBUG /DUNICODE /D_UNICODE /DNOMINMAX /D_WIN32_WINNT=0x0A00 src\main.cpp /Fo:build\ /Fe:build\ThermalrightMiniDisplay.exe /link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF build\appicon.res gdiplus.lib hid.lib setupapi.lib comctl32.lib comdlg32.lib shell32.lib advapi32.lib user32.lib gdi32.lib
if errorlevel 1 (
  echo.
  echo BUILD FAILED. Copy the complete error output above before troubleshooting.
  pause
  exit /b 1
)

copy /y build\ThermalrightMiniDisplay.exe publish\ThermalrightMiniDisplay.exe >nul
if errorlevel 1 (
  echo ERROR: Could not copy the application to the publish folder.
  pause
  exit /b 1
)

echo.
echo BUILD SUCCESSFUL.
echo Output: %CD%\publish\ThermalrightMiniDisplay.exe
echo.
echo The application uses built-in Windows APIs and requires no .NET runtime.
echo Keep the previously working version until this build has been tested with the BA120 display and GIFs.
echo.
pause
endlocal
exit /b 0

:CheckCurrentEnvironment
where cl.exe >nul 2>nul
if errorlevel 1 exit /b 0
set "CL_FOUND=1"
if not defined WindowsSdkDir exit /b 0
if not defined WindowsSDKVersion exit /b 0
call :CheckSdkFiles
if defined SDK_FOUND set "TOOLCHAIN_READY=1"
exit /b 0

:CheckSdkFiles
set "SDKVER=%WindowsSDKVersion%"
if not "%SDKVER:~-1%"=="\" set "SDKVER=%SDKVER%\"
if exist "%WindowsSdkDir%Include\%SDKVER%um\Windows.h" (
  if exist "%WindowsSdkDir%Lib\%SDKVER%um\x64\kernel32.lib" set "SDK_FOUND=1"
)
exit /b 0

:TryVisualStudio
if defined TOOLCHAIN_READY exit /b 0
if exist "%~1\VC\Auxiliary\Build\vcvars64.bat" (
  call "%~1\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
  where cl.exe >nul 2>nul
  if not errorlevel 1 set "CL_FOUND=1"
  if defined WindowsSdkDir if defined WindowsSDKVersion call :CheckSdkFiles
  if defined CL_FOUND if defined SDK_FOUND set "TOOLCHAIN_READY=1"
)
exit /b 0
