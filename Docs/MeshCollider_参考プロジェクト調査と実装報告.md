# MeshCollider 実装報告（参考プロジェクト調査つき）

作成日: 2026-08-01

---

## 1. 調査した参考プロジェクトのファイル一覧

`C:\Users\2250298\Desktop\②sv23提出\...\5_プロジェクト\Source\` を実際に開いて読みました。

| ファイル | 読んだ内容 |
|---|---|
| `Collision.h` | クラス定義・全関数シグネチャ |
| `StageCollisionMesh.h` | クラス定義・全メンバ・コメント |
| `ObjectCollision.h` | クラス定義・全関数・`Collider` 構造体 |
| `ObjectCollision.cpp` | `EnsureCollisionMesh` / `ScratchIndices` / 統計まわり |
| `Stage.h` | 構造体全体 |
| `BoneColliderAsset.h` | 構造体全体 |
| `BoneColliderWorld.h` | クラス定義とコメント |
| `Game.vcxproj` | Jolt の登録状況 |
| `External/JoltPhysics/LICENSE` | ライセンス本文 |

## 2. 調査したクラス / 関数一覧

| クラス | 主な関数 | 責任 | 呼び出し元 |
|---|---|---|---|
| `Collision` | `RayCast` / `SphereCast` / **`DepenetrateSphere`** / `*Triangles` 版 | 三角形群への幾何判定。全 static | `ObjectCollision` |
| `StageCollisionMesh` | コンストラクタ / `Matches` / `CollectTriangles` / `CellRange` | ロード時に全三角形を**ワールド空間へ変換**して保持し、XZ グリッドへ登録 | `ObjectCollision::EnsureCollisionMesh` |
| `ObjectCollision` | `EnsureCollisionMesh` / `RayCastStage` / `SphereCastStages` / `DepenetrateSphereStages` / `ClampSweptPosition` | Stage 群 + 配置コライダーへの問い合わせ窓口。遅延構築 | ゲーム側の移動処理 |
| `ObjectCollision::Collider` | （データのみ） | 配置物用の軽量コライダー。**共通基底も virtual も無いタグ付き struct** | `StageObjectSystem::GetCollisionObjects()` |
| `BoneColliderAsset` | `Save` / `Load` / `ConfigPathForModel` | ボーン単位コライダーを JSON でモデル毎に保存 | 当たり判定エディタ |
| `BoneColliderWorld` | `BuildBoneColliderWorldSpheres` / `GetCachedBoneColliderAsset` | 現在のポーズからワールド球群を毎フレーム生成 | 敵移動・ヒット判定 |

## 3. 参考プロジェクトの Collision 処理フロー

```
移動処理
  └─ ObjectCollision::SphereCastStages(start, end, radius, std::vector<Stage>&, ...)
       └─ 各 Stage について EnsureCollisionMesh(stage)
            └─ stage.collisionMesh が無い / Matches() が false なら
                 make_shared<StageCollisionMesh>(model, transform)
                   └─ 全三角形をワールドへ変換して保持
                   └─ XZ グリッドへ登録
       └─ CollectTriangles(クエリ AABB) で近傍だけ列挙（訪問スタンプで重複除去）
       └─ Collision::SphereCastTriangles(...) で幾何判定
       └─ 全 Stage の中で最も近い Hit を採用
```

## 4. RePlayEngine の完成後の Collision 処理フロー

```
CharacterMotorComponent::OnFixedUpdate
  └─ Scene::Services().Physics()  ← IPhysicsQueryService しか見ない
       └─ SceneCollisionWorld
            ├─ Refresh()（毎フレーム 1 回）
            │    └─ Scene 上の MeshColliderComponent を走査
            │         ├─ EnsureCooked()  … Asset / Cook 設定が変わったときだけ Cook
            │         │    └─ CookedMeshCollisionCache（AssetGUID 単位で共有）
            │         └─ RefreshTransformIfChanged() … Transform が変わったときだけ
            │              World / Inverse World / World Bounds を作り直す
            │
            └─ QueryGround / SweepSphere
                 ├─ Scene Collider へ
                 │    1. ワールド AABB で粗く絞る
                 │    2. Inverse World でクエリを「その Collider のローカル空間」へ変換
                 │    3. Cook データ（ローカル三角形）の XZ グリッドで絞る
                 │    4. CastSphereAgainstTriangles で判定
                 │    5. 位置を World へ、法線を逆行列の転置で World へ戻す
                 │    6. 面の向き（床/壁）はワールドで判定
                 ├─ Legacy へ（Hybrid かつ旧 Stage が未移行のときだけ）
                 └─ 近い方を採用し、Hit へ backend / ObjectID / ColliderID を記録
```

## 5. 採用した設計

| 参考プロジェクトの考え方 | RePlayEngine への反映 |
|---|---|
| XZ グリッド + 訪問スタンプで近傍だけ列挙 | `CookedMeshCollisionData::CollectTriangles`（ただしローカル空間） |
| Cook 済み形状とインスタンスの分離 | `CookedMeshCollisionData`（共有）と `MeshColliderComponent`（個別） |
| 遅延構築（`EnsureCollisionMesh`） | `EnsureCooked()` / `RefreshTransformIfChanged()`。毎フレーム Cook しない |
| クエリ用スクラッチバッファの使い回し | `SceneCollisionWorld::scratch_indices_` / `scratch_triangles_` |
| `minimumNormalY` / `maximumNormalY` での面フィルタ | 同じ方式。床と壁を法線の上向き成分で分ける |

## 6. 調整して採用した設計

| 参考 | RePlayEngine | 調整理由 |
|---|---|---|
| **ワールド空間の三角形を Transform 変更時に再構築** | **ローカル空間で Cook し、クエリ側をローカルへ変換** | AssetGUID 単位で共有するため。Transform が違う 2 体で同じ配列を使えない |
| `std::vector<Stage>&` を毎回引数で渡す | Scene 上の Collider を走査する `SceneCollisionWorld` | Scene 単位で状態を持てる。Edit / Runtime Scene 分離と両立 |
| `stageIndex`（配列添字） | `ObjectID` + `ColliderID` | 配列の並びに依存しない永続参照 |
| `Stage` に `mutable shared_ptr` でキャッシュ | Component が `shared_ptr<const CookedMeshCollisionData>` を保持 | 所有関係が明確。const 共有なので書き換え競合が起きない |
| JSON 独自 Serializer（`BoneColliderAsset`） | PropertyRegistry + v8 Scene | Editor 表示と保存で定義を二重に書かない |
| `ObjectCollision` の static 関数群 | インスタンス（framework が値で所有） | Singleton を増やさない方針 |

## 7. 不採用にした設計と理由

| 項目 | 理由 |
|---|---|
| **Jolt Physics の導入** | `Game.vcxproj` に 578 箇所登録されているが、`Source/` から `#include <Jolt...>` も `JPH::` も **1 件も見つからなかった**。自作コードでは実質未使用。RePlayEngine の規模に対して過大 |
| ワールド空間三角形の保持 | AssetGUID 単位の共有と両立しない（第 6 節） |
| `ObjectCollision` の static 関数群 | 実質グローバル状態。Scene ごとに切り替えられない |
| タグ付き struct 1 個で全形状 | 既に `SphereColliderComponent` が独立している。Component 単位の分割を維持する方が Registry / Inspector と噛み合う |
| `BoneColliderAsset` のボーン単位コライダー | 今回の静的環境コライダーとは目的が違う。将来のヒット判定用として別途 |
| `DepenetrateSphere`（めり込み押し出し） | **考え方は採用したいが今回は未実装**。既存 `SphereCast.h` に相当関数が無く、追加すると既存の移動挙動が変わるため見送った（第 13 節） |

## 8. ライセンス確認結果

| 対象 | ライセンス | 著作権表示 | 扱い |
|---|---|---|---|
| JoltPhysics | **MIT**（Copyright 2021 Jorrit Rouwe） | 再配布時に必要 | **使用せず。コピーもしていない** |
| cereal / Effekseer / imgui-node-editor | 各 LICENSE あり | — | 今回は無関係 |
| 参考プロジェクトの自作コード（`Source/`） | **LICENSE / NOTICE ファイルなし** | 不明 | **コードを 1 行もコピーしていない。設計思想のみ参考** |

ライセンスが明示されていないコードはコピーしない方針に従いました。

## 9. コピーしたコードの有無

**ゼロです。** 参考プロジェクトからコピーした行はありません。

XZ グリッドと訪問スタンプという**手法**は共通ですが、以下がすべて異なります。

- 座標系（ワールド → **ローカル**）
- 所有形態（Stage の mutable キャッシュ → **AssetGUID 共有の不変オブジェクト**）
- クラス構成（static 関数群 → Component + サービス）
- 命名規約・データ構造・API

なお **RePlayEngine の旧 `Core::MeshColliderComponent` は、以前から `StageCollisionMesh` とほぼ同構造**でした。今回それとは別に新設計を作り、旧側は `LegacyMeshColliderComponent.cpp` へリネームして役割を明示しています。

## 10. RePlayEngine 独自実装部分

| 機能 | 参考プロジェクトの状態 |
|---|---|
| **AssetGUID 単位の Cook 共有** | **なし**（Stage ごとに個別構築） |
| **ローカル空間 Cook + クエリのローカル変換** | **なし**（ワールド空間で保持） |
| **ColliderID** | **なし** |
| **Hit への衝突元記録（CollisionBackend / ObjectID / ColliderID）** | **なし** |
| **Collision Backend Mode（Legacy Only / Hybrid / Scene Colliders Only）** | **なし**（移行という概念がない） |
| **非一様・負の拡縮の検出と Inspector 警告** | **なし** |
| Component 化・Registry 登録・Inspector 自動生成・Undo / Redo | **なし** |
| Scene v8 保存・Prefab 対応 | **なし** |
| Trigger フラグ | **なし** |
| Layer / Mask | **なし** |

## 11. 参考プロジェクトに存在しなかった機能（推測なしで確認済み）

コードを検索して不在を確認したものです。

- Capsule Collider の**メッシュ版**（`ObjectCollision::Collider` には Capsule 型があるが三角形メッシュ版は無い）
- Trigger とイベント（Enter / Stay / Exit）
- Mesh Cook Cache（AssetGUID 単位の共有）
- BVH / AABB Tree（XZ グリッドのみ）
- Prefab
- Editor Gizmo（Collider 用の専用ギズモ）
- Layer Matrix / Collision Profile
- Collider 共通基底 / インターフェース
- Physics World（登録・解除を持つオブジェクト）

## 12. 参考プロジェクトより簡略化した機能

| 項目 | 理由 |
|---|---|
| `DepenetrateSphere`（めり込み押し出し） | 既存の移動挙動を変えないため今回は未実装 |
| `ClampInsideCollider`（範囲内へ引き戻す） | 今回の静的環境コライダーの範囲外 |
| `RayCastWorld` / 貫通セーフティネット | `IPhysicsQueryService` に Raycast がまだ無い |
| 配置コライダー（Sphere / Box / Capsule のプリミティブ） | `SphereColliderComponent` のみ実装済み。Box / Capsule は未実装 |
| Layer Matrix | ビットマスクの AND のみ。名前つきレイヤーと行列 UI は未実装 |

## 13. 比較表

| 項目 | 参考プロジェクト | RePlayEngine での実装 | 採用判断 | 理由 |
|---|---|---|---|---|
| Collider 共通構造 | タグ付き struct 1 個（基底なし） | Component 単位に分割（Sphere / Mesh） | 不採用 | Registry / Inspector と噛み合わない |
| Collider ID | なし（配列添字 `stageIndex`） | `ColliderID` + `ObjectID` | 独自追加 | 並び順に依存しない参照が必要 |
| Mesh Cook | ワールド空間、Transform 変更で再構築 | **ローカル空間、Transform 変更では再 Cook しない** | 調整して採用 | AssetGUID 共有と両立させるため |
| Cook Cache | なし | `CookedMeshCollisionCache`（GUID キー、失敗も記録） | 独自追加 | 同じメッシュの複数配置で必須 |
| Broad Phase | XZ グリッド + 訪問スタンプ | 同方式（ローカル空間）+ ワールド AABB の粗い絞り込み | 採用 | 規模に見合っている |
| Physics Query | `SphereCast` / `RayCast` / `Depenetrate` | `QueryGround` / `SweepSphere`（Raycast と Depenetrate は未実装） | 調整して採用 | Motor が必要とする分だけ |
| Trigger イベント | **なし** | `is_trigger` フラグのみ（イベントは未実装） | 独自追加（部分） | 押し戻しから除外する用途に限定 |
| Layer / Mask | **なし** | `collision_layer` / `collision_mask`（ビット AND） | 独自追加（簡略） | Layer Matrix は未実装 |
| CharacterMotor 接続 | 移動処理が `ObjectCollision` を直接呼ぶ | `IPhysicsQueryService` 越し。Motor は実装を知らない | 調整して採用 | Stage 型依存を切るため |
| Debug Draw | `ObjectCollision::DrawDebugGUI`（ON/OFF と計測） | Collider 数 / Hit 元 / Backend の診断表示 | 調整して採用 | 移行状況の可視化を優先 |
| Scene 保存 | JSON（`BoneColliderAsset` のみ） | v8 Scene + PropertyRegistry | 調整して採用 | 定義を二重に書かない |
| Prefab 対応 | **なし** | 既存 PrefabSerializer がそのまま対応 | 独自追加 | Component なので自動的に対応 |
| 所有権 | `Stage` の `mutable shared_ptr` | `shared_ptr<const>` を Component が保持。実体は Cache が所有 | 調整して採用 | const 共有で書き換え競合を排除 |
| Scene 破棄 | Stage 破棄と同時 | Component 破棄で共有参照を手放す。最後の 1 つで実体が消える | 調整して採用 | RAII |

## 14. Hybrid 時の問い合わせ順序と二重衝突の防止

```
SceneCollisionWorld::QueryGround / SweepSphere
  1. Scene 上の有効な MeshCollider へ問い合わせる（Trigger は除外）
  2. ShouldConsultLegacy() が true のときだけ Legacy へ問い合わせる
  3. 両方から返ったら近い方（接地は高い方）を採用する
  4. Hit へ backend / ObjectID / ColliderID を記録する
```

**二重衝突を防ぐ仕組み**

「MeshCollider で当たらなければ Legacy」という順序依存の方式は採っていません。それだと MeshCollider の隙間から旧地形が反応します。

代わりに **Stage 単位の移行フラグ**で入口を閉じます。

```cpp
bool SceneCollisionWorld::ShouldConsultLegacy() const noexcept
{
    if (legacy_ == nullptr) return false;
    switch (mode_) {
    case BackendMode::LegacyOnly:         return true;
    case BackendMode::Hybrid:             return !legacy_stage_migrated_;  // ← ここ
    case BackendMode::SceneCollidersOnly: return false;
    }
}
```

`legacy_stage_migrated_` が true なら Hybrid でも Legacy へ**一切**問い合わせません。同じ地形が新旧両方から返ることは構造的に起こりません。実測で確認済みです。

**Hit 元の診断方法**

```
Ground Hit Source: SceneCollider
Ground ObjectID: 42
Ground ColliderID: 8
```

`GroundHit::source` / `SphereSweepHit::source` に `CollisionBackend` / `ObjectID` / `ColliderID` が入ります。`SceneCollisionWorld::LegacyConsulted()` で「今フレーム Legacy を見たか」も取れます。

**Scene Colliders Only へ切り替える条件 / Legacy 削除条件**

1. 旧 Stage を MeshCollider を持つ GameObject へ変換する
2. 保存・再読み込みで構成が復元されることを確認する
3. 接地・壁・傾斜を実機で確認する
4. `BackendMode` を `SceneCollidersOnly` にする
5. `SceneCollisionWorld::AttachLegacy(nullptr)` にする
6. `LegacyStageCollisionBridge` と `LegacyMeshColliderComponent.cpp` を削除する

## 15. 非一様・負の拡縮の扱い（制限を明記）

| 状況 | 扱い | Inspector 表示 |
|---|---|---|
| 一様な拡縮 | `radius_local = radius_world / scale`。**正確** | 警告なし |
| **非一様な拡縮** | 球がローカルでは楕円体になり**正確には扱えない**。最も縮む軸で割り、**安全側（やや大きめ）に近似** | 「拡大率が軸ごとに異なります。…安全側に近似しています」 |
| **負の拡縮** | 面の裏表が反転するため**法線を反転**して扱う | 「負の拡大率が含まれています。…法線を反転して扱います」 |

法線は**逆行列の転置**で変換しています（位置と同じ行列では非一様拡縮で誤ります）。距離は「ワールドでの Hit 位置」から求めるので、ローカルのスケールに影響されません。

## 16. 根拠となるファイルパス / クラス / 関数

| 参考プロジェクト | 役割 | RePlayEngine への反映 |
|---|---|---|
| `Source/StageCollisionMesh.h` — `StageCollisionMesh` コンストラクタ | ロード時に全三角形をワールドへ変換し XZ グリッドへ登録 | `RePlayEngine/Physics/CookedMeshCollision.cpp` — `CookedMeshCollisionData::BuildGrid()`。**ローカル座標**で同じグリッドを構築 |
| `Source/StageCollisionMesh.h` — `CollectTriangles()` | AABB に掛かるセルの三角形を訪問スタンプで重複なく列挙 | 同 — `CookedMeshCollisionData::CollectTriangles()`。ローカル AABB で受ける |
| `Source/StageCollisionMesh.h` — `Matches()` | モデルと行列が同じかでキャッシュ有効判定 | `MeshColliderComponent::EnsureCooked()`（Asset + Cook 設定）と `RefreshTransformIfChanged()`（Transform）へ**分離**。Transform が変わっても再 Cook しない |
| `Source/ObjectCollision.cpp` — `EnsureCollisionMesh()` | 未構築 / 変更時に遅延構築 | `SceneCollisionWorld::Refresh()` が Scene を走査して同等の役割 |
| `Source/ObjectCollision.cpp` — `ScratchIndices()` | クエリ毎のヒープ確保を避ける | `SceneCollisionWorld::scratch_indices_` / `scratch_triangles_` |
| `Source/ObjectCollision.h` — `SphereCastStages()` | 全 Stage の中で最も近い Hit を返す | `SceneCollisionWorld::SweepSceneColliders()`。ObjectID / ColliderID も返す |
| `Source/Collision.h` — `SphereCastTriangles()` | 抽出済み三角形への球スイープ | 既存 `Physics::CastSphereAgainstTriangles` をそのまま利用（RePlayEngine に既存） |
| `Source/Stage.h` — `mutable shared_ptr<StageCollisionMesh>` | Stage がキャッシュを所有 | `MeshColliderComponent` が `shared_ptr<const CookedMeshCollisionData>` を保持。実体は `CookedMeshCollisionCache` |

## 17. 変更ファイル一覧

### 新規

| ファイル | フィルター | 責任 |
|---|---|---|
| `RePlayEngine/Physics/CookedMeshCollision.h` / `.cpp` | `RePlayEngine\Physics` | ローカル座標の Cook データと AssetGUID 共有キャッシュ |
| `RePlayEngine/Components/Physics/MeshColliderComponent.h` / `.cpp` | `RePlayEngine\Components\Physics` | Collider インスタンス（World / Inverse / Bounds / ColliderID / Layer / Trigger） |
| `RePlayEngine/Scene/Services/SceneCollisionWorld.h` / `.cpp` | `RePlayEngine\Scene\Services` | Scene 走査型の `IPhysicsQueryService`。Backend Mode を持つ |

### 変更

| ファイル | 内容 |
|---|---|
| `RePlayEngine/Scene/Services/IPhysicsQueryService.h` | `CollisionBackend` / `ColliderID` / `CollisionSourceInfo` を追加し、Hit へ衝突元を記録 |
| `RePlayEngine/Object/Registry/BuiltInComponents.cpp` | MeshCollider を**単一登録点**へ 1 か所追加 |
| `Source/game/legacy_stage_collision_bridge.cpp` | Hit へ `LegacyStage` を刻む |
| `RePlayEngine/Physics/MeshColliderComponent.cpp` → `LegacyMeshColliderComponent.cpp` | **リネーム**。新旧同名 `.cpp` による obj 衝突を回避し、役割をファイル名で区別 |

### vcxproj

ClCompile 107 / ClInclude 121。**obj ベース名の重複 0 種**、登録漏れ 0、filters 不一致 0、XML 妥当性 OK。

## 18. 動作確認結果

### g++ で実測（ASan + UBSan + LeakSanitizer 付き）— 27 項目合格

| 確認内容 | 結果 |
|---|---|
| MeshCollider が単一登録点へ登録されている | OK |
| **同じ AssetGUID は Cook を 1 回だけ行い実体を共有する** | OK |
| 読み込めない Asset は nullptr（無効 Collider を作らない） | OK |
| 失敗を記録して再試行しない | OK |
| ローカル Bounds が求まる | OK |
| Renderer のメッシュを既定で使う | OK |
| **Transform が違っても Cook データを共有する** | OK |
| ColliderID は個別に振られる | OK |
| 床へ接地できる / **Hit 元が SceneCollider・ObjectID・ColliderID で返る** | OK |
| 無効化で衝突対象から外れる / Hit 元にならない | OK |
| 削除後に古いポインタを参照しない / 落ちない | OK |
| Renderer が無ければ対象外 + 警告文が入る | OK |
| 欠損 Asset でも無効 Collider を登録しない | OK |
| Legacy Only / Hybrid の切り替え | OK |
| **移行済みなら Hybrid でも Legacy へ問い合わせない（二重衝突しない）** | OK |
| メモリリーク・未定義動作 | **ゼロ** |

## 19. 未確認事項（実機確認が必要）

**Visual Studio Debug x64 でのビルドと実行は未確認です。**

さらに、**検証ハーネスの DirectXMath スタブは行列変換を恒等としてしか扱えない**ため、以下は g++ では検証できていません。

| 未検証項目 | 理由 |
|---|---|
| **非恒等な World / Inverse World での位置変換** | スタブが恒等通過のため |
| **法線の逆行列転置変換** | 同上 |
| **非一様・負の拡縮の実際の挙動** | 同上。実装とロジックは書いたが数値は未検証 |
| ワールド AABB の算出 | 同上 |
| 実際の球対三角形判定 | 既存 `SphereCast.cpp` は DirectXMath のベクトル演算子を全面的に使っており、スタブで再現すると実在しない呼び方まで通す危険がある。テストでは最小実装へ差し替えた |
| Editor での追加・削除・Undo / Redo | ImGui 経路 |
| Scene v8 保存・Prefab 配置での MeshCollider 復元 | framework 経路 |

## 20. Windows 実機で確認していただきたいこと

**Rebuild でお願いします**（ヘッダーを複数変更しているため）。

1. ビルドが通る / assert なしで起動する
2. Add Component に「Mesh Collider」が Physics カテゴリで出る
3. GameObject へ Mesh Renderer + Mesh Collider を付ける
4. Inspector に「メッシュの取得元」が出て、Renderer / 衝突専用の切り替えができる
5. Renderer が無い状態で警告文が出る
6. **Transform を動かすとコライダーも一緒に動く**（非恒等変換の確認）
7. **拡大率を軸ごとに変えると警告が出る**
8. **負の拡大率で警告が出る**
9. 同じメッシュを 2 か所へ置いても Cook が 1 回だけ（フレーム落ちしない）
10. CharacterMotor が床へ接地する / 壁へめり込み続けない
11. Collider を無効化すると衝突対象から外れる
12. Collider を削除しても落ちない
13. Ctrl+S → 再起動で Collider 設定が復元される
14. Prefab 保存・配置で Collider が付いてくる
15. 終了時に assert が出ない

## 21. 未対応項目

- `SceneCollisionWorld` の framework への配線（`AttachScene` / `AttachLegacy` / `SetMeshLoader` の呼び出し）は**未実施**。現状 framework はまだ `LegacyStageCollisionBridge` を直接使っています
- Backend Mode の Scene v8 保存（現状は実行時のみ）
- Mesh から三角形を取り出す Loader の実装（`MeshCollisionCooker::Load` との接続）
- Box / Capsule Collider
- Trigger イベント（Enter / Stay / Exit）
- Layer Matrix / Collision Profile
- Collider Gizmo / Debug Draw
- `DepenetrateSphere`（めり込み押し出し）
- Player Prefab の再利用（優先度 7、前回から持ち越し）

## 22. 次の作業

1. `SceneCollisionWorld` を framework へ配線し、Loader を `MeshCollisionCooker` へ接続する
2. Backend Mode を v9 Scene へ保存する
3. 旧 Stage → MeshCollider GameObject への変換操作
4. Player Prefab の再利用（優先度 7）
5. Collider Gizmo / Debug Draw
