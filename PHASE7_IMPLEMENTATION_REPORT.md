# RePlayEngine Phase 7 — Shader Asset / Material Inspector 実装レポート

更新日: 2026-08-07

## 目的

Phase 7 は Material Inspector を Shader 駆動へ移行し、以下を一つの経路として成立させる。

- Shader を Asset として作成・登録・選択できる
- Built-in の FbxDefault / PBR / Toon / Unlit / Pixelate を Editor から選択できる
- Project の Custom Surface Shader も Editor C++ の shader 固有分岐なしで選択できる
- ShaderPropertySchema から Material Property UI を自動生成する
- Texture Property は AssetGUID を保存しつつ、人間には Asset Picker / Drag & Drop で操作させる
- Shader 切替、Missing Shader、Save/Reload で Material data を失わない
- Phase 6/12 の b9 / t40+ 実描画経路をそのまま利用する
- 将来の Shader Stack / Shader Composer の土台になる metadata を増やす

## 今回の実装

### 1. MaterialSchema

新規:

- `RePlayEngine/Rendering/Materials/MaterialSchema.h`
- `RePlayEngine/Rendering/Materials/MaterialSchema.cpp`

Material と ShaderPropertySchema の橋渡しを Editor から分離した共通 utility。

主な責務:

- Shader property kind -> Reflection::PropertyType の変換
- Shader default value -> PropertyValue の生成
- Schema に存在するが Material に未登録の Property を default から追加
- 互換型 Property の正規化
- Shader 選択時に同名/未知 Property を保持
- Built-in Shader 選択時だけ legacy `shading_model` を同期

Material の正本は引き続き `shader_guid + PropertyBag`。

### 2. ShaderProperty の UI metadata

`ShaderProperty` に追加:

- `category`
- `tooltip`

`ShaderSource` は property pragma で次を解析可能:

- `category "..."`
- `tooltip "..."`

これにより Editor は Shader 名を hard-code せず、Schema だけでカテゴリ分けや tooltip を表示できる。

### 3. MaterialShaderInspector

新規:

- `RePlayEngine/Editor/ShaderEditing/MaterialShaderInspector.h`
- `RePlayEngine/Editor/ShaderEditing/MaterialShaderInspector.cpp`

機能:

- ShaderCatalog から Surface Shader Picker を自動生成
- Built-in と Project Shader をカテゴリ表示
- PBR / Toon / Unlit / Pixelate / Custom を選択可能
- Float / Range / Float2/3/4 / Color / Texture / Toggle / Enum を Schema から自動描画
- Property category ごとに UI 整理
- tooltip 表示
- Texture Asset Picker
- Project Browser の `REPLAY_ASSET_GUID` Drag & Drop
- Missing Shader 表示 + PropertyBag 保持
- compile failure 時は last successful bytecode の状態を表示
- Advanced に Shader GUID / source / schema revision / retained unknown property 情報

Material Editor 内の旧 hard-coded Base Shader selector は非表示にし、新 Picker を正面の UI とする。

### 4. Material の保存性と Live Preview

`framework_asset_browser.cpp` を更新。

- Inspector 変更時に material cache を更新し Scene へ即時 preview
- Save Material ボタンに dirty 表示
- dirty Material から別 Material に移動する前に自動保存
- shutdown 時も dirty Material を自動保存
- Save 時に legacy compatibility field も同期

目的は「選んだ/触ったのに Save を忘れて全部消えた」を減らすこと。

### 5. Surface Shader Asset Factory

新規:

- `RePlayEngine/Rendering/Shaders/ShaderAssetFactory.h`
- `RePlayEngine/Rendering/Shaders/ShaderAssetFactory.cpp`

`CreateSurfaceShader()` で新しい Surface HLSL Asset を Atomic Create する。

生成内容:

- 新しい `replay_guid`
- `replay_domain surface`
- `replay_lighting unlit`
- display name / category
- BaseColor / BaseMap / AlphaCutoff / DoubleSided Schema
- `#define REPLAY_MATERIAL_PROPERTIES 1`

最後の define により、生成 Shader は Phase 6/12 の b9/t40+ Material Property 経路を使用する。

### 6. Project Browser

更新:

- Create -> `Surface Shader`
- Shader/Materials/Project 以下へ安全に作成
- AssetDatabase へ Shader 登録
- ShaderLibrary を即時再 scan
- 作成直後に Material Shader Picker へ出現
- Shader filter 追加
- Shader の double click / context menu で Visual Studio を開く
- `.hlsl/.fx` rename 後に ShaderLibrary を再 scan

Material の Shader 参照は HLSL 内部の `replay_guid` のため、ファイル rename で Material の shader reference を壊さない。

### 7. Built-in Shader metadata

以下の property pragma に category metadata を追加:

- FbxDefault
- PBR
- Toon
- Unlit
- Pixelate

Shader algorithm 自体は変更していない。

固定 GUID は変更していない:

- FbxDefault `00000000000000000000000000000001`
- PBR `00000000000000000000000000000002`
- Toon `00000000000000000000000000000003`
- Unlit `00000000000000000000000000000004`
- Pixelate `00000000000000000000000000000005`

### 8. 新 Validation

新規 command:

`--validate-shader-editor`

検証内容:

- Built-in 5 shader の Catalog 登録
- PBR / Toon / Unlit / Pixelate の Schema/Picker 候補
- property category / Pixelate range
- Shader 選択時の default property 生成
- Texture が AssetReference GUID 型になること
- PBR -> Toon -> Pixelate -> PBR で値が消えないこと
- unknown property retention
- ShaderGUID / Property / Texture GUID の Material Save/Reload
- Missing Shader GUID/property retention
- Surface Shader Asset の Atomic Create
- 作成 Shader の固定 replay_guid / domain/category/schema
- 作成 Shader が b9/t40+ Material 経路を使うこと
- Custom Shader を Catalog 登録後、Editor C++ の shader 固有処理なしで Material へ選択可能なこと
- Custom Shader Material Save/Reload
- category / tooltip parser

成功時 exit code `0`。

## 意図的にまだ残すもの

- MaterialAsset legacy fixed fields
- runtime compatibility 用 `shading_model`
- 既存 ShaderLayerStack の実装

Phase 7 で Editor の正面 UI は新 Shader Picker に移すが、描画互換経路は急に削除しない。
Shader Stack の asset-driven 化、stage role、multi-pass は Phase 10/16 で行う。

## こちらで実施した確認

Windows/MSVC/D3D11 Editor 実行環境はこの環境にはないため、最終 Acceptance は Windows 実機で必要。

こちらでは以下を確認済み:

- `3dgp.vcxproj` XML parse OK
- `3dgp.vcxproj.filters` XML parse OK
- 新規 source/header の project 登録各1件
- `--validate-shader-editor` main wiring OK
- C++17 `-Wall -Wextra -Wpedantic -fsyntax-only` 対象 source PASS
- syntax warning 0
- Shader parser / ShaderAssetFactory executable test PASS
- MaterialSchema selection/retention executable test PASS
- Built-in 5 shader parser executable test PASS
- Built-in fixed GUID 変更なし

## Windows 実機 Acceptance

最初に:

```bat
cd /d C:\Users\2250298\Desktop\teamProject_2_3

msbuild 3dgp.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal
echo BUILD=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-editor
echo SHADER_EDITOR=%ERRORLEVEL%
```

期待値:

```text
BUILD=0
SHADER_EDITOR=0
```

次に既存 Shader 回帰:

```bat
start "" /wait x64\Debug\3dgp.exe --validate-shader-render
echo SHADER_RENDER=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-texture
echo SHADER_TEXTURE=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-material
echo SHADER_MATERIAL=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-builtin
echo SHADER_BUILTIN=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-asset
echo SHADER_ASSET=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-material
echo MATERIAL_LEGACY=%ERRORLEVEL%
```

すべて `0` を期待。

## Editor 手動 Acceptance

1. Material Asset を開く。
2. Shader Picker に PBR / Toon / Unlit / Pixelate が出る。
3. Toon を選ぶと Toon Schema の項目だけが自動生成される。
4. Pixelate を選ぶと PixelSize / Strength 等が表示される。
5. PBR -> Toon -> PBR と戻し、共通値/保持値が消えないことを確認。
6. Image Asset を Texture field へ Project Browser から Drag & Drop。
7. Material を Save、Editor を終了、再起動し、Shader / Texture / Property が復元することを確認。
8. Project Browser の Create -> Surface Shader で Custom Shader を作成。
9. 作成 Shader が Shader Picker の Project category に即時出現することを確認。
10. Custom Shader を Material に選択して Save/Reload できることを確認。

## 問題が出た場合

- Build failure: 最初の `error C...` / `LNK...` / `error X...` から貼る。
- `SHADER_EDITOR != 0`: Validation の `[FAIL xxxx]` 行を貼る。
- UI/runtime issue: 操作手順 + screenshot + `Saved/Diagnostics/editor_log.txt` の該当部分を貼る。
