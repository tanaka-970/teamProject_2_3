@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "Tools\GrowRush\Play-GrowRush.ps1"
if errorlevel 1 pause
