# Player 移行後の不具合修正 第 1 次報告（優先度 1〜6）

作成日: 2026-07-31
対象: ご指定の作業順序 1〜6
未着手: 7（Player Prefab 再利用）/ 8（MeshCollider）/ 9（診断 UI の残り）

---

## 1. Controller ありでも動かない問題 — **根本原因を特定して修正**

### 原因

`refresh_object_scene_services()` が `Playing` を **`object_scene_play_mode`（F5）だけ**で決めていました。

```cpp
services.SetPlaying(object_scene_play_mode);   // ← これが原因
```

`object_scene_play_mode` が true になるのは F5 のときだけで、その F5 は **`editor_mode` が true のときしか効きません**。

| 状態 | 結果 |
|---|---|
| 起動直後（Editor 非表示） | `Playing = false` → `PlayerInputComponent::OnUpdate` が即 return → **入力が一切効かない** |
| F1 で Editor を開く | `edit_mode_active = true` → `Scene::Update` すら走らない |
| F1 → F5 | ようやく動く |

つまり **F1 → F5 を押さない限り絶対に動かない**状態でした。PlayerController があってもなくても関係ありません。

### 修正

「実行してよいか」を明確に定義し直しました。

```cpp
bool framework::object_runtime_active() const noexcept
{
    const bool editing = editor_mode && edit_mode_active && !object_scene_play_mode;
    return !editing;
}
```

| 状態 | 動作 |
|---|---|
| Editor 非表示（通常のゲーム実行） | **動く**（旧 Player と同じ体感） |
| Editor 表示 + Edit Mode | 止まる（編集中なので正しい） |
| Editor 表示 + Play Mode (F5) | 動く |

`Playing` / FixedUpdate / `Scene::Update` の 3 か所すべてをこの 1 つの判定へ統一しました。

---

## 2. 旧 Player の自動復活と二重描画 — **修正**

### 原因

旧 Player を出すかどうかを **`PlayerControllerComponent` の有無**で判定していました。そのため Controller を削除した瞬間に旧 Player が復活し、同じキャラクターが 2 体表示されていました。

### 修正

**移行状態を Component から完全に分離**しました。新規追加した `ScenePlayerMigrationState` が持ちます。

```
ScenePlayerMigrationState   … 移行が済んでいるか（Scene に保存）
PlayerControlSystem         … 今どれを操作しているか（ObjectID）
PlayerCompositionValidator  … 必要な Component が揃っているか（警告のみ）
```

判定を移行状態だけに変えました。

```cpp
object_player_active = services.PlayerMigration().Migrated();
```

**Component を削除しても移行状態は取り消されません。** 実測で確認済みの挙動:

| 操作 | 結果 |
|---|---|
| PlayerController を削除 | 新 Player は表示されたまま / 操作だけ停止 / **旧 Player は復活しない** |
| SkinnedMeshRenderer を削除 | 見えなくなる / **旧 Player は復活しない** |
| CharacterMotor を削除 | 移動できなくなる / **旧 Player は復活しない** |

移行状態は v8 Scene へ保存され、再起動後も維持されます。

### 追加の修正

`PlayerControlSystem::Resolve` が **Controller の有無で乗り移らない**ようにしました。以前は Controller を消すと勝手に別の GameObject へ操作が移っていました。今は「対象が存在するか」だけを見ます。

---

## 3. モデルの向き・大きさ — **修正**

### 原因

`SkinnedMeshRendererComponent` に姿勢補正はありましたが**縮尺の補正がなく**、GameObject の Scale をそのまま使っていました。新規作成した GameObject は Scale が 1.0 なので、旧 Player の 0.01 に対して **100 倍**になります。

### 修正

モデル座標系の補正を 3 つ追加し、`PropertyRegistry` へ登録しました（Scene 保存・Prefab 保存に対応）。**Player 固有のハードコードではありません。**

```cpp
DirectX::XMFLOAT3 visual_rotation_offset{ 90.0f, 188.5f, 180.0f };  // 姿勢補正（度）
DirectX::XMFLOAT3 local_position_offset{ 0.0f, 0.0f, 0.0f };        // 位置ずらし
DirectX::XMFLOAT3 local_scale_multiplier{ 1.0f, 1.0f, 1.0f };       // 縮尺倍率
```

行列の合成順は旧 Player と完全に同じです。

```
world = FbxC * ( (S_object × S_multiplier) * R(visual_offset + euler) * T(pos + pos_offset) ) * parentWorld
```

変換時の扱いも変えました。**GameObject の Scale は 1.0 のままにし、旧 Player の 0.01 を `local_scale_multiplier` へ移します。** 見た目は完全に同じで、かつ Collider の半径やギズモが実寸の単位になります。

---

## 4. controlledObjectId の保存と Runtime Scene への引き継ぎ — **修正**

**Scene 形式を v8 へ上げました**（v7 も引き続き読めます）。

```
REPLAY_SCENE 8
SCENE "TrainingStage"
SCENE_STATE <controlledObjectId> <migrated> <migratedPlayerObjectId>
OBJECT_COUNT 3
...
```

`ApplySceneData` で **必ず対応表を通して再マッピング**します。ID が採番し直された場合でも操作対象を見失いません。

```cpp
scene.Services().SetControlledObject(remap(data.controlled_object));
scene.Services().PlayerMigration().Restore(data.legacy_player_migrated, remap(data.migrated_player_object));
```

Play 開始時の Runtime Scene 複製も同じ `ApplySceneData` を通るので、そのまま引き継がれます。Prefab 配置（`InstantiateSceneData`）では**逆に持ち込みません** — 操作対象は配置先 Scene が決めるものだからです。

実測で確認: v8 保存 → 読込 → 移行状態と操作対象が復元される / v7 も読める。

---

## 5. Component 削除・再追加時のライフサイクル — **確認と補強**

`PlayerControllerComponent` は**依存 Component をキャッシュしていません**。毎回 `Owner()->GetComponent<T>()` で引き直します。古いポインタを保持する経路が存在しないので、削除→再追加でも壊れません。

| 保証項目 | 根拠 |
|---|---|
| 削除した Component へアクセスしない | `GetComponent` は `PendingDestroy` を除外する |
| vector 再配置後の古いポインタを保持しない | そもそもキャッシュしない |
| 再追加すると依存先を再解決する | 毎フレーム引き直すため自動 |
| Edit Scene のポインタを Runtime Scene へ持ち込まない | 複製は `SceneData` 経由。ポインタは 1 つも渡らない |
| Update 中の削除は Deferred | 予約フラグ + 同期点で回収 |
| `OnStart` が毎フレーム呼ばれない | `started_` フラグで一度だけ |
| `OnDetach` が複数回呼ばれない | `Owner() == nullptr` で二重実行を防止 |

`CharacterMotorComponent::OnDisable` で速度をリセットするようにしました。無効化→再有効化で古い速度のまま飛び出しません。

---

## 6. AssetGUID 表示の改善 — **修正**

生の GUID を通常の Inspector から撤去しました。**Component ごとの処理ではなく、`AssetPath` 型の Property すべてに効く共通 Drawer** です。

```
メッシュ  [ AllAnimation1 ▼ ]
          resources/AnimationModel/AllAnimation1.cereal
          [解除]  ▶ 詳細
```

欠損時:

```
メッシュ  [ Missing Asset ]
          GUID に対応する Asset が見つかりません
```

GUID は折り畳みの「詳細」の中にのみ表示し、コピーボタンを付けました。内部と Scene 保存は従来どおり GUID を使います。

---

## おまけ（優先度 2〜3 のうち今回入ったもの）

### Play / Edit Mode の表示

メニューバーへ現在の状態を常時表示するようにしました。

```
PLAY MODE   実行シーンで動作中 / 入力有効
RUNNING     編集シーンをそのまま実行中 / 入力有効
EDIT MODE   編集中 / 物理と入力は停止 (F3 で切替、F5 で Play)
```

Debug ビルドではウィンドウが非アクティブなときに「ゲーム画面をクリックすると入力を受け取ります」も出ます。Play 開始・停止時に FixedUpdate の accumulator をリセットしています。

### Player 構成の診断 UI

Inspector に「操作対象としての構成」を追加しました。型ごとの専用 Editor ではなく、`PlayerCompositionValidator` の表を描くだけです。

```
操作対象: この GameObject
OK   Player Input
必須 Player Controller
         入力を Character Motor へ渡せないため操作できません
OK   Character Motor
...
[操作対象に設定] [不足 Component を追加] [構成を再検証]
```

**「不足 Component を追加」はユーザーが押したときだけ実行**します。削除した Component を勝手に復活させません。重複追加も起きません。

### Player Runtime Diagnostics（Debug ビルドのみ）

インスペクターの「ワールド」に折り畳みで追加しました。`#ifdef _DEBUG` で囲んであるので Release には残りません。

Mode / Runtime active / Legacy migrated / Legacy Player Update / Legacy Player Render / Fixed accumulator / Render items / Controlled Object / Input enabled / Input X,Y / Jump latched / Controller enabled / Motor enabled / Velocity / Grounded / Vertical physics / Position / Collision available を表示します。

**「入力で止まっているのか、Controller か、Motor か、FixedUpdate か、描画か」を画面上で切り分けられます。**

### 変換ボタンの再実行防止

移行済みなら変換ボタン自体を出さず、`旧 Player は変換済みです（Player ObjectID: 2）` と表示します。Component を削除していても移行状態で判定するので、二重作成は起きません。

### Hierarchy から旧 Player 項目を撤去

移行後は固定項目「プレイヤー」を出しません。Player は下の GameObject ツリーへ通常の GameObject として現れます。未変換のときだけ `Legacy 旧プレイヤー（未変換）` を出します。

---

## 検証結果

### g++ で実測（ASan + UBSan + LeakSanitizer 付き）

**51 項目すべて合格**。うち今回追加した項目:

| 確認内容 | 結果 |
|---|---|
| 移行状態を記録できる | OK |
| **Controller を削除しても移行状態は取り消されない** | OK |
| Controller を削除しても GameObject は残る | OK |
| **Controller 削除でも操作対象は維持される（乗り移らない）** | OK |
| 操作可能かどうかは別途判定できる | OK |
| v8 に移行状態と操作対象が入る | OK |
| **再起動後も移行状態が維持される** | OK |
| **操作対象 ObjectID が復元される** | OK |
| v7 ファイルも引き続き読める | OK |
| メモリリーク・未定義動作 | **ゼロ** |

**ASan が実際にヒープ破壊を 1 件検出しました。** 検証ハーネス側で、ヘッダー変更前の `.o` が混在していたことによる ODR 違反でした（`SkinnedMeshRendererComponent` のサイズが 104 バイトから増えたのに、古いオブジェクトが 104 バイトで確保していた）。全再ビルドで解消しています。

**同じことが Visual Studio の増分ビルドでも起こり得ます。** 今回ヘッダーを複数変更しているので、**必ず Rebuild してください**（Build ではなく）。

### vcxproj

| 項目 | 結果 |
|---|---|
| ClCompile | 104 件 |
| obj ベース名の重複 | **0 種** |
| 存在しない登録 / filters 不一致 | **0 件** |
| XML 妥当性 | OK |

---

## MSVC 未確認範囲

**Visual Studio Debug x64 での実行は未確認です。** 特に以下は g++ に通らないため静的レビューのみです。

- `framework_gameobject_scene.cpp` の `object_runtime_active` と移行状態の配線
- `framework_editor.cpp` のモードバナーと診断表示
- `framework_inspector.cpp` の旧 Player UI 撤去
- 実際の入力・移動・モデルの見た目

**確認していただきたい順序:**

1. **Rebuild**（増分ビルドはヘッダー変更のため危険）
2. 起動 → F1 を押さずに WASD で **そのまま動くか**（これが今回の本丸）
3. メニューバーに `RUNNING` と出るか
4. 旧 Player を変換 → **モデルの大きさと向きが変換前と同じか**
5. モデルが **1 体だけ**表示されているか
6. PlayerController を削除 → 操作だけ止まり、**旧 Player が復活しないか**
7. Undo で戻るか / 再追加できるか
8. Ctrl+S → 再起動 → 構成と操作対象が復元されるか
9. Inspector の Asset 欄に GUID が出ていないか
10. 「ワールド」の Player Runtime Diagnostics が読めるか

---

## 次の作業（優先度 7〜9）

ご指定の順序に従い、**Player が動く状態を確認してから**進めます。

| # | 内容 | 状態 |
|---|---|---|
| 7 | Player Prefab の再利用（Default Controlled Character Prefab / 新規 Scene 種別 / 名前非依存） | 未着手 |
| 8 | MeshColliderComponent | ヘッダーのみ作成済み。**vcxproj へ未登録・未使用なのでビルドに影響しません** |
| 9 | 診断 UI の残りと Undo/Redo・Prefab の再確認 | 一部実装済み |

**8 の `RePlayEngine/Components/Physics/MeshColliderComponent.h` は書きかけです。** どこからも include しておらず vcxproj にも入っていないため、ビルドには一切影響しません。次のバッチで `.cpp` と登録を完成させます。
