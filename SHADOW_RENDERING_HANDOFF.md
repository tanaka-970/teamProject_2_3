# Unreal型 3Dリアルタイム影対応 引き継ぎ書

調査日: 2026-08-22  
対象: `C:\Users\2250298\Desktop\teamProject_2_3`  
状態: 調査・方針整理のみ。影機能の実装変更は行っていない。

## 1. この引き継ぎ書の目的

「3Dオブジェクトを動かしている最中も影が更新される」状態を、Unreal EngineのMovable Light / Movable Actorに近い動的影として、UIコンポーネントではなく標準レンダリング機能として整備するための調査結果をまとめる。

ここでいう影は、画面上のUIテキスト影やUIエフェクトではなく、3Dライトがメッシュへ落とすリアルタイム影を指す。

### 目標の定義（Unreal型）

Unrealの「Movable」をこのプロジェクトの第一目標に相当する動作とする。つまり、ライトまたは物体が動く場合、焼き込み結果へ依存せず、リアルタイムシャドウマップを更新する。

少なくとも1つの有効な動的影ライトがある場合、以下を満たすこと。

- 静的メッシュ、Primitive、Landscape、Skinned Meshが影を落とせる。
- オブジェクトを移動・回転・拡縮・アニメーションしている間も、影が同じフレームの姿勢に追従する。
- 影を受ける側も同じフレームの位置で影を受ける。
- Scene View、Play中、Standalone実行時で同じ影の基本経路を使う。
- `Cast Shadow` / `Receive Shadow` はメッシュ描画設定として扱い、`ShadowComponent` のような別コンポーネントは作らない。
- Directional / Point / Spotのすべてで、動的影を生成できる。
- ライトを動かした場合も、そのライトの影マップを更新する。
- 接触部分の補助影は、通常のシャドウマップに加算する独立機能として扱える。
- 静的ジオメトリだけを対象にする場合は、正しさを確認した後にシャドウマップキャッシュを使える。

ライトが1つも存在しない場合に暗黙のライトを使うかどうかは、実装前に決定する必要がある。推奨は「Runtimeは暗黙の光を出さず、Scene Viewだけ非保存のプレビューライトを使う」方針である。

## 2. 結論

現在のプロジェクトには、ライト・CSM・影サンプリング用シェーダーの基盤はある。しかし、現状はUnrealのMovable相当、つまり「移動するGameObjectやライトが常に動的影を更新する」状態にはなっていない。

最重要の理由は次のとおり。

1. 現在のCSM影生成パスは、`object_render_items` ではなくデバッグ用の `static_meshes[0]` だけを描いている。
2. `csm_caster_skinned_vs` と `shadow_caster_skinned_mesh_vs` は存在するが、現在の影生成パスからGameObjectのSkinned Meshへ使われていない。
3. `RenderItem::cast_shadow` / `receive_shadow` はComponentからRenderItemへコピーされるが、影生成・GBuffer・照明シェーダーで参照されていない。
4. Point Light / Spot Lightは通常照明へ投入されるだけで、Point/Spot用シャドウマップの生成・サンプリング経路が確認できない。
5. CSMの有効設定は標準レンダラー側にあるが、Light ComponentのInspectorとグローバル描画設定が分かれており、影の設定の正本が分かりにくい。

したがって、最初に実装すべきなのは、Unreal型の動的影の共通契約を決めたうえで、既存CSMを全GameObject・Landscape・Skinned Meshへ接続する「Directional Lightの動的影の完成」である。その後、Point/Spotの動的影を別方式で追加する。Directionalだけ実装して完了扱いにはしない。

## 3. 現在のコード構造

### 3.1 ライトコンポーネント

対象: `RePlayEngine/Components/Rendering/LightComponents.h`

- `DirectionalLightComponent`
  - `color`
  - `intensity`
  - `cast_shadows = true`
- `PointLightComponent`
  - `color`
  - `intensity`
  - `range`
- `SpotLightComponent`
  - `color`
  - `intensity`
  - `range`
  - `inner_angle_degrees`
  - `outer_angle_degrees`

Directionalだけに `cast_shadows` があり、Point/Spotには影の有効化プロパティがない。ライトの共通設定として揃っていない。

登録場所: `RePlayEngine/Object/Registry/BuiltInComponentsRendering.cpp` の `RegisterLights()`

- Directional: `色`、`強さ`、`影を落とす`
- Point: `色`、`強さ`、`範囲`
- Spot: `色`、`強さ`、`範囲`、`内側角度`、`外側角度`

### 3.2 Runtimeのライト同期

対象: `Source/app/Runtime/framework_gameobject_scene_runtime.cpp` の `sync_object_lights()`

- 有効なDirectional LightはScene内の先頭1つだけを採用する。
- Directionalの方向はGameObjectのワールド回転から作る。
- Directionalの `cast_shadows` は `pbr.light.shadow_params.w` へ反映される。
- Point Lightは最大8個を `lights_manager` へ投入する。
- Spot Lightは最大4個を `lights_manager` へ投入する。
- Point/Spotは位置・色・強さ・範囲・円錐情報を渡すが、影用情報は渡していない。

Directionalが存在しない場合、Scene Viewでは補助光を使って見た目を確認できるが、`pbr.light.shadow_params.w` は0にされる。つまり現在の空SceneのScene Viewは、明るく見えても補助光の影は出ない。

### 3.3 描画提出リスト

対象:

- `RePlayEngine/Rendering/Adapter/SceneRenderCollector.cpp`
- `RePlayEngine/Rendering/Adapter/RenderItem.h`
- `Source/app/Runtime/framework_gameobject_scene_runtime.cpp`

`SceneRenderCollector::Collect()` は毎フレーム、階層的に有効なGameObjectの `IRenderSubmitter` を走査し、`RenderItem` を作り直す。`RenderItem` には次の影関連値がある。

```cpp
bool cast_shadow = true;
bool receive_shadow = true;
```

Mesh、Primitive、Skinned Meshの各Componentは、`BuildRenderItem()` でComponentの値をこの2つへコピーしている。

ただし、調査時点で `cast_shadow` / `receive_shadow` を描画結果へ使っている箇所は、値の定義・登録・コピー以外には確認できない。現在は「値を運んでいるだけ」の状態である。

### 3.4 既存のCSM

対象:

- `Source/render/csm_renderer.h`
- `Source/render/csm_renderer.cpp`
- `Shader/csm_common.hlsli`
- `Shader/csm_caster_static_vs.hlsl`
- `Shader/csm_caster_skinned_vs.hlsl`
- `Shader/csm_caster_gs.hlsl`

既存CSMの仕様:

- 4カスケード
- 1カスケードあたり2048px
- Texture2DArrayの深度シャドウマップ
- 比較サンプラーによるPCF
- PCSS用のブロッカー探索と可変半影
- カスケード間ブレンド
- 深度バイアス、法線オフセット、傾斜バイアス、影の濃さ
- カメラ視錐台からカスケードを再計算
- ライト空間中心をテクセル単位へスナップしてシマリングを抑制
- `shadow_distance = 240.0f`
- `caster_extrusion = 60.0f`

CSMの設計自体は、Directional Lightの動的影の土台として再利用できる。

### 3.5 現在の影生成パス

対象: `Source/app/Rendering/framework_render_scene_setup.inl`

CSMパスの条件は概ね次のとおり。

```cpp
if (csm.constants.params.w > 0.5f &&
    enable_static_meshes && static_meshes[0])
```

この中で描かれるのは、次のデバッグ用メッシュだけである。

```cpp
static_meshes[0]->render(..., csm.caster_static_vs, csm.caster_static_il, ...);
```

`draw_object_scene_meshes()`、`draw_landscape_scene_meshes()`、`object_render_items` はこのCSMパスから呼ばれていない。

同じ構造が旧PBR単一シャドウマップのパスにもある。PBR影パスも `static_meshes[0]` だけを描く。

なお、影用のスキンメッシュ頂点シェーダーはロードされているが、現在のGameObject影生成処理から呼び出されていない。これは「シェーダーが無い」のではなく、「影用提出・描画の接続が無い」問題である。

### 3.6 通常のGameObject描画

対象: `Source/app/Runtime/framework_gameobject_scene_rendering_draw.cpp`

`draw_object_scene_meshes()` は、`object_render_items` を使って次の描画を行う。

- 深度プリパス
- Deferred GBuffer
- Forward fallback
- Built-in Primitive
- 静的glTF
- Skinned Mesh / Animator

この関数には通常描画のための `depth_only` 引数はあるが、CSM/PBRシャドウ用の別パスとして呼ばれてはいない。通常の深度プリパスとライト視点の影深度パスは、目的も行列も違うため、同じフラグだけで代用してはいけない。

### 3.7 シェーダー側の影の範囲

対象:

- `Shader/deferred_lighting_ps.hlsl`
- `Shader/tiled_deferred_lighting_cs.hlsl`
- `Shader/pbr_brdf.hlsli`
- `Shader/csm_common.hlsli`

Deferredの照明では、Directional相当の直接光に対してCSM影を適用している。

一方、Point/Spotの照明は `evaluate_point_lights()` / `evaluate_spot_lights()` で加算されるが、Point/Spotのシャドウマップをサンプルしていない。したがって、Point/Spotは現在「照らすが遮られない」照明である。

GBufferには現在、影を受けるかどうかの情報がない。`GBufferData` はBase Color、Normal、Roughness、Metalness、AOなどを持つが、`receive_shadow` は格納していない。Deferredで `Receive Shadow` を有効にするには、GBufferへフラグを追加するか、別のマスク経路を用意する必要がある。

### 3.8 Landscape

対象: `RePlayEngine/Components/Landscape/LandscapeRendererComponent.h`

Landscapeにも次の値がある。

```cpp
bool cast_shadow = true;
bool receive_shadow = true;
```

しかしLandscape描画処理は現在、通常のGBuffer/Forward/深度プリパスへ描くだけで、影生成パスへは接続されていない。Mesh系の影対応と別扱いにせず、同じ標準ShadowCaster提出へ統合する必要がある。

### 3.9 Editorの表示と設定

主な場所:

- Component表示: `RePlayEngine/Editor/Inspector/InspectorPanelComponents.cpp`
- グローバル描画設定: `Source/app/Editor/framework_inspector.cpp`
- CSM調整UI: `Source/app/Editor/framework_screen_space.cpp`

現在の表示は分散している。

- Light ComponentのInspector: 光源の色・強さ・範囲など。影トグルはDirectionalにしかない。
- Mesh系ComponentのInspector: `影を落とす`、`影を受ける` がある。
- 描画設定: CSMの有効・PCSS・バイアス・フィルタ・カスケード距離など。
- 旧Directional選択画面: PBR影とCSMの別設定を持つ。

このため、Light Componentを選択しても「そのライトがどの影方式で、何を影対象にしているか」がまとまって見えない。Unity/Unrealに寄せるなら、Light選択時の共通Shadowセクションへ統合するのがよい。

また、CSM設定をProjectSettingsへ永続化する明確な経路は、今回の検索では確認できなかった。現状はEditorセッション中のレンダラー状態を直接変更している可能性が高い。実装時には保存先を決めること。

## 4. Unity / Unrealとの比較

### 4.1 Unity 6

Unityは影を1つの専用コンポーネントにせず、次の3か所へ分けている。

1. Light Inspector
   - Type、Range、Spot Angle、Color、Mode、Intensity
   - Shadow Type: 影なし / Hard / Soft
   - Realtime Shadows: Strength、Resolution、Bias、Normal Bias、Near Plane
   - Culling Mask
2. Mesh Renderer / Skinned Mesh Renderer
   - Cast Shadows
   - Receive Shadows
3. Project / Pipeline Settings
   - 影距離、品質、レンダーパイプライン全体の制限

UnityのRealtime Shadowはシャドウマップを毎フレーム更新し、ライトが動けば影も動く。Unityの説明でも、GameObjectはRealtime影を標準で受け、影を落とす側はMesh RendererのCast Shadowsで指定する構造になっている。

参考:

- [Unity 6 Light component Inspector](https://docs.unity3d.com/6000.0/Documentation/Manual/class-Light.html)
- [Unity 6 Mesh Renderer component](https://docs.unity3d.com/6000.0/Documentation/Manual/class-MeshRenderer.html)
- [Unity 6 Enable shadows](https://docs.unity3d.com/6000.0/Documentation/Manual/shadow-configuration.html)

### 4.2 Unreal Engine 5.8

UnrealはLightのDetailsパネルで、次のようにライト単位で影をまとめている。

- Mobility: Static / Stationary / Movable
- Cast Shadows
- Cast Static Shadows / Cast Dynamic Shadows
- Shadow Bias / Shadow Slope Bias
- Contact Shadow Length
- Shadow Resolutionやキャッシュ関連
- Directional Lightの場合はCSM距離、カスケード数、分布指数、遷移幅など

Movable Lightは完全動的なライトで、Point / SpotもMovableにすれば動的影をサポートする。Unrealは動かないジオメトリについては後からシャドウマップキャッシュを使ってコストを下げるが、最初からキャッシュ前提にはしていない。

参考:

- [Unreal Engine 5.8 Shadowing](https://dev.epicgames.com/documentation/en-us/unreal-engine/shadowing-in-unreal-engine)
- [Unreal Engine 5.8 Directional Lights](https://dev.epicgames.com/documentation/en-us/unreal-engine/directional-lights-in-unreal-engine)
- [Unreal Engine 5.8 Movable Light Mobility](https://dev.epicgames.com/documentation/en-us/unreal-engine/movable-light-mobility-in-unreal-engine)
- [Unreal Engine 5.8 Contact Shadows](https://dev.epicgames.com/documentation/en-us/unreal-engine/contact-shadows-in-unreal-engine)

### 4.3 このプロジェクトへ取り入れるべき部分

- 影そのものは標準Renderer機能として扱う。
- Light Componentには「光源」と「その光源の影設定」を置く。
- Mesh RendererにはCast / Receiveを置く。
- DirectionalのCSM設定だけは光源固有の詳細設定として表示する。
- Point/Spotにも同じ共通Shadowセクションを表示し、方式だけライト種別ごとに変える。
- EditorとRuntimeで影生成の正本を分けない。

UnityのModeやUnrealのMobilityに相当する設定は、将来のStatic / Stationary / Movable相当の選択肢として設計する。ただし最初に成立させるべきモードはMovable相当であり、焼き込みではなくリアルタイム動的影を正しく出すことを優先する。Static相当のキャッシュは、その後の最適化として追加する。

## 5. 推奨する実装方針

### Phase 0: 仕様を固定する

最初に以下を決める。

- Unreal型の第一目標はMovable相当。ライトまたは影Casterが動けば、同フレームの動的シャドウマップへ反映する。
- 第一段階はリアルタイム影のみ。Baked / Lightmapは対象外。
- Light側にMobility相当の状態を持たせる場合、初期値はMovable相当とする。別の状態を追加しても、Movableの挙動を弱めない。
- 「常に影」は、影を落とすLightが存在し、影がそのLightの有効範囲・CSM距離内にある場合と定義する。
- RuntimeでLightが無いSceneに暗黙の影ライトを出すかどうか。
- Scene Viewのみプレビューライトを使うかどうか。
- Directional CSMを最初の検証対象にするが、最終目標はPoint/Spotを含む全ライトの動的影である。
- 影を落とすライト数の上限と、超過時の選択規則。

### Phase 1: Directional Light + CSMを全ジオメトリへ接続する

既存CSMを壊さず、影用の共通提出処理を追加する。

影用提出処理の入力は、毎フレーム更新される `object_render_items` を正本にする。

対象:

- デバッグ用 `static_meshes[0]`（互換用）
- Built-in Primitive
- 静的glTF
- Skinned Mesh / Animator
- Landscape

影用描画では、通常のマテリアル色やGBufferを書かず、ライト視点の深度だけを書く。`cast_shadow == false` は影用提出から除外する。`receive_shadow` は影用提出ではなく照明側で使う。

既存シェーダーの再利用候補:

- `csm_caster_static_vs` + `csm_caster_gs`: static / Primitive / Landscape
- `csm_caster_skinned_vs` + `csm_caster_gs`: Skinned Mesh

注意点:

- `gltf_model::render()` は現在、任意の影用Vertex Shader/InputLayoutを受け取らない。静的glTFをCSMへ入れるには、影用Render APIを追加するか、同等の頂点バッファ描画入口を公開する必要がある。
- `skinned_mesh::render()` と `static_mesh::render()` は代替Vertex Shader/InputLayoutを受け取れるため、既存入口を利用できる可能性が高い。
- glTFの通常描画にはメインカメラ視錐台カリングがあるため、影パスでそのカリングをそのまま使わない。影カメラ／カスケードの視錐台で判定するか、まず影パスだけカリングを無効化する。
- Skinned Meshのアニメーション中のBoundsが小さいと、影が欠ける。最初は安全側に広く取り、後でアニメーションBoundsを整備する。
- Alpha Clip材質を影へ正しく反映する場合、深度だけの影用Pixel Shaderでアルファ破棄が必要になる。第一段階で不透明材質に限定するか、Alpha Clipを含めるかを決める。

### Phase 2: Cast / Receiveを実際の描画へ反映する

#### Cast Shadow

影用深度パスで `RenderItem::cast_shadow` を必ず参照する。現在は値が存在するだけなので、これを接続するだけで既存Inspectorの意味が実体化する。

#### Receive Shadow

Deferred照明が全ピクセルへCSM影を掛けているため、`receive_shadow == false` を使うにはGBufferまたは照明入力へ受影マスクを渡す必要がある。

候補:

- GBufferの既存パラメータへ受影ビットを追加する。
- 影受け専用のマスクRender Targetを追加する。
- マテリアル定数へ入れる。ただしObjectごとに値を更新できることが条件。

GBufferの既存alphaは照明モデル、Pixelate、AO等に使われているので、意味を変える場合は全関連Shaderとデバッグ出力を同時に更新する。単純に既存のalphaへ上書きしてはいけない。

### Phase 3: Point / Spotの動的影（Unreal型の必須範囲）

Point / SpotはDirectionalのCSMを流用できない。ただしUnrealのMovable Lightと同じ目標にするため、Point/Spotを「将来対応」だけで止めず、動的影の完成範囲に含める。

- Point Light: 1ライトにつき6方向のキューブシャドウ、または6スライスのTexture2DArray。
- Spot Light: ライト視点の透視シャドウマップ。
- 各Light GPUデータへ影マップスロット、影の有効・強さ・バイアス等を追加。
- Deferred PS版とTiled Deferred CS版の両方で、同じ影判定を実装する。
- Point/Spotのライト上限と影付きライト上限を分ける。全Lightを無条件に影付きにするとGPU負荷が急増する。
- 動くライト、動くCaster、動くReceiverでは影マップを毎フレーム更新する。
- LightにMovable相当の状態が設定されている場合、ライトの位置・回転・色・強さ・範囲の変更を次フレームへ持ち越さない。
- 静的ジオメトリだけを対象にするキャッシュは、Movable相当の正しさを壊さない範囲でのみ有効化する。

Point/Spotの影は、最初はHard/PCFで成立させ、PCSS・キャッシュ・接触影は後段にする方が安全である。

Unreal型の完成目標では、接触影は通常のシャドウマップの代替ではなく、画面深度を使う補助的な加算影として扱う。これにより、遠方のシャドウマップ品質を上げずに、足元や接触部分の浮きを抑えられる。

### Phase 4: Editorの表示を整理する

Light Component選択時に、少なくとも次の共通セクションを表示する。

- Cast Shadows
- Shadow Strength
- Shadow Resolution / Quality
- Depth Bias
- Normal Bias
- Near Plane
- Contact Shadow Length（対応時）

Directional固有:

- CSM Enable
- Shadow Distance
- Cascade Count
- Cascade Distribution
- Cascade Blend / Fade
- PCF / PCSS

Point/Spot固有:

- Shadow Map Resolution
- Near / Far Plane
- Pointの場合は6面体の方式、Spotの場合は透視方式

Mesh Renderer / Primitive / Skinned / Landscape側は、既存の `影を落とす` と `影を受ける` をRenderingセクションへまとめる。

グローバル描画設定には、個別ライトの設定ではなく次を置く。

- 標準影機能の全体有効化
- 最大影距離の上限
- シャドウマップの全体品質上限
- 影付きPoint/Spotの最大数
- 影マップ更新統計・デバッグ表示

旧 `editor_selection::directional_light` のPBR/CSM操作は、Light Componentの設定と二重管理になる。新しいComponent Inspectorを正本にする場合は、旧UIを段階的に読み取り専用または削除対象へする。

### Phase 5: キャッシュと性能改善

動的影を完成させた後に、必要なら次を追加する。

- 静的Casterだけのシャドウマップキャッシュ
- LightまたはCasterが変化したときだけ再生成
- CSMカスケードごとのCasterカリング
- Point/Spotの影付きライト選択
- Shadow LOD / 解像度低下
- 影マップ更新をフレーム分散する仕組み

最初からキャッシュを入れると、動く物体の更新漏れと区別しにくくなる。まず毎フレーム更新で正しさを確認する。

## 6. 実装時に守るべき設計上の注意

- `ShadowComponent` は追加しない。影は標準Rendererのパスとして扱う。
- Componentごとに影処理を増やさず、`RenderItem`を標準入力にする。
- 通常のカメラ深度プリパスとライト視点のShadow Depth Passを混同しない。
- `cast_shadow` と `receive_shadow` を名前だけ残さず、実際のGPU結果まで接続する。
- Directionalの「Scene内先頭1つだけ」仕様は、実装中に暗黙に複数ライト対応へ変えない。複数対応する場合は別仕様にする。
- Point/Spotの通常照明と影処理は、PS版とTiled CS版の両方を同時に確認する。
- Scene Viewのプレビューライトを使う場合、Runtimeの正規Lightと区別できる表示を用意する。
- UIテキストの影、トゥーン材質の影色、3Dシャドウマップを同じ設定名で混ぜない。
- CSMのカスケード外は「影が壊れた」のではなく、影距離外として扱う。Debug表示で判別できるようにする。

## 7. 検証シナリオ

### Directional / CSM

- Ground上にCubeを置き、CubeをX/Y/Zへ移動する。
- Cubeの影が同フレームに移動する。
- Cubeを回転・拡縮し、影の向き・大きさが追従する。
- Cubeを2つ以上置き、互いに影を落とす。
- Primitive、MeshRenderer、SkinnedMeshRenderer、Landscapeを1つずつCasterにする。
- Skinned Meshをアニメーションさせ、骨格変形に影が追従する。
- CSMの第1～第4カスケード境界を横切る。
- カメラを移動・回転し、カスケード境界のちらつき・継ぎ目・急な消失を確認する。
- Directional Lightを回転・強さ変更し、影方向・濃さが追従する。

### Cast / Receive

- Casterの `Cast Shadow=false` で、その物体の影だけが消える。
- Receiverの `Receive Shadow=false` で、その物体だけ影を受けなくなる。
- Receiverを無効にしても、その物体が他の物体へ影を落とすことを確認する。
- LandscapeのCast/Receiveも同じ挙動になる。

### Point / Spot

- Point Lightを動かし、影方向と範囲が追従する。
- Point Lightの6方向すべてで影が欠けない。
- Spot Lightを回転・移動し、円錐外で影が適切に消える。
- Spotの内側角度・外側角度境界で影が不自然に反転しない。
- 複数Light上限を超えた場合、どのライトが影付きになるかが明示される。

### Editor / Runtime

- Scene View編集モードで移動中の影が更新される。
- Play中にTransformが変化する物体の影が更新される。
- Standalone実行でも同じ影が出る。
- Scene ViewのプレビューライトとRuntimeのScene Lightを混同しない。
- LightなしScene、Directionalのみ、Pointのみ、Spotのみ、混在Sceneを確認する。

### 品質・安定性

- Shadow Acne、Peter Panning、カスケード継ぎ目、ちらつき、画面外Casterの欠落を確認する。
- Alpha Clip材質の影が必要なら、アルファなしの深度影になっていないことを確認する。
- D3D11のShadow DSV/SRVのバインド解除漏れがないことを確認する。
- `Render Output` のCSM影デバッグ表示を追加する場合、通常出力と数値が一致することを確認する。

## 8. 完了条件

第一段階（Directional + CSM）の完了条件は次のとおり。

- `object_render_items` のCasterがCSMへ描かれる。
- 移動中の静的・Primitive・Skinned・Landscapeが影を落とす。
- `cast_shadow` が実際の影生成に効く。
- `receive_shadow` が実際のDeferred照明に効く。
- Scene View / Play / Standaloneで影の更新経路が一致する。
- CSMの距離外・Lightなし・Cast/Receive無効がDebug表示で区別できる。
- 既存のデバッグ用 `static_meshes[0]` を表示しなくても、GameObjectだけで影のテストが成立する。

第二段階（Point / Spot、Unreal型の動的影）の完了条件は次のとおり。

- Point/SpotにDirectionalと同じ意味のCast Shadows設定がある。
- Point/Spotの影マップが毎フレーム必要なとき更新される。
- Point/SpotのLightをMovable相当として動かしたとき、影の位置・向き・範囲が即時に更新される。
- Deferred PS版とTiled Deferred CS版の結果が一致する。
- 影付きライト数の上限と負荷がEditor上で分かる。
- 動的影を壊さない範囲で、静的Casterのシャドウマップキャッシュを任意に有効化できる。

## 9. 着手順の推奨

1. 仕様を固定する（LightなしSceneの扱い、第一段階の対象、影付きLight上限）。
2. CSMの影用提出処理を分離し、`RenderItem`全件とLandscapeを描く。
3. Static / Primitive / Skinned / glTFの影描画入口を揃える。
4. `cast_shadow` を接続する。
5. GBufferへ `receive_shadow` を伝え、照明側へ接続する。
6. Scene View / Play / Standaloneの動的更新を検証する。
7. InspectorをLight共通Shadowセクションへ整理する。
8. Point/Spotのシャドウマップを追加する。
9. 最後にキャッシュ・PCSS・接触影・性能最適化を追加する。

## 10. 参照ファイル一覧

- `RePlayEngine/Components/Rendering/LightComponents.h`
- `RePlayEngine/Object/Registry/BuiltInComponentsRendering.cpp`
- `Source/app/Runtime/framework_gameobject_scene_runtime.cpp`
- `RePlayEngine/Rendering/Adapter/SceneRenderCollector.cpp`
- `RePlayEngine/Rendering/Adapter/RenderItem.h`
- `RePlayEngine/Components/Rendering/MeshRendererComponent.cpp`
- `RePlayEngine/Components/Rendering/PrimitiveMeshRendererComponent.cpp`
- `RePlayEngine/Components/Rendering/SkinnedMeshRendererComponent.cpp`
- `RePlayEngine/Components/Landscape/LandscapeRendererComponent.h`
- `Source/render/csm_renderer.h`
- `Source/render/csm_renderer.cpp`
- `Source/render/pbr_renderer.h`
- `Source/render/pbr_renderer.cpp`
- `Source/app/Rendering/framework_render_setup.inl`
- `Source/app/Rendering/framework_render_scene_setup.inl`
- `Source/app/Runtime/framework_gameobject_scene_rendering_draw.cpp`
- `Shader/csm_common.hlsli`
- `Shader/csm_caster_static_vs.hlsl`
- `Shader/csm_caster_skinned_vs.hlsl`
- `Shader/csm_caster_gs.hlsl`
- `Shader/deferred_lighting_ps.hlsl`
- `Shader/tiled_deferred_lighting_cs.hlsl`
- `Shader/pbr_brdf.hlsli`
- `Shader/gbuffer_common.hlsli`
- `Source/app/Editor/framework_inspector.cpp`
- `Source/app/Editor/framework_screen_space.cpp`
- `RePlayEngine/Editor/Inspector/InspectorPanelComponents.cpp`
