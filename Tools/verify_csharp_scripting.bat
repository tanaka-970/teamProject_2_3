@echo off
chcp 65001 >nul
setlocal
REM =====================================================================
REM  C# Scripting - Windows validation runner
REM  ASCII ONLY. Do not add Japanese text to this file:
REM  cmd.exe parses .bat bytes in the OEM codepage (CP932 here) and
REM  UTF-8 multibyte sequences corrupt line parsing.
REM
REM  HOW TO RUN:
REM    Open "x64 Native Tools Command Prompt for VS 2022"
REM    then run: Tools\verify_csharp_scripting.bat
REM  A plain cmd window will fail: msbuild is not on PATH.
REM =====================================================================

cd /d "%~dp0.."
if not exist Saved\Build mkdir Saved\Build

set FAILED=

REM ---------------------------------------------------------------------
REM  Locate MSBuild. Works from a plain cmd window or by double-click.
REM ---------------------------------------------------------------------
where msbuild >nul 2>&1
if not errorlevel 1 goto :have_msbuild

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [STOP] vswhere.exe not found. Visual Studio does not look installed.
  echo        Looked under Microsoft Visual Studio\Installer\vswhere.exe
  goto :end
)

set "VSPATH="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.Component.MSBuild -property installationPath`) do set "VSPATH=%%I"
if not defined VSPATH (
  echo [STOP] vswhere found no Visual Studio install with the MSBuild component.
  echo        Install the "Desktop development with C++" workload.
  goto :end
)

echo Using Visual Studio at: %VSPATH%
if not exist "%VSPATH%\Common7\Tools\VsDevCmd.bat" (
  echo [STOP] VsDevCmd.bat not found under %VSPATH%
  goto :end
)
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 -no_logo
cd /d "%~dp0.."

where msbuild >nul 2>&1
if errorlevel 1 (
  echo [STOP] msbuild still not on PATH after VsDevCmd.
  goto :end
)

:have_msbuild
for /f "usebackq delims=" %%V in (`msbuild -version -nologo 2^>nul`) do set "MSBVER=%%V"
echo MSBuild version: %MSBVER%

echo.
echo ==================================================
echo  STEP 1 - Debug x64 full rebuild
echo ==================================================
msbuild 3dgp.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal "/flp1:LogFile=Saved\Build\csharp_debug.errors.txt;errorsonly;verbosity=normal;Encoding=UTF-8" "/flp2:LogFile=Saved\Build\csharp_debug.warnings.txt;warningsonly;verbosity=normal;Encoding=UTF-8"
echo CSHARP_DEBUG_BUILD=%ERRORLEVEL%
if errorlevel 1 (
  echo.
  echo [STOP] Debug build failed.
  echo        Open Saved\Build\csharp_debug.errors.txt
  echo        and fix the FIRST root error.
  echo        A single-file cl compile is NOT a full build.
  goto :end
)

echo.
echo ==================================================
echo  STEP 2 - C# full validation - Debug
echo ==================================================
start "" /wait x64\Debug\3dgp.exe --validate-csharp-scripting
echo CSHARP_SCRIPTING=%ERRORLEVEL%
if errorlevel 1 set FAILED=%FAILED% csharp-scripting

echo.
echo ==================================================
echo  STEP 3 - existing regression suite - Debug
echo ==================================================
call :run script-core           --validate-script-core
call :run script-lifecycle      --validate-script-lifecycle
call :run script-serialization  --validate-script-serialization
call :run animation-undo        --validate-animation-undo
call :run camera-component      --validate-camera-component
call :run player-speed          --validate-player-speed
call :run runtime-scene         --validate-runtime-scene
call :run scene-flow            --validate-scene-flow
call :run runtime-api           --validate-runtime-api
call :run behaviour             --validate-behaviour

echo.
echo ==================================================
echo  STEP 5 - Release x64
echo ==================================================
msbuild 3dgp.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal "/flp1:LogFile=Saved\Build\csharp_release.errors.txt;errorsonly;verbosity=normal;Encoding=UTF-8" "/flp2:LogFile=Saved\Build\csharp_release.warnings.txt;warningsonly;verbosity=normal;Encoding=UTF-8"
echo CSHARP_RELEASE_BUILD=%ERRORLEVEL%
if errorlevel 1 (
  set FAILED=%FAILED% release-build
  goto :summary
)

start "" /wait x64\Release\3dgp.exe --validate-csharp-scripting
echo CSHARP_RELEASE_VALIDATION=%ERRORLEVEL%
if errorlevel 1 set FAILED=%FAILED% release-csharp-scripting

:summary
echo.
echo ==================================================
echo  SUMMARY
echo ==================================================
if "%FAILED%"=="" (
  echo ALL AUTOMATED CHECKS PASSED
  echo.
  echo STEP 4 - Visual Studio manual check - NOT DONE YET.
  echo See CSHARP_PREVALIDATION_REPORT.md before reporting completion.
) else (
  echo FAILED:%FAILED%
)
goto :end

:run
start "" /wait x64\Debug\3dgp.exe %2
echo %~1=%ERRORLEVEL%
if errorlevel 1 set FAILED=%FAILED% %~1
exit /b 0

:end
endlocal
pause

