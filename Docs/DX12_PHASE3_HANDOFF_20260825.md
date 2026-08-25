# DX12 Phase 3 引き継ぎ書

最終更新: 2026-08-25  
対象ワークスペース: `C:\Users\2250298\Desktop\teamProject_2_3`  
状態: **Phase 3 PASS。DX12のテクスチャ付きScene3D、Shadow、PostProcess、Editor接続を実機確認済み。**

この文書を、現在のDX12移行作業の事実上の引き継ぎ基準とする。`PHASE3_STATUS.txt`、
`NEXT_STEPS.md`、`SHADOW_RENDERING_HANDOFF.md` は別目的の既存資料であり、DX12 Phase 3全体の
完了判定にはこの文書の「完了条件」を使うこと。

## 1. 現在の結論

次の経路は、Windows実機で動作確認できている。

```
既存 Scene / GameObject / Component
  -> SceneRenderCollector / RenderItem
  -> DX12 Scene3D submission
  -> Static / Skinned geometry
  -> Texture sampling
  -> GBuffer 5枚
  -> Deferred lighting / Forward transparent
  -> DX12 swap chain
```

確認対象は `Saved\Validation\SkinnedShadowTest.roundtrip.replayscene` である。実行時ログでは、
次の内訳を確認している。

```
render_items=4 static=2 skinned=2 mesh_sources=2 skinned_sources=1 texture_sources=1
mesh0=4/6 skin0=25710/25710 light=1/0.50 build=1 draw=1
```

この結果は「コードが存在する」だけではなく、既存ランタイムのSceneからDX12描画提出が発生し、
静的メッシュ・Skinned Mesh・テクスチャ・照明まで同じフレームに到達していることを示す。

次の範囲はPhase 3で受け入れ済みである。

- CSM / Point / Spot ShadowのDX12経路とShadow Skinning
- HDR PostProcess、Editor ImGui、Canvas Preview、Particle、Trail、Landscape
- Debug Layer、GPU-Based Validation、Releaseの完全ヘッドレスValidation

Runtime UIRendererのDX12完全移行とDX11完全撤去は、既存移行計画のPhase 4/5境界として残す。
Phase 3のScene3D受け入れを未完了扱いに戻すものではない。

## 2. 完了済みの実装範囲

### 2.1 DX12 Scene3D描画

- 既存のCPU側Scene / GameObject / Component / RenderItem構造を変更せず、DX12 backendを接続した。
- Static Mesh、builtin Primitive、Skinned Meshを同一のScene3D提出にまとめた。
- Skinned Meshは既存Animatorのcurrent / previous clip blendを利用する。
- Bone Paletteは固定64本・256本にせず、メッシュの実データに必要な長さを確保する。
- Skinned Meshのcurrent / previous boneを保持し、Motion Vector用の前フレーム情報を確保する。
- HLSLはCPPに埋め込まず、`Shader\dx12_*.hlsl` / `Shader\dx12_*.hlsli`へ分離した。
- CPP名に`phase`を含む新規ファイルは作成していない。

主実装:

- `RePlayEngine\Rendering\DX12\D3D12SceneRenderer.cpp`
- `RePlayEngine\Rendering\DX12\D3D12DeviceContext.cpp`
- `RePlayEngine\Rendering\DX12\D3D12DeviceContext.h`
- `Source\app\Runtime\framework_gameobject_scene_rendering_draw.cpp`

### 2.2 GBuffer / Lighting / Texture

- 既存の5枚GBufferの枚数・Formatだけでなく、既存ABIに合わせた。
- `BaseColor.a`のLightingModel、Normalの符号によるReceiveShadow、Material RTの
  AO / Roughness / Metalness / AO Strengthを維持した。
- DeferredとForward Transparentで共通の表示エンコードを使う。
- 線形照明をLDR swap chainへ直接書かず、共通HLSLで表示エンコードする。
- Builtin PrimitiveはMaterial Assetが無い場合に、存在しないShader CatalogのMaterialへ
  誤解決しない。既存のshading modelとPrimitive設定を維持する。

関連シェーダー:

- `Shader\dx12_gbuffer_ps.hlsl`
- `Shader\dx12_lighting_ps.hlsl`
- `Shader\dx12_forward_ps.hlsl`
- `Shader\dx12_lighting_common.hlsli`
- `Shader\dx12_static_vs.hlsl`
- `Shader\dx12_skinned_vs.hlsl`

### 2.3 ShadowのDX12接続

ソース上は、既存CPU側のCSM / LocalShadowAtlasの結果をDX12へ橋渡しする構造がある。

- Directional: 4カスケードのTexture2DArray
- Point / Spot: Local Shadow AtlasのTexture2DArray
- Static / Skinned Shadow Caster PSO
- Alpha Clip用Shadow PS
- Shadow対象のCast Shadow / Receive Shadow
- Skinned Shadow用のBone Palette
- Scene提出のライト行列・slice・shadow flagのDX12定数化

主な実装箇所:

- `Source\app\Runtime\framework_gameobject_scene_rendering_draw.cpp`
- `RePlayEngine\Rendering\DX12\D3D12SceneRenderer.cpp`
- `RePlayEngine\Rendering\Shadows\LocalShadowAtlas.h`
- `Shader\dx12_shadow_static_vs.hlsl`
- `Shader\dx12_shadow_skinned_vs.hlsl`
- `Shader\dx12_shadow_alpha_ps.hlsl`

ここは「実装接続済み」と「受け入れ検証済み」を分ける。現在のテクスチャ付きモデル確認だけで、
CSM・Point・Spotの影品質までPASSにしてはいけない。

### 2.4 Profiler / Runtime lifecycle

- DX12経路でもRenderStatsのBeginFrame / EndFrameを実行する。
- Scene object / component / draw callをDX12描画経路から数える。
- Runtime Worldが準備できた時点でProfile captureを開始する。
- Smoke Test終了通知がProfile captureを先に終了させないようにした。
- Profile benchmarkではEditor復元カメラではなくRuntime cameraを使う。

関連ファイル:

- `Source\app\Runtime\framework_update.cpp`
- `Source\app\Rendering\framework_render.cpp`
- `Source\app\framework_class_application_loop.inl`
- `Source\app\Runtime\mainApplication.cpp`
- `Source\app\Editor\framework_editor_camera.cpp`

### 2.5 PostProcess / Editor / Particle / Landscape

- HDR Scene Targetを最終表示へ接続し、Exposure、Tone Mapping、Bloom、Vignette、FXAAを
  外部HLSLで実行する。
- 現フレームのVelocity、Depth、Normal、Materialと前フレームHDR履歴を使い、TAA、SSAO、
  SSRを最終表示へ接続する。固定明るさを照明側へ加算していない。
- ImGuiのWin32入力とDX12描画を接続し、EditorのAsset TextureをDX12 SRVへ遅延登録する。
  Canvas PreviewはDX12時も既存SceneのUIデータを編集できる。
- ParticleEmitterは既存Component設定からCPU状態を更新し、透明Forward経路へ提出する。
  Trail / Lineはカメラ向きRibbon、Landscapeは既存頂点・IndexをDX12 Meshへ橋渡しする。
- 通常Debugは軽量Debug Layer、GPU-Based Validationは`--validate-dx12-gpu`でのみ有効にする。
  CLI未接続時にInfoQueueが停止せず、検証結果とLive Objectを回収できる。

## 3. 検証結果

### 3.1 ビルド

| 構成 | 結果 | 備考 |
|---|---:|---|
| x64 Debug | PASS | MSBuild成功 |
| x64 Release | PASS | MSBuild成功 |
| vcxproj / filters XML | PASS | XML parse成功 |
| `git diff --check` | PASS | 既存AssetDatabaseの改行警告のみ |

標準ビルドコマンド:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild.exe" `
  "C:\Users\2250298\Desktop\teamProject_2_3\3dgp.sln" `
  /p:Configuration=Debug /p:Platform=x64 /m

& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild.exe" `
  "C:\Users\2250298\Desktop\teamProject_2_3\3dgp.sln" `
  /p:Configuration=Release /p:Platform=x64 /m
```

### 3.2 DX12 headless validation

Release実行ファイルで次を実行し、**149 checks PASS**を確認済み。

```powershell
& "C:\Users\2250298\Desktop\teamProject_2_3\x64\Release\3dgp.exe" `
  --validate-dx12-device
```

検証には初期フレーム、パイプラインフレーム0〜5、Resize後フレーム0〜3を含む。

### 3.3 Debug Layer付き実機描画

Debug Layerを有効にしたProfile benchmarkで、Runtime Sceneのロード、DX12描画、終了、
CSV / Trace出力を確認済み。

最終確認コマンド:

```powershell
& "C:\Users\2250298\Desktop\teamProject_2_3\x64\Debug\3dgp.exe" `
  --dx12-framework `
  --profile-scene "Saved\Validation\SkinnedShadowTest.roundtrip.replayscene" `
  --warmup 4 --frames 6 --out dx12_phase3_debug_shutdown_warm `
  --capture-frame dx12_phase3_debug_shutdown_warm --validate-shutdown
```

実測結果はShadow統計、撮影RESULT OK、D3D11 Live Object 0行、DXGI Live Object 0行である。
生成物:

- `Saved\Golden\dx12_final_contract.png`
- `Saved\Profile\dx12_final_contract.csv`
- `Saved\Profile\dx12_final_contract.trace.json`

CSVの実測値には、少なくとも次が含まれる。

```
draw_calls=4
objects=6
components=12
culling_tested=4
culling_visible=4
```

画像にはGround Plane、Cube、テクスチャ付きSkinned humanoid 2体が写っている。
使用テクスチャは次である。

```
resources\AnimationModel\AllAnimation1.fbm\spaceMan_basecolor.PNG
```

### 3.4 Shadow実機受け入れの更新

2026-08-25にRelease DX12経路で、Profile Sceneを直接指定して撮影した。

| 経路 | 結果 | 実績 |
|---|---:|---|
| Directional CSM + Skinned caster | PASS | `Saved\Golden\dx12_phase3_release_final_SkinnedShadowTest.roundtrip.png` |
| Point / Spot Local Shadow | PASS | `Saved\Golden\dx12_phase3_release_final_LocalShadowTest.roundtrip.png` |
| Model Effectを含むSceneの描画 | PASS | `Saved\Golden\dx12_phase3_release_EffectShadowTest.png` |
| Static / Skinned / GLB混在Scene | PASS | `Saved\Golden\dx12_phase3_release_final_AllFormatShadowTest.roundtrip.png` |

同時に、DX12経路で既存D3D11用の`shadow_stats`が更新されず0表示になる計測不整合を修正した。
修正後のShadowTestの診断値は次のとおり。

```
directional=1 rendered=1 casters(prim=7 static=0 skinned=1 landscape=0)
skipped=2 draws=88 spot=1 point=1
```

LocalShadowTestでは次を確認した。

```
directional=1 rendered=0 casters(prim=6 static=0 skinned=0 landscape=0)
skipped=1 draws=42 spot=1 point=1
```

### 3.5 Debug / GPU Validation

次の3経路を同じ最新ソースで確認済みである。

```text
Release --validate-dx12-device : 149 checks PASS
Debug   --validate-dx12-device : gpu_validation=0, 149 checks PASS
Debug   --validate-dx12-gpu    : gpu_validation=1, 149 checks PASS
```

InfoQueueはデバッガ接続時だけErrorで停止し、CLIではメッセージを収集して最後まで完走する。

## 4. 次担当がPhase 4以降で行うこと

優先順は「描画経路の正しさ」「検証の再現性」「機能の拡張」の順とする。

### P0: ShadowのDX12受け入れを維持する

CSM、Skinned caster、Point、Spot、Effect、Resize、終了時のShadow resource解放は上記の
実機撮影と149 checksで確認済み。今後は仕様変更時の回帰確認として同じScene群を使う。

各ケースを個別Sceneで撮影し、DX11または既存CPU影経路と比較する。

1. Directional CSM 4カスケード
2. Point Shadow 6面
3. Spot Shadow
4. Static caster
5. Skinned caster（アニメーション中）
6. Cast Shadow=false / Receive Shadow=false
7. Alpha Clip材質
8. 同一フレームのcurrent / previous boneで影が破綻しないこと
9. 影付きライトが0件、上限超過、slice未割当時の安全なfallback
10. Resizeと終了時のShadow resource解放

最低限、次の既存Sceneを使う。

- `resources\Scenes\ShadowTest.replayscene`
- `resources\Scenes\LocalShadowTest.replayscene`
- `resources\Scenes\SkinnedShadowTest.replayscene`
- `resources\Scenes\AllFormatShadowTest.replayscene`
- `resources\Scenes\EffectShadowTest.replayscene`

影が見えない場合は、すぐに光量や表示倍率を変更しない。先にCPU診断値、caster数、slice、
shadow resourceの有無、Shadow Mapの中身を確認する。既存影の詳細は
`SHADOW_RENDERING_HANDOFF.md`を参照する。

### P1: DX12の受け入れコマンドを固定する

- Release headless validationを毎回149 checksで再実行する。
- Debug Layer付きProfileでCSV / Trace / PNGを再生成する。
- `--validate-shutdown`をProfileと同時指定し、十分なwarmup/frame数で終了時のresource leakを確認する。
- `--capture-frame`では起動シーンを変更する前に`Saved\EditorSession\session.ini`を退避する。
- Sceneを切り替えた後は、保存データが意図せず変更されていないか`git status`で確認する。

Profileの形式:

```powershell
& "C:\Users\2250298\Desktop\teamProject_2_3\x64\Debug\3dgp.exe" `
  --dx12-framework `
  --profile-scene "Saved\Validation\SkinnedShadowTest.roundtrip.replayscene" `
  --warmup 60 --frames 4 --out dx12_handoff_profile
```

### P2: Phase 3後の境界

次は既存計画のPhase 4/5として扱う。

- Runtime UIRendererのText、Mask、EffectをDX12へ完全移行する
- DX11依存の完全撤去とプロジェクト参照整理
- Phase 3後の追加Material、複数カメラ、複数Sceneを製品仕様として拡張する

Phase 3で受け入れたPostProcess、Editor ImGui、Particle、Trail、Landscapeを後戻りさせない。

## 5. 変更箇所の地図

### DX12 backend

- `RePlayEngine\Rendering\DX12\D3D12SceneRenderer.cpp`
- `RePlayEngine\Rendering\DX12\D3D12DeviceContext.cpp`
- `RePlayEngine\Rendering\DX12\D3D12DeviceContext.h`

### Runtimeからの提出

- `Source\app\Runtime\framework_gameobject_scene_rendering_draw.cpp`
- `Source\app\Runtime\framework_gameobject_scene_runtime.cpp`
- `Source\app\Runtime\framework_update.cpp`
- `Source\app\Runtime\mainApplication.cpp`

### Render / profile / camera

- `Source\app\Rendering\framework_render.cpp`
- `Source\app\Editor\framework_editor_camera.cpp`
- `Source\app\framework_class_application_loop.inl`
- `Source\app\Editor\framework_golden.cpp`

### Shader

`Shader\dx12_*.hlsl` / `Shader\dx12_*.hlsli`を正本とする。CPPへHLSL文字列を戻さない。
新しいShaderを追加したら、`3dgp.vcxproj`と`3dgp.vcxproj.filters`への登録、Debug / Releaseの
コンパイル、実機描画を同じ変更単位で行う。

## 6. 壊してはいけない設計上の前提

- 既存GameObject / Component / RenderItem / Animatorを作り直さず、CPU側の既存データを正本にする。
- Bone Paletteを固定本数へ戻さない。
- GBufferのRT枚数・Format・格納ABIを独断で変更しない。
- Builtin Primitiveを存在しないMaterial Assetへ解決してマゼンタfallbackにしない。
- Lightingへ一時的な`*10`、固定露出、固定明るさを入れて見た目だけを合わせない。
- PostProcess未接続を照明側のゲインで隠さない。
- Profile統計を固定値や空のfallbackで通さない。実際のScene / RenderItemから数える。
- ShadowのCPU正本（CSM / LocalShadowAtlas）の行列・sliceをDX12側で再計算しない。
- HLSLをCPPへ埋め込まない。
- CPPファイル名に`phase`を入れない。
- 新規コメントを英語の文章で追加しない。コメントは日本語で、必要最小限にする。
- `git reset --hard`、`git checkout --`で現在の作業ツリーを消さない。

## 7. 作業ツリーと成果物の注意

Phase 3の主要実装は日本語コミット済みである。主なコミットは次のとおり。

- `828229f` DX12のTAA・SSAO・SSR履歴を最終表示へ接続
- `8f728fc` DX12のDebug検証と実行時GPU検証を分離
- `ee4f4cf` DX12検証の停止条件と終了撮影を修正

作業ツリーにはDX12移行以外の既存変更も残るため、全体をリセットせず対象ファイルだけを確認する。

未追跡には新規DX12 HLSL、検証生成物`DX12ValidationTexture.dds`が含まれる。
`Shader\compiled\`のCSO差分や既存Asset変更もあるため、配布時は成果物と生成物を分ける。

アップロード用ZIPを作る場合は、`3dgp.vcxproj`が参照するImGuiソース一式を含めること。
ソースが欠けたZIPへ推測で最新版ImGuiを入れない。使用するImGuiの版は既存プロジェクトの版に
合わせる。

## 8. Phase 3完了判定

次のすべてを確認したため、Phase 3をPASSとする。

- Debug / Releaseのクリーンに近いビルドが成功する
- Runtime SceneがDX12で起動する
- テクスチャ付きStatic / Skinned Meshが実機で表示される
- GBuffer、Deferred、Forward Transparentの実機画像が成立する
- Directional / Point / Spot Shadowを実機で個別確認する
- Skinned ShadowとCast / Receive Shadowを実機で確認する
- Resize、終了、resource解放を確認する
- Debug Layerのエラーを確認し、未解決エラーを残さない
- headless DX12 validationが全件PASSする（Release、Debug Layer、GPU-Based Validation）
- Profile CSV / TraceのScene統計が0ではない
- HLSLがCPPに埋め込まれていない
- CPP名に`phase`がない
- 明るさを固定値でごまかす診断コード、probe、temporary diagnosticが残っていない

## 9. 引き継ぎ時の最初の確認

```powershell
Set-Location "C:\Users\2250298\Desktop\teamProject_2_3"
git status --short
git diff --check
rg --files RePlayEngine\Rendering\DX12 Shader | rg "D3D12SceneRenderer|dx12_"
rg -n "lit \* 10|diagnostic_non_indexed|draw_diagnostics|dx12_draw_diag|dx12_bridge_diag|dx12_info_queue" .
```

最後の検索結果が空であること、CPPへHLSL文字列が戻っていないこと、`phase`付きCPPがないことを
確認済みである。次担当はPhase 4のRuntime UI移行へ進む。

## 10. コミットメッセージ例

```text
DX12 Phase 3のテクスチャ付きScene3Dを接続
```

Phase 3完了後のメッセージ例:

```text
DX12 Phase 3のScene3DとShadow検証を完了
```
