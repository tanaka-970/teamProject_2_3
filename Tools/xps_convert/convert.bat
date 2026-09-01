@echo off
rem XPS / XNALara / PMX を GLB へ変換する薄いラッパー。
rem   convert.bat 入力パス [出力フォルダ] [--fbx] [--no-armature]
rem 山括弧は cmd がリダイレクトとして解釈するため rem 行にも書かない。
python "%~dp0convert.py" %*
