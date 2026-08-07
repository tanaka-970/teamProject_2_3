# RePlayEngine Shader Rebuild Phase 5 実装報告

## 実装内容

- `MaterialAsset::current_version` を 2 から 3 へ更新
- `shader_guid` を追加
- `Reflection::PropertyBag properties` を追加
- v1 / v2 読み込み時に `shading_model` から組み込み Shader GUID へ移行
- 旧固定フィールドを `prop.*` へ移行
- v3 保存時は常に `REPLAY_MATERIAL 3`
- v3 の未知 Property を PropertyBag に保持して再保存
- Phase 6 まで既存描画を壊さないため、固定フィールドと PropertyBag の互換同期を追加
- `--validate-shader-material` を追加
- `main.cpp`、`.vcxproj`、`.vcxproj.filters` へ登録

## 追加Validation

- v3 Save / Load
- Shader GUID round-trip
- `prop.BaseColor` 等の固定フィールド移行
- 未知Property保持
- PropertyBagから旧固定フィールドへの同期
- v1読み込み
- v1 `shading_model` → PBR GUID移行
- 保存が常にversion 3になること

## Windowsでの確認コマンド

```bat
msbuild 3dgp.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal
echo BUILD=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-material
echo SHADER_MATERIAL=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-builtin
echo SHADER_BUILTIN=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-material
echo MATERIAL_LEGACY=%ERRORLEVEL%
```

## 未確認

この環境にはWindows MSVC / D3D11実行環境がないため、full MSBuildと実機Validationは未実施です。

## 注意

Phase 6の描画接続は変更していません。`shading_model`、既存固定Materialフィールド、現在の描画分岐は互換用に残しています。
