@echo off
rem XPS / XNALara の Generic_Item.mesh を GLB へ変換する薄いラッパー。
rem   convert.bat <入力.mesh か フォルダ> [出力フォルダ] [--fbx] [--no-armature]
python "%~dp0convert.py" %*
