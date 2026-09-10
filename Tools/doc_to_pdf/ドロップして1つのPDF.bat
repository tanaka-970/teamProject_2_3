@echo off
rem Drop files or folders here. Everything is merged into one matome.pdf.
if "%~1"=="" (
  echo Word / PowerPoint no file ya folder wo kono bat ni drop shite kudasai.
  pause
  exit /b 1
)
python "%~dp0convert.py" %* --merge
pause
