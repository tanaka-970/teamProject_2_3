# DX12 Phase 5 引き継ぎ書

最終更新: 2026-08-25
対象ワークスペース: `C:\Users\2250298\Desktop\teamProject_2_3`
状態: **Phase 5 未着手。Phase 4のRuntime UI受け入れ後に、DX11完全撤去と最終製品検証を行う。**

Phase 5の目的は、DX12経路を追加した状態で止めることではない。製品の実行経路、Editor経路、
テスト経路、プロジェクト参照、Shader／Texture／Capture／ProfilerをDX12へ統一し、旧D3D11経路を
削除してもDebug / Release / 実機描画 / 終了処理が成立する状態にすることである。

## 1. Phase 5開始条件

Phase 4が次を満たすまで、Phase 5の削除作業を開始しない。

- Runtime UIのSprite、Shape、Text、Mask、Effect、BackdropがDX12で受け入れ済み
- InputField、Focus、Selectable、Scroll、Sliderが実機で操作できる
- Editor ImGui、Canvas Preview、Runtime UIを同一DX12実行で確認済み
- UI Texture、Font Atlas、Effect RTのResize / Reload / Shutdownが確認済み
- Debug / Release / GPU-Based Validationが通る
- Golden画像と入力回帰結果が保存されている
- Phase 4の未解決TODO、仮実装、診断用の常時描画がない

Phase 4が未完了のままDX11を削除すると、失敗時に比較対象がなくなり、原因を切り分けられない。

## 2. 現在残っている旧経路の分類

現時点では、プロジェクト内にD3D11依存が残っている。これはPhase 3の失敗ではなく、Phase 4/5の
明示的な残作業である。削除前に毎回実測して一覧を更新する。

### 2.1 Runtime / Renderer

優先して棚卸しする対象:

- `Source\app\Runtime\framework_initialize.cpp`
- `Source\app\Runtime\framework.cpp`
- `Source\app\Runtime\framework_class.h` と関連 `.inl`
- `Source\app\Rendering\framework_render*.cpp/.inl`
- `Source\app\Rendering\framework_effect_stack.cpp`
- `Source\app\Rendering\framework_model_effects.cpp`
- `Source\render\csm_renderer.*`
- `Source\render\trail.*`
- `Source\render\particle_system.*`
- `Source\core\texture.*`
- `Source\core\shader.*`
- `Source\core\framebuffer.*`
- `Source\mesh\*` のD3D11 Buffer / View生成部分

ファイル名は例であり、削除対象を名前だけで決めない。DX12側に機能が移ったこと、呼び出し元が
無くなったこと、プロジェクトから参照されていないことを順に確認する。

### 2.2 UI / Editor

- `RePlayEngine\UI\UIRenderer.*`
- `RePlayEngine\UI\Effects\UIRenderTargetPool.*`
- UIのD3D11用Shader / CSO / State生成
- `imgui\imgui_impl_dx11.cpp`
- `imgui\imgui_impl_dx11.h`
- `3dgp.vcxproj` / `3dgp.vcxproj.filters`の旧ImGui backend参照

ImGuiの既存バージョンを勝手に更新しない。Phase 4で使用した同じImGui APIをDX12描画へ接続し、
不要になったDX11 backendだけをプロジェクトから外す。

### 2.3 Shader / Asset / Build

- `#include <d3d11.h>`、`<d3d11_*.h>`
- `ID3D11*`、`D3D11_*`
- `D3D11CreateDeviceAndSwapChain`
- `D3DCompile` / fxc専用のShaderCompiler経路
- `imgui_impl_dx11`、`DirectXTK-main`の製品側参照
- `.cso`生成規則、Shader Item設定、出力先
- `d3d11.lib`、旧DXGI factory初期化、旧Debug Layer初期化

DXGIはDX12でも使用するため、`dxgi.h`や`dxgi.lib`を文字列検索だけで一括削除しない。
削除するのは「D3D11専用の使用箇所」である。

## 3. 完全撤去の実装順序

### P0: DX12の単一実行経路を固定

1. 起動時のRenderer選択をDX12だけにする。
2. `--dx12-framework`などの移行用フラグを、製品仕様として残すか通常起動へ統合するか決める。
3. D3D11へフォールバックする暗黙の分岐を、ログで全件確認する。
4. Runtime、Editor、Golden、Validation、Shutdownが同じDX12 DeviceContextを使うことを確認する。
5. DX12未初期化時にD3D11へ逃げる処理を、無言のfallbackではなく明示的な起動エラーへする。

### P1: 旧描画クラスと旧リソースの置換確認

- D3D11のFrameBuffer、Fullscreen Quad、Shader loader、Texture loaderをDX12実装へ置換する。
- Static / Skinned / Primitive / Landscape / Particle / Trail / Line / UI / Shadowの各提出元が、
  DX12のsubmission境界を通ることを確認する。
- Scene3DのGBuffer 5枚、HDR target、Shadow target、PostProcess target、Back Bufferの状態遷移を
  `D3D12ResourceStateTracker`で一貫して管理する。
- Capture / ReadbackはFence待機後に行い、D3D11のImmediate Context依存を残さない。
- RenderStatsはD3D12 QueryまたはCPU側の実提出統計を正本とし、D3D11 Queryを使わない。

### P2: Shader / Texture / DirectXTKの整理

1. HLSLは外部ファイルを正本とし、DXCのコンパイル結果とSourceを対応付ける。
2. Phase 4で使ったUI Shader、Font Shader、Effect Shaderを含めて、Debug / Releaseで再コンパイルする。
3. Texture読み込みはDX12 Upload ContextとResource State Trackerを通す。
4. DDS / WICの読み込みがDirectXTKに残っている場合は、使用中の版に合うDX12ローダーまたは
   自前実装へ置き換える。
5. 使用箇所が0であることを確認してから、旧`DirectXTK-main`をソリューションから外す。

DirectXTKやImGuiを削除すること自体を目的にしない。製品経路で使われていないこと、代替経路の
実機確認が済んでいること、ビルド参照が消えていることの3条件を満たしたときだけ削除する。

### P3: Project / Build / Runtimeの整理

- `3dgp.vcxproj`のD3D11 Source、Header、Shader、Library参照を整理する。
- `3dgp.vcxproj.filters`から削除済みファイルの参照を消す。
- `.sln`の旧DirectXTK project依存を整理する。
- Debug / Release、x64、Incremental Build、Clean Buildをそれぞれ確認する。
- 配布用フォルダへ不要なD3D11 CSO、backend DLL、旧Shaderをコピーしない。
- 起動、Scene Reload、Asset Reload、Resize、Minimize/Restore、Shutdownを全構成で確認する。

### P4: 最終最適化

最適化は機能受け入れ後に行う。見た目を合わせるための固定ゲインや固定遅延を追加しない。

- Descriptor Heapの使用率、Fragmentation、再利用回数を計測する。
- Upload HeapのFrameごとの使用量とピークを計測する。
- PSO Cache hit / miss、Shader compile時間、初回表示時間を計測する。
- Texture / Mesh / Font Atlasの重複Uploadをなくす。
- Shadow、UI Effect、PostProcessのRT poolをFence安全な範囲で再利用する。
- CPU draw command生成とGPU待機をプロファイルし、無意味なFlush / Waitを減らす。
- Debug Layerを無効にしたReleaseでのフレーム時間と、Debug Layer / GPU Validationの検証時間を
  混同しない。
- 最適化後もGolden画像、149 checks、Live Object、操作回帰を再実行する。

## 4. DX11削除の安全な判定

### 4.1 検索で確認するもの

削除前に次の検索を行い、結果を保存する。

```powershell
Set-Location "C:\Users\2250298\Desktop\teamProject_2_3"

rg -n -i --glob '!x64/**' --glob '!Saved/**' --glob '!Shader/compiled/**' `
  'ID3D11|D3D11_|d3d11\.h|D3D11CreateDevice|imgui_impl_dx11|D3DCompile|DirectXTK-main' .

rg -n -i --glob '3dgp.vcxproj*' --glob '*.sln' `
  'd3d11|imgui_impl_dx11|DirectXTK-main|D3D11' .

rg --files -g '*.cpp' -g '*.h' | rg -i 'phase'
git diff --check
```

検索結果が0件になることを目標にする。ただし、移行検証用の説明書や履歴文書に文字列が残る
場合は、製品ソース・プロジェクト参照・配布物と分けて判定する。コメントだけを消して検索を
0件にすることは禁止する。

### 4.2 削除前に必要な証拠

- D3D11参照元の全件について、DX12代替ファイルまたは不要になった理由がある。
- Clean Buildで成功する。
- DX12の通常起動でScene3D、Runtime UI、Editorが表示される。
- Debug Layer / GPU-Based Validationに未解決エラーがない。
- Releaseで実機のテクスチャ付きStatic / Skinned、Shadow、UI、PostProcessを確認する。
- Resize、Scene Reload、Asset Reload、ShutdownでLive Objectが0になる。
- Golden画像の重要シーンが一致または許容差内である。
- 旧経路を無効化した状態で149 checksがPASSする。

## 5. 最終検証マトリクス

| 構成 | 実行内容 | 必須結果 |
|---|---|---|
| Debug | 通常起動、UI操作、Scene Reload | 表示・操作成功、未処理例外なし |
| Debug Layer | `--validate-dx12-device` | 全check PASS、未解決Errorなし |
| GPU Validation | `--validate-dx12-gpu` | 全check PASS、GPU停止なし |
| Release | Profile Scene群 | PNG / CSV / Trace生成、統計が実値 |
| Release | Texture / Skinned / Shadow / UI | 実機画像が成立 |
| 全構成 | Resize / Minimize / Restore | Resource Stateと表示が復帰 |
| 全構成 | Shutdown / Live Object | D3D12 / DXGI Live Object 0 |
| Clean Build | x64 Debug / Release | 0 error、不要な旧参照なし |
| 配布確認 | 実行フォルダ | D3D11専用DLL / CSOを含まない |

Phase 3で固定した次の検証を基準として、UI追加後も再実行する。

```text
Release --validate-dx12-device : 149 checks PASS
Debug   --validate-dx12-device : 149 checks PASS
Debug   --validate-dx12-gpu    : 149 checks PASS
```

## 6. Phase 5のPASS条件

- Runtime、Editor、Validation、Capture、ProfilerがDX12のみで起動する。
- 製品ソース、プロジェクトファイル、ビルド設定、配布物にD3D11実行依存がない。
- `imgui_impl_dx11`のCompile / Include / Link経路がない。
- 不要なDirectXTK-mainとD3D11専用Shader / CSOが整理されている。
- DX12 Shader、Texture、UI、Shadow、PostProcess、Particle、Trail、Landscapeが実機で表示される。
- Scene Reload、Asset Reload、Resize、Minimize/Restore、ShutdownがDebug / Releaseで成立する。
- Debug Layer、GPU-Based Validation、Release headless validationがPASSする。
- D3D12 Live ObjectとDXGI Live Objectが0である。
- Golden画像、入力操作、Runtime統計、Frame timeの基準値が保存されている。
- HLSLをCPPへ埋め込んでいない。
- CPP名に`phase`を含む新規ファイルがない。
- 固定明るさ、固定ゲイン、ダミー描画、エラー隠しのfallbackがない。
- 無関係な既存作業ツリー変更を破壊していない。

## 7. 削除作業の手順

削除は一括で行わず、機能単位で実施する。

1. 参照元とDX12代替を一覧化する。
2. 旧経路を使わない構成でBuild / Runtime / Validationを通す。
3. プロジェクト参照を外す。
4. Clean Buildを行う。
5. 実機描画・操作・終了を行う。
6. `rg`とLive Objectを再確認する。
7. その単位だけを日本語コミットする。
8. 次の機能単位へ進む。

削除対象が不明な場合は削除しない。まず呼び出し元、代替経路、生成物、配布物を調査する。
`git reset --hard`、`git checkout --`、作業ツリー全体の再生成で未関係変更を消してはいけない。

## 8. Phase 6以降への引き渡し

Phase 5 PASS後に残すものは、新機能のためのDX12抽象と検証基盤である。

- D3D12 resource factory、upload、descriptor、state tracker、frame resource
- SceneRenderCollector / RenderItemのD3D非依存境界
- UI draw commandとRuntime／Editor共通の座標・入力規約
- Shader外部ファイルとDXCコンパイル規約
- Golden、Validation、Live Object、Profileの再現可能なコマンド

DXR、Mesh Shader、Bindless化などは、Phase 5の完全撤去と混ぜない。追加する場合は別フェーズの
設計書と受け入れ条件を作成する。

## 9. コミットメッセージ例

```text
DX12 Runtime UI移行後のD3D11参照を整理
DX12のImGui backendを統一
DX12の旧ShaderとDirectXTK参照を整理
DX12 Phase 5のD3D11完全撤去を完了
DX12最終実機検証と終了処理を固定
```
