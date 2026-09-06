# Landscape / Inspector 修正報告

2026-09-06。Release x64。対象は添付の修正作業指示書の Phase 1〜4。

## 結果

Inspectorの表示・Scene View入力からUndo開始処理を切り離した。Landscapeの編集候補とプレビューは同じ連結Surface検索を使い、共有頂点法線・低FPS時のストローク補間・Bounds更新を修正した。通常のScene UndoはLandscape形状の不変スナップショットを共有し、SubdivideはLandscape専用Undoへ移した。

自動回帰検証は通過。実際の重いモデルを選択してカメラ操作する手動FPS比較、長時間編集の操作確認は未実施。以下のCPU値は自動検証の局所処理時間であり、Scene View全体のFPSではない。

## 原因と対応

| 原因 | 対応と効果 |
| --- | --- |
| Inspectorがアプリ全体のマウス入力を編集とみなす | PropertyDrawerの実際のsetter直前にコールバックを呼ぶ。最初の変更前の値を保存し、対象ImGui ActiveIdが終了したら自分のtransactionだけを確定する。パネルが閉じても毎フレーム確定判定を行う。 |
| Multi Selectionが表示だけでSnapshotを作る | 同じsetter直前コールバックを使う。表示だけではBeginEditを呼ばない。 |
| 汎用Undoが巨大Landscapeを文字列化する | File/Undo Captureを分け、Undoではmesh_dataを除外し、不変のLandscapeGeometry共有参照を保存する。通常ファイル保存は従来経路。 |
| SubdivideがScene全体Undoを使う | 一連のSubdivideをLandscapeUndoCommandの形状before/afterにまとめる。他のObjectを複製しない。 |
| LocalYがXZ距離だけで別Surfaceも編集する | Hit Faceから隣接Faceをたどり、3D半径内の連結Surfaceに頂点候補を限定。LocalYは移動方向だけをYに限定する。 |
| Previewが真上からのRaycastで地上へ吸着する | 同じSurfaceRegion内でHit法線の両方向へ短い投影を行う。Face表示・Gridも同じ局所領域を使う。 |
| Chunkごとの法線初期化で共有頂点の別Chunk寄与を失う | 変更頂点の影響範囲を求め、各頂点の全隣接Faceから法線を再計算。法線を共有する描画Chunkもdirtyにする。 |
| 1フレーム1サンプルで低FPS時に隙間が生じる | 半径の15%を間隔とする位置補間とdt分配。同じ連結Surface上で投影し、Surfaceを外れたら前回点を切る。 |
| Boundsが拡大するだけ | ストローク終了時に全Boundsを再計算して縮小。水平移動またはTopology変更後は終了時にChunkを再分割する。 |
| 大域Candidate検索・Smooth隣接重複 | 頂点→Face、頂点→重複除去済み隣接頂点を保持し、局所検索に使う。Subdivideは変更Faceの隣接・所属Chunkを増分更新する。 |
| SculptごとのCollision/GPU更新が広すぎる | 移動頂点を含むCollision Chunkだけ更新。通常Sculptは既存Index Bufferとlocal remapを再利用し、頂点Bufferだけ差し替える。古いBufferはFence完了まで保持する。 |
| Material/Rig/設定保存/Debugの余分な処理 | Material名と読込済みshading metadataをキャッシュ。不要なRig表示準備を止め、速度設定の保存を遅延。非選択AI・Stage表示は明示トグルを有効にした時だけ全Scene走査する。 |

## Undoと互換性

- 通常のSculpt: 変更頂点のbefore/after差分をLandscape専用履歴へ記録する。
- Subdivide: Landscape形状だけのbefore/afterスナップショットを専用履歴へ記録する。Topology差分ではなく、指示書で許容された全Landscape保存の代替案。
- Transform等の汎用編集: 既存Scene履歴を維持し、Landscape形状部分はshared_ptr<const LandscapeGeometry>で共有する。形状更新後の初回Captureではバイナリ形状コピーが一度発生する。
- 通常のScene保存ではLandscapeを完全保存する。Component ID、Property ID、既存Serialization key、Scene/Landscapeファイル形式は変更していない。
- キャッシュ識別に単調増加のGeometry/Topology revisionを使い、復元後に古いPreview/GPUキャッシュを取り違えないようにした。

## 検証と計測

計測は同一PCのRelease x64、自動検証1回の値。Beforeの実時間は未取得のため倍率改善は主張しない。

| 項目 | Before（コード・既存検証の確認） | After（実測または検証） |
| --- | --- | --- |
| 単体・複数選択Inspector、Scene View相当の入力120フレーム | 全体マウス入力/表示からCaptureを開始 | Scene/Capture合計0 |
| Checkbox編集 | 誤入力依存の開始条件 | setter前に開始、before/afterのCapture計2回、初回変更をUndo可能 |
| 502,681頂点のLandscapeを含む汎用Undo | 毎回mesh_dataを文字列化 | 2回のウォームCapture合計0.0813ms、同一形状ポインタ共有、mesh_data無し |
| Inspector描画120フレーム | Before時間未計測 | 合計2.4709ms（平均約0.0206ms）。ImGuiフレーム生成等を含む簡易測定、選択モデルはPrimitive |
| 小ブラシCollision更新の既存検証ケース | 4 Chunk / 14,450 triangle | 1 Chunk / 3,613 triangle。全体Cookとの判定比較も通過 |
| 120/60/30/15 FPSの同一路線Sculpt | フレーム毎に1サンプル | 120 FPS形状に対する最大誤差は0 / 0.013% / 2.760% / 2.742%（基準形状の最大高さで正規化） |
| 通常Sculpt GPU更新 | 頂点・Indexを再生成 | Index BufferのGPUアドレスを維持し頂点Bufferだけ変更。元Buffer破棄後も新Meshが有効 |

小ブラシの局所処理（ウォーム検索100回、Brush20回、Subdivide3回の平均、単位ms）。BrushはHit取得を含む。Subdivide列は編集後Face数で、開始/終了時のUndoコピーと終了時の全再分割は含めない。

| 初期Face数 | 候補Face数 | SurfaceQuery | Brush | Subdivide後Face数 | Subdivide |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 10,368 | 42 | 0.002559 | 0.027085 | 12,342 | 0.385733 |
| 100,352 | 42 | 0.002616 | 0.045035 | 102,326 | 0.444700 |
| 1,002,528 | 42 | 0.002575 | 0.096855 | 1,004,502 | 0.831233 |

実行した確認:

- Release x64ビルド。
- `--validate-landscape`: Raise/Lower/Smooth/FlattenのUndo/Redo完全一致、上下Surface分離、Preview投影、Chunk境界の部分法線と全再計算法線一致、Bounds縮小、Subdivide Undo/RedoとCapture 0、規模別候補数、低FPS形状、汎用Undo共有、実ImGuiの入力分離とCheckbox編集。OVERALL PASS。
- `--validate-dx12`: 204項目通過。頂点更新でIndex共有、Preload経路、キャッシュBounds更新を追加検証。
- `--validate-dx12 --validate-dx12-gpu`: Debug Layer/GPU Validation/DREDを有効にし、204項目通過。終了時には既存のlive object診断出力があるため、この結果をGPUリソース漏れが無い証明とは扱わない。
- 既存DX12検証の、失敗しても終了コードが成功になる問題と、再Preload時にも必ずキャッシュ件数が増えると期待する問題を修正した。
- `git diff --check`。

検証用Sceneは `Saved/Validation/LandscapeRegression` に生成する。01_Normal、02_Subdivided、03_CaveOverhang、04_ChunkBoundary、05_HeavySkinned、06_HeavyMaterials。重いモデルの2シーンは既存Stocking GLB（970ボーン・28 primitive slot）を参照する。これらは操作確認用fixtureであり、選択/Inspector開閉の実FPS測定結果ではない。Saved配下はGit管理外なので、検証コマンドで再生成できる。

## Profilerの対象と未計測範囲

既存の `Scene/Capture`、`Editor/Inspector`、`Landscape/Viewport`、`Landscape/Preview`、`Landscape/Brush`、`Landscape/ChunkUpload`、`Item/RigPose`、`Item/BonePalette` のスコープを維持した。

Capture回数・局所Brush・簡易Inspector時間は上記の自動検証で計測した。8スコープすべてを実Scene View上で同時採取する作業、重いモデルの選択有無・Inspector開閉・RMB操作による比較は未実施。既存 `--profile-scene` はRuntime専用でEditorを無効化するため、この比較の代用には使っていない。

## 残る制約

1. ストローク終了時のBounds再計算、必要時のChunk再分割、Subdivide前後の形状コピーは全Landscape規模に依存する。小ブラシのサンプル処理時間と分けて評価が必要。
2. GPU更新はIndex Buffer再利用まで。dirty Chunk全頂点（位置・法線・UV）の新しいBufferを確保する方式で、同一Bufferへのdirty range更新は未実装。
3. Previewの投影は局所Faceだけを対象にするが、毎フレーム各リング点の局所交差判定は残る。大ブラシや極端な高密度領域では候補領域の大きさに応じて増える。
4. Surface検索は半径内で連結する三角形に限定する方式で、厳密な測地距離ではない。極端な折り返し形状の見た目は追加確認が必要。
5. Material Inspectorは読込済みmetadataを参照し、毎フレームのファイル更新時刻照会を廃止した。通常のMaterialロード機構が持つ更新時刻確認までは変更していない。
6. 実際の操作での長時間編集、重いSkinnedモデルの全モード、外部Material編集後のUI反映は手動確認が残る。完全解決・FPS向上率は未確認。

## 変更ファイル・関数一覧

今回の作業全体を記載（途中コミットに入った変更も含む）。

| ファイル | 主な関数・型 | 理由 |
| --- | --- | --- |
| [RePlayEngine/Editor/Inspector/InspectorPanel.cpp](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Editor/Inspector/InspectorPanel.cpp) | BeginPropertyEdit / FinishPropertyEdit / DrawContents | 実変更だけのtransactionと所有権管理 |
| [RePlayEngine/Editor/Inspector/InspectorPanel.h](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Editor/Inspector/InspectorPanel.h) | BeginPropertyEdit / FinishPropertyEdit / DrawContents | 実変更だけのtransactionと所有権管理 |
| [RePlayEngine/Editor/Inspector/InspectorPanelComponents.cpp](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Editor/Inspector/InspectorPanelComponents.cpp) | DrawComponent / DrawCommonComponent | 単体・複数選択へsetter前コールバックを接続 |
| [RePlayEngine/Editor/Inspector/InspectorPanelMultiSelection.cpp](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Editor/Inspector/InspectorPanelMultiSelection.cpp) | DrawComponent / DrawCommonComponent | 単体・複数選択へsetter前コールバックを接続 |
| [RePlayEngine/Editor/Inspector/PropertyDrawer.cpp](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Editor/Inspector/PropertyDrawer.cpp) | Draw / DrawAll | setter前通知と読込済みMaterial情報参照 |
| [RePlayEngine/Editor/Inspector/PropertyDrawer.h](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Editor/Inspector/PropertyDrawer.h) | Draw / DrawAll | setter前通知と読込済みMaterial情報参照 |
| [RePlayEngine/Editor/Inspector/PropertyDrawerDraw.cpp](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Editor/Inspector/PropertyDrawerDraw.cpp) | Draw / DrawAll | setter前通知と読込済みMaterial情報参照 |
| [RePlayEngine/Editor/Commands/SceneEditHistory.cpp](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Editor/Commands/SceneEditHistory.cpp) | Begin / Commit | Undo専用Captureモード |
| [RePlayEngine/Scene/Serialization/SceneData.cpp](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Scene/Serialization/SceneData.cpp) | CaptureScene / SceneCaptureCount / ComponentData | 不変形状共有・Capture回数計測 |
| [RePlayEngine/Scene/Serialization/SceneData.h](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Scene/Serialization/SceneData.h) | CaptureScene / SceneCaptureCount / ComponentData | 不変形状共有・Capture回数計測 |
| [RePlayEngine/Scene/Serialization/SceneDataApply.cpp](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Scene/Serialization/SceneDataApply.cpp) | ApplySceneData | 共有Landscape形状の復元 |
| [RePlayEngine/Reflection/Registry/PropertyRegistry.cpp](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Reflection/Registry/PropertyRegistry.cpp) | Capture | Undo時にcustom Serializeを省略可能にする |
| [RePlayEngine/Reflection/Registry/PropertyRegistry.h](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Reflection/Registry/PropertyRegistry.h) | Capture | Undo時にcustom Serializeを省略可能にする |
| [RePlayEngine/Landscape/LandscapeData.cpp](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Landscape/LandscapeData.cpp) | QuerySurface / ProjectSurface / TouchGeometry / FinishSculpt / CaptureGeometry / UpdateSubdivisionAdjacency | 局所Surface・法線・Bounds・形状共有・増分Topology |
| [RePlayEngine/Landscape/LandscapeData.h](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Landscape/LandscapeData.h) | QuerySurface / ProjectSurface / TouchGeometry / FinishSculpt / CaptureGeometry / UpdateSubdivisionAdjacency | 局所Surface・法線・Bounds・形状共有・増分Topology |
| [RePlayEngine/Landscape/LandscapeDataTopology.cpp](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Landscape/LandscapeDataTopology.cpp) | SubdivideFace | 変更Faceの隣接と所属Chunk更新 |
| [RePlayEngine/Landscape/LandscapeEditorTool.cpp](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Landscape/LandscapeEditorTool.cpp) | BeginStroke / ApplyStrokeSample / ApplySurfaceSample / ApplySubdivideSample / EndStroke | Surface候補・補間・専用Undo |
| [RePlayEngine/Landscape/LandscapeEditorTool.h](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Landscape/LandscapeEditorTool.h) | BeginStroke / ApplyStrokeSample / ApplySurfaceSample / ApplySubdivideSample / EndStroke | Surface候補・補間・専用Undo |
| [RePlayEngine/Landscape/LandscapeUndoCommand.cpp](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Landscape/LandscapeUndoCommand.cpp) | BeginTopology / EndTopology / Undo / Redo | 形状Snapshotと通常頂点差分の復元 |
| [RePlayEngine/Landscape/LandscapeUndoCommand.h](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Landscape/LandscapeUndoCommand.h) | BeginTopology / EndTopology / Undo / Redo | 形状Snapshotと通常頂点差分の復元 |
| [RePlayEngine/Landscape/LandscapeMeshGenerator.cpp](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Landscape/LandscapeMeshGenerator.cpp) | Generate | 指定Chunkだけの頂点とIndexを生成 |
| [Source/app/Editor/framework_landscape_editor.cpp](C:/Users/2250298/Desktop/teamProject_2_3/Source/app/Editor/framework_landscape_editor.cpp) | Landscape stroke開始・適用・終了処理 | Subdivide専用履歴、対象ObjectだけのCollider更新 |
| [Source/app/Editor/framework_landscape_editor_viewport.cpp](C:/Users/2250298/Desktop/teamProject_2_3/Source/app/Editor/framework_landscape_editor_viewport.cpp) | Landscape stroke開始・適用・終了処理 | Subdivide専用履歴、対象ObjectだけのCollider更新 |
| [Source/app/Editor/framework_landscape_editorInternal.h](C:/Users/2250298/Desktop/teamProject_2_3/Source/app/Editor/framework_landscape_editorInternal.h) | TerrainRingCache / Surface Preview helpers | 同じSurfaceRegionでリング・Face・Grid表示 |
| [RePlayEngine/Rendering/DX12/D3D12MeshBuffer.cpp](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Rendering/DX12/D3D12MeshBuffer.cpp) | UploadVerticesSharingIndices | 既存Indexリソースを共有して新規頂点Bufferを作成 |
| [RePlayEngine/Rendering/DX12/D3D12MeshBuffer.h](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Rendering/DX12/D3D12MeshBuffer.h) | UploadVerticesSharingIndices | 既存Indexリソースを共有して新規頂点Bufferを作成 |
| [RePlayEngine/Rendering/DX12/D3D12DeviceContext.cpp](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Rendering/DX12/D3D12DeviceContext.cpp) | UpdateStaticMeshVertices / D3D12StaticMeshSource | 頂点のみの更新経路とFenceを守った置換 |
| [RePlayEngine/Rendering/DX12/D3D12DeviceContext.h](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Rendering/DX12/D3D12DeviceContext.h) | UpdateStaticMeshVertices / D3D12StaticMeshSource | 頂点のみの更新経路とFenceを守った置換 |
| [RePlayEngine/Rendering/DX12/D3D12SceneRenderer.cpp](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Rendering/DX12/D3D12SceneRenderer.cpp) | Preload / DrawScene3D | 頂点のみの更新ソースを受け付ける |
| [Source/app/Runtime/framework_gameobject_scene_rendering_draw.cpp](C:/Users/2250298/Desktop/teamProject_2_3/Source/app/Runtime/framework_gameobject_scene_rendering_draw.cpp) | Scene描画のRig処理 / Landscape ChunkUpload | 不要Rig処理停止、local remapとIndex共有 |
| [Source/app/framework_class_scene_services.inl](C:/Users/2250298/Desktop/teamProject_2_3/Source/app/framework_class_scene_services.inl) | Landscape GPU cache / Motion panel state | Chunk topology revisionと実表示状態の保持 |
| [Source/app/Editor/framework_motion_workspace_layers.cpp](C:/Users/2250298/Desktop/teamProject_2_3/Source/app/Editor/framework_motion_workspace_layers.cpp) | draw_motion_rig_panel / draw_editor | Rigパネルの実表示を追跡、閉じたInspectorの編集終了 |
| [Source/app/Editor/framework_editor_docking.cpp](C:/Users/2250298/Desktop/teamProject_2_3/Source/app/Editor/framework_editor_docking.cpp) | draw_motion_rig_panel / draw_editor | Rigパネルの実表示を追跡、閉じたInspectorの編集終了 |
| [Source/mesh/skinned_mesh.cpp](C:/Users/2250298/Desktop/teamProject_2_3/Source/mesh/skinned_mesh.cpp) | MaterialSubsetNames / InvalidateMaterialSubsetNames | Material subset名の再走査を防ぐ |
| [Source/mesh/skinned_mesh.h](C:/Users/2250298/Desktop/teamProject_2_3/Source/mesh/skinned_mesh.h) | MaterialSubsetNames / InvalidateMaterialSubsetNames | Material subset名の再走査を防ぐ |
| [Source/mesh/skinned_meshGltf.cpp](C:/Users/2250298/Desktop/teamProject_2_3/Source/mesh/skinned_meshGltf.cpp) | glTF mesh load / fetch_meshes | モデル読込時にsubset名キャッシュを破棄 |
| [Source/mesh/skinned_meshImport.cpp](C:/Users/2250298/Desktop/teamProject_2_3/Source/mesh/skinned_meshImport.cpp) | glTF mesh load / fetch_meshes | モデル読込時にsubset名キャッシュを破棄 |
| [Source/app/Editor/framework_inspector.cpp](C:/Users/2250298/Desktop/teamProject_2_3/Source/app/Editor/framework_inspector.cpp) | Material slot Inspector描画 | subset名・文字入力バッファ再利用 |
| [RePlayEngine/Rendering/Materials/MaterialAsset.cpp](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Rendering/Materials/MaterialAsset.cpp) | PublishShadingMetadata / LoadedShadingModel / Save / Load | 明示的な読込・保存時のmetadata更新 |
| [RePlayEngine/Rendering/Materials/MaterialAsset.h](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Rendering/Materials/MaterialAsset.h) | PublishShadingMetadata / LoadedShadingModel / Save / Load | 明示的な読込・保存時のmetadata更新 |
| [RePlayEngine/Rendering/Materials/MaterialAssetLoad.cpp](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Rendering/Materials/MaterialAssetLoad.cpp) | PublishShadingMetadata / LoadedShadingModel / Save / Load | 明示的な読込・保存時のmetadata更新 |
| [Source/app/Editor/framework_asset_browser_material.cpp](C:/Users/2250298/Desktop/teamProject_2_3/Source/app/Editor/framework_asset_browser_material.cpp) | Material preview更新 | 編集中shading metadataの反映 |
| [Source/app/Editor/framework_editor_camera.cpp](C:/Users/2250298/Desktop/teamProject_2_3/Source/app/Editor/framework_editor_camera.cpp) | update_editor_camera / flush_editor_camera_preset_save / switch_editor_camera_preset | 0.75秒無操作・RMB解放時に設定保存を集約 |
| [Source/app/Editor/framework_editor_camera_presets.cpp](C:/Users/2250298/Desktop/teamProject_2_3/Source/app/Editor/framework_editor_camera_presets.cpp) | update_editor_camera / flush_editor_camera_preset_save / switch_editor_camera_preset | 0.75秒無操作・RMB解放時に設定保存を集約 |
| [Source/app/Runtime/framework.cpp](C:/Users/2250298/Desktop/teamProject_2_3/Source/app/Runtime/framework.cpp) | uninitialize | 終了時に未保存Camera速度を確定 |
| [Source/app/framework_class_editor_state.inl](C:/Users/2250298/Desktop/teamProject_2_3/Source/app/framework_class_editor_state.inl) | Camera保存状態・flush宣言 / Debug toggle | 保存遅延と表示条件の保持 |
| [Source/app/framework_class_runtime_scene.inl](C:/Users/2250298/Desktop/teamProject_2_3/Source/app/framework_class_runtime_scene.inl) | Camera保存状態・flush宣言 / Debug toggle | 保存遅延と表示条件の保持 |
| [RePlayEngine/Editor/Debug/AINavigationDebugDraw.cpp](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Editor/Debug/AINavigationDebugDraw.cpp) | Build | 選択Objectだけを直接取得できる経路 |
| [RePlayEngine/Editor/Debug/AINavigationDebugDraw.h](C:/Users/2250298/Desktop/teamProject_2_3/RePlayEngine/Editor/Debug/AINavigationDebugDraw.h) | Build | 選択Objectだけを直接取得できる経路 |
| [Source/app/Editor/framework_collider_debug.cpp](C:/Users/2250298/Desktop/teamProject_2_3/Source/app/Editor/framework_collider_debug.cpp) | draw_collider_debug_overlay / draw_collision_diagnostics_panel | 非選択AI・Stage全走査を明示表示時だけに限定 |
| [Source/app/Runtime/mainValidationLandscape.cpp](C:/Users/2250298/Desktop/teamProject_2_3/Source/app/Runtime/mainValidationLandscape.cpp) | RunLandscapeEditorRegression / Landscape validation | 回帰・規模別計測・6種fixture生成、局所Cook期待値 |
| [Source/app/Runtime/mainValidationLandscapeRegression.inl](C:/Users/2250298/Desktop/teamProject_2_3/Source/app/Runtime/mainValidationLandscapeRegression.inl) | RunLandscapeEditorRegression / Landscape validation | 回帰・規模別計測・6種fixture生成、局所Cook期待値 |
| [Source/app/Runtime/mainValidationDX12.cpp](C:/Users/2250298/Desktop/teamProject_2_3/Source/app/Runtime/mainValidationDX12.cpp) | Check / DX12 validation | Index共有の寿命と更新検証、失敗終了コード修正 |

## ログ

- [Saved/Validation/LandscapeRegression/results.txt](C:/Users/2250298/Desktop/teamProject_2_3/Saved/Validation/LandscapeRegression/results.txt)
- [Saved/Build/landscape_fix_build.log](C:/Users/2250298/Desktop/teamProject_2_3/Saved/Build/landscape_fix_build.log)
- [Saved/Build/landscape_validation_final.log](C:/Users/2250298/Desktop/teamProject_2_3/Saved/Build/landscape_validation_final.log)
- [Saved/Build/landscape_dx12_validation.log](C:/Users/2250298/Desktop/teamProject_2_3/Saved/Build/landscape_dx12_validation.log)
- [GPU検証ログ](C:/Users/2250298/Desktop/teamProject_2_3/Saved/Build/landscape_dx12_gpu_validation.log)

## コミット

実装修正: `73b7f35` — LandscapeのSurface編集とUndoを修正しEditorの更新負荷を削減。検証コードと本報告は別コミットで記録する。
