# RePlayEngine 引継ぎ資料（次の担当者へ）

最終更新: 2026-08-01
書いた人: 前任の AI アシスタント
読む人: 次にこのプロジェクトを引き継ぐ AI / 開発者

---

## 0. まず読むところ

1. この文書の **第 1 章（今どうなっているか）** と **第 2 章（最優先の作業）**
2. `Docs/Collision_Runtime接続_実装報告.md` — 直近の作業の詳細
3. 「守ってほしい約束」（第 8 章）— ここを破ると手戻りが大きいです

**最重要**: このプロジェクトは **Visual Studio でのビルドが長期間行われていません。**
最後に「Windows 実機で動いた」と確認できているのは、GameObject 基盤の導入前です。
新しい大きな機能を足す前に、まず **Debug x64 でリビルドして動かしてください。**

---

## 1. 今どうなっているか

### 1-1. プロジェクトの素性

| 項目 | 値 |
|---|---|
| 場所 | `C:\Users\2250298\Desktop\teamProject_2_3` |
| ソリューション | `3dgp.sln` / `3dgp.vcxproj` |
| 言語規格 | **C++17**（C++20 の機能は使えません。第 8 章参照） |
| 描画 | Direct3D 11 |
| UI | ImGui 1.80 WIP（`BeginDisabled` などは**無い**。`PushItemFlag` で代用） |
| 構成 | Debug x64 で作業してください |

### 1-2. 大きな構造

```
Source/            旧来のゲーム側。framework が全体を束ねる
  app/             framework 本体（部分ごとに .cpp を分割）
  game/            旧 Player / Stage / Camera / SceneGame
  mesh/            skinned_mesh / static_mesh / gltf_model

RePlayEngine/      新しいエンジン層。Source からの一方向依存
  Core/            ObjectID / Transform / Threading
  Object/          Component / GameObject / Registry
  Components/      具体的な Component 群（Physics / Gameplay / Rendering / Camera / Core）
  Scene/           Runtime(Scene) / Serialization / Services
  Physics/         SphereCast / ShapeSweep / CookedMeshCollision / CollisionLayers
  Reflection/      PropertyValue / PropertyDesc / PropertyRegistry
  Editor/          Hierarchy / Inspector / Debug / Gizmo / Commands
  Assets/          AssetDatabase
  Rendering/       RenderGraph / Passes / Adapter
```

**依存の向きは `Source` → `RePlayEngine` の一方向だけ**です。
RePlayEngine 側から Source を参照している箇所はありません。壊さないでください。

### 1-3. 3 つの「Scene」の違い（混同しやすい）

| 名前 | 責任 |
|---|---|
| `ReplayEngine::Scene::SceneManager` / `IScene` | 起動ロゴ → ロード画面 → ゲームという**画面遷移** |
| `ReplayEngine::Scene::SceneDocument` | **旧エディタ**のステージ配置記録（`.replaystage`）。段階移行のため残置 |
| `ReplayEngine::Scene::Scene` | **GameObject の入れ物**（`.replayscene`）。**今の主役** |

### 1-4. 完了している大きな仕事

| 段階 | 内容 | 実機確認 |
|---|---|---|
| 1 | GameObject / Component 基盤（ObjectID・Registry・PropertyRegistry・Scene） | 一部のみ |
| 2 | Scene 保存形式 v7 → v8 → **v9** | **未確認** |
| 3 | Editor パネルの分離（Hierarchy / Inspector / PropertyDrawer） | 一部のみ |
| 4 | 旧 Player の Component 分解（巨大 PlayerComponent は作っていない） | 一部のみ |
| 5 | MeshCollider の中核（ローカル空間 Cook・AssetGUID 共有） | **未確認** |
| 6 | **Collision の Runtime 接続・Collider 4 形状・Layer/Trigger/Debug Draw** | **未確認** |

### 1-5. 未着手

| # | 項目 | 備考 |
|---|---|---|
| 1 | **Player Prefab の再利用** | ユーザーが元々出した優先度 15。**次の大きな仕事** |
| 2 | 旧 Stage → MeshCollider GameObject への変換操作 | 移行の総仕上げ |
| 3 | `CookedMeshCollisionCache::Clear()` / `Invalidate()` の呼び出し配線 | 関数はある。呼ぶ場所が未定 |
| 4 | Layer 名のプロジェクト設定化 / Layer Matrix | 現在は固定表 + ビット AND |
| 5 | `DepenetrateSphere`（めり込み押し出し） | — |

---

## 2. 最優先の作業

### 2-1. まず Visual Studio でリビルド

「ビルド」ではなく **「リビルド」** を実行してください。

> **理由**: ヘッダーの構造（クラスのサイズ・基底クラス）が大きく変わっています。
> 古い `.obj` が残ると ODR 違反になり、原因の分かりにくいメモリ破壊として現れます。
> 前回のセッションでは、これが原因の heap-buffer-overflow を実際に踏みました。

コンパイルエラーが出た場合、**まず疑うべきは次の 3 つ**です。

1. `SphereColliderComponent` の基底が `Core::Component` から
   `Components::ColliderComponent` へ変わりました。`center_offset` は基底へ移動しています。
2. `IPhysicsQueryService::SweepSphereFiltered` / `QueryGroundFiltered` の引数が
   `int layer, int mask` から `const CollisionQueryFilter&` へ変わりました。
3. `CookedMeshCollisionData::CookSettings()` は `Settings()` へ改名しました。

### 2-2. 実機で確認すること

`Docs/Collision_Runtime接続_実装報告.md` の第 14 章に 13 項目の確認手順があります。
そちらを見てください。

うまくいかない場合の切り分けは **「衝突の診断」ウィンドウ**（Editor → Player Runtime
Diagnostics → 「衝突の診断ウィンドウを開く」）が最短です。
登録数・有効数・再走査回数・Cook 実行回数・直近の Hit 元が出ます。

### 2-3. それが済んでから Player Prefab

ユーザーの元の要求（原文）:

> 新規シーン作成しても再度プレイヤーのコンポーネントを付け直す必要はなく、
> 保存されてる自由に名前変えれるプレハブを読み込んだら同じ奴が使えるようにお願い

要件として整理すると:

- Player の構成を **Prefab Asset** として保存する
- 参照は **AssetGUID**。GameObject 名でも Prefab のファイル名でもない
- プロジェクト設定に `Default Controlled Character Prefab` を持つ
- 「Empty Scene」と「Default Scene」を作り分ける
- **毎回の起動や Scene 読み込みでコードから Component を付け直さない**
- Prefab の名前を変えても壊れない

必要な部品は既にあります（`PrefabSerializer` / `SceneData::InstantiateSceneData` /
`AssetDatabase`）。**ゼロから作らないでください。**

---

## 3. 設計上、意図的にそうしている点（変えないでほしい）

理由なしに見ると「もっと簡単に書けるのに」と見える箇所があります。
それぞれ理由があるので、変える前に読んでください。

### 3-1. 削除は「フラグ + 走査」。コマンドキューにしない

`Component::Destroy()` / `GameObject::Destroy()` はフラグを立てるだけで、
実際の破棄は Scene の同期点（`ProcessPendingOperations`）で行います。

> キューへポインタを積む方式にすると、実行前に破棄済みのオブジェクトを
> 指す危険があります。フラグ + 走査なら宙に浮いたポインタが構造的に発生しません。

### 3-2. Transform はオイラー角（ラジアン）。クォータニオンにしない

既存の Player / Stage / TransformGizmo / 旧 TransformData がすべてオイラー角前提です。
切り替えるとギズモまで作り直しになります。ワールド回転はクォータニオンで取れます。

### 3-3. Transform のワールド行列をキャッシュしない

毎回親をたどって合成します。ダーティ伝播のバグ（親を付け替えた・親の親が動いた・
削除予約中に読んだ）を構造的に発生させないことを優先しました。階層の深さは実用上わずかです。

### 3-4. Scene はコピー・ムーブ禁止。複製は SceneData 経由

GameObject が Scene への生ポインタを持っているため、Scene の実体が動くと参照が壊れます。
Play Mode の複製は `CaptureScene` → `ApplySceneData` で行っています。

### 3-5. Collider の登録は「Scene の構成世代」で駆動。Component からの通知にしない

`Scene::StructureGeneration()` が変わったフレームだけ登録表を作り直します。

> Component の `OnEnable` は Scene の同期点で走りますが、そのとき framework が
> サービスを張り替え済みとは限りません。通知先が居ない瞬間に登録が落ちると、
> 「消したはずの Collider が残る」より厄介な「有るはずの Collider が無い」不具合になります。
> 世代番号なら通知先の有無に依存しません。

### 3-6. Collider の登録表は生ポインタを持たない

ObjectID / ColliderID だけを持ち、必要になったときに Scene から引き直します。
削除済み Component を指したまま問い合わせることが構造的に起こりません。

### 3-7. `ColliderComponent` のライフサイクルは `final`

`OnAttach` / `OnDetach` / `OnEnable` / `OnDisable` は基底で `final` です。
派生は `OnColliderAttach()` などを使います。

> 派生が override して基底呼び出しを忘れると、登録表と実体がずれて
> 「消したはずの Collider に当たり続ける」不具合になります。
> そもそも override できない形にしました。

### 3-8. Cook データはローカル座標。ワールドへ変換して持たない

`CookedMeshCollisionData` はローカル三角形と加速構造だけを持ちます。
問い合わせは「クエリを Collider のローカル空間へ移す → 判定 → 結果をワールドへ戻す」
という流れです。

> ワールド変換済みで持つと、同じ Asset を別の場所へ 2 つ置いたときに共有できません。
> 参考プロジェクトは「Transform 変更時にワールド三角形を作り直す」方式でしたが、
> 単体 Stage でしか成り立たないので採用していません。

### 3-9. Cook キャッシュは weak_ptr。shared_ptr を永久保持しない

実体の所有者は `MeshColliderComponent` だけです。
キャッシュが shared_ptr を持つと、Scene を切り替えても解放されずメモリが増え続けます。

### 3-10. 二重衝突は「移行済み集合」で防ぐ。順序依存にしない

`SceneCollisionWorld::ShouldConsultLegacy()` が、移行済みの移行元へは
Hybrid でも一切問い合わせません。

> 「片方が外れたらもう片方」という順序依存の方式は採っていません。
> MeshCollider の隙間から旧地形が反応してしまうためです。

### 3-11. Backend Mode は Scene 単位。Collider ごとに持たない

Collider ごとの Property にすると、同じ Scene に「Legacy を見る Collider」と
「見ない Collider」が混在し、二重衝突の管理が破綻します。

### 3-12. Character Motor の Primary Collider は明示選択。自動選択しない

「同じ GameObject にある最初の Collider」のような暗黙選択をしません。
Collider が複数ある構成（本体 + 攻撃判定 + 検知範囲）で、
どれが移動用なのかがコードを読まないと分からない状態を避けるためです。

例外は旧 Player 変換時だけ（既存の Sphere Collider を自動設定）。

### 3-13. 問い合わせには必ず `ignore_object` を渡す

Character Motor は自分の Collider を持ったまま自分の周りへ球を飛ばします。
除外しないと**自分に当たって毎フレーム宙へ持ち上がります**（実際に起きました）。

---

## 4. 「単一の登録点」という決まり

新しい Component を足すときは、
**`RePlayEngine/Object/Registry/BuiltInComponents.cpp` の `RegisterBuiltInComponents()`
に 1 行足すだけ**にしてください。

それだけで次のすべてへ反映されます。

- Add Component 一覧
- Inspector の入力欄（型・範囲・刻み・表示名・ツールチップ）
- Scene ファイルへの保存と復元
- GameObject / Component の複製
- Undo / Redo
- Prefab

**Editor 用と保存用を別々に定義しないでください。** これがこの基盤の中心的な約束です。

Collider を足す場合は `RegisterColliderCommon<T>()` を呼んで共通プロパティを入れてください
（`collider_key` / `center_offset` / `collision_layer` / `collision_mask` /
`is_trigger` / `debug_draw`）。

> 注意: 基底クラスのメンバを `MakeProperty` へ渡すときは
> `MakeProperty<T, int>("collider_key", &T::collider_key)` のように
> テンプレート引数を**明示**してください。省略すると型推論が基底クラスになり、
> 基底が `StaticTypeID` を持たないためコンパイルできません。

---

## 5. 検証のやりかた（g++ + サニタイザ）

Visual Studio が使えない環境でも、エンジン側のロジックは検証できます。

### 5-1. 何を使うか

- g++ 11、`-std=c++17`
- 作業領域に置いた **DirectXMath 互換ヘッダー**（行主・行ベクトル規約の実装）
- AddressSanitizer + UndefinedBehaviorSanitizer + LeakSanitizer

### 5-2. 守ること（ユーザーからの明示的な指示）

- **スタブをリポジトリへ追加しない**
- `.vcxproj` や実際のソースからスタブを参照させない
- **スタブに合わせて本番コードの API を歪めない**
- DirectXMath / D3D11 の型・関数を過度に簡略化し、
  **実際には存在しない呼び方まで通してしまわない**
- g++ で通ったことを「Visual Studio でコンパイル確認済み」と表現しない
- **g++ 検証済みと MSVC 未確認を明確に分けて報告する**

### 5-3. 直近の結果

エンジン側 23 翻訳単位が `-Wall -Wextra -Wpedantic -Wshadow` で警告ゼロ。
受け入れテスト **118 検査 / 失敗 0**、サニタイザの報告なし。

前回のスタブは行列変換を恒等としてしか扱えませんでしたが、
今回は行主・行ベクトル規約どおりに実装したので、
**非恒等 Transform・回転・非一様拡縮・負の拡縮の数値まで検証できています。**

### 5-4. 検証できないもの

ImGui / D3D11 / windows.h に依存するファイル（`framework_*.cpp`、`PropertyDrawer.cpp` など）は
g++ で動かせません。**これらはすべて「Windows 実機で未確認」です。**

---

## 6. ファイルの分け方（ユーザーからの要望）

> できるだけコード分けれそうなら分ける、フィルターにも分けるなどすれば見やすくなる

- **1 クラス 1 ファイル**を基本にしてください
- 1 つの `.cpp` が 400 行を超えたら、責任で分けられないか考えてください
  （例: `SceneCollisionWorld` は「登録表」「スイープ」「Trigger」の 3 ファイルに分けました）
- 新しいファイルは **`.vcxproj` と `.vcxproj.filters` の両方**へ登録してください
- filters のフォルダー構成はディレクトリ構成と一致させてください

### 6-1. obj 名の衝突に注意

同じベース名の `.cpp` が別フォルダーにあると、既定では同じ `.obj` を出力して
**リンクエラー（LNK2001）**になります。過去に実際に踏みました。

対策として全構成に次を入れてあります。**消さないでください。**

```xml
<ObjectFileName>$(IntDir)%(RelativeDir)</ObjectFileName>
```

### 6-2. 確認スクリプト

`.vcxproj` の健全性は次で確認できます（**Linux 上で `os.path.basename` を使わないこと**。
`\` を区切りとみなさないため、過去にこれで重複を見逃しました）。

```python
def win_dirname(p): return p.rsplit('\\', 1)[0]
def win_basename(p): return p.rsplit('\\', 1)[-1]
```

チェック項目: XML 妥当性 / 重複エントリ / obj 名衝突 / 実体の有無 /
未登録ファイル / filters 未記載 / 未宣言フィルター

---

## 7. よく踏む落とし穴

| 症状 | 原因 | 対処 |
|---|---|---|
| 原因不明のメモリ破壊 | 古い `.obj` が残っている | **リビルド**（ビルドでは不十分） |
| LNK2001 | 同名 `.cpp` の obj 衝突 | `ObjectFileName` の設定を確認 |
| MSVC だけコンパイルが通らない | C++20 の指定イニシャライザを使った | 連結設定（`.Display().Range()`）で書く |
| 「Collider を置いたのに当たらない」 | Cook 失敗 | Debug Draw で**赤い境界ボックス**が出ます |
| 「Player が動かない」 | Edit Mode で停止中 | `object_runtime_active()` を確認。F3 / F5 |
| キャラが宙へ浮く | 自分の Collider に当たっている | `CollisionQueryFilter::ignore_object` を設定 |
| Trigger の中で反応しない | 表面判定になっている | Mesh Trigger の制限。Box / Sphere / Capsule を使う |

---

## 8. 守ってほしい約束（ユーザーからの明示的な指示）

これらは会話の中でユーザーが繰り返し出した条件です。

### 8-1. コード規約

- **C++20 の機能を使わない。** 特に**指定イニシャライザ（designated initializers）**。
  g++ は拡張として通しますが **MSVC の `/std:c++17` は拒否します。**
  代わりに連結設定（fluent builder）で書いてください。
- 1 つの `.cpp` へ大量のコードを付けない
- あとから拡張しやすい形にする

### 8-2. 消してはいけないもの

- 旧 `Player` / `Stage` / `Camera` / `Light` を削除しない（段階移行中）
- `SceneDocument`（旧エディタの配置記録）を削除しない

### 8-3. 作ってはいけないもの

- **「プレイヤー」の全機能をまとめた巨大な `PlayerComponent`**
- 新しい JobSystem / ThreadPool（既存のものを使うか、メインスレッドで統一）

### 8-4. ライセンス

- 参考プロジェクト（`5_プロジェクト`）の**コードをコピーしない**
- Flax Engine の**コードをコピーしない**
- **ライセンスが不明なコードはコピーしない**
- 設計の考え方を参考にするのは可。その場合は報告書へ「採用 / 調整して採用 / 不採用」を明記する

参考プロジェクトについて調査済みの事実:
- 自作コードに LICENSE / NOTICE ファイルが**無い** → **1 行もコピーしていません**
- Jolt Physics が `.vcxproj` へ 578 箇所登録されているが、
  `Source/` から `#include <Jolt` も `JPH::` も **1 件も見つからない** → 使われていない

### 8-5. git

- 破壊的操作（force push / reset --hard など）を行わない
- 定期的にコミットする

### 8-6. 報告のしかた

- **実装済み / g++ で実測 / Windows 実機で未確認 / 未実装** を明確に分ける
- 存在しない機能を「存在するように装って報告しない」
- ファイル存在だけを根拠に「調査した」と言わない（実際のクラス定義・呼び出し元・
  データの流れ・所有関係まで読むこと）
- 配線していない状態で「完成」と報告しない

---

## 9. 用語の対応表

| 用語 | 実体 | 場所 |
|---|---|---|
| ObjectID | GameObject の永続 ID（64 bit） | `Core/ObjectID` |
| ColliderID | Collider の実行時 ID。保存しない | `Components/Physics/ColliderComponent` |
| collider_key | Collider の保存用 ID。GameObject 内で一意 | 同上 |
| StructureGeneration | Scene の構成が変わるたびに増える番号 | `Scene/Runtime/Scene` |
| CookKey | Cook キャッシュの主キー（GUID + revision + 設定） | `Physics/CookedMeshCollision` |
| SourceID | 旧 Stage / 配置物を指す移行元 ID | `Scene/Services/LegacyStageMigrationState` |
| Backend Mode | Legacy Only / Hybrid / Scene Colliders Only | `Scene/Services/SceneCollisionWorld` |
| Primary Collider | Character Motor が移動に使う Collider | `Components/Gameplay/CharacterMotorComponent` |

---

## 10. ユーザーとのやりとりについて

- 日本語でやりとりしています
- 説明は簡潔に。ただし**設計判断の理由は必ず書いてください**
- 段階ごとに確認を挟むより、**まとめて進めて最後に詳しく報告する**ことを好みます
  （ただし「実機で確認してほしいこと」は明確に列挙すること）
- 分からないことは推測で埋めず、**調査してから答えてください**
- できていないことを「できた」と言わないでください。これがいちばん嫌がられます

---

## 11. 直近セッションで直したバグ（再発したら疑うところ）

| # | 症状 | 原因 | 直した場所 |
|---|---|---|---|
| 1 | キャラが毎フレーム約 0.76 上昇し、4 秒で y ≈ +185 | 自分の Collider に接地判定 | `CollisionQueryFilter::ignore_object` |
| 2 | Trigger の内側に入ると反応しない | 表面三角形との交差でしか判定していない | `SceneCollisionWorldTrigger.cpp::Overlaps` |
| 3 | Capsule / Box で床にめり込む | 接地高さの式に半径が入っていない | `CharacterMotorComponent::ResolveGround` |
| 4 | コンパイルエラー（名前の意味が変わる） | 型名と同名のメンバ関数 | `CookedMeshCollisionData::Settings()` へ改名 |
| 5 | コンパイルエラー（StaticTypeID が無い） | 基底メンバの型推論が基底になる | `MakeProperty<T, int>(...)` と明示 |

すべて g++ の受け入れテストが検出したものです。
**同じ種類のバグを防ぐには、テストを増やすのがいちばん確実です。**
