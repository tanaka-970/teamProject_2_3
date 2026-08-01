# Collision の Runtime 接続と Collider 群 実装報告

対象コミット時点: 2026-08-01
対象: RePlayEngine（C++17 / Direct3D 11 / VS ソリューション `3dgp.sln`）

---

## 0. 状態の区分（最初に読むこと）

| 区分 | 意味 |
|---|---|
| **実装済み / g++ 実測** | コードがあり、g++ 11 + ASan/UBSan/LeakSanitizer の受け入れテストで動作を数値で確認した |
| **実装済み / 静的検証のみ** | コードがあり、g++ で構文・警告ゼロを確認したが、実行しての確認はしていない |
| **Windows 実機で未確認** | MSVC でのビルドと実行を一度も行っていない |
| **未実装** | 手を付けていない |

**このセッションでは Visual Studio でのビルドを 1 度も行っていません。**
以下すべての項目に「Windows 実機で未確認」が掛かります。

---

## 1. 最優先だった「Runtime へ正式接続」

### 1-1. Runtime Scene ごとに SceneCollisionWorld を接続する — 実装済み / g++ 実測

`framework` が `SceneCollisionWorld` と `CookedMeshCollisionCache` を値メンバとして 1 つずつ所有します。

```
framework.h
    ReplayEngine::Scene::SceneCollisionWorld          object_collision_world;
    ReplayEngine::Physics::CookedMeshCollisionCache   object_collision_cook_cache;
```

つなぎ替えは `Source/app/Runtime/framework_collision_world.cpp` の 1 か所に集約しました。

| 場面 | 呼ぶ関数 |
|---|---|
| 起動時 | `initialize_collision_world()` → 編集 Scene へ Attach |
| Play 開始 | `enter_object_play_mode()` → `attach_collision_world(object_scene_runtime)` |
| Play 終了 | `exit_object_play_mode()` → `detach_collision_world()` を **Scene::Clear の前に** |
| Scene 読み込み | `load_object_scene()` → 読み込み前に Detach、読み込み後に Attach |
| 毎フレーム | `refresh_object_scene_services()` → `refresh_collision_world()` |

`Component` が見る `IPhysicsQueryService` は `object_collision_world` になりました。
`LegacyStageCollisionBridge` を直接 Physics サービスへ挿す経路は削除しています。

### 1-2. Play 終了 / Scene 切り替えで古い参照を捨てる — 実装済み / g++ 実測

`SceneCollisionWorld::AttachScene()` が、Scene が変わったときに
登録表・Trigger 接触ペア・世代番号・診断値・直近 Hit 情報をすべて捨てます。

> 受け入れテスト「Scene 切り替えで古い ObjectID / ColliderID を保持しない」
> 「切り替え後に編集 Scene の Collider へ当たらない」で確認済み。

`exit_object_play_mode()` は **Detach を Clear より前**に行います。
逆順にすると、破棄済み GameObject を引きに行く可能性があるためです。

### 1-3. Mesh Loader の接続 — 実装済み / 静的検証のみ

`Source/app/Runtime/framework_collision_mesh_source.cpp`

```
CookKey ──> load_collision_triangles()
              1) ディスク Cook キャッシュ（キー全体のハッシュをファイル名に含む）
              2) resolve_object_mesh() で skinned_mesh を取得
              3) default_global_transform だけを掛けて「モデルのローカル座標」で返す
              4) 次回のためディスクへ書き出す
```

**ワールド行列は掛けません。** 掛けると同じ Asset を別の場所へ置いた 2 体で
Cook 結果を共有できなくなるためです。

> **Windows 実機で未確認**: 実際の FBX / .cereal から三角形が取れるかは未検証。
> g++ 側のテストは、三角形を返すだけの差し替え Loader で行っています。

### 1-4. CharacterMotor の判定経路 — 実装済み / g++ 実測

`CharacterMotorComponent` は `Services().Physics()` 経由でしか地形を見ません。
`SweepSphereFiltered` / `QueryGroundFiltered` を使い、Layer / Mask と
**自分自身の除外**を条件として渡します。

### 1-5. Hit 元の診断表示 — 実装済み / 静的検証のみ

- `Player Runtime Diagnostics`（Debug ビルドのみ）へ Backend Mode / 有効 Collider 数 /
  Legacy 参照有無 / 接地と壁それぞれの Hit 元（Backend・ObjectID・ColliderID）を追加
- 新規パネル「衝突の診断」（`draw_collision_diagnostics_panel`）
  - Backend Mode の切り替え
  - 旧 Stage の移行状態と、移行済みにする / 取り消すボタン
  - 登録数・再走査回数・Cook キャッシュの生存数と Cook 実行回数
  - 接触ペア数

---

## 2. Legacy 移行状態の粒度 — 調査結果と結論

### 2-1. 調査したこと（ファイル名と行を挙げます）

| 確認項目 | 結果 | 根拠 |
|---|---|---|
| 旧 Stage は常に 1 つか | **はい。`SceneGame` に `Stage stage;` が 1 つだけ** | `Source/game/scene_game.h:66` |
| Stage の衝突は不可分か | **はい。1 つの `MeshColliderComponent collision_mesh` のみ** | `Source/game/stage.h:44` |
| 複数メッシュはどう扱われるか | **全メッシュを 1 本の三角形配列へ平坦化して Build** | `Source/game/stage.cpp:21-53` |
| 問い合わせの単位 | **`Stage&` を 1 つ受け取る形** | `Source/game/raycast.cpp:86,156` / `legacy_stage_collision_bridge.cpp:8` |
| 配置物単位の Collider は存在するか | **存在する。ただし同時に有効化できるのは 1 つだけ** | `framework_scene_document.cpp:405-412`, `633-636` |

### 2-2. 結論：単一 bool はやめました

**旧 Stage 本体だけを見れば、今日の実装は不可分なので単一 bool でも破綻しません。**

ただし `SceneDocument` の配置物（`entity->mesh_collider`）は既に「配置物ごとの
衝突メッシュ」という単位を持っており、読み込み時にそのうち 1 つだけが
Stage の衝突メッシュへ焼き込まれています（`framework_scene_document.cpp:405-412`）。
つまり **「配置物という単位は既に存在していて、たまたま同時に 1 つしか有効化できない」**
という状態です。

ここで単一 bool にすると、2 つ目の配置物を衝突対象にした瞬間に
「片方だけ移行したら両方が問い合わせ対象から外れる」という直しにくい不具合になります。
そのため移行元 ID の集合方式へ変更しました。

```
RePlayEngine/Scene/Services/LegacyStageMigrationState.h

    using SourceID = std::uint64_t;
    static constexpr SourceID stage_source_id = 1;   // 旧 Stage 本体
    // SceneDocument 配置物は EntityId をそのまま SourceID として使える

    bool IsMigrated(SourceID) const;
    void MarkMigrated(SourceID, Core::ObjectID);
    void ClearMigrated(SourceID);
```

`ShouldConsultLegacy()` は `legacy_migration_.IsMigrated(legacy_source_)` を見ます。
**同じ地形を新旧両方へ登録しないという条件は維持しています。**

> 受け入れテスト「別の移行元が未移行なら、そちらは引き続き Legacy が担当する」
> 「移行済みなら Hybrid でも旧 Stage へ当たらない」「移行済みなら Legacy を呼びもしない」で確認済み。

---

## 3. Scene 全走査 → 差分登録 — 実装済み / g++ 実測

### 3-1. 方式

`Component` から登録表へ直接通知する方式は**採りませんでした**。理由:

> Component の `OnEnable` は Scene の同期点で走りますが、そのとき framework が
> サービスを張り替え済みとは限りません。通知先が居ない瞬間に登録が落ちると、
> 「消したはずの Collider が残る」より厄介な「有るはずの Collider が無い」不具合になります。

代わりに **Scene の構成世代（`StructureGeneration`）** を使います。

```
Scene::StructureGeneration()   構成が変わるたびに +1
Scene::BumpStructureGeneration()
```

世代が進むのは次のときだけです。Transform の変更では進みません。

- `Scene::CreateGameObjectWithID` / `DestroyGameObject` / `Clear`
- `Scene::ProcessPendingOperations`（GameObject を実際に破棄したとき）
- `GameObject::AttachComponent` / `RemoveComponent` / `SetEnabled`
- `ColliderComponent::OnAttach / OnDetach / OnEnable / OnDisable`

`SceneCollisionWorld::Refresh()` は世代が変わったフレームだけ全走査します。

### 3-2. Dirty の分離

| 変更 | 作り直すもの | 作り直さないもの |
|---|---|---|
| Transform | World / Inverse World / World Bounds | Cook データ |
| Asset / Cook 設定 | Cook データ（次の必要時） | — |

`MeshColliderComponent::EnsureCooked` は **CookKey の一致**で判断します。
キーに Transform は入っていないので、動かしただけでは Cook が走りません。

> 受け入れテスト「構成が変わらなければ全走査しない」「Transform 変更では再走査しない」
> 「Transform 変更で World Bounds だけが更新される」「Transform 変更では再 Cook しない」で確認済み。

### 3-3. 生ポインタを持たない

登録表 1 件が持つのは次だけです。

```
ObjectID / ColliderID / 形状 / Layer / Mask / Trigger / active / World AABB
```

実体が必要になったときだけ `Scene::FindGameObjectByID` → `FindColliderByID` で引き直します。
Broad Phase（AABB）で落ちた Collider は実体を引きすらしません。

### 3-4. ColliderID と collider_key の 2 本立て

| | 用途 | 寿命 | 保存 |
|---|---|---|---|
| `ColliderID` | 実行中の参照解決・Hit の出所表示 | プロセス内 | しない |
| `collider_key` | Character Motor の Primary Collider 参照 | Scene / Prefab をまたぐ | **する** |

`collider_key` は GameObject 内で一意な番号です。**「何番目の Collider か」ではない**ので、
並び替えても壊れません。壊れた Scene ファイルで重複した場合は
`ReconcileRegistrations` が振り直します。

---

## 4. Cook Cache のキーと寿命 — 実装済み / g++ 実測

### 4-1. キー

```
RePlayEngine/Physics/CookedMeshCollision.h

    struct CookKey {
        std::string  asset_guid;        // 実際に解決された AssetGUID
        std::string  content_revision;  // 更新時刻 + ファイルサイズ
        CookSettings settings;          // cell_size / double_sided / sub_mesh_index
    };
```

Renderer Mesh モードでは `ResolveMeshAssetGuid()` が
MeshRenderer / SkinnedMeshRenderer の**現在の**参照先 GUID を返します。

`content_revision` は framework の `resolve_asset_revision()` が
`.cereal`（無ければ元ファイル）の最終更新時刻とサイズから作ります。
再インポートすれば必ず変わるので、**古い Cook 結果は自動的に使われなくなります。**

> content hash まで取らない理由: 大きな FBX を毎回全走査すると起動が目に見えて遅くなるため。
> 精度が必要になったら `resolve_asset_revision` だけを差し替えれば済みます。

### 4-2. 所有権と寿命 — **weak_ptr 共有 + 明示的 eviction** を採用

| 誰が | 何を持つ |
|---|---|
| `MeshColliderComponent` | `shared_ptr<const CookedMeshCollisionData>` — **唯一の所有者** |
| `CookedMeshCollisionCache` | `weak_ptr` のみ — **所有しない** |

shared_ptr を永久保持すると、Scene を切り替えても使っていない Asset の
三角形が解放されずメモリが増え続けます。weak_ptr なら
最後の Collider が消えた瞬間に実体も消えます。

掃除の手段:

| 関数 | いつ呼ぶか |
|---|---|
| `Collect()` | `detach_collision_world()` の中（Scene から離れたとき） |
| `Clear()` | Project 終了時（呼び出しは未配線 — 後述） |
| `Invalidate(guid)` | Asset を差し替えたとき（呼び出しは未配線 — 後述） |

> 受け入れテスト「参照が 0 になった Cook データを表から取り除ける」
> 「生存エントリが 0 になる（永久保持しない）」で確認済み。

---

## 5. Backend Mode の保存 — Scene v9 / 実装済み / 静的検証のみ

### 5-1. 保存先

**Scene 単位**の設定として `SceneServices` が持ち、Scene ファイルへ保存します。
Collider の Property としては保存しません。

```
SceneServices::CollisionBackendMode()          0=Legacy Only / 1=Hybrid / 2=Scene Colliders Only
SceneServices::LegacyStageMigration()          移行済み SourceID の集合
```

### 5-2. バージョンを上げた理由

保存項目が増えたので v8 のままでは読めません。v9 へ上げました。

```
SceneSerializer 出力（v9 で追加した行）
    COLLISION_STATE <backendMode> <migratedCount> <sourceId...>
```

### 5-3. 既存ファイルの扱い

| ファイル | 読めるか | Backend Mode | 移行済み集合 |
|---|---|---|---|
| v7 | 読める | 既定 **Hybrid** | 空 |
| v8 | 読める | 既定 **Hybrid** | 空 |
| v9 | 読める | ファイルの値 | ファイルの値 |
| v6 以前 | 読めない（従来どおり） | — | — |

**Hybrid を既定にした理由**: 既存 Scene は旧 Stage の衝突で動いていました。
ここで Scene Colliders Only を既定にすると、開いた瞬間に床が消えます。
Hybrid なら「MeshCollider があればそれを使い、無ければ旧 Stage」となり、
開く前と同じ挙動から始められます。

保存すると v9 になります。v8 へ戻す経路はありません。

---

## 6. Collider を自由に選択可能に — 実装済み / g++ 実測

### 6-1. 形状ごとに独立した Component

**1 つの Component 内で enum 切り替えにはしていません。**

```
ColliderComponent（抽象基底）
  ├── SphereColliderComponent    radius / skin_width / walkable_normal_y
  ├── BoxColliderComponent       size
  ├── CapsuleColliderComponent   radius / height / axis
  └── MeshColliderComponent      mesh_source / mesh_asset / cook_cell_size / double_sided
```

基底が持つ共通項目: `collider_key` / `center_offset` / `collision_layer` /
`collision_mask` / `is_trigger` / `debug_draw`

### 6-2. ライフサイクルを基底で final にした理由

```cpp
void OnAttach() final;    // 派生は OnColliderAttach() を使う
void OnDetach() final;
void OnEnable() final;
void OnDisable() final;
```

派生が override して基底呼び出しを忘れると、登録表と実体がずれて
「消したはずの Collider に当たり続ける」不具合になります。
**そもそも override できない形**にしました。

### 6-3. 登録は 1 か所

`BuiltInComponents.cpp` の `RegisterBuiltInComponents()` に 3 行足しただけです。
これで Add Component 一覧・Inspector・Scene 保存・読み込み・複製・Undo/Redo・Prefab
のすべてへ反映されます。

共通プロパティは `RegisterColliderCommon<T>()` テンプレートに 1 か所へまとめ、
形状ごとの登録から呼んでいます（同じ 6 行を 4 回書き写して片方だけ直し忘れる事故を防ぐため）。

### 6-4. Capsule の高さ警告

`height` が直径未満の場合、判定は直径へ切り上げ、Inspector へ警告を出します。
黙って別の形で当たるより、今どう解釈されているかが見える方を選びました。

---

## 7. CharacterMotor の Primary Collider — 実装済み / g++ 実測

### 7-1. 明示選択

```
Character Motor
  移動用 Collider  [ Sphere Collider #1 ▼ ]
```

参照は `collider_key`。Component 名でも GameObject 名でもありません。

**未設定のときに不透明な自動選択をしません。** 未設定なら地形判定を行わず、警告を出します。

```
  移動用 Collider  [ Missing Collider ]
  ⚠ 移動用 Collider が設定されていません
```

### 7-2. 候補の制限

Inspector の一覧には `UsableAsCharacterShape()` が true でかつ Trigger でないものだけを出します。
Mesh Collider と Trigger Collider は**出しませんし、直接指定されても受け付けません**。

### 7-3. 旧 Player 変換時だけ自動設定

`convert_legacy_player_to_gameobject()` の中で、作成した Sphere Collider を
Primary として自動設定します。変換直後に「Collider はあるのに動かない」状態で
渡すのは不親切なためです。

### 7-4. 形状 → 移動判定の球

問い合わせ窓口が球のスイープしか持たないため、形状ごとに**安全側**の球へ落とします。

| 形状 | 半径 | 接地の原点 |
|---|---|---|
| Sphere | そのまま | center_offset |
| Capsule | そのまま | 下側の半球の中心（中心で見ると床へめり込む） |
| Box | 内接球（最小の半辺長） | 中心を内接球ぶん下げた位置 |

---

## 8. Layer / Mask / Trigger — 実装済み / g++ 実測

### 8-1. 固定レイヤー表

```
0 Default / 1 Player / 2 Enemy / 3 Environment
4 Player Attack / 5 Enemy Attack / 6 Projectile / 7 Trigger
```

保存されるのは常に**番号**です。名前は Inspector の表示にしか使いません。

### 8-2. Inspector で整数を見せない

新しい PropertyType を 3 つ追加しました（内部表現はすべて int）。

| 型 | 表示 |
|---|---|
| `CollisionLayer` | Layer 名のドロップダウン |
| `CollisionMask` | Layer 名のチェックボックス（「すべて」「なし」ボタン付き） |
| `ColliderReference` | 同じ GameObject の Collider 一覧 |

### 8-3. 双方向で一致したときだけ衝突

```cpp
bool Interact(layer_a, mask_a, layer_b, mask_b) {
    return MaskContains(mask_a, layer_b) && MaskContains(mask_b, layer_a);
}
```

片側だけが相手を見ている状態を衝突とすると、どちらを主体に問い合わせたかで
結果が変わってしまうためです。

旧データ互換: `mask == -1` は「すべてと衝突する」として読みます。

### 8-4. Trigger は押し戻しと接地から除外

`SweepSceneColliders` の中で `entry.trigger` を無条件に飛ばします。
Motor 側ではなく問い合わせ窓口の中で弾くので、呼び出し側の書き忘れが起きません。

---

## 9. Trigger イベント — 実装済み / g++ 実測

### 9-1. 配送

`Core::Component` へ 3 つの仮想関数を追加しました。

```cpp
virtual void OnTriggerEnter(const TriggerContact&);
virtual void OnTriggerStay (const TriggerContact&);
virtual void OnTriggerExit (const TriggerContact&);
```

`TriggerContact` が持つのは **ObjectID と ColliderID だけ**です。生ポインタは持ちません。
受け取った側は Scene から引き直すことになるので、確かめずに使う書き方ができません。

Trigger 側と入った側の**両方**の GameObject へ届きます。

### 9-2. Enter / Stay / Exit の作り方

接触ペアをフレーム番号で追跡します。

```
初めて見えた     -> Enter
前回も見えた     -> Stay
今回見えなかった -> Exit （そしてペアを捨てる）
```

接触している間ずっと Enter が飛ぶことはありません。

### 9-3. 削除への強さ

- Exit は**ペアを外してから**配送します（配送先が Destroy を呼んでも二重 Exit にならない）
- `ReconcileRegistrations` が、登録表から消えた Collider のペアを片付けます
- 配送先は ObjectID から引き直すので、既に消えていれば何も起きません

> 受け入れテスト「接触中に GameObject を削除してもペアが安全に破棄される」で確認済み。

### 9-4. 【制限】Mesh Trigger の内外判定

**Mesh Collider を Trigger にした場合、内外判定は行いません。**
閉じたメッシュとは限らないため、内側かどうかを正しく決められないからです。
World Bounds の内側にあることをもって重なりとみなします。
凹んだ形では、実際には外側の場所でも反応しえます。
正確さが要る場所には Box / Sphere / Capsule の Trigger を使ってください。

---

## 10. Collider Debug Draw — 実装済み / g++ 実測（線分の生成のみ）

### 10-1. 役割の分離

| どこ | 何をするか | 検証状況 |
|---|---|---|
| `RePlayEngine/Editor/Debug/ColliderDebugDraw` | ワールド空間の線分を作る。D3D も ImGui も触らない | **g++ 実測** |
| `Source/app/Editor/framework_collider_debug.cpp` | 画面へ投影して ImGui の背景描画リストへ積む | **Windows 実機で未確認** |

このプロジェクトには線を引く仕組みが無く、`TransformGizmo` も数値入力だけで
3D ギズモを描いていません。線描画のパイプラインを新設すると影響範囲が広すぎるため、
ImGui のオーバーレイへ投影する方式にしました。
将来ちゃんとした線描画を入れるときは、framework 側の 1 ファイルだけ差し替えれば済みます。

### 10-2. 表示内容

| 色 | 意味 |
|---|---|
| 緑 | 通常の当たり判定 |
| 水色 | Trigger |
| 橙 | Character Motor の移動用 Collider |
| 灰 | 無効 |
| 赤 | Mesh の Cook 失敗（置いたのに当たらない原因が画面で分かる） |
| 半透明の白 | 境界ボックス |

中心オフセットの位置には小さな十字を出します（Transform と別の位置なので）。

**Mesh Collider の既定は Bounds のみ**です。三角形は
「表示設定の `Mesh の三角形を描く`」と「Collider 側の `三角形を表示`」の
両方を有効にしたときだけ描きます。

---

## 11. このセッションで見つけて直した不具合

いずれも受け入れテストが検出したものです。

### 11-1. Character Motor が自分の Collider に当たって毎フレーム宙へ上がる — 修正済み

`QueryGround` の下向きキャストが**自分自身の Sphere Collider** にヒットし、
そこを地面と判定して毎フレーム約 0.76 単位ずつ上昇していました。
240 フレームで y ≈ +185 まで浮きます。

対策として問い合わせ条件へ除外指定を追加しました。

```cpp
struct CollisionQueryFilter {
    int layer = 0;
    int mask = -1;
    Core::ObjectID ignore_object;   // この GameObject の Collider は結果に含めない
};
```

除外の単位は GameObject です。同じ GameObject に付いた本体・攻撃判定・検知範囲を
まとめて自分自身として扱います。

### 11-2. Trigger の内側に入ると反応しなくなる — 修正済み

Trigger の重なり判定を「移動量 0 のスイープを Cook 三角形へ掛ける」方式にしていたため、
**表面に触れたときしか当たりませんでした**。Trigger の内側へ完全に入ると
どの三角形とも交わらず、「中に居るのに反応しない」状態になります。

Trigger の形ごとに判定を分けました。

| Trigger の形 | 判定 |
|---|---|
| Sphere | 中心間距離（内包を含む） |
| Capsule | 解析解（開始時点の重なりを必ず拾う） |
| Box | 回転を戻してから軸並行の距離判定（内包を含む） |
| Mesh | World Bounds の内包（**近似。9-4 の制限を参照**） |

### 11-3. 中心オフセット付き Collider で接地高さがずれる — 修正済み

接地高さの計算が `hit.position.y - ground_offset.y` になっており、
`center_offset` と半径が一致しない形状（Capsule / Box）で床へめり込みました。

```cpp
// 落とした球の中心は  local.y + ground_offset.y
// 床へ乗ったとき      球の中心 = 床の高さ + 半径
ground_height_ = hit.position.y + shape.radius - shape.ground_offset.y;
```

旧 Player は `center_offset.y` と半径がどちらも 0.38 だったため、
この式は「床の高さそのもの」に一致します。**旧挙動は変わりません。**

### 11-4. 型名と同じ名前のメンバ関数 — 修正済み

`CookedMeshCollisionData::CookSettings()` が型 `CookSettings` と同名で、
C++ の規則により名前の意味が変わりコンパイルできませんでした（MSVC でも同じく失敗します）。
`Settings()` へ改名しました。

### 11-5. 基底クラスのメンバポインタで型推論が基底になる — 修正済み

`MakeProperty("collider_key", &T::collider_key)` は、`collider_key` が基底
`ColliderComponent` のメンバのため `C = ColliderComponent` と推論され、
基底が `StaticTypeID` を持たないためコンパイルできませんでした。
`MakeProperty<T, int>(...)` と明示して、メンバポインタを基底→派生へ暗黙変換させています。

---

## 12. 検証の内容と限界

### 12-1. 何をやったか

| 項目 | 内容 |
|---|---|
| コンパイラ | g++ 11、`-std=c++17` |
| 警告 | `-Wall -Wextra -Wpedantic -Wshadow` で**警告ゼロ** |
| サニタイザ | AddressSanitizer + UndefinedBehaviorSanitizer + LeakSanitizer |
| 受け入れテスト | **118 検査 / 失敗 0**、サニタイザの報告なし |
| 対象 | エンジン側 23 翻訳単位（Physics / Object / Components / Scene / Editor::Debug） |
| プロジェクトファイル | XML 妥当・ClCompile 117 / ClInclude 129・重複 0・obj 名衝突 0・登録漏れ 0・filters 未記載 0 |

### 12-2. 前回との違い

前回の DirectXMath スタブは**行列変換を恒等としてしか扱えず**、
非恒等 World / Inverse World の数値を検証できませんでした。

今回は行主・行ベクトル規約（`v' = v * M`）どおりの実装に差し替えたので、
次が実数値で確認できています。

- 平行移動した Mesh Collider の接地高さ（y=5 の床で 5.0、y=-2 で -2.0）
- 45 度回転した Box の法線 y 成分（cos45 と一致、正規化済み）
- 一様拡縮 2 倍での接地高さ
- 非一様拡縮でのローカル半径倍率（最小軸 0.5 → 2.0 倍、安全側）
- 負の拡縮での法線反転
- 球同士・カプセル側面の接触位置

### 12-3. **検証できていないこと**

| 項目 | 状態 |
|---|---|
| **MSVC でのビルド** | **一度も行っていない** |
| Windows 実機での実行 | **一度も行っていない** |
| 実際の FBX / .cereal から三角形が取れるか | **未確認** |
| ImGui オーバーレイの投影が正しいか | **未確認** |
| Inspector の新しい 3 つの Property 描画 | **未確認**（ImGui 依存のため g++ で動かせない） |
| Scene v9 の保存・再読み込み | **未確認**（実機での往復未検証） |
| Prefab 配置後の衝突 | **未確認** |
| Undo / Redo での Collider 複製 | **未確認** |
| 実機での Trigger イベント | **未確認** |

### 12-4. 検証ハーネスについて

- **リポジトリへは追加していません**（作業領域のみ）
- `.vcxproj` からも参照させていません
- スタブに合わせて本番コードの API を歪めていません
- DirectXMath に存在しない呼び方は通しません（引数の型と個数は本物に合わせてあります）

---

## 13. 未対応（次にやること）

| # | 項目 | 状態 |
|---|---|---|
| 1 | **Visual Studio Debug x64 での Rebuild と実機確認** | 未実施。**最優先** |
| 2 | Player Prefab の再利用（元の優先度 15） | **未着手** |
| 3 | `CookedMeshCollisionCache::Clear()` の呼び出し配線（Project 終了時） | 未配線 |
| 4 | `CookedMeshCollisionCache::Invalidate()` の呼び出し配線（Asset 差し替え時） | 未配線 |
| 5 | 旧 Stage → MeshCollider GameObject への変換操作 | 未実装 |
| 6 | Layer 名のプロジェクト設定化（現在は固定表） | 未実装 |
| 7 | Layer Matrix（現在は単純なビット AND） | 未実装 |
| 8 | `DepenetrateSphere`（めり込みからの押し出し） | 未実装 |
| 9 | Mesh Trigger の正確な内外判定 | 制限として明記（9-4） |

---

## 14. 実機で確認してほしいこと

**Visual Studio で Debug x64 の「リビルド」を実行してください**（「ビルド」では不十分です。
ヘッダーの構造が変わっているため、古い .obj が残ると不整合が起きます）。

その後、次の順で確認をお願いします。

1. 起動してクラッシュしないか
2. F1 で Editor を開き、「衝突の診断」ウィンドウが出るか（Player Runtime Diagnostics のボタンから）
3. 既存 Scene（v8）が読み込めて、Backend Mode が Hybrid になっているか
4. GameObject へ Box Collider を付け、`Collider を描画` で形が見えるか
5. Player の Character Motor に「移動用 Collider」が設定されているか
6. MeshCollider を持つ床へ接地するか（診断の「Ground hit from」が SceneCollider になるか）
7. MeshCollider を持つ壁を通過しないか
8. Transform を動かすと衝突位置も動くか
9. Collider を無効化すると衝突しなくなるか
10. Collider を削除してもクラッシュしないか
11. Ctrl+S で保存 → 再読み込みしても衝突するか（v9 になります）
12. Prefab を配置した後も衝突するか
13. Trigger を付けた Box へ入ると `OnTriggerEnter` 相当が動くか（接触ペア数が 1 になるか）

うまくいかない場合は「衝突の診断」ウィンドウの
**登録数・有効数・再走査回数・Cook キャッシュの Cook 実行回数・直近の Hit 元**を
教えてください。どこで止まっているか切り分けられます。
