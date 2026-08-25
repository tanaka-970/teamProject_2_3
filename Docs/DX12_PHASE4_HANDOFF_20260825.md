# DX12 Phase 4 引き継ぎ書

最終更新: 2026-08-25
対象ワークスペース: `C:\Users\2250298\Desktop\teamProject_2_3`
状態: **Phase 4 未完了。Phase 3で接続したEditor ImGui / Canvas Previewを土台に、Runtime UIをDX12へ完全移行する。**

この文書は、Phase 3完了後に開始する「UI・Editor・PostProcessの製品経路化」の正本とする。
Phase 4は、見た目だけの仮描画を追加する作業ではない。既存のCanvas、RectTransform、FontAtlas、
UIEffect、Mask、入力、Undo、Asset参照をCPU側の正本として維持し、その実データをDX12の描画経路へ
到達させることを完了条件とする。

## 1. 現在の結論

Phase 3で次のDX12接続は確認済みである。

- `D3D12DeviceContext::InitializeImGui()` によるImGuiフォントテクスチャのDX12アップロード
- `D3D12DeviceContext::DrawImGui()` によるImGui頂点・Index・Scissor・Texture描画
- `Shader\dx12_imgui_vs.hlsl` / `Shader\dx12_imgui_ps.hlsl`
- EditorのCanvas Previewと既存Canvasデータの表示
- Scene3D、HDR、TAA、SSAO、SSR、Bloom、FXAAを含む最終表示への接続

一方、次はPhase 4の未完了範囲である。

- `RePlayEngine\UI\UIRenderer` 本体のDX12移行
- Runtime UIのText、Sprite、Shape、Mask、Scroll、InputField、Slider、Selectable
- UIのBackdrop / Effect Stack / オフスクリーンRenderTarget
- FontAtlasとSDF文字のDX12テクスチャ・バッファ寿命管理
- UIテクスチャのAsset GUID / パス解決、Descriptorの再利用、差し替え
- Editorの全UI操作をDX12実行時に回帰確認すること
- `imgui_impl_dx11.cpp/h` を使用しないImGui backendへの切り替え確認

従って、現在の状態を「Runtime UIもDX12化済み」と宣言してはいけない。Phase 4の受け入れまでは、
既存のD3D11 `UIRenderer` を削除・無効化せず、DX12側で同等機能を段階的に検証する。

## 2. 既存実装の境界

### 2.1 CPU側の正本

次のデータモデルと編集機能は作り直さない。

- `RePlayEngine\Components\UI\CanvasComponent.*`
- `RePlayEngine\Components\UI\RectTransformComponent.*`
- `RePlayEngine\Components\UI\UIImageComponent.*`
- `RePlayEngine\Components\UI\UITextComponent.*`
- `RePlayEngine\Components\UI\UIShapeImageComponent.*`
- `RePlayEngine\Components\UI\UIMaskComponent.*`
- `RePlayEngine\Components\UI\UIInputFieldComponent.*`
- `RePlayEngine\Components\UI\UIScrollViewComponent.*`
- `RePlayEngine\Components\UI\UISelectableComponent.*`
- `RePlayEngine\Components\UI\UISliderComponent.*`
- `RePlayEngine\UI\UILayout.*`
- `RePlayEngine\UI\UIFocusManager.*`
- `RePlayEngine\UI\UIInputFieldSystem.*`
- `RePlayEngine\UI\FontAtlas.*`
- `RePlayEngine\UI\RichTextParser.*`

描画側は上記のコンポーネントから、フレームごとのUI draw commandを構築する。コンポーネントへ
DX12リソースを持たせない。GPUリソースの所有者はDX12 renderer / resource cacheに限定する。

### 2.2 現在のD3D11依存

`RePlayEngine\UI\UIRenderer.h/.cpp` には、少なくとも次のD3D11依存がある。

- `ID3D11Device`、`ID3D11DeviceContext`
- `ID3D11VertexShader`、`ID3D11PixelShader`、`ID3D11InputLayout`
- `ID3D11Buffer`
- `ID3D11ShaderResourceView`
- `ID3D11BlendState`、`ID3D11SamplerState`
- `ID3D11DepthStencilState`、`ID3D11RasterizerState`
- `UIRenderTargetPool`のD3D11テクスチャ／RTV／SRV

`UIRendererRenderSetup.inl`、`UIRendererRenderTextInput.inl`などのUIロジックは、GPU APIを直接
持たない部分を優先して再利用する。ただし、D3D11型を間接的に要求する関数は、DX12向けの
draw command生成層と分離する。

### 2.3 Phase 3のDX12基盤を再利用する

- `RePlayEngine\Rendering\DX12\D3D12DeviceContext.*`
- `RePlayEngine\Rendering\DX12\D3D12DescriptorHeapAllocator.*`
- `RePlayEngine\Rendering\DX12\D3D12ResourceFactory.*`
- `RePlayEngine\Rendering\DX12\D3D12ResourceStateTracker.*`
- `RePlayEngine\Rendering\DX12\D3D12UploadContext.*`
- `RePlayEngine\Rendering\DX12\D3D12FrameResource.*`
- `RePlayEngine\Rendering\DX12\D3D12SceneRenderer.cpp`
- `Shader\dx12_fullscreen_vs.hlsl`
- `Shader\dx12_postprocess_ps.hlsl`

既存Scene3DのGBuffer、HDR Scene Target、最終PostProcess、Readback、Resize、Fence待機の規約を
UIだけ別実装にしない。UIは適切な順序で同じフレームのCommand Listへ記録する。

## 3. Phase 4の実装目標

### P0: UI描画データの共通化

1. `UIRenderer`のCPU側走査を、GPU APIを呼ばないUI draw command生成へ分離する。
2. 1 draw commandに次を含める。
   - 頂点範囲または頂点バッファ参照
   - Index範囲
   - Texture Asset GUID / 解決済みテクスチャキー
   - UV、色、矩形、変換
   - Clip矩形とClip階層
   - Blend / Sampler / Shader variant
   - MaterialまたはEffectの定数
3. Canvasの親子順、Sibling順、Sort Order、Screen Space / World Spaceを既存の意味のまま保持する。
4. UI command生成とDX12 command list記録を別層にする。Editor PreviewとRuntimeで別の座標系を作らない。

### P1: Sprite / Shape / Transparent UI

- `UIImageComponent`のAsset Texture、Color、UV、Nine Sliceを実装する。
- `UIShapeComponent` / `UIShapeImageComponent`の形状と塗りを実装する。
- Premultiplied Alpha、Straight Alpha、Add、Multiply、Screenなど既存Blendの意味を固定する。
- ScissorをCommand単位で設定し、CanvasのClip階層を正しく閉じる。
- Texture未解決、Asset削除、ロード中、サイズ0を安全なWhite Texture / no-opへ戻す。
- UIの画面外・負サイズ・極端な矩形でDescriptorや頂点数が破綻しないようにする。

### P2: Text / FontAtlas / Rich Text

- `FontAtlas`が生成したAtlas画像をDX12 Textureへアップロードする。
- Atlasの更新・再生成時に古いSRVをGPU完了前に解放しない。
- SDF / 通常文字、字間、行間、改行、縦揃え、複数行、RichTextParserの装飾を維持する。
- TextのGlyphごとにCPU側の一時Textureを作らず、Atlas参照と頂点生成を使う。
- Atlasの最大サイズ超過時は既存仕様に沿った分割または安全な失敗にし、描画を停止させない。
- 日本語グリフ範囲とFallback Fontを実データで確認する。

### P3: Mask / Scroll / InputField / Selectable

- `UIMaskComponent`のClip、Stencil相当処理、Nested MaskをDX12で再現する。
- RectTransformのClipとGPU Scissorの境界を一致させる。
- ScrollViewのContent移動、Clip、スクロールバー表示を確認する。
- `UIInputFieldSystem`、`UIFocusManager`、IME、カーソル、選択範囲、文字入力を維持する。
- Button / Toggle / Slider / SelectableのHover、Pressed、Disabled、Focus状態を表示へ反映する。
- 入力イベントは描画backendから発生させない。既存のInput / Focus処理を正本とする。

### P4: UI Effect / Backdrop / Offscreen

- `RePlayEngine\UI\Effects\UIRenderTargetPool.*`をDX12のRTV/SRV resource poolへ置き換える。
- EffectごとのExpand Boundsを累積し、Glow / Blur / Shadowが矩形外へ出ても切れないようにする。
- Backdropは現在のSceneまたはUI前段を正しいResource Stateで読む。
- Effectの入力・出力・マスク・元画像を同一passで誤ってSRV/RTV兼用しない。
- Poolの再利用はFence完了後だけ許可する。フレーム数だけで安全と判断しない。
- `EffectChain`の既存順序と合成式を維持し、固定色や固定ゲインで欠落を隠さない。

### P5: ImGui / Editorの完全確認

- ImGuiのWin32入力は既存入力の所有権を壊さない。
- DX12 ImGui backendはfont texture、vertex/index upload、scissor、texture binding、複数DrawListを扱う。
- Asset Browser、Inspector、Hierarchy、Scene View、Canvas Preview、Undo/Redo、保存を操作確認する。
- Canvas Previewだけが動き、Runtime UIだけが古い経路に残る状態を合格にしない。
- `imgui_impl_dx11.cpp/h`を使う経路が残っていないことを、Phase 4完了時に確認する。

## 4. 推奨するDX12 UIデータ／GPU設計

### 4.1 Root Signature / PSO

UI用Root Signatureは用途ごとに増殖させない。最低限、次の共通構成を基準にする。

- b0: UI object / transform / clip / color
- b1: visual / effect constants
- t0: UI textureまたはFont Atlas
- t1以降: Effect入力、Mask、Backdropなどの入力
- s0: Linear/Point sampler
- Blend、Depth、Rasterizer、RTV FormatはPSOで固定する

Blend variantは既存の実装に対応する数だけ作る。MaterialやTextureごとにRoot Signatureを
作らない。PSOキーにはShader variant、Blend、Mask、Effect入力数、RTV Formatを含める。

### 4.2 DescriptorとTexture Cache

- Asset GUIDを正規キーとし、パス文字列だけでGPU Textureを重複生成しない。
- White Texture、Font Atlas、Missing Textureを予約Descriptorとして確保する。
- Descriptorの解放はFence完了後に遅延する。
- Scene / Asset Reload時はCPU cacheとGPU cacheのRevisionを一致させる。
- Descriptor heap満杯時にNULLを無条件で渡さず、容量不足を診断ログへ出して安全な代替へ戻す。

### 4.3 フレーム寿命

UI頂点、Index、定数は`D3D12FrameResource`のUpload領域へ置く。毎フレーム再利用する領域は、
そのフレームのFenceが完了してから再利用する。Font Atlas、Effect RT、Texture Uploadも同じ
寿命規則に従う。

## 5. 実装順序と受け入れテスト

```
[4-0] UI command生成とDX12 resource cacheの境界を固定
  └ D3D11 UIRendererへ影響を出さずにテスト可能にする

[4-1] Sprite / Shape / Clip / Blend
  └ 1 Canvas、画像、Shape、Nested Clip、全Blendを確認

[4-2] FontAtlas / Text / RichText
  └ 日本語、改行、Atlas再生成、Fallbackを確認

[4-3] Mask / Scroll / InputField / Selectable
  └ キーボード、IME、マウス、Focus、Undoを確認

[4-4] Effect / Backdrop / Offscreen Pool
  └ Glow、Blur、Shadow、Backdrop、Nested Effect、Resizeを確認

[4-5] Editor / Runtime統合
  └ Editor操作、Runtime操作、保存／再読込、DX12 Debug Layerを確認

[4-6] Phase 4受け入れ
  └ Debug / Release / GPU Validation / Golden / Live Objectを確認
```

### 最低限の受け入れScene

専用のUI検証Sceneを作る場合も、既存Componentと既存保存形式を使う。

1. Sprite、Shape、Text、RichTextを1 Canvasへ配置
2. Nested MaskとScrollViewを配置
3. InputField、Button、Toggle、Sliderを配置
4. Font Atlasの再生成が発生する日本語文字列を配置
5. Glow、Blur、Backdrop、Shadow、Maskを同時に配置
6. Scene3D、Shadow、Particle、Trailの上にUIを表示
7. Resize、Minimize/Restore、Scene Reload、Asset Reloadを実行

### Phase 4のPASS条件

- Debug / Releaseが警告・エラーなくビルドできる。
- RuntimeのUI描画がDX12 Command Listだけで完了する。
- Runtime UIでD3D11のDevice / Context / View / Stateへ到達しない。
- Sprite、Shape、Text、Mask、Effect、Backdropが実データで表示される。
- InputField、Focus、Selectable、Scroll、Sliderが操作できる。
- ImGui、Canvas Preview、Runtime UIの入力と描画が同時に成立する。
- UI Texture、Font Atlas、Effect RTがResize / Reload / 終了時に解放される。
- Debug Layer、GPU-Based Validation、Releaseの各経路で未解決エラーがない。
- Golden画像とログに、UIが欠落していないことを確認できる。
- 固定色、固定ゲイン、ダミー頂点、常時表示の診断UIで合格扱いにしていない。
- HLSLは外部ファイルにあり、CPPへ埋め込まれていない。

## 6. 検証コマンドと診断

Phase 3のDX12検証を毎回先に通す。

```powershell
& "C:\Users\2250298\Desktop\teamProject_2_3\x64\Release\3dgp.exe" --validate-dx12-device
& "C:\Users\2250298\Desktop\teamProject_2_3\x64\Debug\3dgp.exe" --validate-dx12-device
& "C:\Users\2250298\Desktop\teamProject_2_3\x64\Debug\3dgp.exe" --validate-dx12-gpu
```

UI検証では、次をProfileへ追加する。

- UI draw command数、頂点数、Index数、Texture数
- Font Atlas数、Glyph数、Atlas再生成回数
- Mask階層数、Effect RT pool借用数／返却数
- Descriptor cache hit / miss / eviction
- Clip外へ捨てたcommand数
- UI入力対象、Focus対象、Pointer capture
- Scene3D描画、PostProcess、UIの順序

診断値は実際のcommandとComponentから算出する。固定の非0値を出してはいけない。

## 7. 触ってはいけない前提

- GameObject、Component、Canvas、RectTransform、FontAtlas、Focusの責務をGPU backendへ移さない。
- UIが見えないときにScene照明、Exposure、Tone Mappingへ固定ゲインを入れない。
- D3D11型を新しいDX12 UI層へ持ち込まない。
- Resource Stateを「たぶんRTV」「たぶんSRV」で扱わない。pass境界で明示する。
- Fence完了前のUpload／Descriptor／RenderTargetを再利用しない。
- `imgui_impl_dx11`をPhase 4の暫定実装として残したままPASSにしない。
- HLSLをCPPへ戻さない。新規Shaderは`Shader\*.hlsl` / `*.hlsli`へ置き、プロジェクトへ登録する。
- `phase`を含むCPP名を新設しない。コメントは日本語で追加する。
- 現在の作業ツリー全体をリセットしない。無関係な変更を保全する。

## 8. Phase 5への引き渡し

Phase 4 PASS後も、旧D3D11資産をすぐ削除してはいけない。次を記録してからPhase 5へ渡す。

1. UI全機能のDX12 Golden画像と操作結果
2. DX12 Debug Layer / GPU Validation / Releaseのログ
3. D3D12 Live Object、DXGI Live Object、Descriptor／Upload統計
4. Runtime UI、Editor ImGui、Scene3D、PostProcessのフレーム順
5. `rg`によるD3D11参照の棚卸し結果
6. `3dgp.vcxproj` / `.filters` / `.sln`に残る旧backend参照の一覧

## 9. コミットメッセージ例

```text
DX12 Runtime UIのSpriteとClipを接続
DX12 UIのFontAtlasとText描画を接続
DX12 UIのEffectとBackdropを接続
DX12 Phase 4のRuntime UIを検証
```
