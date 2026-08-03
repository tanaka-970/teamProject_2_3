@echo off
chcp 65001 >nul
setlocal
REM  Called by RUN_RELEASE.bat. Do not double-click directly.
REM  ASCII ONLY.

cd /d "%~dp0.."

where msbuild >nul 2>&1
if not errorlevel 1 goto :have_msbuild

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [STOP] vswhere.exe not found.
  goto :end
)
set "VSPATH="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.Component.MSBuild -property installationPath`) do set "VSPATH=%%I"
if not defined VSPATH (
  echo [STOP] No Visual Studio install with MSBuild found.
  goto :end
)
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 -no_logo
cd /d "%~dp0.."

:have_msbuild
echo ==================================================
echo  Release x64 rebuild
echo ==================================================
msbuild 3dgp.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal "/flp1:LogFile=Saved\Build\csharp_release.errors.txt;errorsonly;verbosity=normal;Encoding=UTF-8" "/flp2:LogFile=Saved\Build\csharp_release.warnings.txt;warningsonly;verbosity=normal;Encoding=UTF-8"
echo CSHARP_RELEASE_BUILD=%ERRORLEVEL%
if errorlevel 1 (
  echo.
  echo [STOP] Release build failed.
  echo        If the error is MSB3061 on DirectXTK .inc files again,
  echo        something is holding those files - do not just retry.
  echo        Report the exact filenames back.
  goto :end
)

echo.
echo ==================================================
echo  Release C# validation
echo ==================================================
start "" /wait x64\Release\3dgp.exe --validate-csharp-scripting
echo CSHARP_RELEASE_VALIDATION=%ERRORLEVEL%
if errorlevel 1 (
  echo [FAIL] Release C# validation failed.
  goto :end
)

echo.
echo RELEASE OK
echo Note: STEP 4 - Visual Studio manual check - still NOT done.

:end
endlocal
