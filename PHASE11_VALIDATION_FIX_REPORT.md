# Phase 11 Shader Asset Validation 修正

## 原因

`--validate-shader-asset` の FAIL 1033 は、ランタイム実装ではなく検証側の期待値が誤っていた。

HLSL cbuffer では `float` (4 bytes) の直後へ `float3` (12 bytes) を配置でき、合計 16 bytes に収まる。
既存 `ShaderConstantPacker::AssignOffsets` はこの配置を正しく生成していた。

## 修正

- `ShaderAssetValidation.cpp`
  - `float + float3` の期待サイズを 32 → 16 bytes
  - `float3` の期待offsetを 16 → 4
- `Docs/SHADER_REBUILD_PLAN.md`
  - 同じ誤記を訂正

## 変更していないもの

- `ShaderConstantPacker.cpp`
- Phase 11 lighting model 実装
- Material v3
- HLSL描画コード

## 再検証

```bat
msbuild 3dgp.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal
echo BUILD=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-asset
echo SHADER_ASSET=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-lighting
echo SHADER_LIGHTING=%ERRORLEVEL%
```

両方 0 が期待値。
