@echo off
rem Drop files or folders here. Each one becomes its own PDF next to the source.
if "%~1"=="" (
  echo Word / PowerPoint no file ya folder wo kono bat ni drop shite kudasai.
  pause
  exit /b 1
)
python "%~dp0convert.py" %*
pause
