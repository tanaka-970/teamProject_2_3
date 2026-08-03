@echo off
chcp 65001 >nul
REM =====================================================================
REM  Release x64 only. Double-click this file.
REM  Debug already passed, so this does NOT rebuild Debug.
REM  ASCII ONLY.
REM =====================================================================
cd /d C:\Users\2250298\Desktop\teamProject_2_3
if not exist Saved\Build mkdir Saved\Build
set "LOG=Saved\Build\verify_release.log"

echo Checking for processes that could hold build files...
echo.
echo --- 3dgp.exe ---
tasklist /fi "imagename eq 3dgp.exe" 2>nul | find /i "3dgp.exe" || echo   none
echo --- fxc.exe ---
tasklist /fi "imagename eq fxc.exe" 2>nul | find /i "fxc.exe" || echo   none
echo --- devenv.exe - Visual Studio ---
tasklist /fi "imagename eq devenv.exe" 2>nul | find /i "devenv.exe" || echo   none
echo --- MSBuild.exe - leftover build nodes ---
tasklist /fi "imagename eq MSBuild.exe" 2>nul | find /i "MSBuild.exe" || echo   none
echo --- dotnet.exe ---
tasklist /fi "imagename eq dotnet.exe" 2>nul | find /i "dotnet.exe" || echo   none
echo.
echo 3dgp.exe listed above means LNK1104 will happen. Close it first.
echo Press a key to continue anyway.
pause >nul

echo Letting file handles settle...
ping -n 11 127.0.0.1 >nul

echo Running Release build and validation. This takes several minutes.
echo Output goes to %LOG%
echo.

cmd /c Tools\_release_inner.bat > "%LOG%" 2>&1

echo.
echo ======================= LOG =======================
type "%LOG%"
echo ===================================================
echo.
echo Log saved to: %LOG%
echo.
pause
