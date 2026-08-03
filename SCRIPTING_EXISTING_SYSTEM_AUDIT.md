# Lua＋C# スクリプト機能 Phase 0 — 既存システム調査報告書

対象リポジトリ: `teamProject_2_3`（RePlayEngine / 3dgp）
初版: 2026-08-03
**改訂 2: 2026-08-03（大地さんの判断事項回答を反映）**
**改訂 3: 2026-08-03（Enable / Disable ライフサイクル 3 要件の実コード確認を反映。4.11 節を新設）**
実施範囲: **調査のみ**。コードは 1 行も追加・変更していません。`.vcxproj` / `.vcxproj.filters` も未変更です。
前提文書: 「RePlayEngine Lua＋C# マルチ言語スクリプト機能 実装指示書」
先行文書: `HANDOVER_FINAL_RESULT.md`（Phase 6〜9 の最終報告）

---

## 改訂 2 での変更点

大地さんから判断事項 10 件への回答と、3 件の訂正指示をいただいたので反映しました。

| 章 | 変更内容 |
|---|---|
| 1 | 「要判断」だった 7 件が全て確定。結論を更新 |
| 2.1 | ユーザー向け Callback 名を `Awake` / `Start` 系へ訂正。`OnCreate` は不採用 |
| 4.1 | 案 A で確定。**Script Field Schema の共有キャッシュ**という要件を追加 |
| 4.3 | 既定値は Schema が持つ形で確定 |
| 4.4 | 予約接頭辞方式で確定。`UnknownProperties` とは併用しない |
| 4.9 | **新設** — ユーザー向けライフサイクル名の対応表 |
| 4.10 | **新設** — Add Component へスクリプト名を並べる仕組みが無い |
| 5.2 | **全面改訂** — Play 開始順序を実コードで再確認。フック追加方針を確定 |
| 6 | 「要判断」から「確定した設計方針」へ。6.8 / 6.9 を新設 |
| 7 | ファイル一覧を確定方針で更新 |
| 8 | Phase 1 計画を全面改訂 |
| 10 | 判断事項 → **確定事項一覧**へ |
| 11 | 推測・未確認リストを更新（2 件を実確認へ昇格） |

---

## 0. この報告書の読み方

指示書 22 章「作業開始前に既存の以下を必ず調査すること」に挙げられた全項目を実ファイルで確認しました。

各節の記述は次の 3 種類に分けてあります。混同しないでください。

| 印 | 意味 |
|---|---|
| **実確認** | 該当ファイルを開いて読み、行番号まで特定した内容 |
| **推測** | コードから読み取れる意図の解釈。実行して確かめてはいない |
| **未確認** | 今回は見ていない。次 Phase で確かめる必要がある |

行番号は調査時点のものです。以降の編集でずれます。

---

## 1. 結論

**この engine は、指示書のスクリプト機能を載せるうえで極めて条件が良い状態にあります。**

Phase 6〜9 の作業で作られた次の 3 つが、そのままスクリプト基盤の土台になります。

1. `PropertyRegistry` / `PropertyDesc` / `PropertyValue`（Inspector と保存の単一定義点）
2. `MissingComponent` と未知プロパティ保持（型が読めない間もデータを失わない仕組み）
3. `IBehaviourProvider`（型の「供給元」を差し替えられる境界）

特に重要なのは、**これらのコメントに「将来 C# Script を載せる」ことが明記されている**点です。後付けではなく、最初からこの拡張を見込んで設計されています。

- `PropertyValue.h:49-50` — 「将来 C# の field をそのまま保存できるようにするための拡張」
- `MissingComponent.h:20-24` — 「C# Script が Compile できていない…Scene を開いて保存しただけで設定値が全部消える」
- `ComponentTypeInfo.h:44` — 「Behaviour と、将来の C# Script 型では必須にする」
- `PropertyRegistry.cpp:104-107` — 「C# Script が Compile できておらず型が読めていない」
- `BehaviourRegistry.h:23-38` — 「問題は次の Phase で C# を載せたときに起きる」
- `TypeGUID.h:20-24` — 「将来 C# Script を載せたとき、クラス名変更・名前空間変更・ファイル移動が日常操作になる」

**改訂 2 時点での状態:** 指示書と既存設計が衝突していた 9 箇所すべてについて、大地さんの判断で方針が確定しました（10 章）。加えて、**指示書にも初版報告書にも無かった重大な順序問題を 1 件発見しています**（5.2 節）。これは実コードで裏を取りました。

Phase 1 は**新規 24 ファイル / 変更 13 ファイル / 削除 0**。Lua も C# も含みません。

---

## 2. 既存基盤の棚卸し — 再利用できるもの

### 2.1 Component 基盤 — `RePlayEngine/Object/Component/Component.h`（280 行）

**実確認。** `ScriptComponent` はこれを継承するだけで、ライフサイクル・削除の安全性・保存経路をすべて手に入れます。

ライフサイクル仮想関数（全て `Component` が持つ）:

```
OnAttach          AddComponent 直後。プロパティ未反映
OnRuntimeAwake    Scene 開始後の最初の同期点。プロパティ・参照解決すべて完了済み。無効でも 1 回
OnEnable / OnStart  初めて実際に有効になったとき。OnStart は 1 回だけ
OnUpdate / OnFixedUpdate / OnLateUpdate   毎フレーム
OnDisable
OnRuntimeDestroy  実体破棄の直前に 1 回
OnDetach
OnTriggerEnter / Stay / Exit
```

**ユーザー向け Callback 名との対応（改訂 2 で確定）:**

| 既存 Component 仮想関数 | ユーザー向け Callback 名（Lua / C# 共通） |
|---|---|
| `OnRuntimeAwake` | `Awake` |
| `OnEnable` | `OnEnable` |
| `OnStart` | `Start` |
| `OnFixedUpdate` | `FixedUpdate` |
| `OnUpdate` | `Update` |
| `OnLateUpdate` | `LateUpdate` |
| `OnDisable` | `OnDisable` |
| `OnRuntimeDestroy` | `OnDestroy` |

**`OnCreate` はユーザー向け API として採用しません。** 指示書 6 章 / 8.2 / 9.2 / 9.3 の `OnCreate` は、すべて `Awake` へ読み替えます。

`OnAttach` / `OnDetach` はユーザーへ公開しません。プロパティ未反映の状態でユーザーコードを走らせないためです。

そのまま使える保存の逃げ道（**Phase 1 の要**）:

| メンバ | 行 | 用途 |
|---|---|---|
| `virtual void OnSerialize(PropertyBag&) const` | 135 | PropertyRegistry で表現できない値の書き出し |
| `virtual void OnDeserialize(const PropertyBag&)` | 136 | 同・読み込み |
| `const PropertyBag* UnknownProperties() const` | 157 | 型が知らないまま預かっている値 |
| `void RetainUnknownProperties(const PropertyBag&)` | 166 | 預かりの差し替え |
| `virtual void OnPropertyChanged(const char*)` | 172 | 値変更後の内部キャッシュ再構築 |
| `void Destroy() noexcept` | 218 | 削除予約（即 delete しない） |

コピー・ムーブは全て `= delete`（52-55 行）。複製は `ComponentRegistry` 生成 + `PropertyRegistry::CopyValues` で行う規約です。

### 2.2 ComponentRegistry / ComponentTypeInfo

**実確認。** `Object/Registry/ComponentRegistry.h`（129 行）、`ComponentTypeInfo.h`（200 行）。

`ComponentTypeInfo` へ 1 回登録すると、次の全部へ反映される単一定義点です（`ComponentTypeInfo.h:17-23` の記述と、実際の利用箇所で確認）:

- Editor の Add Component 一覧（`AddComponentPanel` は `ComponentRegistry::All()` を列挙するだけ）
- Scene 読み込み時の生成 / 保存時の型名
- Inspector のヘッダ表示と削除ボタンの有効・無効
- GameObject の複製

型解決の唯一の入口は `ComponentRegistry::Resolve(type_guid, type_name)`（`ComponentRegistry.h:78`）。優先順位は `type_guid` → `alias_guids` → `type_name`。

連結設定 API（C++17 のため指定イニシャライザは使わない規約。`ComponentTypeInfo.h:105-107` に明記）:

```cpp
ComponentTypeInfo::Describe("Script", "Scripting")
    .WithTypeGUID(Reflection::MakeTypeGUID("...32文字..."))
    .InModule("RePlayEngine.Scripting")
    .WithVersion(1)
    .AllowMultipleInstances()
    .WithTooltip("...")
```

`ScriptComponent` は**この 1 か所の登録だけで Add Component / Inspector / 保存 / Clone / Undo に載ります。**

**改訂 2 の補足:** ただし Add Component へ「スクリプト名で並べる」完成像（4.10 節）は、この Registry だけでは実現できません。`ScriptTypeCatalog` を別に用意して `AddComponentPanel` が 2 つの供給元を合流させる形になります。**スクリプト型を `ComponentRegistry` へ動的登録することはしません**（それが「第二の Component システム」になるため）。

### 2.3 Property 基盤 — 指示書の `ScriptValue` はここへ吸収する（確定）

**実確認。** `Reflection/Property/PropertyValue.h`（203 行）。

`PropertyType` は現在 22 種類。指示書 7.1 の `ScriptValue` が要求する型との対応:

| 指示書の要求型 | 既存 `PropertyType` | 状態 |
|---|---|---|
| bool / int / float / double / string | `Bool` / `Int` / `Float` / `Double` / `String` | **あり** |
| Vector2 / Vector3 / Vector4 / Quaternion / Color | 同名 | **あり** |
| int64_t | `Int64` | **あり**（v11 で C# 用に追加済み） |
| GameObject 参照 | `ObjectReference`（内部 `ObjectID`） | **あり** |
| Component 参照 | `ComponentReference`（`owner` + `ComponentStableID`） | **あり** |
| Asset 参照 | `AssetReference`（内部 AssetGUID 文字列） | **あり** |
| `std::monostate`（未設定） | — | **無い**（4.3 節参照） |

`PropertyValue` の内部 `Storage` は 12 型の `std::variant`（86-98 行）。

`PropertyValue` が既に持っている機能で、自作すると重複するもの:

- `ConvertTo(target, out)`（174 行）— 型が変わったときの救済変換
- `IsFinite()`（169 行）— NaN / Inf の検出
- `ValuesEqual(a, b)`（202 行）— 浮動小数点の許容差付き比較。Prefab override 検出と Inspector 差分表示が共用

**確定:** `using ScriptValue = Reflection::PropertyValue;`。独自 variant は作りません。

### 2.4 PropertyDesc — Script Field Schema の基礎（確定）

**実確認。** `Reflection/Property/PropertyDesc.h`（393 行）。

| 指示書 `ScriptFieldMetadata` | 既存 `PropertyDesc` |
|---|---|
| `name` | `name` |
| `display_name` | `display_name` / `DisplayName()` |
| `type` | `type`（`PropertyType`） |
| `default_value` | **無い**（Schema 側が持つ。4.3 節） |
| `serializable` | `serializable` |
| `visible_in_inspector` | `editor_visible` |
| `read_only` | `read_only` |
| `minimum` / `maximum` | `has_range` + `minimum` / `maximum` |
| `tooltip` | `tooltip` |
| `id`（安定 Field ID） | **無い**（Phase 1 では `name` を主キーにする。4.3 節） |
| — | `step` / `category` / `unit` / `runtime_only` / `advanced` / `asset_type` / `expected_component_type` / `array_element_type` / `enum_labels` |

`getter` / `setter` は `std::function<PropertyValue(const Component&)>` / `std::function<void(Component&, const PropertyValue&)>`（23-24 行）。

**これが決定的に重要です。** `PropertyDesc` は「型 T の何番目のメンバ」に固定されていません。`Component&` を受け取る任意のラムダを入れられます。

**改訂 2 の要件:** ただし **`PropertyDesc` 配列を `ScriptComponent` インスタンスごとに作ってはいけません**。同じ `RotatingObject.lua` を 1000 体へ付けたとき、`std::function` を 2 個持つ `PropertyDesc` を 1000 セット持つことになり、メモリと構築コストが無駄になります。

**確定した形:** `PropertyDesc` の getter / setter は**インスタンスを捕捉せず、`Component&` 引数から `ScriptComponent` へダウンキャストして「名前でフィールド表を引く」**ラムダにします。こうすると `PropertyDesc` 配列はインスタンスに依存しなくなり、`ScriptTypeID` ごとに 1 セットだけ持って全インスタンスで共有できます（4.1 節）。

### 2.5 PropertyRegistry — 保存 / 読み込み / 複製の合流点

**実確認。** `Reflection/Registry/PropertyRegistry.cpp`（175 行）。

```
Capture(component, output)                      … 62 行
  1. PropertiesOf(TypeID) を serializable だけ書き出す
  2. component.OnSerialize(output)               ← 動的フィールドはここから出せる
  3. UnknownProperties() のうち未出力の名前を書き戻す

Apply(component, input, unknown_names)          … 89 行
  1. 名前ごとに Find(type_id, name)
     見つからない -> retained へ退避 + unknown_names へ記録
     型が違う     -> ConvertTo で寄せる。無理なら初期値維持
  2. component.RetainUnknownProperties(retained) ← Rehydrate もここ
  3. component.OnDeserialize(input)              ← 動的フィールドはここで受ける
  4. component.OnPropertyChanged(nullptr)

CopyValues(source, destination)                 … 147 行（Clone 用）
  1. 登録済みプロパティを写す
  2. source.OnSerialize -> destination.OnDeserialize  ← 動的フィールドもここで写る
  3. UnknownProperties を引き継ぐ
```

### 2.6 MissingComponent — スクリプト未ロード時のデータ保護の思想

**実確認。** `Object/Component/MissingComponent.h`（75 行）、`Scene/Serialization/SceneData.cpp:239-294`。

型が解決できないとき、`SceneData.cpp:250` が `MissingComponent` を作り、`type_name` / `type_guid` / `module_id` / `type_version` / `PropertyBag` を丸ごと預かります。保存時（`SceneData.cpp:152-170`）は `"MissingComponent"` ではなく**元の型名として書き戻す**ため、ファイルは往復しても 1 バイトも変わりません。

`MissingComponent` は仮想関数を 1 つも override しないので、壊れた型が中途半端に動くことはありません（`MissingComponent.h:32-36`）。

**Phase 1 では、この思想をフィールド単位へ 1 段下ろします**（4.4 節）。`MissingComponent` そのものには手を入れません。

### 2.7 Scene — 更新ループと遅延操作

**実確認。** `Scene/Runtime/Scene.cpp`。

| 関数 | 行 | 内容 |
|---|---|---|
| `Start()` | 148 | `started_ = true` → `SynchronizeStates()` |
| `SynchronizeStates()` | 154 | 全 GameObject の `SyncComponentStates()`。`OnRuntimeAwake` / `OnEnable` / `OnStart` / `OnDisable` を呼ぶ |
| `Update(dt)` | 169 | `SynchronizeStates()` → 全 Component の `OnUpdate` → `ProcessPendingOperations()` |
| `FixedUpdate(dt)` | 197 | 同形。`SynchronizeStates()` は呼ばない |
| `LateUpdate(dt)` | 221 | 同形 |
| `ProcessPendingOperations()` | 245 | Component 回収 → GameObject 回収（**階層の深い順**） |

走査の安全策（**実確認**、`Scene.cpp:177-186`）:

```cpp
const std::size_t object_count = objects_.size();
for (std::size_t i = 0; i < object_count && i < objects_.size(); ++i)
```

開始時点の個数を控え、かつ毎回現在サイズとも比較します。ループ中に増えた分は次フレームから、減った分は即座に打ち切られます。`updating_` フラグが立っている間は `ProcessPendingOperations()` が何もしません（249 行）。

削除方式は「コマンドキュー」ではなく「予約フラグ + 同期点での全体走査」です（`Scene.h:28-33`）。宙に浮いたポインタが構造的に発生しません。

### 2.8 ObjectID / Handle / TypeGUID — スクリプトへ渡す安全な識別子

**実確認。** `Core/ObjectID/ObjectID.h`、`Core/ObjectID/RuntimeIdentity.h`、`Reflection/Registry/TypeGUID.h`。

| 型 | 保存 | 単位 | 用途 |
|---|---|---|---|
| `ObjectID` | する | Scene | GameObject の永続 ID（64bit） |
| `ObjectGeneration` | しない | World | 同じ ObjectID が作り直されたことの検出 |
| `WorldInstanceID` | しない | プロセス | World 総入れ替えの検出 |
| `ComponentStableID` | する | GameObject | `ComponentReference` の解決先 |
| `ComponentInstanceID` | しない | World | 古い `ComponentHandle` の弾き |
| `TypeGUID` | する | プロセス | 型の永続 ID（128bit POD） |

`RuntimeIdentity.h:12-14` に「将来 C ABI / C# へそのまま渡せるようにするため、クラスにして不変条件を持たせることはしない」と明記されており、**すべて整数の別名です。**

`TypeGUID`（**改訂 2 で重要度が上がったため追記**）:

- `{ uint64 high; uint64 low; }` の POD。`is_trivially_copyable` / `is_standard_layout` が `static_assert` で保証されている（63-66 行）
- `ToString()` は 32 桁小文字 16 進、ハイフン無し。**AssetGUID とまったく同じ表記**（27-28 行にその旨のコメントあり）
- `std::hash<TypeGUID>` の特殊化が既にある（117-127 行）
- `TryParse` はハイフン入り・大文字も受け付ける

**これがそのまま `ScriptTypeID` の実体になります**（6.9 節）。新しい ID 型を作る必要がありません。

### 2.9 保存形式 — `.replayscene`（テキスト行形式。JSON ではない）

**実確認。** `Scene/Serialization/SceneSerializer.cpp`（854 行）。

COMPONENT ブロックの実際の書式（`SceneSerializer.cpp:447-475`）:

```
  COMPONENT "RotatorComponent" 1
    STABLE_ID 3
    TYPE_GUID "00000000000000000000000000000000"
    TYPE_MODULE ""
    TYPE_VERSION 1
    PROPERTY_COUNT 2
    PROPERTY "axis" vec3 0 1 0
    PROPERTY "degrees_per_second" float 90
  END_COMPONENT
```

`PROPERTY` 行の書式は `PROPERTY "<名前>" <型名> <値...>`。名前は `std::quoted` で囲まれるため、**ドットを含む名前も問題なく往復します**（`__script.language` / `field.RotationSpeed` を使える根拠）。

型名は `PropertyValue.cpp:23-46` の表（`bool` / `int` / `float` / `double` / `string` / `vec2` / `vec3` / `vec4` / `quat` / `color` / `enum` / `asset` / `objref` / `layer` / `layermask` / `colliderref` / `int64` / `uint64` / `assetref` / `sceneref` / `compref` / `array`）。

上限値（`SceneSerializer.cpp:26-29`。**実確認**）:

```cpp
maximum_objects                  = 200000
maximum_components_per_object    = 512
maximum_properties_per_component = 512   ← 内部 4 個 + ユーザー Field。実質 508 個まで
maximum_array_elements           = 65536
```

バージョン: `current_version = 11` / `minimum_supported_version = 7`（`SceneData.h:138-139`）。

**ScriptComponent を追加しても Scene バージョンは v11 のままにします**（確定事項 2）。

### 2.10 Editor 統合点

**実確認。**

| 場所 | ファイル | 状態 |
|---|---|---|
| Add Component | `Editor/ComponentBrowser/AddComponentPanel.cpp` | `ComponentRegistry` を列挙するだけ。**Phase 1 は変更不要。Phase 6 で Catalog 合流** |
| Inspector 本体 | `Editor/Inspector/InspectorPanel.cpp` `DrawComponent()` | `PropertyRegistry::HasProperties(TypeID)` で分岐 → `PropertyDrawer::DrawAll`。**動的分の判定を足す** |
| 入力欄の描画 | `Editor/Inspector/PropertyDrawer.h` | `Draw(desc, component, assets, scene, mixed)` が **public**。任意の `PropertyDesc` を 1 件描ける |
| Undo / Redo | `Editor/Commands/SceneEditHistory.h` | `SceneData` スナップショット方式（上限 64 件）。**変更不要** |
| 編集トランザクション | `Editor/Core/EditorContext.h:94-99` | `BeginEdit` / `CommitEdit` / `CancelEdit` / `Undo` / `Redo` |
| Play 中の編集禁止 | `EditorContext.h:81` | `CanEdit()` = Scene あり かつ `!play_mode_` |

`InspectorPanel::DrawComponent` には既に `MissingComponent` 用の `dynamic_cast` 特例が 1 つあります（**実確認**）。**確定事項 1（案 A）を採るため、ScriptComponent 用の型特例は追加しません。**

### 2.11 Play / Edit Mode

**実確認。** `Source/app/Runtime/framework_gameobject_scene.cpp`。

```
enter_object_play_mode()   … 820 行
  1. initialize_runtime_services()
  2. CaptureScene(object_scene, snapshot)          ← 編集 Scene を SceneData へ
  3. object_runtime_scenes.RequestAdopt(snapshot, guid)
  4. Tick() × 2（Staging 構築 → 入れ替え + Scene::Start()）  ★ ここで OnRuntimeAwake が走る
  5. attach_collision_world(runtime_world)
  6. object_scene_play_mode = true / SetPlayMode(true)
  7. AttachScene(&runtime_world) / ResetSceneState()

exit_object_play_mode()    … 888 行
  1. detach_collision_world()
  2. object_runtime_scenes.ResetToEmptyWorld()     ← Runtime World を捨てる
  3. object_scene_play_mode = false / SetPlayMode(false)
  4. AttachScene(&object_scene) / ResetSceneState()
  5. attach_collision_world(object_scene)
```

**★ の位置が改訂 2 で問題として確定しました。5.2 節を参照してください。**

`framework_gameobject_scene.cpp:899-901` に「編集 Scene へ書き戻すことはしない。Play 中の変更はすべてここで消える。暗黙保存の経路そのものを置かない」と明記。**指示書 3.2 の「11. Play中の変更がEdit Sceneへ不正に残らない」は構造的に保証済みです。**

毎フレームの更新は `update_object_scene(elapsed_time)`（**実確認**）:

```
tick_runtime_scene_flow()             ← World 入れ替えの安全点（フレーム先頭）
RuntimeContext::SetTime(...)
refresh_object_scene_services()
if (object_runtime_active()) {
    scene.Update(dt)                  ← Component::OnUpdate
    update_object_fixed_step(dt)      ← scene.FixedUpdate() をサブステップで
    scene.LateUpdate(dt)              ← Component::OnLateUpdate
    dispatch_collision_triggers()
    object_collision_events.Dispatch(...)
    RuntimeContext::Events().Dispatch(...) / FlushDeferredOperations()
    update_object_camera_follow(dt)
} else {
    scene.ProcessPendingOperations()  ← 停止中も予約だけは反映
}
```

`object_runtime_active()`（221 行）の条件は `!(editor_mode && edit_mode_active && !object_scene_play_mode)`、かつ `object_scene_paused` なら false。**「Edit Mode ではゲームプレイスクリプトを勝手に実行しない」（指示書 5.5）はこの 1 関数で満たせます。**

### 2.12 Runtime API — スクリプト公開 API の供給元

**実確認。** `Runtime/API/RuntimeContext.h`。

| 指示書 | `RuntimeContext` の該当 | 状態 |
|---|---|---|
| `GameObject.Name` / `Active` | `GetName` / `SetName` / `SetEnabled` / `IsEnabled` | **あり** |
| `GameObject.GetComponent` / `HasComponent` | `GetComponent` / `HasComponent` / `GetComponents` | **あり** |
| `GameObject.Find` | `FindByObjectID` | 名前検索は `Scene::FindGameObjectByName` にある |
| `GameObject.Instantiate` | `InstantiatePrefab` / `InstantiatePrefabDeferred` | **あり** |
| `GameObject.Destroy` | `DestroyGameObject` / `DestroyComponent` | **あり** |
| `Transform.Position` ほか | `Get/SetLocalPosition` / `Get/SetLocalRotationEuler` / `Get/SetLocalScale` / `GetWorldPosition` | **ほぼあり**（`Translate` / `Rotate` は無い） |
| `Time.DeltaTime` ほか | `RuntimeTime`（`delta_time` / `fixed_delta_time` / `unscaled_delta_time` / `frame_index`） | **あり**（`ElapsedTime` は無い） |
| `Debug.Log` / `LogWarning` / `LogError` | `Log` / `LogWarning` / `LogError`（4 段階） | **あり** |
| `Scene.Load` / `Reload` | `LoadScene` / `ReloadCurrentScene` / `ReturnToPreviousScene` | **あり** |
| `Scene.CurrentScene` | `RuntimeSceneService::ActiveSceneGuid()` | **あり**（改訂 2 で実確認） |
| **`Input.IsKeyDown` ほか** | `InputActionAvailable()` が `return false`（314 行） | **無い**（4.5 節） |

全 API が `RuntimeStatus` を返し、`ObjectHandle` / `ComponentHandle` を受け取ります。生ポインタは 1 つも出入りしません。

### 2.13 IBehaviourProvider — 触らずに残す（確定）

**実確認。** `Runtime/Behaviour/BehaviourRegistry.h`（119 行）。

```cpp
class IBehaviourProvider {
    virtual const char* ProviderName() const noexcept = 0;   // "Native" / "Managed"
    virtual bool CanInstantiate(TypeGUID) const = 0;         // 「登録済み」と「今作れる」を分ける
    virtual std::unique_ptr<Component> Instantiate(TypeGUID) = 0;
};
```

**確定（確定事項 9）:** `IBehaviourProvider` は Native Behaviour 用としてそのまま残し、Phase 1〜8 を通じて**一切変更しません**。`IScriptBackend` は Lua / C# の**実行** Backend として新設しますが、Component の所有には一切関与しません。

### 2.14 Validation ハーネス

**実確認。** `Source/app/Runtime/main.cpp`（868 行）、`Runtime/Validation/*`、`Editor/Validation/EditorIntegrationValidation.h`。

規約（`BehaviourValidation.cpp:26-56` の `Checker` クラスで確認）:

- 検査 1 件ごとに固有の終了コードを割り当て、失敗時は**最初に失敗した検査のコード**を返す
- 全件成功で `0`
- 出力は `stderr`

既存の帯:

```
80-139   handles          140-179  serialization    180-209  missing-component
210-249  scene-version    250-289  behaviour        290-329  events
330-369  runtime-api      370-409  collision        410-457  runtime-scene
460-519  scene-flow       520-579  editor-integration   580-604  stress
```

**スクリプト系の帯は 620 以降が空いています**（各 Validation の `Checker` 開始コードを全て抽出し、最大が 580 であることを確認）。

**Phase 1 で予約する帯（確定）:**

```
620-679  --validate-script-core
680-739  --validate-script-lifecycle
740-799  --validate-script-serialization
800-     Phase 2 以降のスクリプト系 9 コマンド用に予約
```

`Editor` に触る検証は `ReplayEngine::Editor::Validation` へ置く規約（`EditorIntegrationValidation.h:2-5`。Runtime → Editor の逆依存を作らないため）。

### 2.15 ビルド構成

**実確認。** `3dgp.vcxproj` / `3dgp.vcxproj.filters`。

| 項目 | 値 |
|---|---|
| `ClCompile` 登録数 | 164 |
| `ClInclude` 登録数 | 177 |
| Filter 定義数 | 90 |
| 構成 | `Debug\|Win32` / `Release\|Win32` / `Debug\|x64` / `Release\|x64` |
| `LanguageStandard` | **x64 の 2 構成のみ `stdcpp17`。Win32 の 2 構成には指定が無い** |
| インクルードパス | `.` / `Source\app` / `Source\core` / `Source\mesh` / `Source\render` / `cereal-master\include` / `DirectXTK-main\Inc` |
| 追加オプション | `/utf-8 /validate-charset /FS` |
| ObjectFileName | `$(IntDir)%(RelativeDir)`（同名 .cpp の衝突回避） |
| ASan | `Debug\|x64` のみ `ReplayEnableASAN` プロパティで切替 |

Filter 命名規約: `RePlayEngine\<サブシステム>\<フォルダ>` と `Source\App\...` / `Source\Game\...`（**Source 側は先頭大文字**）。

---

## 3. 所有権とライフタイム

**実確認。** 現行の所有関係:

```
framework（唯一の最上位所有者）
├─ ReplayEngine::Scene::Scene object_scene            編集 Scene（実体を直接保持）
├─ RuntimeSceneService object_runtime_scenes          Runtime World の唯一の所有者
├─ unique_ptr<RuntimeContext> object_runtime_context  World の view（先に壊れる）
├─ unique_ptr<SceneFlowService> object_scene_flow     さらに先に壊れる
├─ AssetDatabase                                       Asset 台帳
└─ EditorContext object_editor_context                 Scene / AssetDatabase は非所有参照

Scene ──(unique_ptr)── GameObject ──(unique_ptr)── Component
GameObject → 親 / Scene は非所有の生ポインタ
Component  → GameObject は非所有の生ポインタ
```

破棄順は `framework.h:307-313` にコメントで明記（World 所有者が最後、view が先）。

### 3.1 スクリプト基盤を足したときの所有権（確定）

```
framework
└─ unique_ptr<ScriptRuntime> object_script_runtime
       ├─ ScriptTypeCatalog             … ScriptTypeID -> Descriptor + Schema（Play をまたいで生存）
       │    └─ shared_ptr<const ScriptFieldSchema>  … 全インスタンスで共有する Field 定義
       ├─ unique_ptr<LuaScriptBackend>     … Lua State の唯一の所有者（Phase 2）
       ├─ unique_ptr<CSharpScriptBackend>  … .NET Host / ALC の唯一の所有者（Phase 3）
       └─ unique_ptr<ScriptWorld>          … Play セッションごとに作り直す

ScriptComponent（GameObject が所有。Component 派生）
       ├─ ScriptTypeID                     … 値（TypeGUID）。保存対象
       ├─ shared_ptr<const ScriptFieldSchema>  … Catalog から受け取った共有参照
       ├─ PropertyBag field_values_        … 自分の Field 値。保存対象
       ├─ PropertyBag pending_values_      … Schema 未解決のあいだ預かる値
       └─ ScriptInstanceHandle             … 整数。実体は Backend 側。保存しない
```

**守るべき境界（指示書 21 章・確定事項 9・既存規約から）:**

1. `ScriptComponent` は Lua State ポインタも managed オブジェクト参照も**持たない**。持つのは整数ハンドルだけ
2. **Backend は Component を所有しない。** Lua / C# のインスタンスと整数 Handle の対応表だけを持つ
3. Lua Registry 参照と `GCHandle` の解放責任は Backend 側に一本化する
4. `ScriptFieldSchema` は `shared_ptr<const>`。ホットリロード中に古い Schema を参照しているインスタンスがあっても、参照が切れるまで生存する
5. 破棄順は `ScriptWorld` → Backend → `ScriptTypeCatalog` → `ScriptRuntime`
6. `ScriptRuntime` は `framework` が所有し、`object_runtime_scenes` より**後**に破棄する（World の破棄中に Backend が必要なため）

### 3.2 Script Field Schema の共有（確定事項 1 の追加要件）

**要件:** `PropertyDesc` 配列を `ScriptComponent` ごとに重複生成しない。

**確定した形:**

```cpp
// ScriptTypeID ごとに 1 個だけ作られ、全インスタンスで共有される
struct ScriptFieldSchema
{
    ScriptTypeID                            type_id;
    std::uint32_t                           revision;      // ホットリロードで増える
    std::vector<Reflection::PropertyDesc>   descs;         // 保存名は "field.<Name>"
    std::vector<Reflection::PropertyValue>  defaults;      // descs と同じ添字
};
```

`descs[i].getter` / `setter` は**インスタンスを捕捉しません**。次の形にします。

```cpp
desc.getter = [saved_name](const Core::Component& c) -> PropertyValue
{
    const auto* script = ScriptComponent::From(c);          // TypeID 照合つき。dynamic_cast しない
    return script != nullptr ? script->ReadField(saved_name) : PropertyValue{};
};
desc.setter = [saved_name](Core::Component& c, const PropertyValue& v)
{
    if (auto* script = ScriptComponent::From(c)) script->WriteField(saved_name, v);
};
```

捕捉するのは名前文字列だけなので、**同じ Schema を 1000 体の `ScriptComponent` が共有できます。**

**ホットリロードによる Schema 差し替え（確定）:**

差し替えは **Inspector 描画中でも Serialize 中でもなく、メインスレッドの安全な同期点**で行います。

```
場所: framework::update_object_scene() の先頭
      tick_runtime_scene_flow()（World 入れ替えの安全点）の直後
      ▼ ScriptRuntime::ApplyPendingSchemaSwaps()
```

この関数がやること:

1. Backend が非同期に検出した「新しい Schema 候補」を取り込む
2. 構文検証に成功したものだけ、Catalog の `shared_ptr` を差し替えて `revision` を増やす
3. `ScriptWorld` に登録済みの `ScriptComponent` を走査し、旧 Schema の値を新 Schema へ移送する（名前一致 + `ConvertTo`、合わないものは `pending_values_` へ退避）
4. 移送できなかったフィールドを診断へ記録する

**これは「第二の更新経路」ではありません。** ライフサイクル Callback を 1 つも呼ばず、Component の有効・無効も削除予約も見ないためです。Phase 1 では枠だけ作り、Mock Backend が明示的に要求したときだけ動きます。実運用の差し替えは Phase 5 です。

---

## 4. 不足箇所（ギャップ一覧）

### 4.1 【最重要】インスタンスごとの動的プロパティ経路 — **案 A で確定**

**実確認。** `PropertyRegistry` は `unordered_map<ComponentTypeID, vector<PropertyDesc>>` の**型単位の静的表**です（`PropertyRegistry.cpp:10`）。

スクリプトの公開変数は**スクリプト型単位**（`RotatingObject` と `DoorController` でフィールド構成が違う）で、同じ `ScriptComponent` 型でもインスタンスごとに異なります。

現状の描画経路（`InspectorPanel.cpp` `DrawComponent`）:

```cpp
if (Reflection::PropertyRegistry::HasProperties(component.TypeID()))
{
    ... PropertyDrawer::DrawAll(component, assets, scene);   // PropertiesOf(TypeID) を回すだけ
}
else
{
    ImGui::TextDisabled("編集できる設定はありません");
}
```

**このままでは ScriptComponent の公開変数は 1 つも表示されません。**

#### 確定した対処 — 案 A（`Component` へ汎用の動的プロパティ取得口を追加）

```cpp
// Component.h へ追加。既定は「動的プロパティを持たない」
virtual const std::vector<Reflection::PropertyDesc>* DynamicProperties() const noexcept
{
    return nullptr;
}
```

`ScriptComponent` だけがこれを override し、**Catalog から受け取った共有 Schema の `descs` を指すポインタを返します**（コピーしません）。

変更が必要な既存ファイル:

| ファイル | 変更内容 |
|---|---|
| `Object/Component/Component.h` | 仮想関数を 1 つ追加（既定 `nullptr`） |
| `Reflection/Registry/PropertyRegistry.cpp` | `Capture` / `Apply` / `CopyValues` / `Find` が静的表と動的表の両方を見る |
| `Editor/Inspector/PropertyDrawer.cpp` | `DrawAll` が両方を回す |
| `Editor/Inspector/InspectorPanel.cpp` | 「編集できる設定はありません」の判定に動的分を加える |

**これで Inspector / Serialize / Deserialize / Clone / Undo がすべて既存経路を通ります。型ごとの `if` / `switch` は 1 つも増えません。**

**注意点（Phase 1 で必ず守ること）:**

- `DynamicProperties()` が返すポインタの寿命は「次の同期点まで」。Inspector 描画中に Schema が差し替わらないことが前提（3.2 節の同期点設計がこれを保証する）
- `PropertyRegistry::Find` が静的表を先に見る。名前が衝突しないよう、動的分は必ず `field.` 接頭辞を持つ（4.4 節）
- `PropertyRegistry::Apply` は動的表にも当たるようになるため、スクリプト Field が「未知プロパティ」として数えられなくなる。`SceneLoadReport.unknown_properties` が汚れない

### 4.2 `AssetKind` に `Script` が無い

**実確認。** `Assets/AssetCache.h:10-19`:

```cpp
enum class AssetKind : std::uint32_t
{ Unknown, Model, Image, Audio, Shader, Scene, Material };
```

Prefab（`.replayprefab`）は `AssetKind::Scene` として登録されています（`framework_scene_document.cpp:319,354`）。

さらに `PropertyDesc::asset_type`（文字列。`PropertyDesc.h:67`）は**宣言はあるが `PropertyDrawer` が読んでいません**。Asset Picker の絞り込みは `SceneReference` かどうかの 1 分岐だけです（`PropertyDrawer.cpp:771-773`。**実確認**）。

**Phase 1 でやること:** `AssetKind::Script` を 1 つ追加するだけ（enum の**末尾へ**。既存の値がずれると `.replaydb` が壊れる）。
**Phase 6 でやること:** `.lua` / `.cs` のスキャン登録、Script Asset Picker の絞り込み。

### 4.3 既定値と安定 Field ID — **Schema が持つ形で確定**

**実確認。**

- **既定値**: `PropertyDesc` に `default_value` はありません。→ **`ScriptFieldSchema::defaults` が持ちます**（3.2 節）。`PropertyDesc` には手を入れません。
- **安定 Field ID**: `PropertyDesc` の主キーは `name`（文字列）。Phase 1 では `field.<Name>` を主キーにします。Rename 対応は Phase 5 以降で、`ComponentTypeInfo::property_aliases` と同じ「旧名 → 現名」表を Schema へ持たせる形で足します。指示書も「初期版では名前からIDを生成してよい」としています。
- **未設定値（`std::monostate`）**: `Target = nil` は `ObjectReference` の `ObjectID::Invalid()`（値 0）で表します。新しい型は不要です。

### 4.4 スクリプト未解決時の Field 値保護 — **予約接頭辞 + 専用の預かり箱で確定**

**実確認。** `MissingComponent` は「型ごと読めない」ケースを扱います。しかしスクリプトでは次が起きます:

- `ScriptComponent` 型は読めている（C++ の型なので常に読める）
- しかし `RotatingObject.lua` が構文エラーで読めない → **Schema が無い**

**確定した設計:**

`ScriptComponent` が自前の `PropertyBag pending_values_` を持ちます。

```
OnDeserialize(input) で受けたとき:
  Schema が解決済み   -> field_values_ へ流し込む
  Schema が未解決     -> pending_values_ へそのまま預かる

OnSerialize(output) のとき:
  field_values_   を書き出す
  pending_values_ のうち、field_values_ に無い名前も書き出す   ← 値を失わない

Schema が解決できたとき（3.2 節の同期点）:
  pending_values_ を Schema と照合し、一致するものを field_values_ へ移す
  型が違うものは ConvertTo で寄せる。無理なら Schema の既定値を使い、pending へ残す
  余ったものは pending_values_ に残したまま（次のリロードで復活しうる）
```

**`Component::RetainUnknownProperties` の仕組みとは併用しません。** 二重管理になるためです。`field.` 接頭辞のついた名前は `ScriptComponent` が全部引き受け、`PropertyRegistry` の未知プロパティ経路へは流しません。

**Phase 1 の Mock 検証で、この往復を必ず確かめます**（8 章）。

### 4.5 Input API が存在しない

**実確認。** `RuntimeContext.h:314` — `bool InputActionAvailable() const noexcept { return false; }`

現状のゲーム入力は `Source/game/game_input.h` の `GetAsyncKeyState` 直叩きインライン関数のみ。**Engine 層に入力抽象はありません。**

**Phase 7 の作業です。** Phase 1〜4 の完成条件（Cube を回す）には Input は不要です。

### 4.6 `Transform.Translate` / `Rotate` が Runtime API に無い

**実確認。** `RuntimeContext` にあるのは `Get/SetLocalPosition` / `Get/SetLocalRotationEuler` / `Get/SetLocalScale` / `GetWorldPosition` のみ。

Native API 側で「現在値を読む → 加算 → 書き戻す」で合成できます（`local_space` の扱いは要検討）。**Phase 2 で必要になります**（Lua で Cube を回すため）。

### 4.7 実行順序（execution_order）— **Phase 1 では保存・表示のみで確定**

**実確認。** `Scene::Update` は `objects_` の配列順 × `components_` の配列順で回します。実行順の概念はありません。

`BehaviourComponent::execution_order`（`BehaviourComponent.h:65`）は**宣言されているだけで、誰も読んでいません**:

> 現時点では Scene が GameObject 順・Component 順で回すため参照されない。
> 将来 Execution Order を導入するときに、保存形式と Inspector を作り直さずに済むよう、値だけ先に持たせてある。

**確定（確定事項 6）:** Phase 1 では `execution_order` を**保存・読み込み・Inspector 表示まで**とします。Scene 全体の実行順ソートは Phase 1 に混ぜず、後続の専用作業として分離します。

**ただし Validation で次を確かめます:**

- 同じ `execution_order` の ScriptComponent 群が、現在の GameObject 順 × Component 順で**常に同じ順序**で呼ばれること
- Play / Stop を繰り返しても順序が変わらないこと
- GameObject を追加・削除しても、残ったものの相対順序が保たれること

### 4.8 その他の細かい不足

| 項目 | 状態 |
|---|---|
| `Time.ElapsedTime` | `RuntimeTime` に `frame_index` はあるが累積秒が無い。加算するだけ |
| `GameObject.Find`（名前検索） | `Scene::FindGameObjectByName` はあるが `RuntimeContext` に露出していない |
| `Scene.CurrentScene` | **`RuntimeSceneService::ActiveSceneGuid()` があった**（改訂 2 で実確認。`RuntimeSceneService.h:135` 付近） |
| `third_party/` ディレクトリ | **存在しない**（`cereal-master` / `imgui` / `DirectXTK-main` はリポジトリ直下） |
| `Assets/` ディレクトリ | **存在しない**。Asset は `resources/` 配下（6.6 節） |

### 4.9 【改訂 2 で新設】ユーザー向けライフサイクル名の対応

指示書 6 章 / 8.2 / 9.2 / 9.3 の Callback 名を、大地さんの訂正どおり次で統一します。

```
Awake
OnEnable
Start
FixedUpdate
Update
LateUpdate
OnDisable
OnDestroy
```

**`OnCreate` はユーザー向け API として採用しません。**

内部の対応（2.1 節の表と同じ）:

| C++ Component | ユーザー Callback | 呼ばれる条件 |
|---|---|---|
| `OnRuntimeAwake` | `Awake` | Scene 開始後の最初の同期点。**無効な Component でも 1 回呼ばれる** |
| `OnEnable` | `OnEnable` | 有効になるたび |
| `OnStart` | `Start` | 初めて実際に有効になったとき 1 回だけ |
| `OnFixedUpdate` | `FixedUpdate` | 固定ステップ。有効な間だけ |
| `OnUpdate` | `Update` | 毎フレーム。有効な間だけ |
| `OnLateUpdate` | `LateUpdate` | 毎フレーム。有効な間だけ |
| `OnDisable` | `OnDisable` | 無効になるたび |
| `OnRuntimeDestroy` | `OnDestroy` | 実体破棄の直前に 1 回 |

**注意:** `Awake` は無効な Component でも呼ばれます。これは既存 `OnRuntimeAwake` の仕様であり、Unity の `Awake`（無効な GameObject では呼ばれない）とは挙動が違います。**この差はドキュメントへ明記します。** 既存仕様を曲げてまで Unity へ寄せることはしません。

### 4.11 【改訂 3 で新設】Enable / Disable ライフサイクルの実挙動 — 3 要件の突き合わせ

**実確認。** `Object/Component/Component.cpp`（`SyncEnableState` / `ActiveInHierarchy`）、`Object/GameObject/GameObject.cpp:63-74`（`ActiveInHierarchy`）、`同 299-310`（`SyncComponentStates`）、`Scene/Runtime/Scene.cpp:148-167`。

#### 実際のコード

```cpp
// Component::SyncEnableState()   … Component.cpp
if (!runtime_awake_called_)
{
    runtime_awake_called_ = true;
    OnRuntimeAwake();                 // ★ 無条件。ActiveInHierarchy を見ない
}

const bool desired = ActiveInHierarchy();

if (desired != enable_state_applied_)  // OnEnable / OnDisable は必ず対になる
{
    enable_state_applied_ = desired;
    if (desired) OnEnable(); else OnDisable();
}

if (desired && !started_)              // Start は初めて有効になったとき 1 回だけ
{
    started_ = true;
    OnStart();
}
```

```cpp
// Component::ActiveInHierarchy()
if (!enabled_ || pending_destroy_) return false;
return owner_ != nullptr && owner_->ActiveInHierarchy();

// GameObject::ActiveInHierarchy()   … 自分と全祖先が enabled_ かつ !pending_destroy_
```

呼び出し経路は `Scene::Start()`（151 行）と `Scene::Update()` の先頭（175 行）の 2 か所だけ。**`FixedUpdate` / `LateUpdate` は `SynchronizeStates()` を呼びません。**

（補足: `FixedUpdate` / `LateUpdate` のループは `component->ActiveInHierarchy()` を毎回直接見るため、**無効化した Component が即座にスキップされる**点は正しく動きます。ただし `OnDisable` の発火は次の `Update` 先頭までずれます。）

#### 3 要件との突き合わせ結果

| # | 要件 | 既存基盤 | 根拠 |
|---|---|---|---|
| **1** | Active な GameObject 上の Disabled な ScriptComponent | | |
| 1-a | `Awake` は呼ばれる | **✅ 満たす** | `OnRuntimeAwake` は `runtime_awake_called_` だけで判定し、`desired` を見ない |
| 1-b | `OnEnable` / `Start` は呼ばれない | **✅ 満たす** | `desired = false`、`enable_state_applied_` の初期値も `false` なので分岐に入らない |
| 1-c | Enabled になった時に `OnEnable` → `Start` が 1 回だけ | **✅ 満たす** | 同じ同期点で `OnEnable()` の直後に `OnStart()` が走る |
| **2** | Inactive な GameObject 上の ScriptComponent | | |
| 2-a | Scene 開始時に `Awake` を呼ばない | **❌ 満たさない** | `OnRuntimeAwake` が無条件のため呼ばれてしまう |
| 2-b | Scene 開始時に `OnEnable` / `Start` を呼ばない | **✅ 満たす** | `desired = false` |
| 2-c | ActiveInHierarchy になった時に `Awake` → `OnEnable` → `Start` | **△ 部分的** | `OnEnable` → `Start` は正しい。`Awake` は既に済んでいるため呼ばれない |
| **3** | 再度 Disable / Enable | | |
| 3-a | `Awake` を繰り返さない | **✅ 満たす** | `runtime_awake_called_` |
| 3-b | `Start` を繰り返さない | **✅ 満たす** | `started_` |
| 3-c | `OnDisable` / `OnEnable` だけを繰り返す | **✅ 満たす** | `enable_state_applied_` との比較で対称性が保証されている |

**要件 1 と 3 は完全に満たされます。要件 2 の `Awake` のタイミングだけが違います。**

この差は事故ではなく**意図的な設計**です。`Component.h:81` に「無効な Component でも必ず一度呼ばれる」、`BehaviourComponent.h:47` に「OnAwake … 無効でも呼ばれる」と明記されています。

#### 既存 Validation が何を守っているかの確認（重要）

`BehaviourValidation.cpp:154-227` を精読しました。

```cpp
disabled_probe->SetEnabled(false);            // 164 行 … Component を無効化
world.Start();
check.Expect(disabled_probe->awake_count == 1,
    "無効な Behaviour でも Awake が 1 回呼ばれる");   // 174-175 行
```

**この `disabled_object` は GameObject 自体は有効なままです**（`SetEnabled` を呼んでいるのは `_probe`（Component）だけで、GameObject へは呼んでいないことを全 `SetEnabled` 呼び出し箇所で確認）。

つまり既存 Validation が守っているのは**要件 1-a そのもの**であり、「Inactive な GameObject 上の Component にも Awake が来る」ことは**どこも検査していません**。

#### 確定した対処 — `SyncEnableState` の Awake ゲートをオプトインで拡張

**スクリプト専用の第二状態管理は作りません。** `runtime_awake_called_` / `enable_state_applied_` / `started_` という既存の 3 フラグをそのまま使い、判定を 1 か所だけ拡張します。

```cpp
// Component.h へ追加
//
// Awake を「所有 GameObject の階層が有効になってから」に遅らせるか。
//
// 既定は false（従来どおり、Scene が動き出した最初の同期点で必ず呼ぶ）。
// 既存の Component / Behaviour の挙動は 1 ビットも変えない。
//
// ScriptComponent だけが true を返す。ユーザーが書くスクリプトの Awake は
// Unity と同じく「GameObject が有効になってから」の方が予測しやすいため。
virtual bool DeferAwakeUntilObjectActive() const noexcept { return false; }
```

```cpp
// Component.cpp  SyncEnableState() の冒頭だけを差し替える
const bool object_active = owner_ != nullptr && owner_->ActiveInHierarchy();

if (!runtime_awake_called_ &&
    (!DeferAwakeUntilObjectActive() || object_active))
{
    runtime_awake_called_ = true;
    OnRuntimeAwake();
}
// 以降は現状のまま
```

**ゲートに `ActiveInHierarchy()`（Component 自身の `enabled_` を含む）ではなく `owner_->ActiveInHierarchy()`（GameObject 階層のみ）を使うのが要点です。** これにより:

- 要件 1-a: Disabled な Component でも、GameObject が有効なら `Awake` が来る ✅
- 要件 2-a: GameObject が無効なら何も来ない ✅
- 要件 2-c: GameObject が有効になった同期点で `Awake` → `OnEnable` → `Start` がこの順で走る ✅（1 回の `SyncEnableState` 内で上から順に 3 つとも通る）

**変更するファイルは `Component.h`（仮想関数 1 つ）と `Component.cpp`（条件 1 か所）だけです。**

#### 併せて必要になるガード — `Awake` していないスクリプトに `OnDestroy` を呼ばない

**実確認。** `GameObject::CompactComponents()`（312-336 行）と `DetachAllComponents()`（338-352 行）はどちらも次を呼びます。

```cpp
component->ForceDisable();        // enable_state_applied_ が false なら何もしない ✅
component->RaiseRuntimeDestroy(); // ★ 無条件で OnRuntimeDestroy() を呼ぶ
component->OnDetach();
```

要件 2 の対処を入れると「`Awake` が一度も走っていない `ScriptComponent`」が存在しうるため、そのまま破棄すると**ユーザーの `Awake` を呼ばずに `OnDestroy` だけを呼ぶ**ことになります。Unity はこの場合 `OnDestroy` を呼びません。

**対処:** `ScriptComponent::OnRuntimeDestroy()` の中で、**スクリプトインスタンスが作られている場合だけ**ユーザーの `OnDestroy` を呼びます。判定には既存の `ScriptInstanceHandle` の有効性をそのまま使うので、**新しい状態変数は増やしません**。`Component` 側にも手を入れません。

#### 採用しなかった案

**案 α — `SyncEnableState` の `OnRuntimeAwake` を全 Component に対して遅らせる**

engine 全体が Unity と同じ意味になり、`BehaviourComponent` の分も一度に解決します。既存 Validation も（GameObject が有効なため）そのまま通ることを確認済みです。

採用しなかった理由は、`Component.h:81` と `BehaviourComponent.h:47` が現在の挙動を**契約として明文化している**ためです。スクリプト機能の都合で基底クラスの契約を黙って変えると、Inactive な GameObject へ置いた既存 Behaviour の初期化タイミングが予告なくずれます。今回の作業範囲を超えます。

**ただし将来 engine 全体を揃えたくなった場合、`DeferAwakeUntilObjectActive()` の既定値を `true` にするだけで案 α になります。** その際は `Component.h` / `BehaviourComponent.h` のコメント修正と `--validate-behaviour` の再実行が必要です。

### 4.10 【改訂 2 で新設】Add Component へスクリプト名を並べる仕組みが無い

**実確認。** `AddComponentPanel::Draw` は `ComponentRegistry::All()` を列挙し、`category` でグループ分けするだけです。

大地さんが示された完成像:

```
Scripts
├─ C#
│  ├─ RotatingObject
│  └─ PlayerMovement
└─ Lua
   ├─ EnemyBrain
   └─ DoorController
```

これを実現するには次が必要です。

| 必要なもの | Phase |
|---|---|
| `ScriptTypeCatalog`（ScriptTypeID → 表示名 / 言語 / Asset / Schema / 状態） | **Phase 1**（構造だけ用意） |
| `AddComponentPanel` が Catalog も列挙する | Phase 6 |
| Project Browser の `.cs` / `.lua` を GameObject へドラッグ | Phase 6 |
| Inspector の Component ヘッダーをスクリプト名にする | Phase 6 |

**Phase 1 でやること（構造の用意）:**

```cpp
struct ScriptTypeDescriptor
{
    ScriptTypeID  type_id;          // TypeGUID の別名（6.9 節）
    ScriptLanguage language;        // Lua / CSharp
    std::string   script_name;      // "RotatingObject"      … 保存・照合用
    std::string   display_name;     // "Rotating Object"     … Inspector ヘッダー / Add Component
    std::string   asset_guid;       // Script Asset の GUID
    std::string   class_name;       // C# の完全修飾クラス名。Lua は空
    ScriptTypeStatus status;        // Unresolved / Loaded / Error
    std::shared_ptr<const ScriptFieldSchema> schema;   // Error 中は最後に成功したもの
};

class ScriptTypeCatalog
{
    void Register(ScriptTypeDescriptor descriptor);
    const ScriptTypeDescriptor* Find(ScriptTypeID id) const noexcept;
    const std::vector<ScriptTypeDescriptor>& All() const noexcept;  // Add Component 用
    // Schema 差し替えは ScriptRuntime の同期点からのみ
};

// Inspector ヘッダーの表示名。ScriptComponent が返す
std::string ScriptComponent::DisplayLabel() const;   // "Rotating Object" / "Script (Missing)"
```

**`ComponentRegistry` へスクリプト型を動的登録することはしません。** GameObject が所有する実体は常に `ScriptComponent` 1 種であり、Catalog は「表示と Schema の供給元」に徹します（確定事項 9）。

**Phase 6 で `AddComponentPanel` が 2 つの供給元を合流させる**とき、`ComponentRegistry` 側には手を入れません。Panel 側で `ComponentRegistry::All()` と `ScriptTypeCatalog::All()` を並べて描くだけです。

**Inspector ヘッダーについて（Phase 6）:** `InspectorPanel::DrawComponent` は現在 `ComponentRegistry::Find(TypeID)->DisplayName()` を使っています。ここを「Component が表示名を返せるなら優先する」形へ 1 段一般化します（`MissingComponent` の `DescribeMissingType()` と同じ役割を汎用化する）。型特例は増やしません。

---

## 5. 呼び出し経路（Phase 1 で接続する箇所）

### 5.1 起動

**実確認。**

```
framework::initialize()
  └─ initialize_object_scene()                     framework_gameobject_scene.cpp:~100
       ├─ RegisterBuiltInComponents()              106 行  ← ScriptComponent の登録はここ
       ├─ Game::RegisterGameBehaviours()           113 行
       ├─ load_project_settings()
       ├─ initialize_runtime_services()            ← ScriptRuntime::Initialize() はこの直後
       └─ if (object_boot_from_startup_scene) begin_startup_scene()
```

### 5.2 【改訂 2 で全面改訂】Play 開始順序 — 順序問題を実コードで確認

大地さんのご指摘は**正しく、実コードで裏が取れました。**

#### 実際に起きること（実確認）

`RuntimeSceneService::SwapWorlds()`（`RuntimeSceneService.cpp:318-403`）の順序:

```
318  SwapWorlds()
324    BeforeSceneUnload を発行
336    runtime_->Events().Clear()
343    collision_dispatcher_->Reset()
352    active_->Services().SetRuntime(nullptr)
358    active_->Clear()                 ← 旧 World の OnDisable / OnRuntimeDestroy / OnDetach
365    active_ = std::move(staging_)    ← 旧 World の実体をここで解放
371    runtime_->Rebind(*active_)
372    active_->Services().SetRuntime(runtime_)
381    active_->Start()                 ★★ ここで新 World の OnRuntimeAwake が全部走る
388    state_ = Completed
```

`enter_object_play_mode()`（`framework_gameobject_scene.cpp:820-886`）:

```
856    object_runtime_scenes.Tick();   // BuildStagingWorld
857    object_runtime_scenes.Tick();   // SwapWorlds → ★★ が走り切る
...    以降の処理（attach_collision_world / SetPlayMode / AttachScene）
```

**したがって、初版報告書 5.2 の「`Tick()` の後に `ScriptRuntime::BeginPlay` を置く」案は誤りです。** その時点で `ScriptComponent::OnRuntimeAwake` は全て呼ばれ終わっており、Backend も ScriptWorld も無い状態でユーザーの `Awake` を呼ぶことになります。

#### さらに悪いこと — Play Mode だけの問題ではない

`SwapWorlds()` は **`SceneFlowService` 経由のゲーム中 Scene 遷移でも通ります**。`framework::enter_object_play_mode` にだけフックを置くと、**ゲーム中に Scene を切り替えた瞬間にスクリプトが動かなくなります**。Editor 上の Play では気づけません。

`ResetToEmptyWorld()`（Play 停止）にも同じ後始末の並びがあります（**実確認**）:

```
CancelPending()
runtime_->Events().Clear()
collision_dispatcher_->Reset()
active_->Services().SetRuntime(nullptr)
active_->Clear()                 ← 旧 World の OnRuntimeDestroy
active_ = make_unique<Scene>("Runtime World")
runtime_->Rebind(*active_)
```

#### 確定した対処 — `RuntimeSceneService` へ汎用フックを追加

大地さんのご指示どおり、**framework から `ScriptComponent` を直接走査する応急処置は行いません。**

既存の `ISceneAssetResolver` / `IPrefabInstantiator` / `ISceneFlow` と同じ「Runtime 層がインターフェイスを宣言し、上位が実装する」形にします。

```cpp
// RePlayEngine/Runtime/Scene/WorldLifecycleListener.h（新規）
namespace ReplayEngine::Runtime
{
    // Runtime World の入れ替えに合わせて、World 単位の付随状態を作り直すための境界。
    //
    // なぜ必要か:
    //   Scene::Start() は Component の OnRuntimeAwake を全部流す。
    //   World 単位の付随状態（Script の Play Session など）は、その前に
    //   用意されていなければならない。Play Mode の開始処理へ書くと、
    //   ゲーム中の Scene 遷移で同じ準備が漏れる。
    //
    // Runtime は実装を知らない。Scripting 層が実装して framework が接続する。
    class IWorldLifecycleListener
    {
    public:
        virtual ~IWorldLifecycleListener() = default;

        // 旧 World の Clear() の直前。まだ全 Component が生きている。
        // 新しい Callback の受付を止める用途。
        virtual void OnWorldUnloading(Scene::Scene& world) = 0;

        // 旧 World の Clear() の直後、実体解放の前。
        // OnRuntimeDestroy はすべて流れ終わっている。付随状態の破棄と漏れ検証はここ。
        virtual void OnWorldUnloaded(Scene::Scene& world) = 0;

        // 新 World の Rebind 完了後、Scene::Start() の直前。
        // ここで用意したものは、直後の OnRuntimeAwake から必ず見える。
        virtual void OnWorldActivating(Scene::Scene& world) = 0;
    };
}
```

**呼び出し位置（`RuntimeSceneService.cpp` への挿入。3 か所 × 2 関数）:**

| フック | `SwapWorlds()` | `ResetToEmptyWorld()` |
|---|---|---|
| `OnWorldUnloading(*active_)` | 352 行 `SetRuntime(nullptr)` の直前 | `SetRuntime(nullptr)` の直前 |
| `OnWorldUnloaded(*active_)` | 358 行 `Clear()` の直後・365 行 `std::move` の前 | `Clear()` の直後・`make_unique` の前 |
| `OnWorldActivating(*active_)` | 372 行の直後・**381 行 `Start()` の直前** | `SetRuntime(runtime_)` の直後 |

接続は `SetWorldLifecycleListener(IWorldLifecycleListener*)`（非所有）。既存の `SetRuntimeContext` / `SetCollisionDispatcher` と同じ形です。

#### これで保証される順序

```
1. ScriptRuntime::OnWorldActivating(new_world)
     ├─ 旧 ScriptWorld が残っていれば異常として記録
     └─ 新しい ScriptWorld を作る（空。Play Session 世代番号を 1 つ進める）
2. Scene::Start()
3. Scene::SynchronizeStates()
4. ScriptComponent::OnRuntimeAwake()
     ├─ ScriptTypeCatalog から Schema を解決（未解決なら Error 状態のまま何もしない）
     ├─ Backend::CreateInstance() → ScriptInstanceHandle
     ├─ ScriptWorld へ自分を登録
     ├─ pending_values_ / field_values_ をインスタンスへ適用
     └─ Backend::Invoke(handle, Awake)          ← ユーザーの Awake
5. ScriptComponent::OnEnable()  → Backend::Invoke(handle, OnEnable)
6. ScriptComponent::OnStart()   → Backend::Invoke(handle, Start)
```

**`ScriptWorld` への登録は `ScriptComponent` の自己申告**なので、World を走査する処理はどこにもありません。

終了側:

```
1. ScriptRuntime::OnWorldUnloading(old)   … 新規 Callback の受付を止める
2. Scene::Clear()
3. ScriptComponent::OnRuntimeDestroy()
     ├─ Backend::Invoke(handle, OnDestroy)     ← ユーザーの OnDestroy
     ├─ Backend::DestroyInstance(handle)
     └─ ScriptWorld から自分を外す
4. ScriptRuntime::OnWorldUnloaded(old)
     ├─ ScriptWorld の残存インスタンス数が 0 であることを検証（0 でなければ診断へ記録）
     └─ ScriptWorld を破棄
```

**Phase 1 の Validation で、この順序を Mock Backend の記録から直接確かめます。**

### 5.3 毎フレーム（確定事項 3）

**`ScriptRuntime` 独自の `Update` / `FixedUpdate` / `LateUpdate` ループは作りません。**

```
update_object_scene(dt)
  ├─ tick_runtime_scene_flow()
  ├─ ScriptRuntime::ApplyPendingSchemaSwaps()   ← 3.2 節の同期点。Component は走査するが Callback は呼ばない
  └─ if (object_runtime_active()) {
         scene.Update(dt)
           └─ ScriptComponent::OnUpdate(dt)      → Backend::Invoke(handle, Update, dt)
         update_object_fixed_step(dt)
           └─ ScriptComponent::OnFixedUpdate(dt) → Backend::Invoke(handle, FixedUpdate, dt)
         scene.LateUpdate(dt)
           └─ ScriptComponent::OnLateUpdate(dt)  → Backend::Invoke(handle, LateUpdate, dt)
     }
```

`OnEnable` / `OnDisable` / `OnRuntimeDestroy` も同じく `ScriptComponent` の仮想関数から Backend を叩きます。

### 5.4 保存

```
save_object_scene()
  └─ CaptureScene(object_scene, data)              SceneData.cpp:108
       └─ PropertyRegistry::Capture(component, bag)  PropertyRegistry.cpp:62
            ├─ PropertiesOf(TypeID)      … __script.language / __script.asset / __script.class / __script.execution_order
            ├─ DynamicProperties()       … field.RotationSpeed / field.Target …（案 A で追加）
            ├─ component.OnSerialize(bag) … pending_values_ のうち Schema に無いぶん
            └─ UnknownProperties() の書き戻し（ScriptComponent は使わない）
  └─ SceneSerializer::SaveToFile(data, path, error)
```

### 5.5 読み込み

```
SceneSerializer::LoadFromFile(data, path, error)
  └─ ApplySceneData(data, scene, report)            SceneData.cpp:339
       └─ BuildComponents(...)                      SceneData.cpp:223
            ├─ ComponentRegistry::Resolve(guid, name)          233 行
            ├─ 見つからない -> MissingComponent へ預ける        250 行
            ├─ CreateWithStableID(...)                          296 行  ← OnAttach
            └─ PropertyRegistry::Apply(component, bag, &unknown) 319 行
                 ├─ __script.* -> 静的 PropertyDesc へ
                 ├─ field.*    -> 動的 PropertyDesc へ（Schema 解決済みなら）
                 ├─ component.OnDeserialize(bag)   ← Schema 未解決なら pending_values_ へ
                 └─ component.OnPropertyChanged(nullptr)
  └─ Scene::Start()  →  OnRuntimeAwake / OnEnable / OnStart
```

**重要な順序の注意（Phase 1 で必ず検証）:** `PropertyRegistry::Apply` は `OnAttach` の直後、`OnRuntimeAwake` の前に走ります。この時点では `__script.asset` が読めているので **Schema の解決はここで試みられます**。ただし Edit Mode では Backend が動いていないため、Lua / C# の実ロードは行わず、Catalog に既にある Schema だけを引きます。無ければ `pending_values_` へ預かります。

### 5.6 Inspector

```
InspectorPanel::Draw(context)
  └─ DrawComponent(context, component)
       ├─ 表示名（Phase 6 で ScriptComponent::DisplayLabel() を優先する形へ一般化）
       ├─ MissingComponent なら DrawMissingComponentDetails（既存の型特例。増やさない）
       └─ HasProperties(TypeID) || DynamicProperties() != nullptr
            └─ PropertyDrawer::DrawAll(component, assets, scene)
                 ├─ 静的 PropertyDesc を順に Draw   … Language / Script / Execution Order
                 └─ 動的 PropertyDesc を順に Draw   … Rotation Speed / Local Space / Target
```

**接頭辞は Inspector に出ません。** `PropertyDrawer` はラベルに `desc.DisplayName()` を使い、`display_name` が空のときだけ `name` へ落ちます。Schema が `display_name` を必ず埋めるので、`field.RotationSpeed` が画面に出ることはありません。

### 5.7 Undo / Redo / Clone

```
Undo:   EditorContext::BeginEdit → SceneEditHistory::Begin（CaptureScene）
        ... 変更 ...
        EditorContext::CommitEdit → SceneEditHistory::Commit（CaptureScene）
        Undo() → ApplySceneData(before, scene, report)

Clone:  DuplicateGameObject()  SceneData.cpp:253
          └─ ComponentRegistry::CreateWithStableID
          └─ PropertyRegistry::Capture → Apply（SceneData.cpp:729-730）
```

**案 A により、動的フィールドも自動的に通ります。** `PropertyRegistry::CopyValues` も動的表を見るようにするため、Clone で Schema と値の両方が引き継がれます。

---

## 6. 確定した設計方針

初版で「要判断」としていた 9 件は、大地さんの回答ですべて確定しました。

### 6.1 保存形式 — 既存 PROPERTY 行 + 予約接頭辞（確定事項 2・8）

**JSON は新設しません。** Scene version は **v11 のまま**据え置きます。

**保存内部名の名前空間（確定）:**

| 保存内部名 | 種別 | Inspector 表示 |
|---|---|---|
| `__script.language` | 静的（enum） | Language |
| `__script.asset` | 静的（assetref） | Script |
| `__script.class` | 静的（string。C# のみ意味を持つ） | Class |
| `__script.execution_order` | 静的（int） | Execution Order |
| `field.RotationSpeed` | 動的（Schema 由来） | Rotation Speed |
| `field.LocalSpace` | 動的 | Local Space |
| `field.Target` | 動的 | Target |

**`__script.` と `field.` は予約接頭辞です。** ユーザーが `language` / `class_name` / `script_asset` という名前の Field を宣言しても、保存名は `field.language` / `field.class_name` / `field.script_asset` になるため、管理情報と**構造的に衝突しません**。警告に頼りません。

保存例:

```
  COMPONENT "ScriptComponent" 1
    STABLE_ID 4
    TYPE_GUID ".....32桁の固定値....."
    TYPE_MODULE "RePlayEngine.Scripting"
    TYPE_VERSION 1
    PROPERTY_COUNT 8
    PROPERTY "__script.language" enum 0
    PROPERTY "__script.asset" assetref "2d3b51a1418f4f10a40b211a029a7813"
    PROPERTY "__script.class" string ""
    PROPERTY "__script.execution_order" int 0
    PROPERTY "__script.type_id" string "2d3b51a1418f4f10a40b211a029a7813"
    PROPERTY "field.RotationSpeed" float 90
    PROPERTY "field.LocalSpace" bool 1
    PROPERTY "field.Target" objref 0
  END_COMPONENT
```

**`__script.type_id` を足す理由:** C# では `asset` + `class` から `ScriptTypeID` を導きますが、Asset が一時的に見つからない状態でも「どのスクリプト型だったか」を保持できるようにするためです。Lua では `asset` と同値になります（6.9 節）。

**AssetGUID の表記に注意:** 指示書 13 章の例はハイフン付き UUID ですが、実際の engine は**32 桁 16 進・ハイフン無し**です（`resources/AssetDatabase.replaydb` で実確認）。

### 6.2 更新経路 — `ScriptComponent::OnUpdate` の 1 経路のみ（確定事項 3）

**既存の設計:** `BehaviourComponent.h:16-26` が、まさに第二の更新経路を禁止しています（**実確認**、原文）:

> **【なぜ専用の Update 経路を作らないか】**
> Behaviour 専用の更新マネージャを別に持つと、「Scene が回す Component」と「マネージャが回す Behaviour」の 2 経路ができる。どちらが先か、削除予約はどちらが見るか、Play 停止でどちらが止まるか、といった食い違いがそのままバグになる。
> Scene の既存ループ以外に Behaviour を回す場所は存在しない。

**確定:** `ScriptRuntime` から `Update` / `FixedUpdate` / `LateUpdate` を外します。`ScriptRuntime` の責務は次に限定します。

- `IScriptBackend` の所有
- `ScriptTypeCatalog` の所有
- `ScriptWorld` の所有（Play セッション単位）
- `IWorldLifecycleListener` の実装（5.2 節）
- `ApplyPendingSchemaSwaps()`（3.2 節。Callback を呼ばない同期点）
- エラーの集約と抑制

これにより次が自動的に満たされます:

- 削除予約は Scene の同期点が 1 か所で見る
- `object_runtime_active()` が false なら Edit Mode でスクリプトが動かない
- Play 停止で確実に止まる
- `ActiveInHierarchy()` が false のスクリプトは呼ばれない

### 6.3 ScriptValue — `PropertyValue` の別名（確定事項 4）

```cpp
namespace ReplayEngine::Scripting
{
    using ScriptValue        = Reflection::PropertyValue;
    using ScriptValueType    = Reflection::PropertyType;
    using ScriptFieldStorage = Reflection::PropertyBag;
}
```

`ScriptFieldMetadata` は `PropertyDesc` を基礎とし、スクリプト固有情報（既定値・元の Field 名・Schema revision）だけを持つ薄い Schema 定義にします（3.2 節の `ScriptFieldSchema`）。

### 6.4 ディレクトリ構成（確定事項 5）

| 指示書 | **本リポジトリでの確定配置** |
|---|---|
| `Source/scripting/core/` | `RePlayEngine/Scripting/Core/` |
| `Source/scripting/binding/` | `RePlayEngine/Scripting/Binding/` |
| `Source/scripting/lua/` | `RePlayEngine/Scripting/Lua/` |
| `Source/scripting/csharp/` | `RePlayEngine/Scripting/CSharp/` |
| `Source/editor/scripting/` | `RePlayEngine/Editor/Scripting/` |
| （新規） | `RePlayEngine/Scripting/Validation/` |
| `third_party/lua/` | `lua-5.4.x/`（`imgui` などと同じくリポジトリ直下。Phase 2 で確定） |
| `Assets/Scripts/Lua/` | `resources/Scripts/Lua/` |
| `Assets/Scripts/CSharp/` | `resources/Scripts/CSharp/` |
| `Intermediate/Scripts/` | `Saved/Scripts/`（`Assemblies` / `BuildLogs` / `ReflectionCache`） |
| `Managed/` | `Managed/`（リポジトリ直下。そのまま） |

**`Source/scripting` / `Assets/Scripts` / `Intermediate/Scripts` は新設しません。**

Filter 名は既存規約に合わせて `RePlayEngine\Scripting\Core` のようにします。

### 6.5 IScriptBackend と IBehaviourProvider（確定事項 9）

**確定:**

- `IBehaviourProvider` は Native Behaviour 用として**そのまま残す**。Phase 1〜8 で一切変更しない
- `IScriptBackend` は **Lua / C# の実行 Backend として新設**する
- **`IScriptBackend` は Component を所有しない。** GameObject が所有する実体は常に `ScriptComponent`
- Backend が管理するのは「Lua / C# のインスタンス」と「整数 Handle」の対応表だけ

```cpp
class IScriptBackend
{
public:
    virtual ~IScriptBackend() = default;

    virtual bool Initialize(ScriptRuntime& runtime) = 0;
    virtual void Shutdown() = 0;

    // ScriptTypeID -> Schema。読み込み・構文検証まで。インスタンスは作らない
    virtual ScriptLoadResult LoadType(const ScriptTypeDescriptor& descriptor,
        std::shared_ptr<const ScriptFieldSchema>& out_schema) = 0;

    // インスタンスの生成・破棄。返るのは整数だけ
    virtual ScriptInstanceHandle CreateInstance(ScriptTypeID type,
        Core::ObjectID owner, Core::ComponentStableID component) = 0;
    virtual void DestroyInstance(ScriptInstanceHandle instance) = 0;

    // Callback 呼び出し。例外・Lua エラーはここで止め、外へ漏らさない
    virtual ScriptInvokeResult Invoke(ScriptInstanceHandle instance,
        ScriptCallback callback, const ScriptArguments& arguments) = 0;

    // Field 値の適用・取得
    virtual bool SetField(ScriptInstanceHandle, const std::string& name,
        const ScriptValue& value) = 0;
    virtual bool GetField(ScriptInstanceHandle, const std::string& name,
        ScriptValue& out) const = 0;

    // 生存インスタンス数。ScriptWorld 破棄時の漏れ検証に使う
    virtual std::size_t LiveInstanceCount() const noexcept = 0;
};
```

**登録簿は 2 つのまま**です（`ComponentRegistry` / `BehaviourRegistry`）。`ScriptTypeCatalog` は「Component 型の登録簿」ではなく「スクリプトアセットの目録」なので、第三の Component システムにはあたりません。

### 6.6 Win32 構成（確定事項 7）

**実確認。** `LanguageStandard = stdcpp17` が指定されているのは `Debug|x64` / `Release|x64` のみ。`Debug|Win32` / `Release|Win32` には指定がありません（MSVC 既定は C++14）。コードベースは `std::variant` / `if constexpr` / `std::string_view` を多用しています。

**確定:**

- スクリプト機能の正式対象は **`Debug|x64` / `Release|x64` のみ**
- **Win32 対応のために設計を複雑化しない**（`#ifdef` による分岐、構成別のファイル除外などを入れない）
- **既存 Win32 構成の削除も今回は行わない**
- 新規ファイルは 4 構成すべての `ClCompile` / `ClInclude` へ登録する（構成別除外の前例が既存に無いため、登録の形は揃える）
- 完成条件・Validation の対象は x64 の 2 構成のみ

### 6.7 execution_order（確定事項 6）

Phase 1 は保存・読み込み・Inspector 表示まで。ソートは後続の専用作業。同一順序時の安定性は Validation で確認（4.7 節）。

### 6.8 【改訂 2 で新設】Play Session の準備順序（確定）

5.2 節のとおり、`RuntimeSceneService` へ `IWorldLifecycleListener` を追加します。

**これは指示書にも初版報告書にも無かった項目です。** 実コードを読んで初めて分かりました。

**変更する既存ファイル:**

| ファイル | 変更内容 |
|---|---|
| `Runtime/Scene/RuntimeSceneService.h` | `SetWorldLifecycleListener()` と非所有メンバを追加 |
| `Runtime/Scene/RuntimeSceneService.cpp` | `SwapWorlds()` / `ResetToEmptyWorld()` へ 3 か所ずつフック呼び出しを挿入 |

**新規ファイル:** `Runtime/Scene/WorldLifecycleListener.h`

**依存方向:** `Scripting` → `Runtime` の一方向。`Runtime` は `Scripting` を知りません。既存の `ISceneAssetResolver` / `IPrefabInstantiator` / `ISceneFlow` と同じ形です。

**この変更は Behaviour 側にも効きます。** 将来 Behaviour が World 単位の付随状態を持ちたくなったとき、同じフックへ乗れます。スクリプト専用の仕掛けにはしません。

### 6.9 【改訂 2 で新設】ScriptTypeID（確定事項 10）

**確定:**

- **Native の `ScriptComponent` には固定 TypeGUID を 1 つ発行**して `ComponentTypeInfo::WithTypeGUID` へ書く（Phase 1 で 1 回決めて以後不変）
- それとは別に、**各ユーザースクリプトを識別する `ScriptTypeID`** を設ける

**`ScriptTypeID` の実体は `Reflection::TypeGUID` の別名にします。**

```cpp
using ScriptTypeID = Reflection::TypeGUID;
```

**第二の ID 型を作らないための選択です。** `TypeGUID` は 128bit POD で、`std::hash` 特殊化も `ToString` / `TryParse` も既にあり、表記が AssetGUID と同じ 32 桁 16 進です（`TypeGUID.h:27-28` で確認）。

**導出規則（確定）:**

| 言語 | 導出元 | 方法 |
|---|---|---|
| Lua | Script Asset GUID | AssetGUID の 32 桁 16 進をそのまま `TypeGUID::TryParse` で読む。**ハッシュしない**ので可逆で、衝突もしない |
| C# | Script Asset GUID + 完全修飾クラス名 | `"<asset_guid>#<Namespace.Class>"` を FNV-1a 128bit でハッシュ |

C# を分ける理由は、1 つの `.cs` に複数の `ScriptBehaviour` 派生クラスを書けるためです。Lua は 1 ファイル 1 モジュールなので Asset GUID だけで一意になります。

**用途（指示どおり）:**

- Add Component の表示（Catalog のキー）
- Field Schema の対応付け
- Reload 対象の特定
- エラー情報の識別

**用途にしないこと:**

- `ObjectID` の代わりにしない（オブジェクトを指さない）
- `ComponentTypeID` の代わりにしない（`ScriptComponent` の `ComponentTypeID` は 1 つだけ）
- `ComponentRegistry` へ登録しない

---

## 7. 変更予定ファイル一覧（Phase 1・確定）

### 7.1 新規ファイル

| パス | 内容 |
|---|---|
| `RePlayEngine/Scripting/Core/ScriptLanguage.h` | `enum class ScriptLanguage { Lua, CSharp }` + 文字列/enum 変換 |
| `RePlayEngine/Scripting/Core/ScriptTypes.h` | `ScriptTypeID` / `ScriptInstanceHandle` / `ScriptStatus` / `ScriptCallback` / `ScriptInvokeResult` |
| `RePlayEngine/Scripting/Core/ScriptTypes.cpp` | `ScriptTypeID` の導出（Lua: AssetGUID、C#: FNV-1a 128） |
| `RePlayEngine/Scripting/Core/ScriptValue.h` | `PropertyValue` / `PropertyBag` への別名と補助関数 |
| `RePlayEngine/Scripting/Core/ScriptFieldSchema.h` | `ScriptFieldSchema` / `ScriptFieldDefinition` |
| `RePlayEngine/Scripting/Core/ScriptFieldSchema.cpp` | Schema から共有 `PropertyDesc` 配列を組む。名前捕捉のみのラムダ |
| `RePlayEngine/Scripting/Core/ScriptTypeCatalog.h` | `ScriptTypeDescriptor` / `ScriptTypeCatalog` |
| `RePlayEngine/Scripting/Core/ScriptTypeCatalog.cpp` | 登録・検索・Schema 差し替え |
| `RePlayEngine/Scripting/Core/ScriptComponent.h` | `Component` 派生。`DynamicProperties()` を override |
| `RePlayEngine/Scripting/Core/ScriptComponent.cpp` | ライフサイクル転送 / Field 読み書き / `OnSerialize` / `OnDeserialize` |
| `RePlayEngine/Scripting/Core/ScriptBackend.h` | `IScriptBackend`（6.5 節） |
| `RePlayEngine/Scripting/Core/ScriptRuntime.h` | Backend・Catalog・ScriptWorld の所有。`IWorldLifecycleListener` 実装 |
| `RePlayEngine/Scripting/Core/ScriptRuntime.cpp` | 同上 |
| `RePlayEngine/Scripting/Core/ScriptWorld.h` | Play セッション単位の登録簿・世代番号 |
| `RePlayEngine/Scripting/Core/ScriptWorld.cpp` | 同上 |
| `RePlayEngine/Scripting/Core/ScriptError.h` | エラー情報・重複抑制（最初 5 回 → 1 秒集約） |
| `RePlayEngine/Scripting/Core/ScriptError.cpp` | 同上 |
| `RePlayEngine/Scripting/Core/MockScriptBackend.h` | Phase 1 検証用。Mock Script Type 2 種 |
| `RePlayEngine/Scripting/Core/MockScriptBackend.cpp` | 同上 |
| `RePlayEngine/Scripting/Binding/ScriptBindingRegistry.h` | 型・メソッド・プロパティの登録簿（受け皿のみ） |
| `RePlayEngine/Scripting/Binding/ScriptBindingRegistry.cpp` | 同上 |
| `RePlayEngine/Scripting/Validation/ScriptCoreValidation.h` | 3 コマンド分の宣言と終了コード帯 |
| `RePlayEngine/Scripting/Validation/ScriptCoreValidation.cpp` | 3 コマンドの実装 |
| `RePlayEngine/Runtime/Scene/WorldLifecycleListener.h` | `IWorldLifecycleListener`（6.8 節） |

**新規 24 ファイル（.h 13 / .cpp 11）。**

### 7.2 変更する既存ファイル

| パス | 変更内容 | 規模 |
|---|---|---|
| `RePlayEngine/Object/Component/Component.h` | `DynamicProperties()` と `DeferAwakeUntilObjectActive()` の仮想関数を 2 つ追加（既定は `nullptr` / `false`） | 小 |
| `RePlayEngine/Object/Component/Component.cpp` | `SyncEnableState()` の Awake ゲート条件を 1 か所拡張（4.11 節） | 小 |
| `RePlayEngine/Reflection/Registry/PropertyRegistry.cpp` | `Capture` / `Apply` / `CopyValues` / `Find` が動的表も見る | 中 |
| `RePlayEngine/Editor/Inspector/PropertyDrawer.cpp` | `DrawAll` が動的表も回す | 小 |
| `RePlayEngine/Editor/Inspector/InspectorPanel.cpp` | 「編集できる設定はありません」の判定に動的分を加える | 小 |
| `RePlayEngine/Object/Registry/BuiltInComponents.cpp` | `RegisterScriptComponent()` を追加（静的 4 プロパティ含む） | 小 |
| `RePlayEngine/Assets/AssetCache.h` | `AssetKind::Script` を**末尾へ**追加 | 小 |
| `RePlayEngine/Runtime/Scene/RuntimeSceneService.h` | `SetWorldLifecycleListener()` + 非所有メンバ | 小 |
| `RePlayEngine/Runtime/Scene/RuntimeSceneService.cpp` | `SwapWorlds` / `ResetToEmptyWorld` へフック 3 か所ずつ | 中 |
| `Source/app/framework.h` | `unique_ptr<ScriptRuntime>` メンバと前方宣言 | 小 |
| `Source/app/Runtime/framework_gameobject_scene.cpp` | ScriptRuntime の初期化 / Listener 接続 / 同期点呼び出し / 破棄 | 中 |
| `Source/app/Runtime/main.cpp` | Validation 3 コマンドの分岐追加 | 小 |
| `3dgp.vcxproj` | 新規 24 ファイルを 4 構成へ登録 | 中 |
| `3dgp.vcxproj.filters` | `RePlayEngine\Scripting\...` の Filter 定義と割り当て | 中 |

**変更 14 ファイル。削除 0。**

`enter_object_play_mode` / `exit_object_play_mode` には**手を入れません**。Play Session の開始・終了は `IWorldLifecycleListener` 経由で自動的に走ります（6.8 節）。

### 7.3 Phase 2 以降の見込み（概算）

| Phase | 新規 | 変更 |
|---|---|---|
| 2（Lua） | Lua 5.4 ソース一式 + `Scripting/Lua/` 8 ファイル | `.vcxproj`（Lua ソース群 + インクルードパス） |
| 3（C# Host） | `Scripting/CSharp/` 4 ファイル | `.vcxproj`（nethost 参照） |
| 4（C# ScriptBehaviour） | `Managed/` の C# プロジェクト 3 本 + `Scripting/CSharp/` 6 ファイル | ビルド連携 |
| 5（保存・リロード） | 0〜2 | `ScriptComponent` / `ScriptRuntime` / Schema 移送 |
| 6（Editor 統合） | `Editor/Scripting/` 8 ファイル | `AddComponentPanel` / `InspectorPanel` / Asset Browser |
| 7（API 拡張） | `Runtime/API/IInputService.h` ほか | `RuntimeContext` |
| 8（ストレス） | `Scripting/Validation/` 2 ファイル | `main.cpp` |
| 別件 | — | `Scene::Update` の実行順対応（execution_order。Phase から分離） |

---

## 8. Phase 1 実装計画（改訂版）

### 8.1 Phase 1 でやること / やらないこと

**やること:**

- 共通スクリプト基盤（`ScriptComponent` / `IScriptBackend` / `ScriptRuntime` / `ScriptWorld` / `ScriptTypeCatalog` / `ScriptFieldSchema` / `ScriptError`）
- 動的プロパティ経路（案 A）
- 予約接頭辞つき保存形式
- `IWorldLifecycleListener` による Play Session 順序保証
- `MockScriptBackend` と Mock Script Type 2 種
- Validation 3 種

**やらないこと:**

- Lua の組み込み（Phase 2）
- .NET のホスト（Phase 3）
- Editor の Add Component へスクリプト名を並べる（Phase 6。Phase 1 は Catalog 構造だけ）
- Script Asset Picker の絞り込み（Phase 6）
- `Scene::Update` の実行順ソート（別件）
- Input API（Phase 7）

### 8.2 作業手順

各手順の後に必ずビルド確認を挟みます（指示書 22 章の 9 段階手順）。

1. `ScriptLanguage` / `ScriptTypes`（`ScriptTypeID` 導出含む）/ `ScriptValue` 別名
2. `ScriptFieldSchema` — 共有 `PropertyDesc` 配列の組み立て。**インスタンスを捕捉しないラムダ**
3. `ScriptTypeCatalog` — 登録・検索・`shared_ptr` 差し替え
4. `Component::DynamicProperties()` の追加と `PropertyRegistry` / `PropertyDrawer` / `InspectorPanel` の対応（案 A）
5. `ScriptComponent` — 静的 4 プロパティ・`field_values_` / `pending_values_`・ライフサイクル転送
6. `IScriptBackend` / `MockScriptBackend`（Mock Script Type 2 種）
7. `ScriptWorld` / `ScriptError`
8. `IWorldLifecycleListener` の新設と `RuntimeSceneService` への挿入
9. `ScriptRuntime` — Listener 実装・Catalog 所有・`ApplyPendingSchemaSwaps()`
10. `ScriptBindingRegistry`（受け皿のみ）
11. `BuiltInComponents.cpp` への登録・`AssetKind::Script` の追加
12. `framework` への接続（初期化 / Listener 接続 / 同期点 / 破棄）
13. `.vcxproj` / `.vcxproj.filters` 更新
14. Validation 3 種の実装
15. 既存 Validation 12 種の再実行
16. 静的検査（9.3 節）

### 8.3 Mock Script Type（大地さんのご指定どおり）

`MockScriptBackend` は Field 構成の異なる 2 種類を提供します。

```
MockScriptType: RotatingObject
  ScriptTypeID  : 固定値（Lua 相当。AssetGUID を模した 32 桁）
  Language      : Lua
  display_name  : "Rotating Object"
  Fields:
    RotationSpeed : float  default 90.0   range 0..720   display "Rotation Speed"
    LocalSpace    : bool   default true                  display "Local Space"

MockScriptType: DoorController
  ScriptTypeID  : 固定値（C# 相当。asset+class のハッシュを模した 32 桁）
  Language      : CSharp
  class_name    : "Game.DoorController"
  display_name  : "Door Controller"
  Fields:
    OpenAngle : float             default 90.0  range 0..180  display "Open Angle"
    Target    : GameObject 参照   default 無効                 display "Target"
```

さらに検証専用の操作を用意します。

- `SetTypeResolvable(ScriptTypeID, bool)` — 型を一時的に解決不能にする
- `RequestSchemaSwap(ScriptTypeID, new_schema)` — ホットリロードを模す
- `CallLog()` — Callback 呼び出しの順序記録（型 / インスタンス / Callback 名）

### 8.4 Validation 定義

#### `--validate-script-core`（終了コード帯 620-679）

- `ScriptComponent` の Add / Remove
- `ScriptTypeID` 導出の決定性（同じ入力 → 同じ ID。Lua は AssetGUID と一致）
- Catalog への登録・検索
- **Schema の共有** — 同じ `ScriptTypeID` の `ScriptComponent` を 100 体作り、`DynamicProperties()` が返すポインタが**全て同一**であること
- `PropertyDesc` の総生成数が Script Type 数に比例し、インスタンス数に比例しないこと
- 予約接頭辞の衝突回避 — `language` / `class_name` / `script_asset` という名前の Field を宣言しても静的プロパティと衝突しないこと
- Schema 差し替えを Inspector 描画中・Serialize 中に行おうとした場合に拒否されること
- 型が解決不能な `ScriptComponent` が Update で落ちないこと

#### `--validate-script-lifecycle`（終了コード帯 680-739）

- `Awake` → `OnEnable` → `Start` → `Update` → `OnDisable` → `OnDestroy` の順序
- **`ScriptWorld` の準備が `Scene::Start()` より前であること**（`OnWorldActivating` → `Awake` の順序を CallLog で確認）
- **World 入れ替え（`SwapWorlds`）でも同じ順序になること**
- **Play 停止（`ResetToEmptyWorld`）で `OnDestroy` → `OnWorldUnloaded` の順になること**
- `OnWorldUnloaded` の時点で `Backend::LiveInstanceCount() == 0`
**Enable / Disable の 3 要件（4.11 節。改訂 3 で追加）:**

要件 1 — Active な GameObject 上の Disabled な ScriptComponent

- Scene 開始時に `Awake` が 1 回呼ばれること
- 同時に `OnEnable` / `Start` が呼ばれないこと
- 後から Enabled にしたとき `OnEnable` → `Start` の順で 1 回ずつ呼ばれること
- そのとき `Awake` が増えないこと

要件 2 — Inactive な GameObject 上の ScriptComponent

- Scene 開始時に `Awake` / `OnEnable` / `Start` が **1 つも呼ばれない**こと
- GameObject を Active にしたとき、同じ同期点で `Awake` → `OnEnable` → `Start` の順に走ること
- 親を無効にした階層の子（孫まで）でも同じ結果になること
- **`Awake` していない ScriptComponent を破棄したとき、ユーザーの `OnDestroy` が呼ばれないこと**
- そのとき Backend の生存インスタンス数が増減しないこと

要件 3 — Disable / Enable の反復

- `Disable` → `Enable` を 10 回繰り返して `Awake` が 1 回・`Start` が 1 回のままであること
- `OnDisable` / `OnEnable` がちょうど 10 回ずつ、必ず対で呼ばれること
- GameObject 側の `SetEnabled` で反復しても同じ結果になること
- Component 側と GameObject 側を交互に切り替えても `OnEnable` / `OnDisable` が対を崩さないこと

既存挙動の非退行

- `DeferAwakeUntilObjectActive()` が `false` の Component（既存の全 Component / Behaviour）の挙動が変わっていないこと
- `--validate-behaviour`（250-289）が 0 のままであること

その他

- `Awake` の中で GameObject を削除しても壊れないこと
- `Update` の中で Component を削除しても壊れないこと
- **`execution_order` が同じとき、GameObject 順 × Component 順で常に安定すること**（Play/Stop を 100 回繰り返して順序が不変）
- Play / Stop 100 回反復
- 1 つのスクリプトのエラーが他へ波及しないこと

#### `--validate-script-serialization`（終了コード帯 740-799）

- 静的 4 プロパティの往復（`__script.*`）
- **Script Type ごとに異なる Field が保存・復元されること**（RotatingObject と DoorController を同じ Scene に置く）
- 全対応型の往復（float / bool / GameObject 参照）
- **型が一時的に解決不能なときに Field 値が失われないこと**
  1. 値を設定して保存
  2. `SetTypeResolvable(false)` で解決不能にする
  3. 読み込む → `pending_values_` へ入る
  4. **もう一度保存する → ファイルの内容が 1 バイトも変わらない**
  5. `SetTypeResolvable(true)` で再解決
  6. 同期点を通す → 値が `field_values_` へ復元される
- Clone で Schema と Field 値の両方が引き継がれること
- Undo / Redo で Field 値が戻ること
- Scene version が **v11 のまま**であること
- Field を追加・削除した Schema へ差し替えたとき、一致する名前の値が保たれること
- 型を変えた Schema へ差し替えたとき、`ConvertTo` で寄せられるものは寄り、無理なものは既定値になり、元の値が `pending_values_` に残ること

### 8.5 Phase 1 の完成条件

1. Add Component から `Script` を追加でき、Inspector に Language / Script / Class / Execution Order が出る
2. Mock Script Type を選ぶと、その型の Field だけが Inspector へ出る（接頭辞は表示されない）
3. RotatingObject と DoorController で表示される Field が異なる
4. 同じ Script Type の 100 インスタンスが Schema を共有している
5. Inspector で編集した値が Scene へ保存され、再読み込みで復元される
6. Clone / Undo / Redo が通る
7. Play / Stop を 100 回繰り返しても壊れない
8. Play 開始時、ユーザーの `Awake` より前に ScriptWorld が用意されている
9. Play 停止時、managed / Lua インスタンスが 0 になる
10. 型が解決不能でも Field 値が保存され、再解決で復元される
11. `--validate-script-core` / `--validate-script-lifecycle` / `--validate-script-serialization` が終了コード 0
12. 既存 12 種の Validation が全て 0 のまま
13. Debug x64 / Release x64 の両方でビルドが通る

**Lua も C# も一切実装しません。**

### 8.6 Phase 2 以降

| Phase | 内容 | 完成確認 |
|---|---|---|
| 2 | Lua 5.4 組み込み。Play セッションに 1 State、インスタンスごとに環境テーブル。全呼び出しを `lua_pcall` で保護 | Lua で Cube を回す |
| 3 | `nethost` / `hostfxr` / Native API Table / Managed Entry Points | C++ から C# の静的関数を呼んでログが出る |
| 4 | `Managed/` の 3 プロジェクト。`ScriptBehaviour`。`dotnet build` 連携 | C# で Cube を回す |
| 5 | Lua ホットリロード / `AssemblyLoadContext` リロード / 最後に成功したバージョンの維持 | ビルド失敗時に旧版が動き続ける |
| 6 | **Add Component へスクリプト名を並べる** / ドラッグ&ドロップ / Inspector ヘッダー名 / New・Open・Build Scripts / Error Panel | 4.10 節の完成像 |
| 7 | `IInputService` / `Transform.Translate`・`Rotate` / `Time.ElapsedTime` / `GameObject.Find` | 指示書 10 章の API がそろう |
| 8 | 大量インスタンス / 反復 Play・Stop / 反復リロード / Shutdown / D3D11 Live Object | 指示書 19 章の完成条件 |
| 別件 | `Scene::Update` の実行順ソート（`BehaviourComponent` も同時に解決） | execution_order が実際に効く |

**Phase 2 の未決事項:** Lua 5.4 ソースの入手方法。`lua.org` の `lua-5.4.x.tar.gz` を大地さんが `lua-5.4.x/` へ配置していただくのが確実です。Phase 2 着手時に改めて相談します。

**Phase 3 の未決事項:** framework-dependent deployment のため、実行環境に .NET ランタイムが必要になります。配布形態への影響を Phase 3 着手前に確認してください。

---

## 9. 検証計画

### 9.1 サンドボックス側（Linux + g++ 11.4）

**現状の制約（実確認）:** MSVC / Windows SDK / DirectX / .NET SDK / cmake は無い。g++ 11.4.0 はある。

`HANDOVER_FINAL_RESULT.md` 11 章によると、前回作業では Linux で実際にビルドして 530 検査を実行し、ASan + LSan でも全件 PASS させています（**推測**: 同じ方式が再現できる。当時のシム一式はリポジトリに残っていません）。

**Phase 1 で必要なシム:**

| 対象 | 必要性 |
|---|---|
| `DirectXMath.h`（`XMFLOAT2/3/4` ほか） | `PropertyValue` が使うため**必須** |
| `windows.h` 系 | `framework` は検証対象外にするため不要 |
| ImGui | Phase 1 の Validation は Inspector を描かないため不要 |

**やること:**

1. `DirectXMath` の最小シムを作る（サンドボックス側の一時領域。**リポジトリには置かない**）
2. `RePlayEngine/Reflection` / `Object` / `Scene` / `Runtime` / `Scripting` を実際にコンパイル
3. Validation 3 種を実行し、終了コードを確認
4. ASan + LSan で再実行
5. 既存の `--validate-serialization` / `--validate-behaviour` / `--validate-runtime-scene` などを同じハーネスで再実行し、退行が無いことを確認
6. 触った全ファイルへ `-fsyntax-only -Wall -Wextra -std=c++17` で警告 0

**シムはリポジトリへコミットしません。** MSVC ビルドに影響を与えないためです。

### 9.2 Windows 実機（大地さん側）

```bat
start /wait "" x64\Debug\3dgp.exe --validate-script-core
echo ExitCode=%ERRORLEVEL%
```

- Debug x64 / Release x64 のビルド
- 新規 3 コマンド + 既存 12 コマンドの終了コード
- D3D11 Live Object Report
- Editor UI の手動操作（Add Component → Script → Mock Type 選択 → 値編集 → 保存 → 再起動 → 復元）

**`start /wait` を使わないと `%ERRORLEVEL%` が常に 0 になります**（SubSystem が Windows のため。`HANDOVER_FINAL_RESULT.md` 2 章で実際に一度この落とし穴に嵌まっています）。

### 9.3 静的検査

- `.vcxproj` / `.filters` の登録集合一致（ClCompile / ClInclude それぞれ）
- 重複登録 0 / 実体欠落 0 / XML 妥当
- Runtime → Editor の逆依存 0
- Runtime → Scripting の逆依存 0（`IWorldLifecycleListener` は Runtime 側に置くため成立する）
- Engine → Game の逆依存 0
- Scripting → imgui の参照 0
- 新規・変更ファイルの CRLF / 末尾改行 / BOM の維持
- `git diff --check`

---

## 10. 確定事項一覧

初版で「判断事項」としていた 10 件は、すべて確定しました。

| # | 項目 | 確定内容 |
|---|---|---|
| 1 | 動的プロパティ | **案 A**。`Component::DynamicProperties()` を追加。Inspector / Serialize / Deserialize / Clone / Undo は既存経路。**Schema は `ScriptTypeID` ごとにキャッシュして共有**し、インスタンスごとに `PropertyDesc` を作らない。Schema 差し替えはメインスレッドの安全な同期点のみ |
| 2 | 保存形式 | JSON は新設しない。既存 `.replayscene` の PROPERTY 行。**Scene version は v11 のまま** |
| 3 | 更新経路 | `ScriptRuntime` 独自ループなし。`Scene::Update` → `ScriptComponent::OnUpdate` → Backend Callback。FixedUpdate / LateUpdate / OnEnable / OnDisable / OnRuntimeDestroy も同様 |
| 4 | ScriptValue | `Reflection::PropertyValue` を使う。`ScriptFieldStorage` は `PropertyBag`。`ScriptFieldMetadata` は `PropertyDesc` を基礎とした薄い Schema |
| 5 | ディレクトリ | `RePlayEngine/Scripting/` / `RePlayEngine/Editor/Scripting/` / `resources/Scripts/` / `Saved/Scripts/` / `Managed/`。`Source/scripting` / `Assets/Scripts` / `Intermediate/Scripts` は新設しない |
| 6 | execution_order | Phase 1 は保存・読み込み・Inspector 表示まで。ソートは分離。**同一順序時の安定性は Validation で確認** |
| 7 | Win32 | Debug x64 / Release x64 のみ正式対象。Win32 のために設計を複雑化しない。既存 Win32 構成は削除しない |
| 8 | フィールド名の衝突 | **予約接頭辞**。`__script.*`（管理情報）と `field.*`（ユーザー Field）。Inspector では接頭辞を表示しない |
| 9 | IBehaviourProvider | 変更せず Native Behaviour 用として残す。`IScriptBackend` は実行 Backend として新設。**Component の所有には関与しない**。Backend が持つのは Lua / C# インスタンスと整数 Handle だけ |
| 10 | TypeGUID / ScriptTypeID | `ScriptComponent` に固定 TypeGUID を 1 つ発行。`ScriptTypeID` は `Reflection::TypeGUID` の別名。**Lua は AssetGUID そのもの、C# は Asset GUID + 完全修飾クラス名の FNV-1a 128**。Add Component 表示 / Schema / Reload / エラー情報に使う。第二の ObjectID / ComponentTypeID にはしない |
| 11 | Callback 名 | `Awake` / `OnEnable` / `Start` / `FixedUpdate` / `Update` / `LateUpdate` / `OnDisable` / `OnDestroy`。**`OnCreate` は不採用** |
| 12 | 完成像 | 内部は共通 `ScriptComponent`。ただし **Add Component にはスクリプト名で並べる**（Phase 6）。Phase 1 は `ScriptTypeCatalog` / `ScriptFieldSchema` の構造を用意するところまで |
| 13 | Play 開始順序 | **`RuntimeSceneService` へ `IWorldLifecycleListener` を追加**。`Scene::Start()` の直前で Play Session を準備。framework から `ScriptComponent` を直接走査しない |
| 14 | Enable / Disable ライフサイクル | 要件 1・3 は既存基盤がそのまま満たすので**変更しない**。要件 2（Inactive な GameObject）だけ差があるため、`Component::DeferAwakeUntilObjectActive()` を追加して `SyncEnableState()` の Awake ゲートをオプトインで拡張する。既定 `false` なので**既存 Component / Behaviour の挙動は変わらない**。併せて `ScriptComponent::OnRuntimeDestroy()` に「インスタンスがある場合だけユーザーの `OnDestroy` を呼ぶ」ガードを入れる（4.11 節） |

---

## 11. 推測と未確認の一覧（改訂 2 で更新）

### 実確認へ昇格したもの（改訂 3）

1. **`Component::SyncEnableState()` の全分岐** — `OnRuntimeAwake` が `desired` を見ずに無条件で呼ばれること
2. **`GameObject::SyncComponentStates()` / `CompactComponents()` / `DetachAllComponents()`** の実装（299-352 行）
3. **`GameObject::ActiveInHierarchy()`** が祖先を `maximum_hierarchy_depth` まで遡ること（63-74 行）
4. **`BehaviourValidation.cpp:154-227` が守っている範囲** — `disabled_probe` は Component の無効化のみで、GameObject は有効なまま。Inactive な GameObject 上の Awake は検査されていない
5. **`SynchronizeStates()` の呼び出し元が `Scene::Start()`（151 行）と `Scene::Update()`（175 行）の 2 か所だけ**であること

### 実確認へ昇格したもの（改訂 2）

1. **`RuntimeSceneService::SwapWorlds()` の呼び出し順** — `Clear()` → `std::move` → `Rebind` → `Start()`（318-403 行）。**初版で「推測」としていた `BeginPlay` の位置が誤りだったことが判明**
2. **`ResetToEmptyWorld()` の順序** — `SwapWorlds` と同じ並び
3. **`RuntimeSceneService::ActiveSceneGuid()` の存在** — `Scene.CurrentScene` の供給元がある
4. **`TypeGUID` の詳細** — 128bit POD、`std::hash` 特殊化あり、表記が AssetGUID と同じ 32 桁 16 進

### 推測（コードから読み取った解釈。実行して確かめていない）

1. `ScriptRuntime::Initialize()` の適切な位置は `initialize_runtime_services()` の直後（5.1 節）
2. Win32 構成は現時点で既にビルドが通らない（6.6 節）
3. 前回作業の Linux ビルドハーネスは再現可能（9.1 節）
4. `Core/Math/Transform.h` に `Rotate` 相当が無い（4.6 節。ヘッダを開いていない）

### 未確認（Phase 1 着手時に確かめる）

1. `framework::uninitialize()` の正確な破棄順序（3 章。`ScriptRuntime` を挿す位置の決定に必要）
2. `Core/Math/Transform.h` の全 API
3. `Runtime/Handles/RuntimeHandles.h` / `HandleResolver.h` の詳細
4. `AddComponentPanel::Draw` の実装詳細（Phase 6 で Catalog を合流させる際に必要）
6. Asset Browser が `resources/` をどうスキャンして拡張子を種別へ割り当てているか（Phase 6）
7. `Editor/Validation/EditorIntegrationValidation.cpp` の中身（ヘッダの規約のみ確認）
8. Lua 5.4 ソースをサンドボックス側で入手できるか（Phase 2）

### 実確認済み（本報告書の主張の根拠）

`Component.h` / `ComponentTypeID.h` / `ComponentTypeInfo.h` / `ComponentRegistry.h` / `MissingComponent.h` / `PropertyValue.h` / `PropertyValue.cpp`（型名表） / `PropertyDesc.h` / `PropertyBag.h` / `References.h` / `PropertyRegistry.h` / `PropertyRegistry.cpp` / `TypeGUID.h` / `Scene.h` / `Scene.cpp`（該当関数） / `SceneData.h` / `SceneData.cpp`（該当関数） / `SceneSerializer.h` / `SceneSerializer.cpp`（該当関数） / `ObjectID.h` / `RuntimeIdentity.h` / `EditorContext.h` / `SceneEditHistory.h` / `InspectorPanel.h` / `InspectorPanel.cpp`（`DrawComponent`） / `PropertyDrawer.h` / `PropertyDrawer.cpp`（該当箇所） / `AddComponentPanel.h` / `BehaviourComponent.h` / `BehaviourRegistry.h` / `RuntimeContext.h` / **`RuntimeSceneService.h`** / **`RuntimeSceneService.cpp`（`SwapWorlds` / `ResetToEmptyWorld`）** / `AssetDatabase.h` / `AssetCache.h` / `main.cpp`（Validation 分岐） / `BehaviourValidation.h` / `BehaviourValidation.cpp`（`Checker`） / `EditorIntegrationValidation.h` / `framework.h`（メンバ宣言） / `framework_gameobject_scene.cpp`（Play/Edit / update / 登録） / `game_input.h` / `3dgp.vcxproj` / `3dgp.vcxproj.filters` / `resources/AssetDatabase.replaydb`

---

## 12. まとめ

- 既存基盤は**そのまま使える部分が非常に多い**。`PropertyValue` / `PropertyDesc` / `PropertyRegistry` / `MissingComponent` / `TypeGUID` / Scene の遅延操作は、スクリプト機能のために設計されたと言ってよい状態にある
- **本当に足りないのは 4 つ**: インスタンス単位の動的プロパティ経路（4.1）、`AssetKind::Script`（4.2）、Input API（4.5）、**World 入れ替え時の付随状態フック（5.2 / 6.8）**
- **5.2 節の順序問題は、指示書にも初版報告書にも無かった項目**。大地さんのご指摘を受けて実コードを読み、`SwapWorlds()` の 381 行で `Scene::Start()` が走ることを確認して確定した。framework 側のフックだけでは**ゲーム中の Scene 遷移で漏れる**
- 判断事項 13 件はすべて確定（10 章）。Phase 1 は**新規 24 ファイル / 変更 13 ファイル / 削除 0**。Lua も C# も含まない
- Phase 1 の Mock 検証では、Field 構成の異なる 2 種類（RotatingObject / DoorController）で「Schema の共有」「型ごとに異なる Field」「解決不能時の値保護」「再解決後の復元」を確かめる

**次のセッションで Phase 1 を開始してよいか、ご承認をお願いします。**

以上。
