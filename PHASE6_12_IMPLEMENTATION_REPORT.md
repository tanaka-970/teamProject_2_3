# RePlayEngine Shader Phase 6 + Phase 12 実装報告

## 判定

このパッチは、Phase 5 の `MaterialAsset v3` と Phase 11 の
`#pragma replay_lighting` を、実際の D3D11 描画へ接続する実装です。

**コード実装とLinux上の静的検査は完了しています。**
ただし、MSVC / D3DCompile / D3D11実描画はこの環境では実行できないため、
Windows実機のBuild・Validation・目視確認が終わるまでは
Phase 6 / 12 を最終完了扱いにしません。

## 実装した経路

```text
MaterialAsset v3
  shader_guid + PropertyBag + Texture AssetGUID + LayerStack
        ↓
MaterialBindingResolver
  ShaderCatalog / Static・Skinned variant / Schema を解決
  PropertyBag を b9 用byte列へPack
  texture propertyをt40以降の順へ解決
        ↓
RenderItem
  ResolvedMaterialBindingを1 draw分だけ保持
        ↓
MaterialGpuBinder
  Catalogの成功済みbytecode → ID3D11PixelShader
  b9 → Material constant buffer
  t40+ → Image Asset SRV / default SRV
        ↓
ForwardまたはDeferred GBuffer
```

## Phase 6 — 描画への接続

### MaterialBindingResolver

新規:

- `RePlayEngine/Rendering/Materials/MaterialBinding.h`
- `RePlayEngine/Rendering/Materials/MaterialBinding.cpp`

実装内容:

- `shader_guid`から`ShaderCatalog::Entry`を解決
- `surface` domainだけをMaterialへ許可
- Static / Skinnedの成功済みbytecodeを確認
- 直近Compileが失敗していても、保持された最後の成功bytecodeを使用
- `ShaderConstantPacker`でPropertyBagを定数byte列へ変換
- Texture propertyをSchemaのslot順へ解決
- LayerStackは毎drawコピーせず借用
- Missing ShaderはUnlit + Magentaへフォールバック
- Missing時も元のPropertyBagと未知propertyを変更しない
- 旧`AmbientOcclusionMap`保存名を`OcclusionMap`へ救済

### RenderItem

`RenderItem`へ以下を追加しました。

- `ResolvedMaterialBinding material_binding`
- Catalog shader用`tint`
- 旧`.cso` fallback用`legacy_tint`
- GBuffer用BaseColor / EmissiveColor / Metallic / Roughness / AO
- Pixelate設定

GPU resource自体はRenderItemへ入れていません。

### MaterialGpuBinder

新規:

- `RePlayEngine/Rendering/Materials/MaterialGpuBinder.h`
- `RePlayEngine/Rendering/Materials/MaterialGpuBinder.cpp`

実装内容:

- ShaderCatalogの最後に成功したbytecodeからPixel Shaderを生成・cache
- bytecodeが変わった場合だけPixel Shaderを再生成
- 新しいPS生成に失敗した場合は直前のPSを維持
- Material constant bufferをb9へbind
- Texture Assetをt40以降へbind
- draw間で前のSRVを必ずunbind
- shutdown時にPS / SRV / constant bufferを明示解放
- b9を完全にbindできない場合はCatalog PSを使わず旧`.cso`へ戻る

### Forward描画

GameObjectのForward描画は、Materialが解決できた場合に
CatalogのPixel Shaderと自動生成b9 / t40+を使用します。

失敗時は既存の`skinned_forward_shader(shading_model)`へ戻るため、
Editor全体を黒くしません。

### Deferred描画

Deferred GBufferは既存の固定GBuffer shaderを維持しながら、
Material情報を次の固定bridgeへ渡します。

```text
b9   : 80 bytes GBUFFER_MATERIAL_CONSTANTS
t40  : BaseMap
t41  : NormalMap
t42  : MetallicMap
t43  : RoughnessMap
t44  : EmissiveMap
t45  : OcclusionMap
```

Textureの宣言順ではなくproperty名からbridge slotを決めます。
そのため、Toonの`t41 = RampMap`をNormalMapとして扱いません。

GBufferの80-byte cbufferはC++側に`static_assert`を入れ、
HLSLとのサイズ不一致をBuild時に検出します。

### Material固有Layer

MaterialのLayerStackを、Forward / Deferredの追加パスへ接続しました。

- 宣言順で描画
- disabled layerを描かない
- Alpha / Additive / Multiply
- PBR / Toon / Unlit / Wireframe / StylizedCharacter
- Pixelate
- Outline

PixelateはDeferred時、GBuffer設定へ統合します。

## Phase 12 — Texture AssetGUIDと既定Texture

### AssetDatabase接続

`PropertyBag`に保存されたTexture AssetGUIDを`AssetDatabase`で解決し、
`ImageAsset`からSRVを作ります。

次の場合は理由をEditor logへ出し、既定Textureへ落とします。

- GUIDが見つからない
- Image Assetではない
- ファイルが存在しない
- WIC / DDS読み込みに失敗
- D3D11 slot上限を超えた

### 4種類の1x1既定Texture

起動時に次を作成します。

- white `(1,1,1,1)`
- black `(0,0,0,1)`
- gray `(0.5,0.5,0.5,1)`
- bump `(0.5,0.5,1,1)`

未設定Textureをnullのままにせず、必ず宣言された既定Textureをbindします。
前drawのSRVが別Objectへ漏れる状態を防ぎます。

## Material v3補正

- 新保存名を`prop.OcclusionMap`へ統一
- 旧`prop.AmbientOcclusionMap`も読める
- v3 Save時はPropertyBagを主として旧固定fieldへ同期
- 未知propertyは削除しない

## Missing Shader

Forward:

- Unlit Catalog PS
- b9へMagenta BaseColor
- 元のPropertyBagは保持

Deferred:

- 固定GBuffer bridgeにもMagentaを明示
- `lighting_model = Unlit`

Shaderが戻れば、保存していた元の値から復帰できます。

## 新規Validation

### `--validate-shader-render`

- 実Shader Catalog構築
- PBR Static / Skinned解決
- ShaderID / Schema / bytecode
- `replay_lighting`
- Metallic / Roughnessのb9 Pack
- LayerStackの借用、順序、enabled
- Missing ShaderのUnlit fallback
- Missing ShaderのMagenta b9
- Unknown property保持

### `--validate-shader-texture`

- PBR texture property 6件
- t40以降・昇順・重複なし
- AssetGUID / default名
- GBuffer semantic mask
- 固定bridge t40～t45
- Toon RampMapをNormalMapと誤認しない
- 旧AO保存名の救済
- D3D11 Hardware / WARP device作成
- white / black / gray / bumpの実SRV作成
- 未設定Textureの実bind
- Missing AssetGUIDの診断とdefault fallback
- 失敗Textureを成功cacheへ入れない

## 既存互換として意図的に残したもの

Windows実機Acceptance前に破壊的撤去を行わないため、次は残しています。

- 旧`shading_model`
- 旧`.cso`経路
- `material_override`
- debug static mesh用の`*_per_static[8]`
- 旧Material固定field

新経路が実機で通った後、検索結果を0へしながら段階的に撤去します。
このため、Phase 6計画にある「旧経路の完全削除」はこのパッチ時点では未完了です。

## 現時点の制限

1. GameObjectのMeshRendererも現在は内部で`skinned_mesh`描画経路を通るため、
   Catalog variantはSkinnedを選んでいます。真のstatic RenderItem経路へ分離後に
   `source.skinned`でStatic / Skinnedを選択します。

2. Deferredでは、任意のcustom surface shader本文をそのままGBuffer PSとしては使いません。
   現在接続されるのは`replay_lighting`と標準property semanticです。
   任意のGBuffer出力や複数passは、後続のpass contract / Phase 16で設計が必要です。

3. Textureファイル自体の保存監視・再読込は今回の対象外です。
   Shader source hot reloadと同じ依存追跡へ統合する必要があります。

4. RenderStateのshader宣言化はPhase 15です。

## この環境で行った検査

- 新規4 cppをC++17 `-fsyntax-only -Wall -Wextra -Wpedantic`
- `.vcxproj` XML parse
- `.vcxproj.filters` XML parse
- 新規8ファイルの重複登録0
- HLSLの`#if/#endif`対応確認
- 変更textのNUL / trailing whitespace確認
- 既存ファイルのBOM / CRLF形式維持

未実施:

- MSVC full Rebuild
- 実D3DCompile
- D3D11 Debug Layer
- 実画面比較
- Debug / Release全回帰

## Windows Acceptance

### Build

```bat
cd /d C:\Users\2250298\Desktop\teamProject_2_3

msbuild 3dgp.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal
echo BUILD=%ERRORLEVEL%
```

### Validation

```bat
start "" /wait x64\Debug\3dgp.exe --validate-shader-render
echo SHADER_RENDER=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-texture
echo SHADER_TEXTURE=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-builtin
echo SHADER_BUILTIN=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-material
echo SHADER_MATERIAL=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-lighting
echo SHADER_LIGHTING=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-asset
echo SHADER_ASSET=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-shader-compile
echo SHADER_COMPILE=%ERRORLEVEL%

start "" /wait x64\Debug\3dgp.exe --validate-material
echo MATERIAL_LEGACY=%ERRORLEVEL%
```

### 手動確認

- Forward / Deferredを切り替える
- PBR / Toon / Unlit / Pixelateを確認
- BaseMapをImage Assetへ変更して即時反映を確認
- NormalMapの未設定時に法線が壊れない
- 存在しないTexture GUIDでログが出て描画は継続
- 存在しないShader GUIDでMagentaになる
- 複数ObjectでTextureが前drawから漏れない
- Material固有Layerの順序・無効化・Outline
- D3D11 constant buffer size warningが0
- Live SRV / PixelShader / Bufferが終了時に残らない

## 次のゲート

全Validationが0で、通常描画に重大な差がないことを確認後に、
旧`shading_model` / `.cso` / parallel arraysの撤去へ進みます。
