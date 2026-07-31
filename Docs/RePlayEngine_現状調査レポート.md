# RePlayEngine 現状調査レポート

作成日: 2026-07-31
目的: GameObject / Component 基盤への段階移行にあたり、既存コードの構造・所有関係・スレッド方針を確定する。
状態: **調査のみ。コード変更は未実施。**

---

## 0. プロジェクト全体像

| 項目 | 内容 |
|---|---|
| ソリューション | `3dgp.sln` / `3dgp.vcxproj` (手動管理、生成スクリプトなし) |
| C++ 規格 | `stdcpp17` |
| ツールセット | `v145` |
| グラフィックス | Direct3D 11 (immediate context のみ、deferred context 未使用) |
| UI | Dear ImGui (docking ブランチ / DockBuilder / 日本語フォント) |
| 外部ライブラリ | DirectXTK、cereal（**FBX メッシュキャッシュ専用**）、tinygltf、imgui |
| JSON/YAML | **未導入** |
| ファイル数 | ClCompile 69 / ClInclude 75 |

### ソースツリーの二層構造

```
Source/            ← 元の学校フレームワーク (3dgp)。framework クラスが全部を所有。
  app/             ← framework 本体。Runtime / Rendering / Editor に分割済み
  core/  game/  mesh/  render/
RePlayEngine/      ← 後付けのエンジンレイヤ (namespace ReplayEngine::*)
  Core/Components/  Scene/  Assets/  Physics/  Rendering/  Editor/  Presentation/
```

`RePlayEngine/` が新しい層で、`Source/` が旧層。両者は `framework.h` が両方 include することで接続されている。
**依存方向は Source → RePlayEngine の一方向**（RePlayEngine から Source を include している箇所はごく一部）。これは良い状態。

---

## 1. 現在のオブジェクト生成方法

動的生成という概念が存在しない。すべて `framework` の**固定スロット**。

```cpp
// Source/app/framework.h
std::unique_ptr<static_mesh>  static_meshes[8];
std::unique_ptr<skinned_mesh> skinned_meshes[8];
std::unique_ptr<gltf_model>   stage_gltf_model;
int  shading_per_skinned[8];      // 添字 = オブジェクト ID の代用
bool outline_per_skinned[8];
ShaderLayerStack shader_layers_skinned[8];
CharacterMaterialProfile character_profiles_skinned[8];
```

- `skinned_meshes[0]` が事実上「プレイヤーの見た目」。
- 描画設定が `[8]` の並列配列で持たれており、**オブジェクトの同一性が配列添字**になっている。
- `framework_initialize.cpp` で LoadingScene のタスクとして一括ロード。

---

## 2. Player / 敵 / 地形 / カメラ / ライトのクラス構造

| 役割 | クラス | 場所 | 備考 |
|---|---|---|---|
| プレイヤー | `Player` | `Source/game/player.h` | position/angle/scale/velocity/transform、移動パラメータ 8 個、アニメクリップ 3 種、`SphereColliderComponent` を**値で**保持。描画メッシュは `skinned_mesh*` 生ポインタ |
| 地形 | `Stage` | `Source/game/stage.h` | transform + `MeshColliderComponent` を値で保持 + `skinned_mesh*` |
| カメラ | `Camera` | `Source/game/camera.h` | view/proj/eye/focus のみ。操作は `FreeCameraController` が別クラス |
| ライト | `lights_manager` | `Source/render/lights_manager.h` | 配列管理。オブジェクト概念なし |
| 敵 | **存在しない** | — | — |

`SceneGame` がこれらを**値メンバとして直接所有**：

```cpp
class SceneGame {
    Camera camera; FreeCameraController controller; Player player; Stage stage;
    bool follow_player; float follow_distance; ...   // カメラ設定も同居
};
```

→ 指示にある「Stage という巨大クラスが地形やプレイヤーを直接所有」という状況にほぼ該当。ただし規模は小さい。

---

## 3. Scene / World 相当の構造 — **2 系統が並走している**

これが今回の改修で最も重要な発見。

### (A) 実行時シーン: `IScene` / `SceneManager`

```
RePlayEngine/Scene/IScene.h        Initialize / Update / Render / IsFinished / RenderMode
RePlayEngine/Scene/SceneManager.h  SetScene / QueueScene / QueueSceneFactory
  ├─ BootLogoScene   (Exclusive 描画)
  ├─ LoadingScene    (Exclusive 描画、std::future で非同期タスク)
  └─ GameScene       → SceneGame → Camera / Player / Stage
```

**画面遷移用**であって、GameObject コンテナではない。

### (B) エディタ用データ: `SceneDocument` / `SceneEntity`

```cpp
// RePlayEngine/Scene/SceneDocument.h
struct SceneEntity {
    EntityId id; std::string name, identifier; bool active;
    std::optional<TransformData>      transform;
    std::optional<ModelRendererData>  model_renderer;
    std::optional<MeshColliderData>   mesh_collider;
    std::optional<GravityData>        gravity;
    std::optional<AnimationData>      animation;
};
class SceneDocument { std::vector<SceneEntity> entities_; EntityId next_id_; ... };
```

**コンポーネントが `std::optional` メンバとして型ごとにハードコードされている。** 新しい Component を足すたびに、
`SceneDocument.h` / `SceneSerializer.cpp` の if-else / `framework_scene_document.cpp` の Inspector / Add Component メニューの
**4 箇所を同時に書き換える必要がある**。これが指示で明確に避けたい構造そのもの。

### 決定的な事実: (B) は描画されていない

`Entities()` の全参照箇所を確認した結果：

```
framework_asset_browser.cpp:248   アセット参照カウント
framework_editor.cpp:332,434,435  Hierarchy 表示 / Project パネル表示
framework_editor_commands.cpp:131 コンソールコマンド
framework_scene_document.cpp:224,324,373,382  選択/保存/読込
```

**描画コード (`framework_render.cpp`) からの参照はゼロ。**
実際の描画は `skinned_meshes[0]` と `game_scene->Gameplay().GetStage()` を直接読んでいる。
`SceneDocument` は「エディタで置いた記録」を保持するだけで、選択中の 1 体のみ
`sync_selected_entity_to_stage()` / `select_scene_entity()` 経由で旧 `Stage` オブジェクトへ手動同期している。

> **含意**: `SceneDocument` は実行時挙動にほぼ影響していないので、
> **中間データ層として作り替えても既存の描画・入力・当たり判定を壊さない。** 移行リスクが極めて低い。

---

## 4. Update / FixedUpdate / Draw の呼び出し元

```
main.cpp
 └─ framework::run()                       [framework.h 内に inline 実装]
     └─ while (WM_QUIT != msg)
         tictoc.tick()
         update(dt)   ─ framework_update.cpp
         render(dt)   ─ framework_render.cpp
```

`framework::update()` の中身（全文が 30 行程度）:

```cpp
async_asset_manager.PumpMainThread();                    // ← ワーカー結果の反映点
game_scene->Gameplay().SetLegacyStageActive(stage_asset_placed);
if (scene_manager.IsExclusive()) { scene_manager.Update(dt); return; }
if (!editor_mode || !edit_mode_active) scene_manager.Update(dt);   // ← Edit 中は停止
ImGui NewFrame → draw_editor();
```

- **`FixedUpdate` は存在しない。** 可変 dt のみ。
- **`LateUpdate` も存在しない。**
- `edit_mode_active` が true の間、ゲーム更新が完全停止する（＝簡易 Edit Mode が既にある）。
- `render()` は `framework_render.cpp` (39 KB)。CSM → GBuffer → Deferred → Forward → Outline → PostProcess を一本で書いている。

---

## 5. オブジェクトの所有者

```
framework  (スタックまたは main の実体)
 ├─ ComPtr<ID3D11*>                      GPU リソース
 ├─ unique_ptr<skinned_mesh>[8] 等        メッシュ
 ├─ SceneManager
 │   └─ unique_ptr<IScene> current_scene_ → GameScene → SceneGame → Camera/Player/Stage (値)
 ├─ SceneDocument editor_scene_document   (値)
 ├─ SceneDocument runtime_scene_document  (値)
 ├─ UndoStack / TransformGizmo / AssetDatabase / AsyncAssetManager / MeshCollisionCooker
 └─ GameScene* game_scene                 ★ 非所有の生ポインタ
```

**Singleton / グローバル変数は実質ゼロ**（ImGui の extern 2 個のみ）。これは非常に良い。
新設計でも Singleton を足す必要はない。

---

## 6. new / delete / unique_ptr / shared_ptr

- 生の `new` / `delete`: ほぼ皆無。
- `std::unique_ptr` + `Microsoft::WRL::ComPtr`: 主力。
- `std::shared_ptr`: **未使用**。

→ 指示の「所有権は unique_ptr、shared_ptr は Asset に限定」は既存方針とそのまま一致する。

---

## 7. Transform 相当の現在の実装 — **3 重に存在**

| 場所 | 形 |
|---|---|
| `Player` | `XMFLOAT3 position/angle/scale` + `XMFLOAT4X4 transform` + `UpdateTransform()` |
| `Stage` | 同上 + `RebuildCollisionMesh()` を伴う setter |
| `Core/Components/TransformComponent.h` | `position/rotation/scale` + `WorldMatrix()` — **どこからも使われていない** |
| `Scene/SceneDocument.h TransformData` | 保存用 POD |

- **親子関係は存在しない。** すべてワールド座標直書き。
- 回転は全部**オイラー角 (XMFLOAT3, ラジアン)**。クォータニオンは未使用。
- 行列合成は一貫して `S * R * T`（行優先、DirectXMath の慣習どおり）。

---

## 8. 描画クラスとゲームロジックの依存関係

```
framework_render.cpp ──読む──> game_scene->Gameplay().GetPlayer().GetTransform()
                     ──読む──> game_scene->Gameplay().GetStage().GetTransform() / GetModel()
                     ──読む──> game_scene->Gameplay().GetCamera().GetView() / GetEye()
Player / Stage ──描画側へは依存しない（skinned_mesh* を持つだけ）
```

依存方向は「描画 → ゲームロジック」の一方向で健全。
問題は「**何を描くか**」が `skinned_meshes[0]` / `Stage` とハードコードされていること。
GameObject 化では、ここに「描画提出リスト」を差し込むのが最小の接続点になる。

---

## 9. 当たり判定とゲームオブジェクトの依存関係

```cpp
// Source/game/raycast.h
namespace GameRaycast {
    bool SphereCastStageDown(const Stage& stage, ...);   // ★ Stage 型に直接依存
}
// SceneGame::Update()
if (legacy_stage_active_) UpdatePlayerGroundFromStage(player, stage);
player.Update(dt, camera);
// 移動後にスイープ球で壁を解決
```

- 衝突形状はすでに `SphereColliderComponent` / `MeshColliderComponent` にコンポーネント化されている（ただし**値メンバとして直接埋め込み**）。
- `GameRaycast` が `Stage` 具象型を受けるので、ここは将来 `MeshColliderComponent` を直接受ける形に薄く変えられる。

---

## 10. Scene / Stage の切り替え方法

```cpp
scene_manager.SetScene(make_unique<BootLogoScene>(), device);
scene_manager.QueueScene(make_unique<LoadingScene>(), device);
scene_manager.QueueSceneFactory([this]{ return make_unique<GameScene>(...); });
```

- `IsFinished()` のポーリングで自動遷移。
- `SetScene` / `QueueScene` は **その場で `Initialize()` を呼ぶ**（遅延しない）。
- **切り替え時に `game_scene` 生ポインタが更新されない経路がある**（下記 15 参照）。

---

## 11. Editor の構造と UI ライブラリ

**ImGui (docking)**。既に本格的な作りがある。

```
F1 : editor_mode トグル（Editor 表示 ON/OFF）
F3 : edit_mode_active トグル（ゲーム更新の停止 = Edit Mode）
F2 : RenderGraph 出力切替
Ctrl+S / Ctrl+Shift+S : シーン保存
Ctrl+Z / Ctrl+Y : Undo / Redo
Ctrl+D : 複製   Ctrl+C / Ctrl+V : コピー/貼付   Ctrl+F : 検索
W / E / R : ギズモ Translate / Rotate / Scale
```

パネル構成（DockBuilder で自動レイアウト）:

| パネル | 実装 |
|---|---|
| 階層 | `framework::draw_scene_hierarchy()` — framework_editor.cpp:318 |
| インスペクター | `framework::draw_inspector()` — framework_inspector.cpp:99 |
| プロジェクト | `framework::draw_project_panel()` |
| コンソール | `framework::draw_console_panel()` (テキストコマンド実行) |
| ワークスペース | `framework::draw_workspace_panel()` |
| 検索結果 | `framework::draw_search_results()` |

その他の既存エディタ機能:
- `TransformGizmo` (`RePlayEngine/Editor/Gizmo/`) — 移動/回転/拡縮
- `ViewportPicker` — ビューポートのピッキング
- `UndoStack` (`RePlayEngine/Editor/Commands/`) — **SceneDocument 全体のスナップショット方式**
- `ShaderStackEditor` / `ShaderPresetEditor` / `CharacterMaterialEditor` / `PostProcessEditor`

### Editor の 3 つの構造的問題

1. **全パネルが `framework` のメンバ関数**。`framework.h` に private メンバ関数が 40 個以上並ぶ。
2. **階層が固定項目**。`ワールド / メインカメラ / プレイヤー / ステージ / ライト / 描画設定 / ポスト処理` が
   `enum editor_selection` でハードコードされ、その後ろに SceneEntity 一覧がぶら下がる混成構造。
3. **Inspector も Add Component も型ごとの直書き**（`framework_scene_document.cpp:591-760`）:

```cpp
if (entity->transform)       { ...ImGui... if(Button("削除##Transform")) entity->transform.reset(); }
if (entity->model_renderer)  { ... }
if (entity->mesh_collider)   { ... }
if (entity->gravity)         { ... }
if (entity->animation)       { ... }
// Add Component ポップアップ
add("Transform",      &SceneEntity::transform);
add("ModelRenderer",  &SceneEntity::model_renderer);
...
```

→ **これを ComponentRegistry + PropertyRegistry のループに置き換えるのが今回の中核作業。**

### UndoStack の性質（重要）

```cpp
struct Entry { std::string label; SceneDocument before; SceneDocument after; };
```

**SceneDocument 全体を丸ごと 2 回コピーする**スナップショット方式で、最大 128 件。
Entity が増えるとメモリと時間が線形に悪化する。ただし「無効ポインタを作らない」という点では現状最も安全。
→ 今回は方式を維持しつつ、Command 層を挟んで将来差分化できるようにする方針が妥当。

---

## 12. 保存・読み込みの現在の実装

**独自テキスト形式**（`RePlayEngine/Scene/SceneSerializer.cpp`, 18 KB）。

```
REPLAY_SCENE 6
SCENE "main" "Main"
ENTITY_COUNT 1
ENTITY 1 "player_001" "Player" 1
TRANSFORM 0 1 0  0 0 0  1 1 1
MODEL_RENDERER "guid..." "Player.fbx" 1 1 1 1 1 0 1
SHADER_LAYER 3 0 1 0.45 1 64 1 1 1 1
CHARACTER_PROFILE ...
MESH_COLLIDER "cache/xxx.cook" 12345 4 1
GRAVITY 0 -1 0 9.80665 1 55 1 1
ANIMATION 0 1 1 1
END_ENTITY
```

良い点:
- **バージョン番号あり (v1〜v6)**。バージョン分岐による移行が既に実装されている。
- `std::quoted` で文字列を安全に扱い、`std::locale::classic()` でロケール非依存。
- エラーメッセージが日本語で返る `std::string& error` 形式。
- 上限チェック (`max_scene_entities = 100000`) あり。

悪い点:
- 読み込みが `if (token == "TRANSFORM") ... else if (token == "MODEL_RENDERER") ...` の巨大分岐。
- 保存も型ごとに直書き。
- **Component ごとのプロパティを汎用に扱う仕組みがない。**
- 親子関係のフィールドがない。

既存ファイル:
- `resources/Scenes/Main.replayscene` → `REPLAY_SCENE 3 / ENTITY_COUNT 0`（**中身は空**）
- `Saved/EditorSession/LastSession.replayscene`
- `PrefabSerializer` は SceneEntity 1 件を同形式で保存する薄いラッパ。

---

## 13. AssetManager / ResourceManager

**3 つ揃っている。今回の「Assetパスを保存し、読み込み時に再取得」要件はそのまま乗る。**

| クラス | 責任 |
|---|---|
| `AssetDatabase` | GUID ↔ ソースパス / キャッシュパス。`resources/AssetDatabase.replaydb` に永続化 |
| `AssetCache` | `.replay_cache/` へのメッシュ/LOD キャッシュ |
| `AsyncAssetManager` | ワーカースレッド 1 本でファイルをバイト列として非同期読み込み |
| `MeshCollisionCooker` | 衝突メッシュの cook とキャッシュ |

`ModelRendererData` は既に **`asset_guid` を保存**しており、GPU リソースは保存していない。方針は既に正しい。

---

## 14. グローバル変数 / Singleton

`extern LRESULT ImGui_ImplWin32_WndProcHandler(...)` と `extern ImWchar glyphRangesJapanese[]` のみ。
**アプリ固有の Singleton はゼロ。** `framework` がルート所有者として全部持つ構造。

---

## 15. 長期保持している生ポインタ（リスク箇所）

| 箇所 | 内容 | リスク |
|---|---|---|
| `framework::game_scene` | `GameScene*` | `SceneManager` が `current_scene_` を差し替えると**ダングリング**。`QueueSceneFactory` のラムダ内で再代入しているが、`IsFinished()` で別シーンに移った場合の解除処理がない |
| `Player::skinned_` | `skinned_mesh*` | framework が所有。framework より先に死なないので実質安全 |
| `Stage::model` | `skinned_mesh*` | 同上 |
| `IComponent::owner_` | `GameObject*` | 現在 GameObject 自体が未使用なので影響なし |

---

## 16. オブジェクト削除時の参照切れの可能性

**`SceneDocument::Entities()` が `std::vector<SceneEntity>` なので、`CreateEntity()` / `DestroyEntity()` のたびに
既存の `SceneEntity*` が全部無効化される。**

```cpp
SceneEntity* Find(EntityId id) noexcept;   // 返したポインタは次の構造変更で死ぬ
```

現在は Inspector が毎フレーム `Find()` し直しているので偶然壊れていない。
Undo 実行時も `selected_scene_entity_ids` を「存在しない ID を除去」する形で防御している（`framework.h` の Ctrl+Z 処理）。

→ 新設計では `Scene` が `std::vector<std::unique_ptr<GameObject>>` を持てば、
**要素の追加削除で他の GameObject のアドレスが動かない**ので、この問題は構造的に解消する。

---

## 17. 同じ処理が複数箇所から Update されている可能性

現時点では**なし**。ただし注意点：

- `framework::update()` が毎フレーム `game_scene->Gameplay().SetLegacyStageActive(stage_asset_placed)` を呼び、
  それが `stage.GetCollisionMesh().SetEnabled(active)` を実行している。
- `select_scene_entity()` が選択のたびに `Stage` の transform を SceneEntity から書き戻す。
- `sync_selected_entity_to_stage()` が逆方向に書き戻す。

→ **`Stage` が「エディタ上の選択エンティティのプレビュー」と「ゲーム内の地形」を兼務している。**
ここが Component 化で二重 Update を生みやすい最大の危険地帯。移行時に必ず片方向へ整理する必要がある。

---

## 18. 既存コードを維持しながら段階移行できる箇所

| 箇所 | 移行しやすさ | 理由 |
|---|---|---|
| `SceneDocument` / `SceneEntity` | ★★★ 最優先 | 描画に使われていない。作り替えても実行時挙動が変わらない |
| `Core/Components/*` | ★★★ | **完全に未使用のデッドコード**。新基盤の土台として昇格できる（重複クラスを作らずに済む） |
| Inspector / Add Component | ★★★ | 既に「追加・削除」の UI がある。中身をレジストリ駆動に置換するだけ |
| `SceneSerializer` | ★★☆ | バージョン分岐の枠が既にある。v7 を足せる |
| 階層パネル | ★★☆ | 固定項目と Entity 一覧が混在。Entity 側だけ差し替え可能 |
| `Player` / `Stage` | ★☆☆ | 描画・当たり判定・アニメが直結。Adapter 経由の段階移行が必須 |
| `framework_render.cpp` | ★☆☆ | 39 KB の直書き。提出リストの差し込みのみに留める |

### `Core/Components/` は未使用（最重要の発見）

`GameObject` を検索した結果、**`IComponent.h` の前方宣言と friend 宣言以外に参照がゼロ**。
`ReplayEngine::Core::GameObject` は一度もインスタンス化されていない。

一方 `SphereColliderComponent` / `MeshColliderComponent` は `IComponent` を継承しているが、
`Player` / `Stage` の**値メンバとして直接埋め込まれ**、`GameObject` を介さずに使われている。

→ **既存の `IComponent` / `GameObject` を捨てるのではなく、正式な基盤へ昇格させるのが正解。**
指示の「既存クラスがある場合は重複した新クラスを作らず、既存クラスを整理・改修」に合致する。

---

## 19. Player を Component 化する場合の依存関係

```
Player
 ├─ 入力          GameInput::AxisX/AxisY/JumpPressed  (GetAsyncKeyState 直呼び)
 ├─ カメラ        Update(dt, const Camera&) — カメラ基準の移動方向
 ├─ 描画          skinned_mesh* + clip_index/anim_tick + 見た目補正回転 3 つ
 ├─ 当たり判定    SphereColliderComponent (値) + GameRaycast::SphereCastStageDown(Stage&)
 └─ 移動          position/velocity/on_ground + 8 個のパラメータ
```

分解の目標形:

```
PlayerObject
 ├─ TransformComponent        ← 位置/回転/スケール
 ├─ MeshRendererComponent     ← skinned_mesh* + 描画設定（Legacy アダプタ経由）
 ├─ AnimatorComponent         ← clip_index / anim_tick / clip_idle/walk/jump
 ├─ SphereColliderComponent   ← 既存を GameObject 所有へ移す
 ├─ CharacterMotorComponent   ← velocity / on_ground / 移動物理
 ├─ PlayerInputComponent      ← GameInput → 方向ベクトル
 ├─ PlayerControllerComponent ← Input → Motor への橋渡し + パラメータ
 └─ CameraTargetComponent     ← カメラ追従の対象
```

**分離の壁**: `Player::Update(dt, const Camera&)` がカメラ参照を引数で受けている点。
Component 化では「Scene からカメラを引く」か「入力を既にカメラ基準に変換して渡す」かの選択が必要。
後者（PlayerInputComponent がカメラ基準の方向を出力する）のほうが Component 間結合が弱い。

---

## 20. 保存ライブラリ

- **JSON / YAML ライブラリは未導入。**
- `cereal` は同梱されているが、**FBX メッシュのバイナリキャッシュ (`.cereal`) にしか使われていない**。
  シーン保存には使われていないし、`skinned_mesh` のシリアライズ以外に流用されていない。
- シーン保存は `std::ofstream` + `std::quoted` の手書き。

→ JSON にするなら **自前の最小 JSON writer/parser を書く必要がある**（外部ライブラリ無断追加の禁止制約のため）。
既存テキスト形式を拡張するほうが低リスク。**要判断項目。**

---

# 21. マルチスレッド基盤の調査（追加指示への回答）

指定された全項目を検索した結果：

| 探した物 | 存在 | 実体 |
|---|:---:|---|
| ThreadPool | ✗ | なし |
| JobSystem | ✗ | なし |
| TaskSystem / TaskGraph | ✗ | なし |
| WorkerThread 管理クラス | △ | `AsyncAssetManager` が worker **1 本**を内部で持つのみ |
| 非同期 Asset 読み込み | ✓ | `AsyncAssetManager` |
| 並列描画準備 | ✗ | なし |
| 並列 Update | ✗ | なし |
| `std::mutex` | ✓ | `AsyncAssetManager` のみ (6 箇所) |
| `std::shared_mutex` | ✗ | なし |
| `std::condition_variable` | ✓ | `AsyncAssetManager` のみ |
| `std::atomic` | ✓ | `AsyncAssetManager` のカウンタ 4 個 |
| `lock_guard` / `unique_lock` | ✓ | `AsyncAssetManager` のみ |
| `scoped_lock` | ✗ | なし |
| `future` / `promise` | ✓ | `LoadingScene` の `std::future<bool>` + `std::async` |
| スレッドセーフ Queue | △ | `AsyncAssetManager` 内の `std::deque` + mutex（クラス外に公開されていない） |
| メインスレッド専用処理 | ✓ | `AsyncAssetManager::PumpMainThread()` |
| RenderThread | ✗ | なし。D3D11 immediate context を **メインスレッドのみ**で使用 |

### 既存のスレッド基盤は 2 つだけ

**(1) `AsyncAssetManager`** — `RePlayEngine/Assets/AsyncAssetManager.{h,cpp}`

```
[main] QueueFile(path, kind, completion)
          → mutex で jobs_ に push → condition_.notify_one()
[worker] condition_.wait → jobs_ から pop → ifstream で全バイト読み → completed_ に push
[main] PumpMainThread()   ← framework::update() の先頭で毎フレーム呼ばれる
          → completed_ を swap してロック解放 → completion(result) を "メインスレッドで" 実行
```

- **完了コールバックは必ずメインスレッドで走る**。設計として正しい。
- デストラクタで `stopping_ = true` → `notify_all` → `join`。終了は安全。
- `CancelPending()` あり。
- **ジョブの中身はファイル読み込み固定**。任意の計算タスクは投げられない（＝汎用 JobSystem ではない）。

**(2) `LoadingScene`** — `std::async(std::launch::async, task)` を **1 タスクずつ直列**に実行。
`Update()` で `future.wait_for(0s)` をポーリングして完了判定。並列度 1。

### 判断: **新しい JobSystem / ThreadPool は作らない**

理由（指示の「新規設計が必要な場合は具体的理由を説明」への回答として、逆に**新規不要**の理由）:

1. **並列化する対象がまだ存在しない。** 現在の Update はプレイヤー 1 体分のみ。Component 化直後も同様。
   ジョブ分割のオーバーヘッドが利得を上回る。
2. **D3D11 の immediate context がメインスレッド専有**。描画準備の並列化は deferred context か
   コマンド提出リストの分離が前提で、これは今回のスコープ外（「Shader/描画システムの全面改修はしない」）。
3. **重複実装の禁止**。`AsyncAssetManager` の worker/queue/pump パターンが既にあるので、
   同じ役割の 2 個目のスレッド基盤を作るのは指示違反。
4. **既存を壊すな制約に対してリスク最大**。ThreadPool 導入は全域に影響する。

### 代わりに採る方針

- **`AsyncAssetManager` の Pump パターンを設計モデルとして流用**する。
  スレッドは増やさず、`Scene` 内に「構造変更要求のキュー → フレーム末尾の同期ポイントで一括反映」を実装する。
  これは単一スレッドでも「Update 中の追加/削除でクラッシュしない」要件をそのまま満たし、
  将来ワーカーから積むときに mutex を 1 箇所足すだけで並列対応になる。
- **`Component` に `ThreadPolicy` を持たせる（既定 `MainThreadOnly`）**。
  今回は全 Component をメインスレッドで実行する。列挙型と問い合わせ API だけ用意し、
  「将来並列化できない構造にしない」という要件を満たす。
  既存に同等の列挙型は**存在しないので新規追加になる**（重複ではない）。
- **Scene 保存は「メインスレッドで Snapshot → 書き込み」に分離できる形にする。**
  そのために `SceneSerializer` は `Scene&` ではなく**中間データ (`SceneData`) を受け取る**設計にする。
  これで将来 `AsyncAssetManager` へ書き込みジョブを投げられる（今回は同期実行）。
- **Scene 読み込み**: ファイル読み+解析は将来 `AsyncAssetManager` に載せられる形にする。
  GameObject 生成 / ObjectID 登録 / Component 生成 / 親子設定 / Renderer 登録 / Editor 選択更新は**必ずメインスレッド**。
- **ジョブへ生ポインタを渡さない**。渡すのは `ObjectID` か不変データのコピーのみ。

---

# 22. 現状の問題点まとめ

| # | 問題 | 影響 |
|---|---|---|
| P1 | Component 種別が `std::optional` メンバでハードコード | 新 Component 追加時に 4 ファイルを同時改修 |
| P2 | シリアライザが型ごとの巨大 if-else | 同上。指示で明確に禁止された構造 |
| P3 | Inspector / Add Component も型ごとの直書き | 同上 |
| P4 | 実行時 GameObject が存在しない（`SceneDocument` は記録のみ） | エディタで置いた物が動かない・描画されない |
| P5 | `Core/Components/GameObject` が完全な未使用デッドコード | 二重実装のリスク |
| P6 | 親子関係が存在しない | 階層・World/Local Transform が未対応 |
| P7 | Transform が Player / Stage / TransformComponent / TransformData の 4 箇所に分散 | 二重所有 |
| P8 | `SceneEntity*` が vector 再配置で無効化 | 潜在的な参照切れ |
| P9 | `Stage` がエディタプレビューとゲーム地形を兼務 | 二重 Update の温床 |
| P10 | 描画対象が `skinned_meshes[0]` 等の配列添字で固定 | 動的オブジェクトを描けない |
| P11 | Editor パネルが全部 `framework` のメンバ関数 | `framework.h` の肥大化・拡張困難 |
| P12 | Undo が SceneDocument 全体の 2 重コピー | Entity 増加でメモリ・時間が悪化 |
| P13 | `FixedUpdate` / `LateUpdate` が存在しない | 物理更新の分離ができない |
| P14 | `game_scene` 生ポインタがシーン差し替えでダングリングしうる | クラッシュ要因 |
| P15 | 汎用 Property 機構がない | Inspector 表示と保存の二重定義 |

---

# 23. そのまま残すもの / 改修するもの / 非推奨にするもの

### そのまま残す（触らない）

- `framework_render.cpp` の描画パイプライン全体（CSM / Deferred / PostProcess / ShaderStack）
- `pbr_renderer` / `toon_renderer` / `csm_renderer` / `deferred_renderer` / `lights_manager`
- `skinned_mesh` / `static_mesh` / `gltf_model` / `sprite` / `geometric_primitive`
- `AssetDatabase` / `AssetCache` / `AsyncAssetManager` / `MeshCollisionCooker`
- `IScene` / `SceneManager` / `BootLogoScene` / `LoadingScene`（**画面遷移用として残す**）
- `TransformGizmo` / `ViewportPicker`
- `ShaderLayerStack` / `CharacterMaterialProfile` / 各 ShaderEditor
- `GameInput` / `Camera` / `FreeCameraController`
- `Player` / `Stage` / `SceneGame`（**当面残す。段階的に縮小**）

### 改修する

- `Core/Components/IComponent.h` → `Component` へ拡張（ライフサイクル / ThreadPolicy / Serialize）
- `Core/Components/GameObject.h` → 正式な GameObject へ拡張（ObjectID / 親子 / 削除予約）
- `Core/Components/TransformComponent.h` → GameObject の Transform へのビューに変更（データを持たない）
- `Scene/SceneDocument.{h,cpp}` → 固定 optional をやめ、可変 ComponentData リストの**中間データ層**へ
- `Scene/SceneSerializer.{h,cpp}` → v7 追加 + レジストリ駆動化（v1〜v6 の読み込みは維持）
- `Scene/PrefabSerializer` → 新 ComponentData 形式へ追従
- `Editor/Commands/UndoStack` → Command 層を被せる（内部のスナップショット方式は維持）
- `framework_scene_document.cpp` の Inspector / Add Component → レジストリ駆動ループへ置換
- `framework_editor.cpp` の階層パネル → GameObject ツリー表示へ
- `3dgp.vcxproj` / `3dgp.vcxproj.filters` → 新規ファイル登録（責任別フィルター）

### 非推奨にする（削除はしない）

- `SceneEntity` の型別 `std::optional` メンバ → v6 以前の読み込み専用に降格
- `framework::selected_editor_object` の `enum editor_selection` のうち `scene_entity` 以外 → 段階的に GameObject へ吸収
- `Player` / `Stage` の transform 直持ち → 最終的に TransformComponent へ

---

# 24. Flax Engine から参考にする考え方 / 採用しない部分

### 参考にする（コードは流用しない。設計思想のみ）

| Flax の考え方 | RePlayEngine への適用 |
|---|---|
| `SceneObject` が `Guid` と親を持つ共通基底 | `GameObject` が `ObjectID` と親 ID を持つ |
| `Actor` が Transform を**自分で所有**し、Script は Transform を持たない | GameObject が Transform を所有し、`TransformComponent` はビューに徹する（二重所有の回避） |
| `SceneObjectsFactory` が型 ID からオブジェクトを復元 | `ComponentRegistry` が型名から Component を生成 |
| Scene のロードが「生成 → ID テーブル構築 → 親子復元 → プロパティ適用」の多段 | Scene 読み込みを 5 フェーズに分割 |
| `Level` がシーンのリストを持ち、`Scene` が Actor 階層を持つ | `SceneManager`（既存）はそのまま。`Scene` が GameObject を所有 |
| Actor の `_isActive` と `_isActiveInHierarchy` を分離 | `enabled`（自身）と `activeInHierarchy`（親を辿った結果）を分離 |
| 削除は即時 delete せず `DeleteObject` でマーク | 削除予約 + フレーム末尾の `ProcessPendingDestroy()` |
| Editor が Runtime に依存し、Runtime は Editor に依存しない | 同じ。`RePlayEngine/Editor/` → `RePlayEngine/Object/` の一方向 |
| Prefab は「Scene と同じ形式の部分木」 | `SceneData` の部分木として Prefab を表現（拡張点だけ用意） |

### 採用しない部分と理由

| Flax | 採用しない理由 |
|---|---|
| C# スクリプティング / .NET ホスティング | 指示でスコープ外。RePlayEngine は C++ 単体 |
| 完全なリフレクション（`ScriptingType` / コード生成 / Build ツール） | 規模が過大。最小の `PropertyRegistry` で代替 |
| `Actor` が仮想関数で描画・物理へ直接参加する設計 | RePlayEngine は Component 側に寄せる（Unity 寄り）。既存 Player/Stage からの移行が素直 |
| `JobSystem` / `TaskGraph` / `ThreadPool` | **既存に相当物がなく、今回作る必要もない**（21 節参照） |
| `Guid`（128bit）を全オブジェクトに | 64bit の `EntityId` が既にあり、保存形式にも入っている。互換維持のため 64bit を継承しつつ文字列化にも対応 |
| Actor の巨大な仮想 API 群（`OnBeginPlay` / `OnEndPlay` / `OnTransformChanged` / `OnLayerChanged` …） | 最小限のライフサイクルに絞る |
| `LargeWorlds`（double 座標） | 不要 |

---

# 25. 想定する新ディレクトリ構成（案）

既存の `RePlayEngine/` 配下の命名規則に合わせる。

```
RePlayEngine/
├─ Core/
│  ├─ ObjectID/          ObjectID.h
│  └─ Threading/         ThreadPolicy.h  (既存 AsyncAssetManager は Assets/ のまま)
│
├─ Object/
│  ├─ Component/         Component.h / Component.cpp
│  ├─ GameObject/        GameObject.h / GameObject.cpp
│  └─ Registry/          ComponentRegistry.h / .cpp / ComponentTypeInfo.h
│
├─ Reflection/
│  ├─ Property/          PropertyValue.h / PropertyDesc.h
│  └─ Registry/          PropertyRegistry.h / .cpp
│
├─ Scene/                （既存。中を整理）
│  ├─ Runtime/           Scene.h / Scene.cpp
│  ├─ Serialization/     SceneData.h / SceneSerializer.{h,cpp} / SceneTextFormat.cpp / PrefabSerializer
│  └─ Management/        SceneManager.h / IScene.h  （既存をここへ）
│
├─ Components/
│  ├─ Core/              TransformComponent.h
│  ├─ Rendering/         MeshRendererComponent / LegacyMeshRendererComponent
│  ├─ Physics/           SphereColliderComponent / MeshColliderComponent / GravityComponent
│  ├─ Camera/            CameraTargetComponent
│  └─ Gameplay/          HealthComponent / MovingPlatformComponent / RotatorComponent /
│                        PlayerInputComponent / CharacterMotorComponent / PlayerControllerComponent
│
└─ Editor/               （既存。中を整理）
   ├─ Core/              EditorContext
   ├─ Selection/         EditorSelection
   ├─ Hierarchy/         HierarchyPanel
   ├─ Inspector/         InspectorPanel / PropertyDrawer
   ├─ ComponentBrowser/  AddComponentPanel
   ├─ Commands/          EditorCommand / UndoStack（既存）
   └─ Gizmo/             （既存）
```

`.vcxproj.filters` は既存が**実ディレクトリを完全にミラー**しているので、同じ規則で追記すればよい。
（既存フィルター例: `RePlayEngine\Scene`, `RePlayEngine\Editor\Commands`, `Source\App\Editor` …）

---

# 26. 未確認・確認不能な項目

| 項目 | 状態 |
|---|---|
| コンパイル確認 | **不可**。作業環境は Linux サンドボックスで MSVC / Windows SDK / DirectX SDK がない。静的確認のみ |
| 実行確認 | **不可**。同上 |
| ImGui のバージョン/ブランチ詳細 | `imgui/` を未精読。docking API (`DockBuilder*`, `ImGuiConfigFlags_DockingEnable`) の使用は確認済み |
| `framework_render.cpp` の全パス | 39 KB のうち主要な描画分岐のみ確認。全行は未精読 |
| `skinned_mesh.cpp` / `gltf_model.cpp` の内部 | メッシュ実装の詳細は未精読（今回は触らない前提） |
| `Saved/EditorSession/session.ini` の形式 | 未確認 |

---

（以上。この時点でコードの変更は一切行っていない。）
