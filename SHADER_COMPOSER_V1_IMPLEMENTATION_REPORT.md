# RePlayEngine Shader Composer v1 — Implementation Report

更新日: 2026-08-07
基準: ユーザー提供の最新 `ui.zip`（Phase 7 / 10 / 16 全 Validation PASS 後のソース）

## 目的

Shader Graph 専用の別 Renderer は作らず、Graph を「普通の ShaderAsset を生成するフロントエンド」にする。

```
.replayshadergraph
    ↓ ShaderComposerGenerator
generated .hlsl
    ↓ 既存 ShaderSource / ShaderLibrary / ShaderCatalog
ShaderAsset
    ↓
Material / Shader Stack / Renderer
```

このため、Composer で作った Shader も HLSL 直書き Shader も同じ保存・Material・Runtime 経路を使う。

## v1 実装範囲

### Graph Asset

- 拡張子: `.replayshadergraph`
- ShaderGUID を永続保存
- Node の種類、位置、値、Property metadata を保存
- Connection を保存
- Atomic Save
- Surface / Layer graph に対応
- Graph は source of truth。generated HLSL は派生物。

### Node

Input / constant:
- UV
- Time
- Normal
- View Direction
- Float
- Color

Material Property:
- Float Property
- Color Property
- Texture Property

Math:
- Add
- Subtract
- Multiply
- Divide
- Lerp
- Saturate
- Power

Effect:
- Fresnel
- UV Scroll
- Noise
- Dissolve

Output:
- Surface Output
  - Base Color
  - Emission
  - Opacity
- Layer Output
  - Color

### Property の自動接続

Graph の Property node は generated HLSL の `#pragma property` になる。
そのため既存の schema-driven Material Inspector がそのまま使用される。

例:

```
Float Property "DissolveAmount"
    ↓
#pragma property range DissolveAmount ...
    ↓
Material Inspector に Slider が自動出現
```

Texture Property も既存の AssetGUID -> t40+ 経路を利用する。

### Editor

Project Browser の Create に追加:

- `Shader Composer (Surface)`
- `Shader Composer (Layer)`

Graph を右クリック / ダブルクリックすると Shader Composer を開く。

Canvas 操作:

- 右クリック: Node追加
- 青い Out をクリック -> Input pin をクリック: 接続
- Input pin を右クリック: 切断
- Node header drag: 移動
- 中ボタン drag: Pan
- Delete: 選択 Node 削除
- Ctrl+S: Graph保存 -> HLSL生成 -> ShaderLibrary再走査 -> Compile

Output node は誤削除を防ぐため保護。

### 保存安全性

- Graph 切替時に dirty graph を自動保存
- Composer window close 時に dirty graph を自動保存
- Engine 終了時にも dirty graph を自動保存
- Autosave は graph data のみ
- HLSL 生成 / Compile は Ctrl+S を明示操作にしている

理由: 作業途中の cycle / 未接続 / 不正 graph を自動 Compile して、最後に成功した Shader を壊さないため。
既存 ShaderLibrary の last-successful bytecode 保持方針も維持する。

### Time

FrameConstants `frame_params.z` を accumulated effect time として使用する。

- x = frame index
- y = delta time
- z = accumulated effect time
- w = reserved

Golden capture 等で `elapsed_time == 0` の間は time を進めない。

## generated HLSL

生成先:

- Surface: `Shader/Materials/Generated/`
- Layer: `Shader/Layers/Generated/`

同名 Graph の衝突を避けるため、生成ファイル名には ShaderGUID の先頭8文字を含める。

Generated HLSL は通常の pragma を持つ:

- replay_guid
- replay_name
- replay_category
- replay_domain
- replay_lighting
- property declarations

したがって ShaderLibrary に特殊処理は追加していない。

## v1 の意図的な制限

Composer v1 の custom Surface Output は **Unlit** のみ。

既存の PBR / Toon / Unlit / Pixelate / Custom HLSL Shader Picker はそのまま使用可能だが、
Graph から PBR / Toon lighting model を生成する機能はまだ入れていない。

また v1 では以下を後段へ回す:

- PBR Output / Toon Output
- Normal 出力
- Vertex Offset
- PostProcess Graph
- Custom HLSL Node
- Undo / Redo
- Copy / Paste
- Node search
- Zoom
- Node preview / live thumbnail
- drag-wire 接続（v1 は Out -> Input の2クリック）

この制限により、最初から巨大な Unity/Unreal Shader Graph クローンにせず、既存 ShaderAsset 基盤との統合を先に固定する。

## Validation

新規:

```
--validate-shader-composer
```

検査内容:

- validation folder
- default graph
- ShaderGUID
- graph save/load roundtrip
- Node / Connection roundtrip
- HLSL generation
- normal ShaderAsset parser で generated HLSL を parse
- Graph Property -> Shader Property Schema
- Static variant compile
- Skinned variant compile
- Layer graph generation / compile
- cycle を明示 error にする
- duplicate exposed property name を拒否
- Time node -> accumulated `frame_params.z`
- Fresnel / Emission Property 生成

## こちらで実施した確認

- `ShaderComposerAsset.cpp`: C++17 `-Wall -Wextra -Wpedantic` syntax PASS
- `ShaderComposerGenerator.cpp`: C++17 `-Wall -Wextra -Wpedantic` syntax PASS
- `ShaderComposerValidation.cpp`: C++17 syntax PASS（D3D type stub）
- `ShaderComposerEditor.cpp`: プロジェクト既存 ImGui 1.80 WIP header を使った C++17 syntax PASS
- 古い ImGui API に合わせ `AddBezierCurve` を使用（`AddBezierCubic` は使わない）
- Graph Save -> Load -> Generate -> ShaderSource parse smoke test PASS
- cycle functional test PASS
- `.vcxproj` / `.vcxproj.filters` XML PASS
- Shader Composer source/header の project registration 重複なし

Windows MSVC / D3DCompile / 実 Editor 操作はこの環境では未実行。

## Project file について

今回の `ui.zip` には `3dgp.vcxproj` / `.filters` が含まれていなかったため、
直前にユーザー実機で Phase 7 / 10 / 16 を通した project file を基準にし、
Composer の新規 4 cpp + 4 header を登録した版を patch に含めている。

## Windows Acceptance

最初に:

```bat
cd /d C:\Users\2250298\Desktop\teamProject_2_3

msbuild 3dgp.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal
echo BUILD=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-composer
echo SHADER_COMPOSER=%ERRORLEVEL%
```

期待値:

```
BUILD=0
SHADER_COMPOSER=0
```

続けて回帰:

```bat
start "" /wait x64\Debug\3dgp.exe --validate-shader-editor
echo SHADER_EDITOR=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-layer
echo SHADER_LAYER=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-pass
echo SHADER_PASS=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-render
echo SHADER_RENDER=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-texture
echo SHADER_TEXTURE=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-material
echo SHADER_MATERIAL=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-builtin
echo SHADER_BUILTIN=%ERRORLEVEL%
```

すべて 0 が目標。

## Manual Acceptance

1. Project Browser -> Create -> Shader Composer (Surface)
2. Graph window が開き default graph が見える
3. Ctrl+S で `Saved / generated / compiled`
4. Material Shader Picker の Project shader に generated shader が出る
5. Float Property を追加し Material Inspector に値が出る
6. Texture Property を Material から設定できる
7. Surface graph を閉じて再度開き、Node位置 / Connection / Property が戻る
8. Engine を終了 -> 再起動して graph が戻る
9. Shader Composer (Layer) を作成
10. Ctrl+S 後、Material Shader Stack の Add Layer 一覧へ generated layer が出る
11. Layer を Material へ追加し保存 -> 再起動して復元する

## 次世代

v1 を実機で固定した後、次は主に:

1. PBR / Toon Output
2. Normal / Vertex Offset
3. PostProcess Graph
4. Undo / Redo + Copy/Paste + Search + Zoom
5. Custom HLSL Node
6. Preview

へ進める。
