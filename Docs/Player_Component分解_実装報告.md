# 旧 Player の Component 分解 実装報告

作成日: 2026-07-31
規模: 新規 24 ファイル / 既存 9 ファイル改修

---

## 1. 現状調査結果

### `Player` の全メンバ（`Source/game/player.h`）

| 分類 | メンバ |
|---|---|
| 姿勢 | `position` / `angle`(オイラー rad) / `scale` / `transform`(4x4) |
| 物理 | `velocity` / `on_ground` / `vertical_physics_enabled_` / `ground_y` |
| 衝突 | `collider_`（`Core::SphereColliderComponent` を**値メンバとして埋め込み**） |
| 見た目補正 | `visual_pitch_deg`=90 / `visual_yaw_offset_deg`=188.5 / `visual_roll_deg`=180 |
| パラメータ | `gravity`=18 / `move_speed`=6 / `acceleration`=30 / `deceleration`=20 / `turn_speed`=720度 / `jump_speed`=8 |
| 描画 | `skinned_`（`skinned_mesh*`） |
| アニメ | `clip_index` / `anim_tick` / `clip_idle` / `clip_walk` / `clip_jump` |

### `Player::Update(dt, const Camera&)` の処理順

1. `GameInput::AxisX/AxisY/JumpPressed` を直接読む
2. ジャンプ（`vertical_physics_enabled_ && on_ground` のとき）
3. カメラの front/right を XZ へ射影・正規化
4. 移動方向 = forward×ay + right×ax、正規化
5. 加速 → `move_speed` で上限クランプ
6. `turn_speed` で目標ヨーへ旋回
7. 入力なしなら `deceleration` で減速
8. 重力と `ground_y` クランプ
9. `position += velocity * dt`
10. クリップ選択：空中→jump、水平速度>0.2→walk、それ以外→idle
11. `anim_tick += dt`
12. `UpdateTransform()` = `S * R(visual + angle) * T`

**重要な発見: `vertical_physics_enabled_` の既定値が `false`** です。つまり**現状の Player は重力もジャンプも動いていません**。接地は `SnapToGround` で高さを合わせるだけです。挙動を変えないため、`CharacterMotorComponent::vertical_physics` も既定 `false` にしてあります。Inspector で `true` にすれば有効になります。

### 参照箇所

| 参照元 | 内容 |
|---|---|
| `SceneGame::Update` | 接地→`player.Update(dt, camera)`→壁スイープ→再接地→カメラ追従 |
| `framework_render.cpp:30` | `store_object_world` が `player.GetTransform()` を読む |
| `framework_render.cpp:200` | `player.GetAnimationClipIndex()` でクリップを決める |
| `framework_editor.cpp:90` | `player.clip_idle` を初期クリップに使う |
| `game_scene.cpp:15` | `player.ResetPlacement()` |
| `SceneGame::DrawPlayerGUI` | 移動設定 / アニメ割り当て / 表示補正の Inspector |
| `GameRaycast::*(const Stage&, ...)` | **Stage 具象型への直接依存** |

Player を前提にした固定スロットは `skinned_meshes[0]` の 1 か所だけでした。

---

## 2. 旧 Player が持っていた責任一覧

入力取得 / カメラ基準の方向変換 / 加減速 / 旋回 / 重力 / ジャンプ / 接地 / 壁解決 /
座標保持 / 見た目補正回転 / アニメーション状態決定 / アニメーション時間管理 /
描画メッシュ参照 / 衝突形状保持 / カメラ追従対象。**15 の責任が 1 クラスに同居**していました。

---

## 3. 採用した Component 分割

| Component | 責任 | 再利用性 |
|---|---|---|
| `TransformComponent` | 位置・回転・拡縮（既存を再利用） | 全 GameObject |
| `SkinnedMeshRendererComponent` | Asset GUID・見た目補正・描画提出 | 全スキンモデル |
| `AnimatorComponent` | Idle/Walk/Jump の状態と時間 | 全キャラクター |
| `SphereColliderComponent` | 球の形状パラメータ | 全キャラクター |
| `CharacterMotorComponent` | 移動・重力・ジャンプ・接地 | **プレイヤーと敵で共有** |
| `PlayerInputComponent` | 入力の取得のみ | 操作対象 |
| `PlayerControllerComponent` | Input → Motor の橋渡し | 操作対象 |
| `HealthComponent` | 体力（既存を再利用） | 全キャラクター |
| `CameraTargetComponent` | カメラ追従の対象であること | 追従されるもの |

**`PlayerComponent` は作っていません。** プレイヤーは「これらの組み合わせ」として成立します。

**`CharacterMotorComponent` は誰が動かしているかを一切知りません。** 検証テストでは、Controller を持たない `Enemy` GameObject が Motor だけで動くことを実際に確認しています。

---

## 4. Component 間の依存図

```
PlayerInputComponent            （入力取得のみ。何にも依存しない）
        │ MoveX/MoveY/ConsumeJump
        ▼
PlayerControllerComponent ──► ICameraBasisProvider（Scene サービス）
        │ Move() / RequestJump()
        ▼
CharacterMotorComponent  ──► IPhysicsQueryService（Scene サービス）
        │ Grounded() / PlanarSpeed()      └─► SphereColliderComponent（形状）
        ▼
AnimatorComponent
        │ CurrentClip() / AnimationTime()
        ▼
SkinnedMeshRendererComponent ──► RenderItem ──► 既存 Renderer

CameraTargetComponent ◄── カメラ制御側が ObjectID で参照（Player 型を知らない）
HealthComponent            （独立。誰にも依存しない）
```

依存は**一方向のみ**で、循環はありません。下位（Input）は上位（Controller）を知りません。

---

## 5. Update / FixedUpdate 順序

```
framework::update(dt)
 └─ update_object_scene(dt)
     ├─ refresh_object_scene_services()
     │    Camera / Stage Bridge を張り直す
     │    PlayerControlSystem で操作対象 ObjectID を確定
     │    object_player_active を決定 → 旧 Player の更新・描画を止める
     │
     ├─ Scene::Update(dt)            可変フレーム
     │    PlayerInput      : 入力を読む（ジャンプはラッチ）
     │    PlayerController : 方向を作り Motor へ Move / RequestJump
     │    Animator         : Motor の状態から Idle/Walk/Jump を決める
     │
     ├─ update_object_fixed_step(dt) 固定 1/60 秒
     │    accumulator 方式、最大 5 サブステップ
     │    Edit Mode 中は進めず accumulator を 0 に戻す
     │    打ち切った余りは捨てる（追いつき続けない）
     │    CharacterMotor : 速度積分 → Transform 更新 → 壁解決 → 接地解決
     │
     ├─ Scene::LateUpdate(dt)
     ├─ update_object_camera_follow(dt)   Transform 確定後にカメラを動かす
     └─ SceneRenderCollector::Collect()   描画提出リストを作る
```

**ジャンプの一度押しが多重消費されない根拠**（実測で確認済み）:

`PlayerInputComponent` が押下をラッチし、`ConsumeJump()` で 1 回だけ取り出します。`CharacterMotorComponent` 側でも `jump_requested_` フラグを FixedUpdate の先頭で必ず落とします。FixedUpdate が 0 回のフレームでもラッチは次フレームまで残り、3 回走っても最初の 1 回しか実行されません。テストで「2 回目の FixedUpdate では再ジャンプせず重力で減速する」ことを確認しました。

---

## 6. Camera 依存の解消方法

```
旧: Player::Update(float dt, const Camera& camera)   ← 移動処理が Camera 具象型を受け取る

新: PlayerControllerComponent
        └─ ICameraBasisProvider（CameraForward / CameraRight だけ）
                └─ LegacyCameraBasisBridge【移行用】
                        └─ 既存 Camera
```

**`PlayerControllerComponent.cpp` は `camera.h` を include していません。** Camera への依存は `legacy_camera_basis_bridge.cpp` の 1 ファイルに閉じています。カメラが使えない場合はワールド軸基準へフォールバックするので、起動直後でも落ちません。

---

## 7. Stage 依存の解消方法

```
旧: GameRaycast::SphereCastStageDown(const Stage& stage, ...)   ← Player 側が Stage を直接参照

新: CharacterMotorComponent
        └─ IPhysicsQueryService（QueryGround / SweepSphere）
                └─ LegacyStageCollisionBridge【移行用】
                        └─ 既存 Stage / MeshCollider
```

**`CharacterMotorComponent.cpp` は `stage.h` も `raycast.h` も include していません。** 検証テストでは、Stage とは無関係な自作の平床サービスを差し込んで同じ Motor が動くことを確認しています。将来 Stage を地形 GameObject 群へ移しても、**Motor は 1 行も変更不要**です。

---

## 8. 旧 Player から新 Player への切り替え手順

**自動では切り替わりません。Editor から 1 回だけ変換します。**

```
階層パネル > GameObject > 「旧 Player を GameObject へ変換」
   （またはインスペクターの「プレイヤー」からも同じボタン）
        ↓
旧 Player の現在値を読み取って Player GameObject を構築
   位置・回転・拡縮、移動パラメータ、Collider 設定、
   アニメクリップ割り当て、見た目補正、カメラ追従設定をすべて引き継ぐ
        ↓
Ctrl+S で v7 Scene へ保存
        ↓
以降は Scene ファイルが正式な構成元。
起動のたびにコードから AddComponent し直すことはない。
```

既に `PlayerControllerComponent` を持つ GameObject があれば**重複作成しません**。

### 二重更新・二重描画がない根拠

`refresh_object_scene_services()` が毎フレーム操作対象 ObjectID を確定し、その結果で `object_player_active` を決めます。これが**唯一のスイッチ**です。

| 対象 | 停止方法 |
|---|---|
| 旧 Player の更新 | `SceneGame::Update` の冒頭で `legacy_player_active_ == false` なら即 return |
| 旧 Player の描画 | `framework_render.cpp` の `draw_legacy_player` で `skinned_meshes[0]` の描画を全パス（GBuffer / Forward / 影 / アウトライン）でスキップ |
| 旧 Player のカメラ追従 | 同じ return で止まる。新経路は `FollowCameraTarget` を使う |

新旧が同時に動くことは構造上ありません。

---

## 9. Editor 統合方法

- Hierarchy の固定項目「プレイヤー」は、変換後は**設定項目を持ちません**。新 GameObject へ誘導するボタンだけになります
- 移動速度・加速・減速・旋回速度・アニメ割り当て・表示補正は、すべて対応する Component の Property へ移しました。**同じ値を新旧の 2 か所で編集できる状態は残っていません**
- Inspector の表示は `ComponentRegistry` / `PropertyRegistry` 経由で自動生成されます。Editor 側にも Serializer 側にも Component 型ごとの `if` / `switch` は 1 つもありません

---

## 10. Scene 保存・復元方法

**保存されるもの**（テストで往復確認済み）: ObjectID / 名前 / enabled / Transform / 親子関係 / 各 Component の型と enabled / 入力設定 / 移動・ジャンプパラメータ / Collider 設定 / アニメクリップ割り当て / Mesh Asset GUID / 見た目補正 / カメラ追従設定 / Health

**保存されないもの**（テストで 0 から始まることを確認済み）: `velocity` / `grounded` / `animationTime` / 入力の押下状態 / 生ポインタ / Camera・Stage ポインタ / DirectX リソース

実行時状態はすべて `private` の非 Property メンバなので、`PropertyRegistry` が拾いません。構造的に保存されません。

---

## 11. Prefab 対応

Player 構成を `.replayprefab` として保存し、配置できることを確認しました。複数配置しても ObjectID は採番し直され、衝突しません。配置後も `PlayerControlSystem` が操作対象を 1 体に絞ります。

---

## 12-13. 変更ファイル一覧と理由

### 新規（24 ファイル）

| ファイル | フィルター | 責任 |
|---|---|---|
| `RePlayEngine/Scene/Services/ICameraBasisProvider.h` | `RePlayEngine\Scene\Services` | カメラの向きだけを渡す境界 |
| `RePlayEngine/Scene/Services/IPhysicsQueryService.h` | 同上 | 地形問い合わせの境界 |
| `RePlayEngine/Scene/Services/SceneServices.h` | 同上 | Scene が持つ非所有サービスの束 |
| `RePlayEngine/Scene/Services/PlayerControlSystem.h` / `.cpp` | 同上 | 操作対象 ObjectID の管理 |
| `RePlayEngine/Rendering/Adapter/IRenderSubmitter.h` | `RePlayEngine\Rendering\Adapter` | 描画提出の境界（型分岐をなくす） |
| `RePlayEngine/Components/Physics/SphereColliderComponent.h` / `.cpp` | `RePlayEngine\Components\Physics` | 球の形状。Transform を二重所有しない |
| `RePlayEngine/Components/Gameplay/CharacterMotorComponent.h` / `.cpp` | `RePlayEngine\Components\Gameplay` | 移動・重力・ジャンプ・接地 |
| `RePlayEngine/Components/Gameplay/PlayerInputComponent.h` / `.cpp` | 同上 | 入力取得のみ |
| `RePlayEngine/Components/Gameplay/PlayerControllerComponent.h` / `.cpp` | 同上 | Input → Motor の橋渡し |
| `RePlayEngine/Components/Camera/CameraTargetComponent.h` | `RePlayEngine\Components\Camera` | 追従対象であることの表明 |
| `RePlayEngine/Components/Rendering/AnimatorComponent.h` / `.cpp` | `RePlayEngine\Components\Rendering` | アニメ状態と時間 |
| `RePlayEngine/Components/Rendering/SkinnedMeshRendererComponent.h` / `.cpp` | 同上 | スキンメッシュの描画提出 |
| `RePlayEngine/Components/Rendering/MeshRendererComponent.cpp` | 同上 | `IRenderSubmitter` の実装 |
| `Source/game/legacy_stage_collision_bridge.h` / `.cpp` | `Source\Game` | **【移行用】** Stage → 汎用地形問い合わせ |
| `Source/game/legacy_camera_basis_bridge.h` / `.cpp` | 同上 | **【移行用】** Camera → 汎用カメラ基準 |

### 変更（9 ファイル）

| ファイル | 理由 |
|---|---|
| `RePlayEngine/Scene/Runtime/Scene.h` | `SceneServices` を追加（Component がサービスへ辿れるように） |
| `RePlayEngine/Rendering/Adapter/RenderItem.h` | `skinned` / `clip_index` / `animation_time` を追加 |
| `RePlayEngine/Rendering/Adapter/SceneRenderCollector.cpp` | `IRenderSubmitter` 駆動へ変更。型分岐を排除 |
| `RePlayEngine/Components/Rendering/MeshRendererComponent.h` | `IRenderSubmitter` を実装 |
| `RePlayEngine/Object/Registry/BuiltInComponents.cpp` | 新 7 Component を**単一登録点**へ追加 |
| `Source/app/framework.h` | Bridge / PlayerControlSystem / 固定時間更新のメンバとメソッド宣言 |
| `Source/app/Runtime/framework_gameobject_scene.cpp` | サービス配線・固定時間更新・カメラ追従・旧 Player 変換 |
| `Source/app/Rendering/framework_render.cpp` | `draw_legacy_player` で旧描画を止め、提出リストへ clip/time を反映 |
| `Source/app/Editor/framework_editor.cpp` / `framework_inspector.cpp` | 変換ボタン追加、旧 Player 専用 UI の撤去 |
| `Source/game/scene_game.h` / `.cpp` | 旧 Player 更新の停止スイッチ、`FollowCameraTarget` の追加 |

---

## 14. vcxproj / filters 登録結果

| 項目 | 結果 |
|---|---|
| ClCompile | 103 件 |
| **obj ベース名の重複** | **0 種** |
| 登録漏れ | 0 件 |
| `.filters` との不一致 | 0 件 |
| 新規フィルター | `RePlayEngine\Scene\Services` / `RePlayEngine\Components\Physics` / `RePlayEngine\Components\Camera` |
| XML 妥当性 | OK |

---

## 15-16. 動作確認結果（g++ 検証範囲）

**Player 専用テストを新規作成し、ASan + UBSan + LeakSanitizer 付きで実行して 37 項目すべて合格**しました。

| 確認内容 | 結果 |
|---|---|
| Player が Transform 含む 9 Component で成立する | OK |
| PlayerController の依存 Component 検証が働く | OK |
| 操作対象が ObjectID で自動選出される | OK |
| **2 体置いても操作対象は 1 体のまま** | OK |
| **CharacterMotor が Controller 無しでも動く（Enemy 再利用）** | OK |
| 移動要求で前進し、水平速度が公開される | OK |
| 移動中 Walk / 停止で Idle / 空中で Jump へ切り替わる | OK |
| ジャンプで上昇し、**2 回目の FixedUpdate で再ジャンプしない** | OK |
| 重力で落下し床へ接地する | OK |
| Motor 無効で移動しない / Animator 無効でも落ちない / Renderer 無効で非表示 | OK |
| 提出データに clip_index と skinned フラグが載る | OK |
| **Controller 削除で操作対象が別 GameObject へ移る** | OK |
| Component を再追加できる | OK |
| **Physics / Camera サービスが未設定でも落ちない** | OK |
| v7 保存・復元（ObjectID / 移動設定 / アニメ割り当て / Asset / Collider / CameraTarget） | OK |
| **velocity と animationTime が保存されない** | OK |
| Prefab 保存・複数配置・ObjectID 非衝突 | OK |
| メモリリーク・未定義動作 | **ゼロ** |

既存の欠損 Asset テスト 12 項目も再実行して合格しています。エンジン側 `.cpp` は `-Wall -Wextra -Wpedantic` でエラー・警告ゼロです。

---

## 17. MSVC 未確認範囲

**Visual Studio Debug x64 でのビルドと実行は未確認です。** 作業環境に MSVC / Windows SDK がありません。

特に以下は `windows.h` / `wrl.h` / `d3d11.h` 依存で g++ に通らないため、静的レビューのみです。

- `framework.h` / `framework_gameobject_scene.cpp` / `framework_render.cpp` / `framework_editor.cpp` / `framework_inspector.cpp`
- `scene_game.cpp` の `FollowCameraTarget` と旧 Player 停止スイッチ
- `legacy_stage_collision_bridge.cpp` / `legacy_camera_basis_bridge.cpp`
- `PlayerInputComponent` の `GetAsyncKeyState` による実際の入力
- 実行時の描画結果・アニメーション再生・カメラ追従の体感

**Debug x64 でのビルドと、以下の確認をお願いします。**

1. ビルドが通る / assert なしで起動する
2. F1 → 階層 → GameObject に「旧 Player を GameObject へ変換」ボタンが出る
3. 押すと `Player` GameObject が 9 Component 付きで作られる
4. Inspector で移動速度・ジャンプ力・Collider 半径・クリップ番号を変更できる
5. Ctrl+S で保存 → 再起動 → 構成と値が復元される
6. F5 で Play → WASD で移動 / カメラ基準 / Space でジャンプ（`vertical_physics` を有効にした場合）
7. 移動で Walk、停止で Idle、空中で Jump へアニメが切り替わる
8. カメラが新 Player を追従する
9. 旧 Player が二重に描画されていない（モデルが 1 体だけ）
10. 終了時に assert が出ない / D3D11 Live Object が増えていない

---

## 18. 二重 Update / 二重描画がない根拠

第 8 節の表のとおり、`object_player_active` という**単一のスイッチ**で旧経路を完全に止めています。新 Player が成立していない間は旧経路のみ、成立したら新経路のみが動きます。両方が同時に動く分岐は存在しません。

---

## 19. メモリ・所有権確認

| 項目 | 結果 |
|---|---|
| ASan / UBSan / LeakSanitizer | **エラー・リーク・未定義動作ゼロ** |
| 所有権 | Scene → GameObject → Component の一本道を維持 |
| Collider の Transform | **持たない**。毎回 Owner から求める |
| Animator の GPU メッシュ | **持たない**。クリップ番号と時刻だけ |
| サービス | すべて非所有ポインタ。実体は framework が所有 |
| Singleton | 追加なし |
| 生 `new` / `delete` | 追加なし |
| Component 間の循環参照 | なし（依存は一方向） |
| 長期参照 | 操作対象は `ObjectID`。生ポインタで保持しない |

---

## 20. 一時 Bridge の削除条件

| Bridge | 削除条件 |
|---|---|
| `LegacyStageCollisionBridge` | Stage を地形 GameObject 群へ移行し、Scene 上の Collider を走査する `CollisionWorld` を用意した時点。**`CharacterMotorComponent` は変更不要** |
| `LegacyCameraBasisBridge` | カメラを `CameraComponent` として GameObject 化した時点。**`PlayerControllerComponent` は変更不要** |

どちらも「1 つの責任だけを橋渡しする」小さなクラスで、Player 機能を抱えていません。ヘッダーにも削除条件をコメントで明記してあります。

---

## 21. 未対応項目

- 描画補間（固定時間更新とフレーム描画の間の補間）は未実装。既存の体感を変えないことを優先しました
- `Camera` / `FreeCameraController` の Component 化は未着手（今回のスコープ外）
- 旧 `Player` クラス本体はまだ削除していません。変換前の状態からも起動できるようにするためです。Runtime の正式な更新元・描画元・Transform 情報源としては**使われていません**
- 旧 `Core::SphereColliderComponent`（Player / Stage の値メンバ）も残っています。Stage 側がまだ使っているためで、Stage の GameObject 化と同時に撤去します
- ダッシュは倍率のみ（アニメーションは Walk のまま）
- 入力のキーコンフィグ・Gamepad は未対応（`PlayerInputComponent.cpp` の 1 か所を差し替えれば対応できます）

---

## 22. 次に Stage を GameObject 化するための移行計画

```
段階 1  MeshColliderComponent を新 Component 基盤へ移植
          Scene 上の Collider を走査する CollisionWorld を実装
          → LegacyStageCollisionBridge を削除。Motor は無変更

段階 2  ShaderLayerComponent / CharacterMaterialComponent を実装
          → SceneDocument が持っていた描画設定が Component へ移る

段階 3  ステージ素材の配置を GameObject + MeshRenderer + MeshCollider へ移行
          Editor に「旧ステージを GameObject へ変換」を用意（Player と同じ方式）

段階 4  SceneDocument / LegacySceneDocumentSerializer / 旧 UndoStack を一括削除

段階 5  CameraComponent を実装
          → LegacyCameraBasisBridge を削除。Controller は無変更

段階 6  旧 Player / Stage / SceneGame を Legacy へ移すか削除
```

段階 1 が最も費用対効果が高い次の一手です。`MeshColliderComponent` は既に `IComponent` 派生として存在しており、新 `Component` を継承させて GameObject 所有へ移すだけで済みます。
