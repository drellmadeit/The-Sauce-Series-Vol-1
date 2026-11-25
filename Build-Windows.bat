@echo off
setlocal EnableExtensions
title The Sauce Suite - native Windows build (Visual Studio)
cd /d "%~dp0"

rem ---- run as administrator (needed to install into C:\Program Files\Common Files\VST3) ----
net session >nul 2>&1
if errorlevel 1 (
  echo Asking Windows for administrator permission to install the plugin...
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)

echo.
echo  ===== The Sauce Suite - native Windows build =====
echo.

rem ---- find Visual Studio (2022 or newer) with the C++ tools ----
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :novs
set "VSPATH="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH goto :novs
echo Using Visual Studio at: %VSPATH%
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
where cmake >nul 2>&1 || goto :nocmake

rem ---- configure + build (downloads JUCE 8.0.15 the first time) ----
echo.
echo [1/4] Configuring (first run downloads JUCE, about 1-2 minutes)...
cmake -S . -B build-msvc -G Ninja -DCMAKE_BUILD_TYPE=Release || goto :fail
echo.
echo [2/4] Compiling with Microsoft Visual C++ (about 5-15 minutes the first time)...
cmake --build build-msvc --parallel || goto :fail

set "VST=build-msvc\TheSauceSuite_artefacts\Release\VST3\The Sauce Suite.vst3"
if not exist "%VST%" goto :fail

rem ---- run the verification tests on THIS PC ----
echo.
echo [3/4] Running the verification tests on this PC...
"build-msvc\SauceTests_artefacts\Release\SauceTests.exe" > build-msvc\test-results.txt 2>&1
set TESTS=%errorlevel%
type build-msvc\test-results.txt | findstr /C:"checks passed" /C:"FAIL"
"build-msvc\SauceHostCheck_artefacts\Release\SauceHostCheck.exe" "%CD%\%VST%" > build-msvc\host-check.txt 2>&1
set HOST=%errorlevel%
type build-msvc\host-check.txt

rem ---- install ----
echo.
echo [4/4] Installing to "%CommonProgramFiles%\VST3"...
if exist "%CommonProgramFiles%\VST3\The Sauce Suite.vst3" rmdir /s /q "%CommonProgramFiles%\VST3\The Sauce Suite.vst3"
xcopy "%VST%" "%CommonProgramFiles%\VST3\The Sauce Suite.vst3\" /E /I /Y /Q >nul || goto :fail
echo Installed.
echo.
if not "%TESTS%"=="0" echo WARNING: some tests failed - see build-msvc\test-results.txt
if not "%HOST%"=="0"  echo WARNING: host check failed - see build-msvc\host-check.txt
echo DONE. Open your DAW, rescan plugins, and add "The Sauce Suite" to your vocal track.
echo (FL Studio: Options ^> Manage plugins ^> Find more plugins)
echo.
pause
exit /b 0

:novs
echo.
echo Visual Studio with C++ tools was not found.
echo Install the free "Visual Studio 2022 Community" from https://visualstudio.microsoft.com/
echo and tick the workload "Desktop development with C++" (CMake is included). Then run this file again.
echo.
pause
exit /b 1

:nocmake
echo.
echo CMake was not found. In the Visual Studio Installer, click Modify and make sure
echo "C++ CMake tools for Windows" is ticked under "Desktop development with C++". Then run this file again.
echo.
pause
exit /b 1

:fail
echo.
echo The build stopped with an error (see the messages above).
echo Take a screenshot of this window and send it to Claude.
echo.
pause
exit /b 1
