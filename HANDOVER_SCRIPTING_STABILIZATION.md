# 引継ぎ文書 — スクリプト基盤と安定化作業

対象: `teamProject_2_3`（RePlayEngine / 3dgp）
最終更新: 2026-08-03
先行文書: `HANDOVER_FINAL_RESULT.md`（Phase 6〜9。Runtime Scene / Scene Flow / Editor 統合）

**この文書が入口です。** 詳細は各報告書にあります。

| 文書 | 内容 |
|---|---|
| `SCRIPTING_EXISTING_SYSTEM_AUDIT.md`（改訂 3） | 既存システム調査、確定した設計方針 14 件、Phase 1〜8 計画 |
| `SCRIPTING_PHASE1_RESULT.md` | スクリプト共通基盤の実装結果 |
| `STABILIZATION_PHASE_A1_RESULT.md` | Undo で全 Component が止まるバグの修正 |

---

## 1. 現在の状態（最重要）

### コミットしていません

- ブランチ切り替えなし、コミットなし、`git reset` / `clean` / `restore` なし
- 作業ツリーに未コミットの変更が残っています
- **次のセッションを始める前に `git status` と `git diff` で内容を確認してください**

### Windows 実機での検証が未実施です

Linux g++ でのビルドと実行検証は済んでいますが、**MSVC でのビルドは一度も通していません。** g++ が通っても MSVC 固有のエラーは出ます。

とくに `Source/app/` の 4 ファイル（`framework.h` / `framework_runtime_scene.cpp` / `framework_gameobject_scene.cpp` / `main.cpp`）は Windows / D3D11 依存で Linux ハーネスに載らないため、**目視確認しかしていません。** 最初のビルドでエラーが出るとしたらここです。

---

## 2. 完了したこと

| 作業 | 状態 | 検証 |
|---|---|---|
| Phase 0 — 既存システム調査 | 完了 | — |
| Script Phase 1 — 共通スクリプト基盤 | 完了 | Linux 実行 143 検査 + 既存 337 検査 = 0 |
| Stabilization Phase A-1 — Undo バグ修正 | 完了 | Linux 実行 33 検査 = 0 |

**Lua も C# も一切実装していません。** Backend は Mock だけです。

---

## 3. 未着手（次にやること）

ご指示の順序では、**安定化 Phase A〜F をすべて終えてから** Script Phase 2（Lua）へ進みます。

| # | Phase | 内容 | 状態 |
|---|---|---|---|
| 1 | Stabilization A-2 | CameraComponent の値が適用されない | **未着手** |
| 2 | Stabilization A-3 | Player の設定速度と実速度が一致しない | **未着手** |
| 3 | Stabilization B | リアルタイム編集 / Input Routing | 未着手 |
| 4 | Stabilization C | Camera System / Preset / View Cube / Culling | 未着手 |
| 5 | Stabilization D | Collider / Bounds Debug 表示 | 未着手 |
| 6 | Stabilization E | Editor UI 整理 / 日本語化 / 保存ログ / Docking | 未着手 |
| 7 | Stabilization F | Shader System 責務分離 | 未着手 |
| 8 | Script Phase 2 | Lua 5.4 バックエンド | 未着手 |
| 9 | Script Phase 3〜8 | .NET ホスト以降 | 未着手 |

### A-2 の着手ヒント（未調査の推測を含む）

Phase 0 の調査で分かったこと: `PropertyRegistry::Apply` は最後に `OnPropertyChanged(nullptr)` を呼びますが、**Inspector の編集経路はこれを呼んでいませんでした**（宣言コメントには「Editor がプロパティを書き換えた直後に呼ばれる」とあるのに、実装が追いついていなかった）。

Phase 1 で `InspectorPanel::DrawComponent` の編集確定時に呼ぶよう追加済みです。したがって **CameraComponent 側が `OnPropertyChanged` で Projection を作り直す実装を持てば、A-2 の大半はその経路に乗る見込み**です。

ただし次は未調査です。
- Runtime Camera と Editor Camera が状態変数を共有していないか
- Play Mode の Runtime Clone 側への反映経路
- `CameraTargetComponent` と `Source/game/camera.*` の関係

### A-3 の着手ヒント（未調査）

`PlayerControllerComponent` / `CharacterMotorComponent` / `PlayerInputComponent` の 3 つに速度係数が分散していないかの確認から始めるのが確実です。`Source/game/game_input.h` の `AxisX` / `AxisY` は Normalize していないため、斜め入力が √2 倍になる可能性があります（未確認）。

---

## 4. 確定した設計方針（変更するなら再合意が必要）

大地さんの判断で確定済みです。詳細は `SCRIPTING_EXISTING_SYSTEM_AUDIT.md` の 10 章。

| # | 項目 | 確定内容 |
|---|---|---|
| 1 | 動的プロパティ | `Component::DynamicProperties()`。Schema は `ScriptTypeID` ごとに共有 |
| 2 | 保存形式 | JSON 新設せず。既存 `.replayscene` の PROPERTY 行。**Scene version は v11 のまま** |
| 3 | 更新経路 | `Scene::Update` → `ScriptComponent::OnUpdate` → Backend の 1 本のみ |
| 4 | 値の型 | `Reflection::PropertyValue` / `PropertyBag` を再利用。独自 variant を作らない |
| 5 | ディレクトリ | `RePlayEngine/Scripting/` / `resources/Scripts/` / `Saved/Scripts/` / `Managed/` |
| 6 | execution_order | 保存・表示のみ。ソートは別作業 |
| 7 | 構成 | Debug x64 / Release x64 のみ正式対象。Win32 は対象外（削除もしない） |
| 8 | 名前衝突 | 予約接頭辞 `__script.*` と `field.*` |
| 9 | Backend | `IBehaviourProvider` は触らない。`IScriptBackend` は Component を所有しない |
| 10 | ScriptTypeID | `Reflection::TypeGUID` の別名。Lua は AssetGUID そのもの、C# は Asset+クラス名の FNV-1a 128 |
| 11 | Callback 名 | `Awake` / `OnEnable` / `Start` / `FixedUpdate` / `Update` / `LateUpdate` / `OnDisable` / `OnDestroy`。`OnCreate` は不採用 |
| 12 | 完成像 | Add Component へスクリプト名を並べる（Phase 6）。Phase 1 は Catalog 構造まで |
| 13 | Play 開始順序 | `RuntimeSceneService` の `IWorldLifecycleListener` |
| 14 | Enable/Disable | `DeferAwakeUntilObjectActive()`。既定 `false` で既存は不変 |

---

## 5. 引き継ぐべき「踏むと痛い」知識

コードを読んだだけでは気づきにくい落とし穴です。

### 5-1. `ApplySceneData` を呼んだら必ず `Scene::Start()` を対で呼ぶ

`ApplySceneData` は内部で `Scene::Clear()` を通り、`Clear()` の最終行が `started_ = false` です。`ApplySceneData` 自身は `Start()` を呼びません（`SceneData.h:216` に仕様として明記）。

**呼び忘れると Scene 上の全 Component が二度と更新されません。** A-1 のバグがまさにこれでした。

現在の呼び出し元は 4 か所で、すべて対になっています。

| 呼び出し元 | Start() |
|---|---|
| `framework::load_object_scene` | 直後に `object_scene.Start()` |
| `RuntimeSceneService::BuildStagingWorld` | `SwapWorlds` で `active_->Start()` |
| `SceneEditHistory::Undo` / `Redo` | `ApplySnapshot()` が `started_` を復元（A-1 で追加） |

**新しい呼び出し元を足すときは必ず対にしてください。** `--validate-animation-undo` がこの規約を守っています。

### 5-2. `start /wait` を使わないと `%ERRORLEVEL%` が常に 0

SubSystem が全構成 `Windows` のため、`cmd` からの直接起動ではプロセス終了を待ちません。前回作業で実際にこれに嵌まっています（`HANDOVER_FINAL_RESULT.md` 2 章）。

```bat
start /wait "" x64\Debug\3dgp.exe --validate-xxx
echo ExitCode=%ERRORLEVEL%
```

### 5-3. `Scene::SynchronizeStates()` は `Update` の先頭でしか走らない

呼び出し元は `Scene::Start()`（151 行）と `Scene::Update()`（175 行）の 2 か所だけです。`FixedUpdate` / `LateUpdate` は呼びません。

したがって `SetEnabled` の反映（`OnEnable` / `OnDisable` の発火）は**次の `Update` の先頭まで遅れます**。ただし `FixedUpdate` / `LateUpdate` のループは `ActiveInHierarchy()` を毎回直接見るので、更新のスキップ自体は即座に効きます。

### 5-4. Schema の getter / setter にインスタンスを捕捉させない

`ScriptFieldSchema::BuildDescs()` のラムダは**保存名の文字列だけ**を捕捉し、引数の `Component&` から降ります。ここでインスタンスを捕捉すると、1000 体に 1000 セットの `PropertyDesc` ができて共有が壊れます。

`--validate-script-core` が「100 インスタンスが同一ポインタを返す」ことで守っています。

### 5-5. `AssetKind` へ足すときは必ず末尾へ

`.replaydb` へ整数として書かれているため、途中へ挿入すると既存 Asset の種別が化けます。

### 5-6. Win32 構成には `LanguageStandard` の指定がない

`stdcpp17` が指定されているのは `Debug|x64` / `Release|x64` の 2 構成だけです。コードベースは `std::variant` / `if constexpr` を多用しているので、**Win32 はもともとビルドが通らない可能性が高い**です（実際に試してはいません）。新規ファイルは 4 構成すべてへ登録していますが、正式対象は x64 のみです。

### 5-7. `.filters` の既存不整合（今回の変更とは無関係）

`Source\app\Editor` / `Source\app\Runtime` / `Source\mesh` という Filter 参照がありますが、定義側は `Source\App\Editor` のように先頭が大文字です。該当は `framework_collision_world.cpp` / `framework_collision_mesh_source.cpp` / `gltf_model_cache.cpp` の 3 件で、いずれも今回触っていません。Visual Studio の表示にしか影響しないため直していません。

### 5-8. 改行コードと BOM

- 全ファイル CRLF、末尾改行あり
- 新しめのファイル（`Runtime/` 配下など）は **UTF-8 BOM あり**、古いファイル（`Object/` / `Reflection/`）は BOM なし
- **新規ファイルは BOM あり**で作成、**変更した既存ファイルは元の有無を維持**

---

## 6. Validation 一覧と終了コード帯

| コマンド | 帯 | 検査数 | Linux |
|---|---|---|---|
| `--validate-handles` | 80-139 | 53 | 0 |
| `--validate-serialization` | 140-179 | 65 | 0 |
| `--validate-missing-component` | 180-209 | 35 | 0 |
| `--validate-scene-version` | 210-249 | 71 | 0 |
| `--validate-behaviour` | 250-289 | 28 | 0 |
| `--validate-events` | 290-329 | 20 | 0 |
| `--validate-runtime-api` | 330-369 | 32 | 0 |
| `--validate-collision` | 370-409 | 33 | 0 |
| `--validate-runtime-scene` | 410-457 | 48 | 未実行※ |
| `--validate-scene-flow` | 460-519 | 60 | 未実行※ |
| `--validate-editor-integration` | 520-579 | 60 | 未実行※ |
| `--validate-stress` | 580-604 | 25 | 未実行※ |
| **`--validate-script-core`** | **620-679** | **43** | **0** |
| **`--validate-script-lifecycle`** | **680-739** | **46** | **0** |
| **`--validate-script-serialization`** | **740-799** | **54** | **0** |
| **`--validate-animation-undo`** | **800-859** | **33** | **0** |

※ Linux ハーネスへ載せていないだけです（ImGui / D3D11 依存または未整備）。Windows では実行してください。

**次に足すときは 860 以降**を使ってください。ご指示にあった `--validate-camera-component` / `--validate-player-speed` などがここへ入ります。

---

## 7. Linux 検証ハーネスの再構築

`outputs/linux_harness/` に置いてあります（**リポジトリの外**。MSVC ビルドに影響しません）。

| ファイル | 内容 |
|---|---|
| `DirectXMath.h` | 最小シム |
| `windows.h` | 最小シム |
| `main2.cpp` | Validation の分岐 |
| `build.sh` | ビルドスクリプト |

```bash
bash outputs/linux_harness/build.sh /path/to/teamProject_2_3
/tmp/h/validate --validate-script-core
```

**シムの行列・ベクトル演算は「型が通り、実行が完走する」ことだけを目的とした簡易実装です。数値の正しさは保証していません。** Transform の数値検証は Windows 実機に委ねてください。スクリプト基盤と Undo の検査は Transform の計算結果に依存しないため、結論は変わりません。

**シムをリポジトリへコミットしないでください。**

ASan / LSan を使う場合:

```bash
g++ -std=c++17 -g -O1 -w -fsanitize=address -fno-omit-frame-pointer -I<shim> -I. -c ...
ASAN_OPTIONS=detect_leaks=1 ./validate_asan --validate-script-core
```

---

## 8. Windows MSVC コマンド

### Debug ビルド

Visual Studio でソリューションを開き直してから（`.vcxproj` を更新しているため）:

```bat
msbuild 3dgp.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m
```

### 全 Validation

```bat
@echo off
setlocal enabledelayedexpansion
for %%C in (
  --validate-animation-undo
  --validate-script-core --validate-script-lifecycle --validate-script-serialization
  --validate-handles --validate-serialization --validate-missing-component
  --validate-scene-version --validate-behaviour --validate-events
  --validate-runtime-api --validate-collision --validate-runtime-scene
  --validate-scene-flow --validate-editor-integration --validate-stress
  --validate-shutdown
) do (
  start /wait "" x64\Debug\3dgp.exe %%C
  echo %%C ExitCode=!ERRORLEVEL!
)
endlocal
```

### Release Clean Rebuild

```bat
msbuild 3dgp.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m
```

---

## 9. 新規 / 変更 / 削除ファイル

### 新規（27 件）

**`RePlayEngine/Scripting/Core/`（22 件）**
`ScriptLanguage.h/.cpp` / `ScriptTypes.h/.cpp` / `ScriptValue.h/.cpp` / `ScriptFieldSchema.h/.cpp` / `ScriptTypeCatalog.h/.cpp` / `ScriptComponent.h/.cpp` / `ScriptServices.h` / `ScriptBackend.h` / `ScriptWorld.h/.cpp` / `ScriptError.h/.cpp` / `ScriptRuntime.h/.cpp` / `MockScriptBackend.h/.cpp`

**`RePlayEngine/Scripting/Validation/`（2 件）**
`ScriptCoreValidation.h/.cpp`

**`RePlayEngine/Runtime/Scene/`（1 件）**
`WorldLifecycleListener.h`

**`RePlayEngine/Editor/Validation/`（2 件）**
`AnimationUndoValidation.h/.cpp`

### 変更（16 件）

| ファイル | 変更内容 |
|---|---|
| `Object/Component/Component.h` | `DynamicProperties()` / `DeferAwakeUntilObjectActive()` を追加 |
| `Object/Component/Component.cpp` | `SyncEnableState()` の Awake ゲート拡張 |
| `Reflection/Registry/PropertyRegistry.cpp` | 動的プロパティ対応（Capture / Apply / CopyValues） |
| `Editor/Inspector/PropertyDrawer.cpp` | `DrawAll` が動的分も描く |
| `Editor/Inspector/InspectorPanel.cpp` | 編集欄の判定、編集確定時の `OnPropertyChanged` |
| `Editor/Commands/SceneEditHistory.cpp` | `ApplySnapshot()` と実行状態の復元（A-1） |
| `Editor/Core/EditorContext.cpp` | Play 中の理由表示（A-1） |
| `Object/Registry/BuiltInComponents.cpp` | `RegisterScript()` |
| `Assets/AssetCache.h` | `AssetKind::Script` |
| `Scene/Services/SceneServices.h` | `Scripts()` / `SetScripts()` |
| `Runtime/Scene/RuntimeSceneService.h/.cpp` | `IWorldLifecycleListener` のフック 3 か所 × 2 関数 |
| `Source/app/framework.h` | `object_script_runtime`（宣言位置に意味あり） |
| `Source/app/Runtime/framework_runtime_scene.cpp` | ScriptRuntime の生成と接続 |
| `Source/app/Runtime/framework_gameobject_scene.cpp` | フレーム先頭の同期点 |
| `Source/app/Runtime/main.cpp` | Validation 4 コマンドの分岐 |
| `3dgp.vcxproj` / `3dgp.vcxproj.filters` | 新規 27 件の登録 |

### 削除

**0 件。** 既存 Scene / Prefab データも一切変更していません。

---

## 10. 手動確認（Windows で最初にやること）

### A-1（Undo バグ）

1. Animation 付き GameObject を配置
2. プロパティを 1 つ変更 → **Ctrl+Z**
3. → Animation が動き続ける（従来はここで止まった）
4. 履歴の先頭まで戻し、さらに Ctrl+Z → Status に「これ以上元に戻せません」、Animation は動き続ける
5. Play 中に Ctrl+Z → Status に「実行中は元に戻せません」、何も壊れない
6. Play / Stop を 20 回

### Script Phase 1

1. Add Component → **Scripting → Script**
2. Inspector に Language / Script / Class / Execution Order が出る
3. Scene を保存 → 再起動 → Script が残っている

**Backend を挿していないのでスクリプトは実行されません。** Add Component / Inspector / 保存 / 復元 までが確認範囲です。

---

## 11. 既知の制限

| 項目 | 予定 |
|---|---|
| Lua の実行（Backend 未接続） | Script Phase 2 |
| .NET ホスト | Script Phase 3 / 4 |
| `.lua` / `.cs` の Asset 登録と Picker | Script Phase 6 |
| Add Component へスクリプト名を並べる | Script Phase 6 |
| Inspector ヘッダーのスクリプト名（`DisplayLabel()` は実装済み、呼び出し側が未対応） | Script Phase 6 |
| `execution_order` の実ソート | 別作業 |
| Input API | Script Phase 7 |
| 配列 / Dictionary の Field | 対象外 |
| Camera / Input Routing / View Cube / Editor UI / Shader | Stabilization A-2〜F |

---

## 12. Script Phase 2 着手前の未決事項

**Lua 5.4 ソースの入手方法。** `lua.org` の `lua-5.4.x.tar.gz` をリポジトリ直下の `lua-5.4.x/` へ配置していただくのが確実です（`imgui` / `cereal-master` / `DirectXTK-main` と同じ流儀）。`third_party/` は存在しません。

配置後、**4 構成すべての `AdditionalIncludeDirectories` へ追加**が必要です。

**Script Phase 3 の未決事項:** .NET は framework-dependent deployment のため、実行環境に .NET ランタイムが必要になります。配布形態への影響を確認してください。

---

## 13. 禁止事項（引き続き守ること）

- `git reset` / `clean` / `restore` / ブランチ切替 / 自動コミット
- 既存 Scene / Prefab データの破棄、Unknown / Missing Data の破棄
- Ctrl+Z の無効化で原因を隠す
- Frustum Culling の全面無効化、Far Clip の無限大固定
- Runtime と Editor Camera の状態共有
- 生 `GameObject*` / `Scene*` の長期保持
- 第二の Component / Property / ObjectID システム
- 第二の更新経路（`Scene::Update` 以外でライフサイクルを回す）
- Shader Slot 番号のハードコード追加、Shader 警告の InfoQueue 除外
- Stabilization A〜F の完了前に Lua / C# 実装へ着手すること

以上。
