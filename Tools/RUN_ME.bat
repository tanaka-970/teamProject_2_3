@echo off
chcp 65001 >nul
REM =====================================================================
REM  Double-click this file.
REM  It runs the verification in a CHILD cmd process, so this window
REM  survives even if the inner script dies, and everything is logged.
REM  ASCII ONLY.
REM =====================================================================
cd /d C:\Users\2250298\Desktop\teamProject_2_3
if not exist Saved\Build mkdir Saved\Build
set "LOG=Saved\Build\verify_run.log"

echo Running verification. This takes 10-20 minutes.
echo The window will look frozen. That is expected.
echo Output is being written to %LOG%
echo.

cmd /c Tools\verify_csharp_scripting.bat > "%LOG%" 2>&1
set RC=%ERRORLEVEL%

echo.
echo ======================= LOG =======================
type "%LOG%"
echo ===================================================
echo EXIT_CODE=%RC%
echo.
echo Log saved to: %LOG%
echo Send that file to Claude.
echo.
pause
