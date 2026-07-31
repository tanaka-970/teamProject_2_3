# RePlayEngine GameObject / Component 基盤 実装報告

作成日: 2026-07-31
対象: RePlayEngine (3dgp.sln / C++17 / Direct3D 11 / Dear ImGui 1.80 WIP)
規模: 新規 55 ファイル 6,650 行 / 既存 7 ファイル改修 / 既存 2 ファイル削除

---

## 1. 現状分析（要約）

詳細は `Docs/RePlayEngine_現状調査レポート.md` に記載。ここでは実装判断に直結した点だけ。

| # | 発見 | 実装への影響 |
|---|---|---|
| 1 | **`SceneDocument` は描画されていない**（`framework_render.cpp` からの参照ゼロ）。実際の描画は `skinned_meshes[0]` と `Stage` を直接読む | 作り替えても既存描画が壊れない。新基盤を並置できた |
| 2 | **`Core/Components/GameObject.h` は完全な未使用デッドコード** | 新クラスを作らず、この設計を昇格させた（重複クラスを作らない要件に合致） |
| 3 | Component 種別が `std::optional` メンバで 4 ファイルにハードコード | ComponentRegistry + PropertyRegistry で置換した |
| 4 | 親子関係が存在しない。Transform が 4 箇所に分散 | GameObject が Transform を単独所有する設計にした |
| 5 | `FixedUpdate` / `LateUpdate` が存在しない | Scene に用意したが、メインループからは Update / LateUpdate のみ接続 |
| 6 | Singleton ゼロ / `shared_ptr` 未使用 / 生 new ほぼ皆無 | 既存方針をそのまま踏襲した |
| 7 | `Stage` がエディタプレビューとゲーム地形を兼務 | 二重 Update の温床。新基盤は完全に分離した |
| 8 | 保存は独自テキスト `REPLAY_SCENE 6`。JSON ライブラリ未導入 | v7 として独自テキストを拡張した |
| 9 | Editor 全パネルが `framework` のメンバ関数（private メンバ関数 40 個超） | Panel を独立クラスへ切り出した |

### マルチスレッド基盤の調査結果

指定された全項目を検索した結果、**ThreadPool / JobSystem / TaskSystem / TaskGraph / 並列 Update / RenderThread は存在しない**。

| 実在するもの | 内容 |
|---|---|
| `AsyncAssetManager` | ワーカー **1 本** + mutex + condition_variable + deque。`PumpMainThread()` で完了コールバックを**メインスレッドで実行**。ジョブの中身はファイル読み込み固定で、汎用ジョブは投げられない |
| `LoadingScene` | `std::async` で 1 タスクずつ**直列**実行。並列度 1 |

D3D11 は immediate context のみで deferred context 未使用。描画は完全にメインスレッド専有。

**判断: 新しい JobSystem / ThreadPool は作らなかった。** 理由:

1. 並列化する対象がまだ存在しない（Update 対象はプレイヤー 1 体分）
2. D3D11 immediate context 前提の描画で並列化の利得が小さい
3. `AsyncAssetManager` の worker/queue/pump パターンが既にあり、2 個目は重複実装になる
4. 既存を壊さない制約に対してリスクが最大

**代わりに採った方針:**

- `AsyncAssetManager` の Pump パターンを**設計モデルとして流用**。スレッドは増やさず、Scene 内に「予約 → 同期点で一括反映」を実装した
- `Core::ThreadPolicy`（`MainThreadOnly` / `ParallelSafe`）の**区分だけ用意**。現時点では区分によらず全 Component をメインスレッドで順番に実行する
- Scene 保存は「メインスレッドで `SceneData` スナップショット → 書き出し」に分離。将来書き出しだけをワーカーへ回せる形にしてある（今回は同期実行）
- ジョブへ生ポインタを渡さない設計。**遅延操作をコマンドキューにしなかった**のは、キューに積んだポインタが実行前に破棄済みオブジェクトを指す危険を構造的に排除するため

---

## 2. 採用した設計

### 中核となる 6 つの判断

**判断 1: Transform は GameObject が単独所有する**

`TransformComponent` は座標データを一切持たず、`Owner()->GetTransform()` へ委譲するビューに徹する。ご指示の「二重所有は絶対に避ける」を構造的に満たしている。Flax の Actor と同じ考え方。

`ComponentRegistry` 上では `built_in`（GameObject 生成時に自動付与）/ `removable=false` / `serializable=false` として登録している。`serializable=false` の理由は、Transform が GameObject 側の情報として保存されるため、Component としても保存すると同じ値がファイル内に 2 度出てしまうこと。

**判断 2: 回転はオイラー角（ラジアン）を正とする**

既存の `Player` / `Stage` / `TransformGizmo` / 旧 `TransformData` がすべてオイラー角前提で書かれており、クォータニオン化するとギズモまで作り直しになる。ワールド回転はクォータニオンで取り出せるようにしてあり、将来ローカル側を切り替える余地は残した。**これは仕様例（JSON にクォータニオン）とは異なる判断です。**

**判断 3: 遅延操作はコマンドキューではなく「予約フラグ + 走査」で行う**

`Component::Destroy()` / `GameObject::Destroy()` は予約フラグを立てるだけ。Scene が同期点で全体を走査して回収する。

コマンドキュー方式を採らなかったのは、キューに積んだポインタが実行前に破棄済みのオブジェクトを指す危険があるため。走査方式なら宙に浮いたポインタが構造的に発生しない。ご指示の「Component 破棄後にジョブが生ポインタを参照しない」を最も確実に満たす形。

**判断 4: ワールド行列はキャッシュせず、親をたどって毎回合成する**

ダーティ伝播のバグ（親を付け替えた / 親の親が動いた / 削除予約中に読んだ）を構造的に発生させないことを優先した。階層の深さは実用上わずかなので費用は小さい。

**判断 5: Scene → SceneData → v7 テキストの 3 段構成**

`SceneSerializer` は `Scene` に一切依存せず `SceneData` だけを受け取る。これにより、

- Scene と保存処理が密結合しない
- 将来 JSON 出力は `WriteJson` / `ReadJson` を並べるだけで足りる
- Play Mode の複製が `SceneData` の往復で実現できる（Scene はコピー不可）
- Undo 履歴も `SceneData` スナップショットで実現でき、生ポインタを持ち込まない

**判断 6: Property 定義は 1 か所のみ**

`PropertyRegistry` へ 1 回登録すると、Inspector の入力欄・Scene の保存と復元・Component の複製がすべて自動で揃う。表示用と保存用を別々に書く必要がない。

### 単一登録点

新しい Component を追加するときに触るのは `BuiltInComponents.cpp` の 1 関数だけ。それだけで以下すべてに反映される。

```
ComponentRegistry::Register<T>(...)  +  PropertyRegistry::Register<T>(...)
   ↓
Editor の Add Component 一覧（カテゴリ分け・検索・重複防止）
Inspector の表示と編集（型・範囲・刻み・表示名・ツールチップ）
Scene ファイルへの保存と復元
GameObject / Component の複製
Undo / Redo
```

Editor 側にも Serializer 側にも型ごとの `if` / `switch` は 1 つも存在しない。

---

## 3. Flax Engine から参考にした考え方

コードは一切コピーしていない。設計思想のみを研究し、RePlayEngine の既存構造に合わせて作り直した。

| Flax の考え方 | RePlayEngine での実装 |
|---|---|
| `SceneObject` が Guid と親を持つ共通基底 | `GameObject` が `ObjectID` と親ポインタを持つ |
| `Actor` が Transform を自分で所有し、Script は Transform を持たない | GameObject が Transform を所有、`TransformComponent` はビューに徹する |
| `SceneObjectsFactory` が型 ID からオブジェクトを復元 | `ComponentRegistry` が型名 / 型 ID から Component を生成 |
| ロードが「生成 → ID テーブル → 親子 → プロパティ」の多段 | `ApplySceneData` を 8 フェーズに分割 |
| `_isActive` と `_isActiveInHierarchy` の分離 | `Enabled()`（自身）と `ActiveInHierarchy()`（親を辿った結果）を分離 |
| 削除は即 delete せず `DeleteObject` でマーク | 予約フラグ + `ProcessPendingOperations()` |
| Editor が Runtime に依存し、Runtime は Editor に依存しない | 同じ。`RePlayEngine/Editor/` → `RePlayEngine/Object/` の一方向 |
| Prefab は Scene と同じ形式の部分木 | `SceneData` の部分木として表現できる形にした（拡張点のみ用意） |

## 4. Flax をそのまま採用しなかった部分

| Flax | 不採用の理由 |
|---|---|
| C# スクリプティング / .NET ホスティング | スコープ外。RePlayEngine は C++ 単体 |
| 完全リフレクション（ScriptingType / コード生成 / Build ツール） | 規模が過大。最小の `PropertyRegistry` で代替 |
| `Actor` が仮想関数で描画・物理へ直接参加する設計 | Component 側へ寄せる（Unity 寄り）方が既存 Player/Stage からの移行が素直 |
| `JobSystem` / `TaskGraph` / `ThreadPool` | 既存に相当物が無く、今回作る必要も無い（第 1 節参照） |
| `Guid`（128bit）を全オブジェクトに | 64bit の `EntityId` が既にあり保存形式にも入っていた。互換を意識して 64bit を継承 |
| Actor の巨大な仮想 API 群 | 最小限のライフサイクルに絞った |
| `LargeWorlds`（double 座標） | 不要 |
| 遅延操作をコマンドキューで持つ方式 | 生ポインタの寿命問題を避けるため走査方式にした |

---

## 5. クラス構成図

```
RePlayEngine/
│
├─ Core/                          … 基礎型
│   ├─ ObjectID       ObjectID / ObjectIDGenerator
│   ├─ Threading      ThreadPolicy
│   └─ Math           Transform
│
├─ Object/                        … オブジェクト基盤
│   ├─ Component      Component (基底) / ComponentTypeID / REPLAY_COMPONENT_BODY
│   ├─ GameObject     GameObject
│   └─ Registry       ComponentTypeInfo / ComponentRegistry / RegisterBuiltInComponents
│
├─ Reflection/                    … 最小リフレクション
│   ├─ Property       PropertyValue / PropertyDesc / PropertyBag / MakeProperty
│   └─ Registry       PropertyRegistry
│
├─ Components/                    … 標準 Component
│   ├─ Core           TransformComponent      (組込・削除不可・保存対象外)
│   ├─ Rendering      MeshRendererComponent
│   └─ Gameplay       RotatorComponent / HealthComponent
│
├─ Scene/
│   ├─ Runtime        Scene                   ← GameObject の入れ物 (今回の新設)
│   ├─ Serialization  SceneData / SceneSerializer / DuplicateGameObject
│   └─ (既存)         IScene / SceneManager   ← 画面遷移。責任が違うので併存
│
├─ Rendering/
│   └─ Adapter        RenderItem / RenderItemList / SceneRenderCollector
│
└─ Editor/                        … Runtime に依存、Runtime からは参照されない
    ├─ Core           EditorContext
    ├─ Selection      EditorSelection         (ObjectID 管理・生ポインタ非保持)
    ├─ Commands       SceneEditHistory        (SceneData スナップショット Undo)
    ├─ Hierarchy      HierarchyPanel
    ├─ Inspector      InspectorPanel / PropertyDrawer
    └─ ComponentBrowser AddComponentPanel
```

---

## 6. 所有関係図

```
framework                                        [ルート所有者]
 ├─ Scene            object_scene          (値)  … 編集用
 ├─ Scene            object_scene_runtime  (値)  … Play 用
 │    └─ std::vector<std::unique_ptr<GameObject>>        ★ GameObject の唯一の所有者
 │           ├─ Transform                       (値)     ★ Transform の唯一の所在
 │           ├─ std::vector<unique_ptr<Component>>       ★ Component の唯一の所有者
 │           ├─ GameObject* parent_             (非所有)
 │           ├─ std::vector<GameObject*> children_ (非所有・所有権は Scene に残る)
 │           └─ Scene* scene_                   (非所有)
 │
 ├─ EditorContext                          (値)
 │    ├─ Scene*             (非所有)
 │    ├─ AssetDatabase*     (非所有)
 │    ├─ EditorSelection    … ObjectID のみ保持。生ポインタを持たない
 │    └─ SceneEditHistory   … SceneData スナップショット。生ポインタを持たない
 │
 ├─ HierarchyPanel / InspectorPanel        (値)  … framework への参照を持たない
 ├─ RenderItemList                         (値)  … GPU リソースを持たない
 └─ unordered_map<string, unique_ptr<skinned_mesh>>  … Asset GUID → メッシュ

Component  →  GameObject   非所有参照
GameObject →  Scene        非所有参照
GameObject →  子           非所有参照（所有権は Scene）
循環参照なし。shared_ptr 不使用。生 new/delete は Scene 内の 1 箇所のみ（private ctor のため）
```

---

## 7. Update 順序

```
framework::run()
 └─ update(dt)                              framework_update.cpp
     ├─ async_asset_manager.PumpMainThread()   ← 既存。ワーカー結果をメインスレッドで反映
     ├─ scene_manager.Update(dt)               ← 既存。画面遷移（Boot/Loading/Game）
     └─ update_object_scene(dt)                ← 新規
          ├─ Edit Mode 中は Component を更新しない（予約反映のみ）
          ├─ Scene::Update(dt)
          │    ├─ 読み込み中なら何もしない（BeginLoad/EndLoad）
          │    ├─ SynchronizeStates()
          │    │     GameObject ごとに Component の状態を同期
          │    │       状態が変わった → OnEnable / OnDisable
          │    │       初めて有効になった → OnStart（一度だけ）
          │    ├─ 有効な GameObject を添字で走査
          │    │    └─ 有効な Component を添字で走査 → OnUpdate(dt)
          │    └─ ProcessPendingOperations()
          │         ├─ 各 GameObject::CompactComponents()
          │         │     予約された Component → OnDisable → OnDetach → 破棄
          │         └─ 予約された GameObject を階層の深い順に
          │               親子リンク解除 → OnDetach → ID 表から除去 → 破棄
          ├─ Scene::LateUpdate(dt)  … OnLateUpdate → 同じ後処理
          ├─ Selection::PruneMissing()   消えた GameObject を選択から外す
          └─ SceneRenderCollector::Collect()  描画提出リストを作り直す（D3D に触れない）
 └─ render(dt)                              framework_render.cpp
     ├─ (既存の CSM / GBuffer / Deferred / Forward / Outline / PostProcess)
     ├─ GBuffer パス末尾   → draw_object_scene_meshes(gbuffer_ps, true)
     └─ Forward パス       → RenderItem ごとに shading_model を選んで描画
```

### 添字走査による安全性

すべてのループが「開始時点の個数を控えた添字走査」になっている。

- **走査中に追加**された Component / GameObject は、その回では処理されず次フレームから開始する（`unique_ptr` 格納なので実体アドレスは動かない）
- **走査中に削除**された場合は予約フラグが立つだけで、コンテナは詰められない。添字がずれない
- `ProcessPendingOperations()` は `updating_` 中なら何もしない

### 「Component 追加直後の Update 開始タイミング」の定義

追加時に `OnAttach` が即時に呼ばれる。`OnEnable` / `OnStart` は**次の同期点**、`OnUpdate` は**次のフレーム**から。Unity と同じ挙動。

---

## 8. Editor 操作フロー

```
階層パネル (HierarchyPanel)
   GameObject をクリック
        ↓
   EditorSelection.Select(ObjectID)     ← 生ポインタではなく ID を保存
        ↓
   framework が selected_editor_object を game_object へ切り替え
        ↓
インスペクター (InspectorPanel)
   ResolvePrimary(scene)                ← 毎フレーム Scene から引き直す
        ↓
   GameObject が持つ Component を順に走査
        ↓
   ComponentRegistry → 表示名・ツールチップ・削除可否
   PropertyRegistry  → プロパティ定義
        ↓
   PropertyDrawer が型に応じた入力欄を生成
        ↓
   「コンポーネントを追加」ボタン
        ↓
AddComponentPanel
   ComponentRegistry::Categories() でカテゴリ分け
   検索欄（型名・表示名の部分一致）
   重複禁止で既に付いている型は「(追加済み)」表示にして選択不可
        ↓
   EditorContext.BeginEdit("○○ を追加")
   GameObject::AddComponent(type_id)
   EditorContext.CommitEdit()
        ↓
   Scene が Dirty になる（タイトルへ * が付く）
   SceneEditHistory へ 1 件積まれる（Undo 可能）
```

### 安全性のための仕組み

| 危険 | 対策 |
|---|---|
| Inspector 描画中に Component が消える | `RemoveComponent` は予約のみ。破棄は `CommitEdit()` 内 |
| 削除確定でコンテナが詰められ添字がずれる | 削除要求を `pending_removal_` に控え、**Component 一覧の走査を終えてから**確定 |
| 階層の再帰描画中に親子が変わる | 付け替え要求を控え、**ツリー走査を終えてから**適用 |
| 循環階層を作られる | `GameObject::SetParent` が自分自身・子孫を親にする要求を拒否 |
| 選択中の GameObject が削除される | 選択は ObjectID のみ。`ResolvePrimary` が毎回引き直し、削除予約中は nullptr を返す |
| Play 中に編集される | `EditorContext::CanEdit()` が false になり、全編集操作が無効化される |

---

## 9. Scene 保存・読み込みフロー

### 保存

```
Ctrl+S
 ├─ save_scene_document(...)   ← 既存。旧ステージ配置記録（別ファイル）
 └─ save_object_scene(false)   ← 新規
      ├─ Play 中なら中止（実行中の一時変化を焼き込まない）
      ├─ CaptureScene(scene, data)              [メインスレッド]
      │    削除予約中の GameObject / Component は保存しない
      │    serializable=false の型（Transform）は保存しない
      │    PropertyRegistry::Capture で全プロパティを PropertyBag へ
      ├─ SceneSerializer::SaveToFile(data, path, error)
      │    ① メモリ上へ全文を書き切る
      │    ② 一時ファイル (.tmp) へ書く
      │    ③ rename で差し替え（失敗時は copy_file へフォールバック）
      │    → 途中で失敗しても既存ファイルを壊さない
      └─ ClearDirty()（タイトルの * が消える）
```

### v7 ファイル形式（実際の出力）

```
REPLAY_SCENE 7
SCENE "TrainingStage"
OBJECT_COUNT 3
OBJECT
  ID 1
  NAME "Player"
  ENABLED 1
  PARENT 0
  TRANSFORM 0 1 0 0 0.5 0 2 2 2
  COMPONENT_COUNT 3
  COMPONENT "MeshRendererComponent" 1
    PROPERTY_COUNT 7
    PROPERTY "mesh_asset" asset "60412424bb1443240b2bf2160eee6923"
    PROPERTY "material_asset" asset ""
    PROPERTY "tint" color 0.5 0.25 0.75 1
    PROPERTY "shading_model" enum 2
    PROPERTY "outline" bool 1
    PROPERTY "cast_shadow" bool 1
    PROPERTY "visible" bool 1
  END_COMPONENT
  ...
END_OBJECT
```

- `std::locale::classic()` でロケール非依存
- `std::setprecision(max_digits10)` で float / double が往復しても値が変わらない
- 文字列は `std::quoted` で安全に扱う
- **GPU リソースは一切保存しない。** Asset は GUID のみ

### 読み込み（8 フェーズ）

```
load_object_scene()
 ├─ SceneSerializer::LoadFromFile(data, path, error)
 │    ① マジック + バージョン確認 → v7 以外は明確なエラーで拒否
 │    ② 上限チェック（GameObject 20万 / Component 512 / Property 512）
 │    ③ SceneData へ解析（Scene には一切触れない）
 └─ ApplySceneData(data, scene, report)
      1. scene.BeginLoad()          ← 以降 Update が止まる
      2. scene.Clear()
      3. 保存 ObjectID のまま全 GameObject を生成（Transform / 有効状態も設定）
      4. 保存 ID → 実体の対応表を構築（重複時は採番し直し、警告へ記録）
      5. 親子関係を復元（見つからない親 / 循環は Scene 直下へ寄せて警告）
      6. ComponentRegistry で Component を生成（ここで OnAttach）
      7. PropertyRegistry でプロパティを反映（未知の名前は警告へ記録）
      8. scene.EndLoad()
 └─ scene.Start()                   ← ここで初めて OnEnable / OnStart が走る
```

**Deserialize 中に Component が Update されることはない。**

### 異常系の扱い（すべてクラッシュしない）

| 状況 | 挙動 |
|---|---|
| ファイルが存在しない | エラー文字列を返し、現在の Scene を維持 |
| 形式が壊れている | 同上 |
| **旧バージョン (v1〜v6)** | `この Scene ファイルは旧形式 (v6) です。… v7 形式で Scene を作り直してください。` |
| 新しすぎるバージョン | 同様に明確なメッセージ |
| 未登録の Component 型 | その Component だけ飛ばして警告。Scene 全体は読み込む |
| 未知のプロパティ | 無視して警告。他のプロパティは反映 |
| プロパティの型が変わった | `PropertyValue::ConvertTo` で寄せられる範囲は変換、無理なら初期値を維持 |
| 保存に存在しないプロパティ | Component の初期値のまま |
| ObjectID が重複 | 採番し直して警告 |
| 親が存在しない / 親子が循環 | Scene 直下へ寄せて警告 |
| Asset が見つからない | 描画されないだけ。Scene は正常に読み込まれる |

---

## 10-11. 変更したファイル一覧と変更理由

### 新規作成（55 ファイル）

| ファイルパス | フィルター | 責任 |
|---|---|---|
| `RePlayEngine/Core/ObjectID/ObjectID.h` / `.cpp` | `RePlayEngine\Core\ObjectID` | 永続 ObjectID（64bit）と採番器。`EnsureAbove` で読み込み時の衝突を防ぐ |
| `RePlayEngine/Core/Threading/ThreadPolicy.h` | `RePlayEngine\Core\Threading` | `MainThreadOnly` / `ParallelSafe` の区分定義 |
| `RePlayEngine/Core/Math/Transform.h` / `.cpp` | `RePlayEngine\Core\Math` | Local / World 変換。GameObject が値で単独所有 |
| `RePlayEngine/Object/Component/ComponentTypeID.h` | `RePlayEngine\Object\Component` | FNV-1a 型 ID と `REPLAY_COMPONENT_BODY` マクロ |
| `RePlayEngine/Object/Component/Component.h` / `.cpp` | 同上 | Component 基底。ライフサイクル・有効状態・削除予約 |
| `RePlayEngine/Object/GameObject/GameObject.h` / `.cpp` | `RePlayEngine\Object\GameObject` | 識別・名前・有効状態・親子・Component 所有・削除予約 |
| `RePlayEngine/Object/Registry/ComponentTypeInfo.h` | `RePlayEngine\Object\Registry` | 型メタ情報 + C++17 安全な連結設定 API |
| `RePlayEngine/Object/Registry/ComponentRegistry.h` / `.cpp` | 同上 | 型名 / 型 ID からの生成と問い合わせ |
| `RePlayEngine/Object/Registry/BuiltInComponents.h` / `.cpp` | 同上 | **標準 Component の唯一の登録点** |
| `RePlayEngine/Reflection/Property/PropertyValue.h` / `.cpp` | `RePlayEngine\Reflection\Property` | 13 種のプロパティ値と型変換規則 |
| `RePlayEngine/Reflection/Property/PropertyDesc.h` | 同上 | プロパティ定義とメンバポインタからの生成 |
| `RePlayEngine/Reflection/Property/PropertyBag.h` / `.cpp` | 同上 | 名前付き値の集まり（順序保持） |
| `RePlayEngine/Reflection/Registry/PropertyRegistry.h` / `.cpp` | `RePlayEngine\Reflection\Registry` | 型ごとのプロパティ表・一括取得反映・複製 |
| `RePlayEngine/Components/Core/TransformComponent.h` / `.cpp` | `RePlayEngine\Components\Core` | GameObject の Transform へのビュー（データを持たない） |
| `RePlayEngine/Components/Rendering/MeshRendererComponent.h` | `RePlayEngine\Components\Rendering` | 描画する Asset と見た目。**D3D に触れない** |
| `RePlayEngine/Components/Gameplay/RotatorComponent.h` / `.cpp` | `RePlayEngine\Components\Gameplay` | 一定速度で回転。動作確認用 |
| `RePlayEngine/Components/Gameplay/HealthComponent.h` / `.cpp` | 同上 | 体力。範囲補正付き |
| `RePlayEngine/Scene/Runtime/Scene.h` / `.cpp` | `RePlayEngine\Scene\Runtime` | GameObject の所有・ID 検索・更新順序・遅延操作 |
| `RePlayEngine/Scene/Serialization/SceneData.h` / `.cpp` | `RePlayEngine\Scene\Serialization` | 中間データ・Scene 往復変換・GameObject 複製 |
| `RePlayEngine/Scene/Serialization/SceneSerializer.h` / `.cpp` | 同上 | v7 テキスト入出力。Scene に依存しない |
| `RePlayEngine/Rendering/Adapter/RenderItem.h` | `RePlayEngine\Rendering\Adapter` | 描画提出データ。GPU リソースを持たない |
| `RePlayEngine/Rendering/Adapter/SceneRenderCollector.h` / `.cpp` | 同上 | Scene → 提出リスト。D3D に触れない |
| `RePlayEngine/Editor/Core/EditorContext.h` / `.cpp` | `RePlayEngine\Editor\Core` | パネルが必要とするものの束。編集トランザクション |
| `RePlayEngine/Editor/Selection/EditorSelection.h` / `.cpp` | `RePlayEngine\Editor\Selection` | ObjectID による選択管理 |
| `RePlayEngine/Editor/Commands/SceneEditHistory.h` / `.cpp` | `RePlayEngine\Editor\Commands` | SceneData スナップショットによる Undo / Redo |
| `RePlayEngine/Editor/Hierarchy/HierarchyPanel.h` / `.cpp` | `RePlayEngine\Editor\Hierarchy` | ツリー表示・選択・作成・削除・複製・親子変更・D&D |
| `RePlayEngine/Editor/Inspector/InspectorPanel.h` / `.cpp` | `RePlayEngine\Editor\Inspector` | GameObject と Component の編集 |
| `RePlayEngine/Editor/Inspector/PropertyDrawer.h` / `.cpp` | 同上 | PropertyType から ImGui 入力欄を生成 |
| `RePlayEngine/Editor/ComponentBrowser/AddComponentPanel.h` / `.cpp` | `RePlayEngine\Editor\ComponentBrowser` | Add Component 一覧・カテゴリ・検索・重複防止 |
| `Source/app/Runtime/framework_gameobject_scene.cpp` | `Source\App\Runtime` | **framework と新基盤の唯一の橋渡し** |

### 既存ファイルの変更（7 ファイル）

| ファイル | 変更内容 | 理由 |
|---|---|---|
| `Source/app/framework.h` | 新基盤ヘッダの include / メンバ 9 個 / メソッド宣言 11 個 / `editor_selection::game_object` 追加 / Ctrl+S・Ctrl+Z/Y・F5 のショートカット | 新基盤を framework が所有し、Editor 操作を接続するため |
| `Source/app/Runtime/framework.cpp` | `uninitialize()` で `clear_object_mesh_cache()` | メッシュを D3D デバイスより先に明示的に解放 |
| `Source/app/Runtime/framework_update.cpp` | `update_object_scene(elapsed_time)` を 1 行追加 | メインループから Scene を更新 |
| `Source/app/Runtime/framework_initialize.cpp` | `initialize_object_scene()` を 1 行追加 | Component 型登録・Scene 準備・既定 Scene 読み込み |
| `Source/app/Editor/framework_editor.cpp` | **SceneEntity 一覧を削除**し、GameObject ツリーへ置換 | 新旧で同じ UI を二重に出さないため |
| `Source/app/Editor/framework_inspector.cpp` | `case game_object:` を追加し `InspectorPanel::DrawContents` へ委譲 | 型ごとの if-else を framework へ増やさないため |
| `Source/app/Rendering/framework_render.cpp` | GBuffer パスと Forward パスに提出リスト描画を 2 箇所追加 | 既存パイプラインを壊さず最小限で接続 |

### 削除（2 ファイル）

| ファイル | 削除理由 |
|---|---|
| `RePlayEngine/Core/Components/GameObject.h` | 完全な未使用デッドコード。新 `Object/GameObject/GameObject.h` へ発展的に置換。参照ゼロを grep で確認済み |
| `RePlayEngine/Core/Components/TransformComponent.h` | 同上。新 `Components/Core/TransformComponent.h` と**同じ完全修飾名になり衝突する**ため削除が必須だった |

`Core/Components/` に残る 6 ファイル（`IComponent.h` / `AnimationComponent.h` / `GravityComponent.h` / `MeshColliderComponent.h` / `ModelRendererComponent.h` / `SphereColliderComponent.h`）は、`Player` / `Stage` / `BootLogoComponent` が今も使っているため**手を付けていない**。

### プロジェクトファイル

`3dgp.vcxproj` / `3dgp.vcxproj.filters` へ全 55 ファイルを登録。新規フィルター 20 個を作成し、すべて実フォルダと 1 対 1 で対応させた。`.h` と `.cpp` は同じ機能フィルター内に配置している。

---

## 12. 完成したコード

**全ファイルが完全な内容でリポジトリへ配置済み**（省略・断片なし）。上記の表が正確なファイルパスとフィルターの一覧。

---

## 13. 既存コードからの移行手順

**今回、既存の Player / Stage / Camera / Light には一切手を付けていません。** 新基盤は完全に並置されており、既存の描画・入力・当たり判定は従来どおり動きます。

段階移行の道筋:

```
現在  既存 Player / Stage / Camera / lights_manager  … そのまま動作
      新 Scene / GameObject / Component              … 並置、Editor から使える
        ↓
次    影響の小さい処理から Component 化
      （Rotator / Health は実装済み。次は MovingPlatform / DamageArea / CameraTarget）
        ↓
      LegacyPlayerAdapterComponent を作り、既存 Player を GameObject でラップ
      ※ 同じ処理が既存 Player 側と Component 側の両方から Update されないよう、
        ラップした時点で SceneGame::Update から Player::Update を外すこと
        ↓
      Player を PlayerInput / CharacterMotor / PlayerController / Animator / Health へ分解
        ↓
      Stage を GroundObject（Transform + MeshRenderer + Collider）の集合へ置換
        ↓
最終  Player も Ground も特殊クラスではなく GameObject + Component の構成になる
```

### 二重 Update / 二重描画を防いでいる根拠

- **更新**: 新 Scene の更新対象は `Scene` が所有する GameObject のみ。既存 `Player` / `Stage` は `SceneGame::Update` からのみ更新される。両者に重なりはない
- **描画**: 提出リストの対象は `MeshRendererComponent` を持つ GameObject のみ。既存の `skinned_meshes[0]` と `Stage` は従来の経路のみで描かれる。両者に重なりはない
- 将来 Adapter で既存 Player をラップする際は、上記のとおり旧経路を外す必要がある（コメントにも明記）

---

## 14. Editor での使用方法

| 操作 | キー / 場所 |
|---|---|
| Editor 表示 | `F1` |
| Edit Mode（ゲーム更新停止） | `F3` |
| **GameObject シーンの実行 / 停止** | `F5` |
| 保存 | `Ctrl+S`（旧ステージ記録と新 Scene の両方） |
| 名前を付けて保存 | `Ctrl+Shift+S` |
| 元に戻す / やり直す | `Ctrl+Z` / `Ctrl+Y`（GameObject 選択中は新基盤側） |

### 手順

1. **階層**パネルの一番下「GameObject」ツリーを開く
2. 「GameObject を作成」ボタン → 新しい GameObject が作られ選択される
3. **インスペクター**に `Transform` が自動で表示される（削除不可）
4. 位置・回転（度）・拡大率を編集
5. 「コンポーネントを追加」ボタン → カテゴリ別の一覧が開く
6. 検索欄に `Rot` と入力 → `Rotator` が絞り込まれる
7. `Rotator` を選択 → 追加される。既に付いている型は「(追加済み)」と表示され選択できない
8. 「回転速度 (度/秒)」を編集
9. 同様に `Mesh Renderer` を追加し、「メッシュ」欄で AssetDatabase に登録済みの Asset を選ぶ
10. `Ctrl+S` で保存
11. `F5` で実行 → GameObject が描画され回転する。もう一度 `F5` で編集内容に戻る

その他: 右クリックメニュー（作成 / 子として作成 / 名前変更 / 複製 / シーン直下へ移動 / 削除）、ドラッグ＆ドロップでの親子変更、チェックボックスでの有効・無効切り替え、未保存時のタイトル `*` 表示。

---

## 15-16. Scene の保存と読み込み

- **保存先**: `resources/Scenes/TrainingStage.replayscene`（`framework::object_scene_path`）
- **保存**: `Ctrl+S`。またはコードから `save_object_scene(false)`
- **読み込み**: 起動時に `initialize_object_scene()` が自動で読む。手動は `load_object_scene(false)`

コードから使う場合:

```cpp
using namespace ReplayEngine;

Scene::Scene scene("TrainingStage");
Core::GameObject* player = scene.CreateGameObject("Player");   // Transform は自動で付く
player->AddComponent<Components::MeshRendererComponent>();
player->AddComponent<Components::RotatorComponent>();
player->AddComponent<Components::HealthComponent>();

scene.Start();
scene.Update(delta_time);

Scene::Serialization::SceneData data;
Scene::Serialization::CaptureScene(scene, data);
std::string error;
Scene::Serialization::SceneSerializer::SaveToFile(
    data, "resources/Scenes/TrainingStage.replayscene", error);
```

---

## 17. 動作確認結果

### 実行環境の制約（重要）

作業環境は Linux サンドボックスで、**MSVC / Windows SDK / DirectX SDK がありません**。
そのため以下を明確に分けて報告します。

#### A. g++ 11 で構文検証済み（+ 実際に実行して検証）

DirectXMath / d3d11 / windows.h の最小スタブを作業領域に用意し（リポジトリには置いていません）、`g++ -std=c++17 -Wall -Wextra -Wpedantic -Wshadow` で検証。

| 項目 | 結果 |
|---|---|
| RePlayEngine 新規 `.cpp` 23 ファイル | **エラー・警告ゼロ** |
| 強制インクルード無しでの自己完結性 | **23 / 23 通過**（強制インクルードが必要だったのは ImGui 自身の `.cpp` のみ） |
| ImGui コア（`imgui.cpp` / `imgui_draw.cpp` / `imgui_widgets.cpp`）とのリンク | 成功 |
| **最終動作目標 1〜15 の受け入れテスト** | **40 項目すべて合格** |
| ヘッドレス ImGui でのパネル描画 | Scene 無し / 空 Scene / 選択中 / 削除直後 / Play 中で**クラッシュなし** |
| AddressSanitizer + UndefinedBehaviorSanitizer + LeakSanitizer | **エラー・リーク・未定義動作ゼロ** |
| `3dgp.vcxproj` / `.filters` の XML 妥当性 | OK |

受け入れテストで確認した内容（抜粋）:

- GameObject 作成 / Transform 編集（二重所有が無いことを双方向で確認）
- Add Component / 重複禁止型が二重に付かない / 未登録型が nullptr
- Property 編集・v7 保存・再読み込み・ObjectID / 親子 / Transform / Component 構成 / Property 値 / Asset 参照 / enum の復元
- Rotator 無効で回転停止 → 再有効化で再開
- MeshRenderer 無効で非表示 / GameObject 無効で子も含めて非表示
- Component 削除 → 保存 → 再読み込み後も削除状態を維持
- 複数 GameObject が個別の Transform で提出される
- Update 中の自己削除 / 所有 GameObject 削除 / 親削除で子も削除
- Undo / Redo
- Play 中の体力減少が編集シーンへ書き戻らない
- 旧形式 v6 の拒否・破損ファイル・巨大な個数指定・未登録 Component・存在しない親・ID 重複

#### B. MSVC でビルド未確認（要確認事項）

以下は**この環境では検証できていません**。Visual Studio でのビルドが必要です。

| 項目 | 内容 |
|---|---|
| **`framework` 側の 7 ファイルのコンパイル** | `framework.h` は `windows.h` / `d3d11.h` / `wrl.h` / `skinned_mesh.h` に依存するため g++ で検証不能。**宣言と定義の対応、メンバ名の一致は静的に突き合わせ済み**（宣言 10 / 定義 10 一致、メンバ 9 個すべて宣言済み） |
| **実際の DirectXMath の数値挙動** | `XMMatrixDecompose` の戻り値、`XMVector4Equal` による行列式判定、オイラー角抽出の符号。スタブは**シグネチャのみ**実物に合わせており、数値的な正しさは未検証 |
| `dynamic_cast` の RTTI 挙動 | MSVC の設定に依存 |
| `/W4` での MSVC 固有警告 | C4100 / C4127 など |
| **実行時の描画結果** | GameObject が画面に出るか、Transform どおりの位置に描かれるか |
| Asset の実読み込み | `resolve_object_mesh` が `.cereal` キャッシュから `skinned_mesh` を構築できるか |
| ImGui のレイアウト・日本語表示 | 実際の見た目 |

**C++20 機能の混入は 1 件発見して修正済みです。** 当初 `ComponentRegistry::Register<T>({ .display_name = ... })` と指定イニシャライザで書いていましたが、これは C++20 の機能で `stdcpp17` の MSVC では通りません（g++ は拡張として黙認するため `-Wpedantic` で検出）。`ComponentTypeInfo::Describe("Rotator", "Gameplay").NotRemovable()` という連結形式へ変更しました。以降 `-Wpedantic` を常時付けて検証しています。

---

## 18. メモリと所有権の確認結果

| 確認項目 | 結果 |
|---|---|
| ASan / UBSan / LeakSanitizer | **エラー・リーク・未定義動作ゼロ** |
| 所有権の一意性 | Scene → GameObject → Component の一本道。二重所有なし |
| Transform の所在 | GameObject の値メンバ 1 箇所のみ |
| `shared_ptr` | **不使用**（既存方針を維持） |
| 生 `new` / `delete` | `Scene::CreateGameObjectWithID` の `new GameObject(...)` 1 箇所のみ（private ctor のため `make_unique` が使えない）。即座に `unique_ptr` が受け取る。`delete` はゼロ |
| 循環参照 | なし。親→子・Component→GameObject・GameObject→Scene はすべて非所有 |
| 長期保持する生ポインタ | Editor 側はゼロ（ObjectID のみ）。Undo 履歴もゼロ（SceneData のみ） |
| ダングリングの可能性 | 選択・履歴・提出リストのいずれも生ポインタを跨いで保持しない |
| 終了時の解放 | `uninitialize()` で `clear_object_mesh_cache()` を呼び、D3D デバイスより先にメッシュを解放 |

### 既存コードに元からあるリスク（今回は手を付けていない）

`framework::game_scene` は `GameScene*` の生ポインタで、`SceneManager` がシーンを差し替えると理論上ダングリングし得ます。今回の変更とは無関係ですが、調査で見つかったので記録しておきます。

---

## 19. 未対応項目

### スコープ外として実装しなかったもの（ご指示どおり）

C# スクリプト / 完全リフレクション / Prefab の完全実装 / Visual Scripting / ECS 全面移行 / Shader システム全面改修 / Material Editor / ネットワーク同期 / 分散 Asset Build / 複雑な Hot Reload / Undo 履歴の永続化

### 今回の実装で意図的に見送った点

| 項目 | 状況 |
|---|---|
| **Collider / PlayerInput / PlayerController / CharacterMotor / Animator の Component 化** | ご指示により後回し。基盤は完成しているので追加は `BuiltInComponents.cpp` へ数行 |
| **既存 Player / Stage の Component 化** | 未着手。Adapter も未作成 |
| **`FixedUpdate` のメインループ接続** | `Scene::FixedUpdate` は実装済みだが、既存エンジンに固定タイムステップが無いため呼び出していない |
| **影（CSM）への提出** | `cast_shadow` は保存されるが、シャドウパスへ提出していない。GameObject は影を落とさない |
| **アニメーション** | `resolve_object_mesh` は keyframe に nullptr を渡すためバインドポーズ表示 |
| **glTF Asset** | `resolve_object_mesh` は `.cereal` キャッシュのみ対応。glTF は既存ステージ経路のまま |
| **アウトライン** | `outline` は保存されるが、提出リストからのアウトラインパスは未実装 |
| **ShaderLayerStack / CharacterMaterialProfile** | MeshRendererComponent からは扱えない。旧 `ModelRendererData` の機能 |
| **ファイルダイアログ** | `Ctrl+Shift+S` は既定パスを使う。OS ダイアログ未接続 |
| **未保存時の終了確認** | Dirty フラグとタイトル `*` は実装済みだが、終了時の確認ダイアログは未実装 |
| **旧 `SceneDocument` の完全撤去** | ステージ配置と衝突 Cook が依存しているため残している。Hierarchy / Inspector からは撤去済み |
| **Component の並列 Update** | 区分だけ用意。実行はすべてメインスレッド |
| **Scene 保存・読み込みの非同期化** | 分離できる構造にはしたが、実行は同期 |

### 明記しておく仮定

1. **回転はオイラー角（ラジアン）を正とした。** 既存の Player / Stage / ギズモに合わせるため。仕様例のクォータニオンとは異なる
2. **旧 v1〜v6 は読み込まない。** 対象ファイル 2 件がいずれも `ENTITY_COUNT 0`（空）であることを確認済み。`Docs/LegacyBackup/` にバックアップ済み
3. **`ComponentRegistry` / `PropertyRegistry` は静的な表とした。** 型のメタ情報はプロセス全体で 1 つしかない不変の表であり、可変状態を抱えるサービス的な Singleton とは性質が違うため。実体は関数ローカル static で、静的初期化順序に依存しない
4. **削除された GameObject の子は親と一緒に再帰的に削除される**（Scene 直下へ逃がさない）
5. **`AddComponent<T>` は重複禁止型が既にある場合、失敗ではなく既存インスタンスを返す。** 完成条件のコード例が自然に書けるようにするため。Editor 側は `(追加済み)` 表示で追加を防ぐ

---

## 20. 次に Component 化すべき処理

優先度順。いずれも `BuiltInComponents.cpp` へ 1 か所登録するだけで Editor と保存に反映される。

| 順 | 対象 | 理由と移行方法 |
|---|---|---|
| 1 | **`SphereColliderComponent` / `MeshColliderComponent`** | 既に `IComponent` 派生として存在し、`Player` / `Stage` に値メンバとして埋まっている。新 `Component` を継承させて GameObject 所有へ移すのが最も効果が大きい。`GameRaycast` が `Stage` 具象型を受けている箇所を `MeshColliderComponent` を受ける形へ薄く変えるだけで済む |
| 2 | **`MovingPlatformComponent` / `DamageAreaComponent`** | 完全な新規で既存への影響ゼロ。Rotator と同じ構造で書ける。動く床・ダメージ床が Editor だけで作れるようになる |
| 3 | **`CameraTargetComponent`** | `SceneGame` のカメラ追従設定（`follow_distance` / `follow_height` / `follow_lag`）を GameObject 側へ移す。既存 `Camera` は触らない |
| 4 | **`AnimatorComponent`** | 既存 `AnimationComponent.h` を新 `Component` へ昇格。`resolve_object_mesh` が keyframe を渡せるようになり、GameObject がアニメーションする |
| 5 | **`PlayerInputComponent`** | `GameInput` をラップし「カメラ基準の移動方向」を出力する。`Player::Update(dt, const Camera&)` がカメラ参照を引数で受けている点が分離の壁なので、入力側でカメラ基準へ変換しておくと Component 間の結合が弱くなる |
| 6 | **`CharacterMotorComponent`** | `Player` の velocity / on_ground / 移動物理を移す。プレイヤーと敵で共有できる |
| 7 | **`PlayerControllerComponent`** | Input → Motor の橋渡し + パラメータ。ここまで来ると `Player` クラスは薄い殻になる |
| 8 | **`LightComponent`** | `lights_manager` の配列を GameObject へ。Editor でライトを置けるようになる |

**注意**: 既存 Player を Component 化する際は、ラップした時点で `SceneGame::Update` から `player.Update(...)` を外してください。外さないと同じ処理が既存 Player 側と Component 側の両方から Update されます。

---

## 付録: コミットについて

作業環境から `git commit` を実行するとマウントの I/O 遅延で不安定だったため、コミットは行っていません。変更・新規ファイルの一覧は第 10-11 節のとおりです。Windows 側で以下を実行してください。

```
git add RePlayEngine Source 3dgp.vcxproj 3dgp.vcxproj.filters Docs
git commit -m "GameObject / Component 基盤と v7 Scene 形式を追加"
```

検証用のスタブ・強制インクルード・テストコード・オブジェクトファイルは、いずれも作業領域内にのみ存在し、**リポジトリには 1 つも入っていません**（確認済み）。
