# Phase 8A〜8D / Phase 9 実装報告

作業ブランチ: `sinotake`（切り替えていません）
HEAD: `018dbf4ae324257381f9b657e8f48c74dcc61973`（変更していません）
コミット: していません

前回報告（Phase 6 / 7）: `HANDOVER_Phase6-7_RESULT.md`

---

## 1. 完了概要

| Phase | 状態 | 内容 |
|---|---|---|
| 8A | **完了** | Runtime World の所有権を RuntimeSceneService へ統一。framework の `object_scene_runtime` を撤去 |
| 8B | **完了** | ProjectSettings → Startup Scene → SceneFlowService → Swap → Start の起動経路を接続 |
| 8C | **完了** | Startup Scene UI / Add Component / Inspector（ComponentReference・Array・SceneReference）/ Missing Component 表示 / Runtime 診断 / Play Mode |
| 8D | **完了** | `--validate-editor-integration`（520-579、60 検査） |
| 9 | **完了（MSVC を除く）** | 全 12 種の Validation と `--validate-stress`（580-604、25 検査）。g++ と ASan で実行済み |

---

## 2. Phase 8A — World 所有権の統一

### 調査結果

`object_scene` / `object_scene_runtime` の参照は全 108 箇所でしたが、
そのうち **`object_scene_runtime` を直接触っていたのは 8 箇所だけ**でした。
残りは `active_object_scene()` という既存のアクセサ経由です。

用途ごとの内訳:

| 用途 | 参照経路 | 対応 |
|---|---|---|
| GameObject 更新 | `active_object_scene()` | アクセサの中身を差し替えるだけ |
| Render 収集 | `active_object_scene()` | 同上 |
| Collision World | `attach_collision_world(Scene&)` | 引数で受け取る形。World 入れ替え時に張り直し |
| RuntimeContext | 新設 | `RuntimeSceneService::ActiveWorld()` へ結線 |
| Behaviour Lifecycle | `Scene::Start()` / `Clear()` | Swap 経路が呼ぶ |
| Editor の Runtime 診断 | 新設 | 読み取り専用 API のみ |
| Play Mode 中の Scene 参照 | `active_object_scene()` | 同上 |

### 変更

```
- ReplayEngine::Scene::Scene              object_scene_runtime;   ← 削除
+ ReplayEngine::Runtime::RuntimeSceneService object_runtime_scenes;
+ std::unique_ptr<ReplayEngine::Runtime::RuntimeContext> object_runtime_context;
+ std::unique_ptr<ReplayEngine::Runtime::SceneFlowService> object_scene_flow;
+ ReplayEngine::Runtime::CollisionEventDispatcher object_collision_events;
+ scene_asset_resolver        object_scene_resolver{ *this };
+ runtime_prefab_instantiator object_prefab_instantiator{ *this };
+ ReplayEngine::Core::WorldInstanceID object_bound_world_instance;
+ bool object_runtime_world_active;
```

`grep -rn "object_scene_runtime"` の結果は **コメント 1 行のみ**（撤去の経緯説明）。
名前だけ残した二重 World はありません。

### Scene へムーブを足していないこと

```
Scene(Scene&&) = delete;
Scene& operator=(Scene&&) = delete;
```

削除指定のままです。値メンバのまま差し替えるためにムーブを足す、という
逃げ道は使っていません。代わりに **所有そのものを unique_ptr 側へ寄せて**います。

### 「編集中の Scene をそのまま実行する」経路

Play 開始は AssetGUID もファイルも経由できない（未保存の Scene がある）ため、
`RuntimeSceneService::RequestAdopt(SceneData, source_guid)` を追加しました。

これが無いと framework が自分で Scene を組み立てて所有することになり、
所有者が 2 つに割れます。読み込み元が違うだけで、Staging 構築・入れ替え・
通知・診断はファイル経由とまったく同じ経路を通ります。

あわせて `ResetToEmptyWorld()`（Play 停止）、`HasActiveWorld()`、
`ActiveSceneGuid()` を追加しました。

### 生の Scene ポインタを溜めていないこと

framework のメンバに `ReplayEngine::Scene::Scene*` はありません
（`GameScene*` は別物のアプリ層クラス）。

World の実体番号が変わったフレームだけ `rebind_runtime_world_if_changed()` が
結線し直します。検査した保持先と対応:

| 保持先 | 保持しているもの | 切替時の扱い |
|---|---|---|
| Renderer | 毎フレーム `active_object_scene()` から収集 | 溜めない |
| EditorSelection | `ObjectID` のみ | `ResetSceneState()` で番号ごと破棄 |
| Inspector | `EditorContext::GetScene()` を毎回参照 | `AttachScene()` で差し替え |
| Hierarchy | 同上 | 同上 |
| Collision | `SceneCollisionWorld::AttachScene()` | 張り替えで登録表と接触ペアを破棄 |
| Camera | `CameraBasisProvider`（Scene 非依存） | 影響なし |
| Player 制御 | `ObjectID` のみ | 新 World の設定から取り直し |
| RuntimeContext | `world_` を `Rebind()` で更新 | 旧 World は触らない |
| SceneServices | 非所有ポインタ | Swap 前に `SetRuntime(nullptr)` |
| Deferred Operation | `RuntimeContext::pending_instantiations_` | `Rebind()` で破棄 |
| Debug Draw | 毎フレーム作り直し | 溜めない |

**EditorSelection について重要な発見**（実行して判明）:
`PruneMissing()` は「今の Scene に無い ObjectID」しか外しません。
World をまたぐと同じ ObjectID が別 World にも存在しうるため、
`PruneMissing()` だけでは選択が**無関係な GameObject を指したまま残ります**。
そのため World 入れ替え時は `ResetSceneState()` で番号ごと捨てています。
この事実は `--validate-editor-integration` の検査とコメントに残しました。

---

## 3. Phase 8B — 起動経路の接続

```
framework::initialize_object_scene()
  1. RegisterBuiltInComponents() / RegisterGameBehaviours()
  2. load_project_settings()
  3. initialize_runtime_services()
       RuntimeContext / SceneFlowService を生成し、
       Resolver・PrefabInstantiator・CollisionDispatcher を結線
  4. if (!editor_mode) begin_startup_scene()

framework::begin_startup_scene()
  1. object_runtime_world_active = true   ← ここから Runtime World が本番
  2. StartupSceneGuid() を取得
       空          -> BeginStartupScene("") で NotConfigured、Blocked にする
       未解決/型違い -> Blocked にする（GUID は保持）
  3. SceneFlowService::BeginStartupScene(guid)

framework::update_object_scene()  ← フレーム先頭
  1. tick_runtime_scene_flow()
       SceneFlowService::Tick() だけを呼ぶ
       （内部で RuntimeSceneService::Tick() が走る。二重 Tick はしない）
       Startup の Failed / Ready を拾って Blocked を更新
       QuitRequested を受け取ってアプリ終了要求へ変換
       rebind_runtime_world_if_changed()
  2. RuntimeTime を更新
  3. refresh_object_scene_services()
  4. Update -> FixedUpdate -> LateUpdate -> Trigger -> Collision 配送
     -> EventBus::Dispatch -> FlushDeferredOperations
```

### 診断状態

- 空 GUID / 未解決 GUID / 読み込み失敗のいずれでも、**別の Scene は読みません**。
- `object_runtime_blocked` と理由文字列が残り、Runtime 診断パネルに出ます。
- Editor は落ちません。止まるのは Runtime の開始だけです。
- 非 Editor 起動でも編集 Scene へフォールバックしません。空の World のまま
  止まり、理由が残ります（無言で遊べてしまう状態を作らない）。

### 実装レビューで見つけて直した不具合

最初の実装では `active_object_scene()` が `object_scene_play_mode` を見ていました。
Editor を出さない通常起動では Play Mode に入らないため、
**起動した Scene ではなく編集 Scene を更新してしまう**状態でした。
`object_runtime_world_active` という明示フラグを導入して直しています。

### Trigger からの遷移

`SceneTransitionBehaviour::OnTriggerEnter` は要求を出すだけです。
実際の Swap は次フレーム先頭の `tick_runtime_scene_flow()` で起きます。
Trigger 走査中に World が消えることは構造的にありません。

---

## 4. Phase 8C — Editor 統合

### 1. Project Settings（Startup Scene）

`Source/app/Editor/framework_project_settings.cpp`

- `AssetKind::Scene` の Asset だけを候補に出す（Texture 等は出ない）
- AssetGUID で保存（Scene 名もパスも焼き込まない）
- 解決できないときは `[ Missing Scene ]` を赤字表示し、**GUID は保持**
- 「Startup Scene を解除」ボタン
- 保存失敗は `project_settings_status` に理由が出る

### 2. Add Component

`RePlayEngine/Editor/ComponentBrowser/AddComponentPanel.cpp`

既に `ComponentRegistry` を列挙する作りだったため、Behaviour は
`ComponentRegistry::Register<T>(...).WithTypeGUID(...).InModule(...)` の登録だけで
自動的に一覧へ出ます。**型ごとの if / switch は 1 つも足していません。**

追加したのは、`BehaviourRegistry::Find(type_id)` で引いた供給元
（Native / 将来の Managed）と Type GUID / Module をツールチップへ出す部分だけです。
`CanInstantiate()` が false の型には「現在この型は生成できません」と出ます。

### 3. Inspector

`RePlayEngine/Editor/Inspector/PropertyDrawer.cpp`

| 型 | 状態 |
|---|---|
| Bool / Integer / Float / String / Enum / Color | 既存のまま編集可能 |
| ObjectReference | 既存のまま編集可能 |
| **SceneReference** | Scene Asset だけを候補に出すよう絞り込みを追加 |
| AssetReference | 既存（種類フィルタ対応を追加） |
| **ComponentReference** | GameObject 選択 → その中の Component 一覧 → StableID で保存、まで完成 |
| **Array** | 追加・削除・並べ替え・要素編集を実装 |

ComponentReference の詳細:
- 所有 GameObject を変えたら Component 指定は外す（別 GameObject の同番号は無関係）
- `expected_component_type` が宣言されていればその型だけを候補に出す
- 参照先が見つからなくても**値は消さない**。「見つかりません（値は保持しています）」と出す

Array の詳細:
- Reflection 側（`PropertyDesc::CaptureMember` / `ApplyMember`）が `std::vector<T>` の
  取り込みと書き戻しに対応済みなので、正式対応として実装しました
- 1 操作 = 配列まるごと 1 回の Apply。途中の状態を書き戻さないので要素が失われない
- 要素編集は Bool/Int/Enum/Float/Double/String/AssetPath/Vector3/Color/
  ObjectReference/AssetReference/SceneReference に対応
- **未対応の型は編集欄を出さず**、型名と「編集欄は未対応。値は保持されます」と表示
- 既定値を作れない要素型では追加ボタンを出さず、その理由を書く

### 4. Missing Behaviour / Missing Component

`RePlayEngine/Editor/Inspector/InspectorPanel.cpp` に
`DrawMissingComponentDetails()` を追加。

表示するもの:
- 元の型名 / Type GUID / Module ID / Type Version
- 保持しているプロパティ件数
- Serialized Property の一覧（**読み取り専用**）
  - ObjectReference は指していた ObjectID
  - ComponentReference は ObjectID と StableID
  - Asset/SceneReference は GUID
  - Array は要素型と件数

編集欄は作りません。型が分からない状態で値を書き換えると、型が戻ったときに
解釈できなくなるためです。削除は「この Missing Component を削除」ボタンで
明示的に押したときだけで、その旨も画面に書いてあります。

### 5. Runtime 診断

`framework::draw_runtime_diagnostics_panel()`（`framework_runtime_scene.cpp`）

表示項目: World 実体番号 / World の所有者 / GameObject 数 /
Active Scene GUID / Pending Scene GUID / RuntimeSceneService State /
SceneFlow State / Startup State / Startup Scene / StartupBlocked /
Runtime Blocked と理由 / QuitRequested / Play・Edit Mode / Last Error /
履歴件数と戻れるか / 直近の読み込み統計 / Load・Swap・失敗回数 / Behaviour 登録数

Runtime から Editor への参照は **0 件**（後述の検査で確認）。
Editor 側が読み取り専用 API を呼ぶだけです。

### 6. Play Mode

| 要件 | 実装 |
|---|---|
| Runtime 開始前の Editor Scene を保持 | `object_scene` は一切触らない |
| Play 中は ActiveWorld を使用 | `object_runtime_world_active` で切り替え |
| 切替時に Selection を無効化 | `AttachScene()` + `ResetSceneState()` |
| Play 終了時に Editor Scene 復元 | `ResetToEmptyWorld()` → `AttachScene(&object_scene)` |
| Runtime 変更を暗黙保存しない | 書き戻し経路そのものを置いていない |
| Undo へ Runtime 操作を混ぜない | Play 開始・終了の両方で `ResetSceneState()` |
| Runtime Prefab 生成物を残さない | Runtime World ごと破棄される |

---

## 5. Phase 8D — Editor Integration Validation

`--validate-editor-integration`（**520-579、60 検査**）

置き場所は `RePlayEngine/Editor/Validation/`、名前空間は
`ReplayEngine::Editor::Validation` です。Runtime 側に置くと
Runtime → Editor の逆依存になるため、Editor モジュールが持ちます。

### ImGui 操作の扱い

ボタン操作の自動化には Window と D3D11 が要るためヘッドレスに載りません。
**UI が呼ぶのと同じ内部 API を直接叩いて**、データが壊れないことと
状態遷移が正しいことを検査します。UI の描画そのものは手動確認手順（19 章）へ回しました。
この分け方なら UI を作り替えても検査は生き残ります。

### 検査内容（60 件）

ProjectSettings v2 往復 / Startup Scene 変更 / Clear / Missing 保持 /
Unset と Missing の区別 / BehaviourRegistry からの列挙 / Native Provider /
CanInstantiate / Registry メタデータ（表示名・カテゴリ・説明・Module）/
ComponentRegistry と BehaviourRegistry の TypeGUID 一致 /
ComponentTypeID だけでの Behaviour 追加 / StableID 採番 /
Property の保存・再読込 / 配列プロパティの往復 / TypeGUID 保存 / Module ID 保存 /
ObjectReference 解決 / ComponentReference の StableID 解決 / SceneReference /
Missing Component の型名・Module・Version・GUID・PropertyBag 保持 /
Missing の表示文字列 / 往復で内容が失われないこと /
ファイルに MissingComponent の痕跡を残さないこと / Unknown Property 往復 /
Prefab 内 Behaviour / 2 回配置で ObjectID が衝突しないこと /
EditorContext の Selection と Undo / ResetSceneState /
Play 開始（RequestAdopt）/ 編集 Scene と別実体であること /
Runtime の変更が編集 Scene へ戻らないこと / Runtime 生成物が残らないこと /
Play 中の編集禁止 / Undo 履歴の分離 / Runtime Scene 切替 /
旧 Selection の無効化 / PruneMissing の限界 / Play 終了と Editor Scene 復元 /
Startup Scene 未設定と正常起動 / World 実体番号の一意性 /
Runtime World が Editor を参照しないこと

---

## 6. Phase 9 — 最終 QA

### 追加した耐久検査

`--validate-stress`（**580-604、25 検査**）

実行時の実績値:

```
Stress validation: swaps=290 failures=120 awake=1068 destroy=1066 live=2
Stress validation OK: 25 checks passed
```

| 検査 | 実測 |
|---|---|
| Scene Load / Reload | **120 回**（3 回に 1 回は Reload 経路） |
| 連続した失敗 Load | **120 回**（壊れた Scene と存在しない GUID を交互に） |
| GameObject 数 | **1200 体**の Scene を読み込み、そこから切り替え |
| World 入れ替え回数 | **290 回** |
| Play 開始／停止の反復 | **40 回** |
| SceneFlow 経由の遷移 | **120 回** + 戻る操作 8 回 |

毎周回で確認していること:
- Event 購読が 0 件（周回ごとに購読を積んで、入れ替えで消えることを確認）
- Collision 接触状態が 0 件
- 削除予約中の切り替え（5 回に 1 回混ぜている）
- 切り替え前に取った ObjectHandle がすべて無効
- World 実体番号が毎回変わる（実体の使い回しが無い）
- **Awake 総数 = Destroy 総数 + 生存数**（Behaviour の取りこぼしが無い）
- 失敗のたびに World 実体番号と GameObject 数が不変

### 全 Validation の実行結果

Linux + g++ 11.4 上で実際にビルドして実行しました。**MSVC ではありません。**

```
handles              PASS  53 checks
serialization        PASS  65 checks
missing              PASS  35 checks
scene-version        PASS  71 checks
behaviour            PASS  28 checks
events               PASS  20 checks
runtime-api          PASS  32 checks
collision            PASS  33 checks
runtime-scene        PASS  48 checks
scene-flow           PASS  48 + 12 checks
editor-integration   PASS  60 checks
stress               PASS  25 checks
                     ---------------
                     合計 530 checks
```

### AddressSanitizer + LeakSanitizer

`-fsanitize=address` / `ASAN_OPTIONS=detect_leaks=1` で上記 12 種すべてを再実行し、
**全件 PASS**。メモリエラー 0、リーク 0。

### 静的検査

| 項目 | 結果 |
|---|---|
| g++ `-fsyntax-only -Wall -Wextra -std=c++17` | 触った Engine / Editor / Game 側の全ファイルで警告 0・エラー 0 |
| vcxproj / filters 登録集合の一致 | ClCompile 163/163、ClInclude 177/177 |
| 重複登録 | 0 |
| 実体欠落 | 0 |
| XML 妥当性（`xmllint --noout`） | 両方 OK |
| Runtime → Editor 逆依存 | **0 件** |
| Engine → Game 逆依存 | **0 件** |
| Runtime からの imgui 参照 | **0 件** |
| Scene の二重所有 | **0 件**（`object_scene_runtime` は完全撤去） |
| Scene のムーブ追加 | **無し**（`= delete` のまま） |
| 生 Scene* の長期キャッシュ | **0 件** |
| 改行 / 文字コード | 34 ファイルすべて CRLF・末尾改行あり。BOM は元の有無を維持 |
| `git diff --check` | 空白エラー 0 |

### 検証で見つけて直した不具合（今回分）

1. **非 Editor 起動で編集 Scene が動いてしまう**（実装レビューで発見）
   `active_object_scene()` が Play Mode フラグを見ていたため、
   Startup Scene を読み込んでも編集 Scene が更新対象のままでした。
   `object_runtime_world_active` を導入して修正。

2. **Prefab の ObjectID 検査が誤り**（実行して発見）
   「元の ObjectID と違うこと」で確かめていましたが、まっさらな Scene では
   採番が 1 から始まるため、採番し直していても同じ番号になります。
   2 回配置して衝突しないことを見る検査へ修正。

3. **Selection の PruneMissing に対する誤解**（実行して発見）
   「新しい World に居ない GameObject の選択は自動的に外れる」と検査していましたが、
   同じ ObjectID が新 World にも存在すると外れません。
   これは PruneMissing の仕様どおりで、**World 跨ぎでは ResetSceneState が必須**
   という設計上の理由そのものでした。検査を実態に合わせ、理由をコメントへ残しました。

4. `framework.h` の `<cstdint>` / `<memory>` / `<string>` を明示 include
   （推移的 include に頼っていた）。

---

## 7. Scene / ProjectSettings の Version

| 形式 | 変更前 | 変更後 |
|---|---|---|
| Scene（`.replayscene`） | current 11 / minimum 7 | **変更なし** |
| ProjectSettings（`.replayproject`） | current 1 | **current 2 / minimum 1** |

Phase 7 からの追加変更はありません。

---

## 8. Validation 一覧

| コマンド | 終了コード帯 | 検査数 |
|---|---|---|
| `--validate-handles` | 80-139 | 53 |
| `--validate-serialization` | 140-179 | 65 |
| `--validate-missing-component` | 180-209 | 35 |
| `--validate-scene-version` | 210-249 | 71 |
| `--validate-behaviour` | 250-289 | 28 |
| `--validate-events` | 290-329 | 20 |
| `--validate-runtime-api` | 330-369 | 32 |
| `--validate-collision` | 370-409 | 33 |
| `--validate-runtime-scene` | 410-457 | 48 |
| `--validate-scene-flow` | 460-507 / 508-519 | 48 + 12 |
| **`--validate-editor-integration`** | **520-579** | **60** |
| **`--validate-stress`** | **580-604** | **25** |
| `--validate-prefab` | 30-41 | 既存 |
| `--validate-large-scene` | 60-72 | 既存 |
| `--validate-material` | 50-56 | 既存 |
| `--validate-landscape` | 20-28 | 既存 |

---

## 9. 未実施の検証

- **MSVC v145 / Visual Studio 2026 でのビルド**
- **Windows 実機での Validation 実行**
- **Release x64 構成**
- **D3D11 Live Object Report**
- **Windows 上の ASan**
- **Editor（ImGui）の手動操作確認**

とくに `Source/app/` 配下の 5 ファイル（`framework.h`、
`framework_gameobject_scene.cpp`、`framework_runtime_scene.cpp`、
`framework_project_settings.cpp`、`main.cpp`）は
**windows.h / d3d11.h / wrl.h を必要とするため g++ で構文確認できていません**。
代わりに次を手作業で突き合わせています。

- 宣言（framework.h）と定義（各 .cpp）の対応：新規 7 メソッド + 入れ子クラス 2 メソッド、すべて一致
- 参照しているメンバ 17 個すべての宣言確認
- 呼び出している既存 API のシグネチャ確認
- `USE_IMGUI` ガードの整合（全構成で定義されていることも確認済み）

Linux/g++ で通っても MSVC で通る保証はありません。

---

## 10. 既知の制限

- `RuntimeSceneService::Tick()` の末尾で `EventBus::Global().Dispatch(nullptr)` を呼びます。
  自分が積んだ通知を確実に配るためですが、他所が Global Bus へ積んだイベントも
  この同期点で配送されます。
- `SceneFlowService::Tick()` は内部で `RuntimeSceneService::Tick()` を呼びます。
  framework は SceneFlow だけを Tick しており、二重 Tick はしていません。
- Play 開始時だけは `RuntimeSceneService::Tick()` を 2 回続けて呼び、
  構築と入れ替えをその場で済ませます（1 フレームだけ空の World になるのを避けるため）。
- Editor 起動時は Startup Scene を自動で読み込みません。編集中の Scene が
  起動のたびに置き換わったように見えるためです。Play（F5）で初めて Runtime World が作られます。
- `EditorSelection::PruneMissing()` は World 跨ぎの ObjectID 衝突を検出できません。
  World 入れ替え時は `ResetSceneState()` が必須です（コードのコメントにも記載）。
- 一般 RigidBody の衝突は実装していません。Collision で取れるのは
  `CharacterGround` と `CharacterWall` だけです。
- Audio / Input Action / SaveGame / Runtime UI は未実装のまま。`〜Available()` は常に false。
- C# は実装していません。`IBehaviourProvider` / `TypeGUID` / Missing Behaviour 保持 /
  Handle Facade / WorldInstanceID / Runtime API 境界 / Scene 切り替え通知
  （Global Bus の `BeforeSceneUnload` / `WorldChanged`）はすべて拡張点として残してあります。
  `BehaviourRegistry` は Native 専用へ退化させていません。
- `3dgp.vcxproj.filters` に `Source\app\Editor` / `Source\app\Runtime` / `Source\mesh` の
  `<Filter Include>` 宣言がありません（**今回の変更以前から**の大文字小文字の不一致）。
  Visual Studio が自動生成するので実害はなく、無関係な変更を避けるため触っていません。

---

## 11. Windows 実機で実行する Debug x64 ビルドコマンド

```
cd /d C:\Users\2250298\Desktop\teamProject_2_3
```

```
msbuild 3dgp.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m /v:minimal
```

```
echo %ERRORLEVEL%
```

---

## 12. Debug 全 Validation コマンド

1 件ずつ、直後に終了コードを確認してください（0 が合格）。

```
x64\Debug\3dgp.exe --validate-handles
```

```
echo %ERRORLEVEL%
```

```
x64\Debug\3dgp.exe --validate-serialization
```

```
echo %ERRORLEVEL%
```

```
x64\Debug\3dgp.exe --validate-missing-component
```

```
echo %ERRORLEVEL%
```

```
x64\Debug\3dgp.exe --validate-scene-version
```

```
echo %ERRORLEVEL%
```

```
x64\Debug\3dgp.exe --validate-behaviour
```

```
echo %ERRORLEVEL%
```

```
x64\Debug\3dgp.exe --validate-events
```

```
echo %ERRORLEVEL%
```

```
x64\Debug\3dgp.exe --validate-runtime-api
```

```
echo %ERRORLEVEL%
```

```
x64\Debug\3dgp.exe --validate-collision
```

```
echo %ERRORLEVEL%
```

```
x64\Debug\3dgp.exe --validate-runtime-scene
```

```
echo %ERRORLEVEL%
```

```
x64\Debug\3dgp.exe --validate-scene-flow
```

```
echo %ERRORLEVEL%
```

```
x64\Debug\3dgp.exe --validate-editor-integration
```

```
echo %ERRORLEVEL%
```

```
x64\Debug\3dgp.exe --validate-stress
```

```
echo %ERRORLEVEL%
```

```
x64\Debug\3dgp.exe --validate-prefab
```

```
echo %ERRORLEVEL%
```

```
x64\Debug\3dgp.exe --validate-large-scene
```

```
echo %ERRORLEVEL%
```

```
x64\Debug\3dgp.exe --validate-material
```

```
echo %ERRORLEVEL%
```

```
x64\Debug\3dgp.exe --validate-landscape
```

```
echo %ERRORLEVEL%
```

---

## 13. Release x64 Clean Rebuild コマンド

```
cd /d C:\Users\2250298\Desktop\teamProject_2_3
```

```
msbuild 3dgp.sln /t:Clean /p:Configuration=Release /p:Platform=x64 /v:minimal
```

```
echo %ERRORLEVEL%
```

```
msbuild 3dgp.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m /v:minimal
```

```
echo %ERRORLEVEL%
```

---

## 14. Release 全 Validation コマンド

Debug と同じ順序で、パスだけ `x64\Release\3dgp.exe` に読み替えてください。

```
x64\Release\3dgp.exe --validate-handles
```

```
echo %ERRORLEVEL%
```

```
x64\Release\3dgp.exe --validate-serialization
```

```
echo %ERRORLEVEL%
```

```
x64\Release\3dgp.exe --validate-missing-component
```

```
echo %ERRORLEVEL%
```

```
x64\Release\3dgp.exe --validate-scene-version
```

```
echo %ERRORLEVEL%
```

```
x64\Release\3dgp.exe --validate-behaviour
```

```
echo %ERRORLEVEL%
```

```
x64\Release\3dgp.exe --validate-events
```

```
echo %ERRORLEVEL%
```

```
x64\Release\3dgp.exe --validate-runtime-api
```

```
echo %ERRORLEVEL%
```

```
x64\Release\3dgp.exe --validate-collision
```

```
echo %ERRORLEVEL%
```

```
x64\Release\3dgp.exe --validate-runtime-scene
```

```
echo %ERRORLEVEL%
```

```
x64\Release\3dgp.exe --validate-scene-flow
```

```
echo %ERRORLEVEL%
```

```
x64\Release\3dgp.exe --validate-editor-integration
```

```
echo %ERRORLEVEL%
```

```
x64\Release\3dgp.exe --validate-stress
```

```
echo %ERRORLEVEL%
```

```
x64\Release\3dgp.exe --validate-prefab
```

```
echo %ERRORLEVEL%
```

```
x64\Release\3dgp.exe --validate-large-scene
```

```
echo %ERRORLEVEL%
```

```
x64\Release\3dgp.exe --validate-material
```

```
echo %ERRORLEVEL%
```

```
x64\Release\3dgp.exe --validate-landscape
```

```
echo %ERRORLEVEL%
```

---

## 15. 手動確認手順

### A. Startup Scene の設定

1. Editor を起動し、Scene を 1 つ保存する（AssetDatabase へ登録される）
2. プロジェクト設定パネル → Startup Scene のコンボを開く
3. **`.replayscene` だけが候補に出ること**を確認する（モデルやテクスチャが出ないこと）
4. 保存した Scene を選ぶ
5. `resources/Project.replayproject` を開き、次を確認する
   - 1 行目が `REPLAY_PROJECT 2`
   - `STARTUP_SCENE "<GUID>"` が書かれている
6. 「Startup Scene を解除」を押し、`STARTUP_SCENE ""` になることを確認する

### B. Missing Startup Scene

1. Startup Scene を設定したあと、その `.replayscene` を別の場所へ移動する
2. Editor を起動し直し、コンボが赤字で `[ Missing Scene ]` になることを確認する
3. 詳細を開き、**GUID が保持されている**ことを確認する
4. ファイルを元へ戻すと、自動的に元の表示へ戻ることを確認する

### C. Runtime 診断パネル

1. プロジェクト設定パネルの下にある「Runtime 診断」を開く
2. Edit Mode では
   - モード = Edit
   - Active Scene GUID =（未読み込み）
   - GameObject 数 = 0
3. F5 で Play に入り
   - モード = Play
   - GameObject 数が編集 Scene と一致
   - World 実体番号が変わっている
4. F5 で停止し、GameObject 数が 0 へ戻ることを確認する

### D. Play Mode の安全性

1. Editor で GameObject を 1 つ選択する
2. F5 で Play に入り、**選択が解除されている**ことを確認する
3. Play 中に GameObject を動かす／新しく作る
4. F5 で停止し、**Play 中の変更が編集 Scene に残っていない**ことを確認する
5. Undo（Ctrl+Z）を押し、**Play 中の操作が戻ってこない**ことを確認する

### E. Inspector（Phase 8C の追加分）

1. ComponentReference を持つ Component を選び
   - GameObject を選ぶと Component のコンボが出ること
   - Component を選ぶと StableID が表示されること
   - GameObject を変えると Component 指定が外れること
2. 配列プロパティを持つ Component を選び
   - `＋ 要素を追加` / `▲` / `▼` / `削除` が動くこと
   - 保存して開き直しても内容が保たれること
3. SceneReference を持つ Component を選び、**Scene Asset だけが候補に出る**こと

### F. Missing Component

1. Scene を保存する
2. その Scene に含まれる Behaviour の登録を一時的に外したビルドで開く
   （または存在しない型名を含む Scene を用意する）
3. Inspector に元の型名 / Type GUID / Module ID / Type Version /
   Serialized Property が出ることを確認する
4. **編集欄が出ないこと**を確認する
5. そのまま上書き保存し、ファイルの中身が変わっていないことを確認する
6. 型が使えるビルドで開き直し、元の Component として復元されることを確認する

### G. Scene 遷移

1. Scene A に `Scene Transition` Behaviour を持つ Trigger を置き、遷移先を Scene B にする
2. Play で Trigger に入る
3. **そのフレームでは World が入れ替わらず**、次のフレームで切り替わることを確認する
4. Runtime 診断で Active Scene GUID が Scene B になっていることを確認する
5. 選択が解除されていることを確認する

### H. Validation が通常起動へ影響しないこと

1. 引数なしで `3dgp.exe` を起動し、通常どおり Editor が立ち上がることを確認する
2. `Saved/Validation/` 配下に検証用の一時ファイルが作られていないことを確認する
   （Validation を実行したときだけ作られる）

---

## 16. 変更量

### 追跡済みファイルの差分

```
 3dgp.vcxproj                                       |  15 +
 3dgp.vcxproj.filters                               |  48 +++
 RePlayEngine/Editor/ComponentBrowser/AddComponentPanel.cpp |  33 +-
 RePlayEngine/Editor/Inspector/InspectorPanel.cpp   | 128 ++++++-
 RePlayEngine/Editor/Inspector/InspectorPanel.h     |   5 +
 RePlayEngine/Editor/Inspector/PropertyDrawer.cpp   | 400 +++++++++++++++++++--
 RePlayEngine/Project/ProjectSettings.cpp           |  35 ++
 RePlayEngine/Project/ProjectSettings.h             |  49 ++-
 RePlayEngine/Project/ProjectSettingsSerializer.cpp |  41 ++-
 RePlayEngine/Project/ProjectSettingsSerializer.h   |  16 +-
 RePlayEngine/Runtime/API/RuntimeContext.cpp        |  90 ++++-
 RePlayEngine/Runtime/API/RuntimeContext.h          |  66 ++++
 RePlayEngine/Runtime/Validation/BehaviourValidation.cpp |  23 +-
 Source/app/Editor/framework_project_settings.cpp   |  91 +++++
 Source/app/Runtime/framework_gameobject_scene.cpp  | 126 ++++++-
 Source/app/Runtime/main.cpp                        |  50 +++
 Source/app/framework.h                             | 113 +++++-
 Source/game/Behaviours/ValidationBehaviours.cpp    | 323 +++++++++++++++++
 Source/game/Behaviours/ValidationBehaviours.h      |  13 +
 19 files changed, 1608 insertions(+), 57 deletions(-)
```

### 新規ファイル（15 個、合計 4526 行）

```
RePlayEngine/Runtime/Scene/RuntimeSceneService.h / .cpp
RePlayEngine/Runtime/Scene/SceneFlowService.h / .cpp
RePlayEngine/Runtime/Validation/RuntimeSceneValidation.h / .cpp
RePlayEngine/Runtime/Validation/SceneFlowValidation.h / .cpp
RePlayEngine/Runtime/Validation/StressValidation.h / .cpp
RePlayEngine/Editor/Validation/EditorIntegrationValidation.h / .cpp
Source/app/Runtime/framework_runtime_scene.cpp
Source/game/Behaviours/SceneTransitionBehaviour.h / .cpp
```

### 合計

- 新規: **15 ファイル / 4526 行**
- 変更: **19 ファイル / +1608 行 / −57 行**
- 削除: **0 ファイル**
