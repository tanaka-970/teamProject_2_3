@echo off
rem プロファイルを自動で残すエディタ起動。ふだんの起動と挙動は同じで、
rem 120 フレームごとに Saved\Profile へ CSV と trace が出るだけ。
rem 重いと感じた操作をしたあと、その CSV を見れば内訳が分かる。
cd /d "%~dp0"
set REPLAY_AUTO_PROFILE=1
start "" "x64\Release\3dgp.exe"
