@echo off
rem Word / PowerPoint wo matomete PDF ni suru wrapper.
rem   convert.bat <file or folder...> [-o outdir] [--merge [out.pdf]] [--skip-existing]
python "%~dp0convert.py" %*
