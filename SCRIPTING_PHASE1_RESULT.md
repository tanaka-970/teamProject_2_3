# Lua＋C# スクリプト機能 Phase 1 — 実装結果報告書

対象リポジトリ: `teamProject_2_3`（RePlayEngine / 3dgp）
実施日: 2026-08-03
前提文書: `SCRIPTING_EXISTING_SYSTEM_AUDIT.md`（改訂 3）
実施範囲: **Phase 1（共通スクリプト基盤）のみ。Lua も C# も実装していません。**

コミットはしていません。ブランチも切り替えていません。

---

## 1. 完了状況

| 項目 | 状態 |
|---|---|
| 共通スクリプト基盤（型 / Schema / Catalog / Component / Backend / Runtime / World / Error） | **完了** |
| 動的プロパティ経路（案 A） | **完了** |
| 予約接頭辞つき保存形式 | **完了** |
| Enable / Disable ライフサイクル 3 要件 | **完了** |
| Play Session 準備順序（`IWorldLifecycleListener`） | **完了** |
| MockScriptBackend（Script Type 2 種） | **完了** |
| Validation 3 種 | **完了** |
| `.vcxproj` / `.vcxproj.filters` 更新 | **完了** |
| Linux g++ ハーネスでのビルドと実行検証 | **完了** |
| **Windows 実機（MSVC）でのビルドと実行** | **未実施**（大地さん側でお願いします） |

---

## 2. 検証結果

### 2.1 新規 Validation（Linux g++ 11.4 / 実行）

| コマンド | 終了コード帯 | 検査数 | 結果 |
|---|---|---|---|
| `--validate-script-core` | 620-679 | 43 | **ExitCode=0** |
| `--validate-script-lifecycle` | 680-739 | 46 | **ExitCode=0** |
| `--validate-script-serialization` | 740-799 | 54 | **ExitCode=0** |

合計 **143 検査**。

### 2.2 既存 Validation の再実行（退行チェック）

| コマンド | 検査数 | 結果 |
|---|---|---|
| `--validate-handles` | 53 | **ExitCode=0** |
| `--validate-serialization` | 65 | **ExitCode=0** |
| `--validate-missing-component` | 35 | **ExitCode=0** |
| `--validate-scene-version` | 71 | **ExitCode=0** |
| `--validate-behaviour` | 28 | **ExitCode=0** |
| `--validate-events` | 20 | **ExitCode=0** |
| `--validate-runtime-api` | 32 | **ExitCode=0** |
| `--validate-collision` | 33 | **ExitCode=0** |

合計 **337 検査**。**総計 480 検査すべて 0。**

**とくに `--validate-behaviour` が 0 のままである点が重要です。** この検証は「無効な Behaviour でも Awake が 1 回呼ばれる」（`BehaviourValidation.cpp:174-175`）を明示的に守っています。`DeferAwakeUntilObjectActive()` の既定値を `false` にしたことで、既存 Component / Behaviour の挙動が 1 ビットも変わっていないことが実行で確認できました。

### 2.3 AddressSanitizer + LeakSanitizer

新規 3 コマンドを `-fsanitize=address`（`detect_leaks=1`）で再実行しました。

| コマンド | 終了コード | Sanitizer エラー | リーク |
|---|---|---|---|
| `--validate-script-core` | 0 | 0 | 0 |
| `--validate-script-lifecycle` | 0 | 0 | 0 |
| `--validate-script-serialization` | 0 | 0 | 0 |

### 2.4 静的検査

| 項目 | 結果 |
|---|---|
| 新規・変更ファイルの `-fsyntax-only -Wall -Wextra -std=c++17` | **警告 0**（17 ファイル） |
| `.vcxproj` と `.filters` の ClCompile 集合一致 | **175 / 175** |
| `.vcxproj` と `.filters` の ClInclude 集合一致 | **191 / 191** |
| 重複登録 | **0** |
| 実体が存在しない登録 | **0** |
| Filter 定義の網羅 | 新規追加分は**すべて定義済み**（下記の既知事項を除く） |
| 新規ファイルの CRLF / 末尾改行 / BOM | **UTF-8 BOM + CRLF + 末尾改行**（`Runtime/` 配下の既存ファイルと同じ流儀） |
| 変更した既存ファイルの BOM | **元の有無を維持** |
| Runtime → Scripting の逆依存 | **0**（`IWorldLifecycleListener` を Runtime 側に置いたため成立） |

**既知事項（今回の変更とは無関係の既存の不整合）:** `.filters` に `Source\app\Editor` / `Source\app\Runtime` / `Source\mesh` という Filter 参照がありますが、定義側は `Source\App\Editor` のように先頭が大文字です。該当するのは `framework_collision_world.cpp` / `framework_collision_mesh_source.cpp` / `gltf_model_cache.cpp` で、いずれも今回触っていません。Visual Studio 上の表示にしか影響しないため、今回は直していません。

### 2.5 Linux ハーネスについて

`DirectXMath.h` と `windows.h` の最小シムを作り、実際にコンパイル・リンク・実行しています。**シムはリポジトリへ置いていません**（MSVC ビルドに影響を与えないため）。

**シムの行列・ベクトル演算は「型が通り、実行が完走する」ことだけを目的とした簡易実装です。数値の正しさは保証していません。** スクリプト基盤の検査は Transform の計算結果に一切依存しないため、この割り切りで結論は変わりません。Transform の数値検証は Windows 実機に委ねます。

---

## 3. 新規ファイル一覧（25 件）

### `RePlayEngine/Scripting/Core/`（22 件）

| ファイル | 役割 |
|---|---|
| `ScriptLanguage.h` / `.cpp` | `enum class ScriptLanguage { Lua, CSharp }`、拡張子、カテゴリ名 |
| `ScriptTypes.h` / `.cpp` | `ScriptTypeID` / `ScriptInstanceHandle` / `ScriptCallback` / `ScriptStatus` / `ScriptInvokeResult` と ID 導出 |
| `ScriptValue.h` / `.cpp` | `PropertyValue` 系への別名、予約接頭辞、表示名の整形 |
| `ScriptFieldSchema.h` / `.cpp` | Field 定義と、共有される `PropertyDesc` 配列の組み立て |
| `ScriptTypeCatalog.h` / `.cpp` | `ScriptTypeID` → 表示名 / 言語 / Asset / Schema / 状態 |
| `ScriptComponent.h` / `.cpp` | `Component` 派生。ライフサイクル転送・Field 保持・保存 |
| `ScriptServices.h` | `IScriptServices`（Component から Runtime を触る細い口） |
| `ScriptBackend.h` | `IScriptBackend`（Lua / C# の実行 Backend） |
| `ScriptWorld.h` / `.cpp` | Play セッション単位の登録簿 |
| `ScriptError.h` / `.cpp` | エラー記録と重複抑制（最初 5 回 → 1 秒ごとの集約） |
| `ScriptRuntime.h` / `.cpp` | Backend / Catalog / World の所有、World フックの実装、同期点 |
| `MockScriptBackend.h` / `.cpp` | Phase 1 検証用。Script Type 2 種 |

### `RePlayEngine/Scripting/Validation/`（2 件）

`ScriptCoreValidation.h` / `.cpp` — Validation 3 種

### `RePlayEngine/Runtime/Scene/`（1 件）

`WorldLifecycleListener.h` — `IWorldLifecycleListener`

---

## 4. 変更した既存ファイル一覧（14 件）

| ファイル | 変更内容 |
|---|---|
| `Object/Component/Component.h` | `DynamicProperties()` と `DeferAwakeUntilObjectActive()` を追加（どちらも既定は従来どおり） |
| `Object/Component/Component.cpp` | `SyncEnableState()` の Awake ゲート条件を拡張 |
| `Reflection/Registry/PropertyRegistry.cpp` | `Capture` / `Apply` / `CopyValues` が動的プロパティも見る |
| `Editor/Inspector/PropertyDrawer.cpp` | `DrawAll` が動的プロパティも描く |
| `Editor/Inspector/InspectorPanel.cpp` | 編集欄の有無判定に動的分を加え、編集確定時に `OnPropertyChanged` を通知 |
| `Object/Registry/BuiltInComponents.cpp` | `RegisterScript()` を追加（静的 5 プロパティ含む） |
| `Assets/AssetCache.h` | `AssetKind::Script` を末尾へ追加 |
| `Scene/Services/SceneServices.h` | `Scripts()` / `SetScripts()` を追加 |
| `Runtime/Scene/RuntimeSceneService.h` | `SetWorldLifecycleListener()` と非所有メンバ |
| `Runtime/Scene/RuntimeSceneService.cpp` | `SwapWorlds` / `ResetToEmptyWorld` へフックを 3 か所ずつ挿入 |
| `Source/app/framework.h` | `object_script_runtime` メンバ（宣言位置に意味あり） |
| `Source/app/Runtime/framework_runtime_scene.cpp` | ScriptRuntime の生成・Listener 接続・編集 Scene への接続 |
| `Source/app/Runtime/framework_gameobject_scene.cpp` | フレーム先頭の同期点とログの吸い出し |
| `Source/app/Runtime/main.cpp` | Validation 3 コマンドの分岐 |

**削除したファイル: 0。**

`enter_object_play_mode` / `exit_object_play_mode` には**手を入れていません**。Play Session の開始・終了は World フック経由で自動的に走ります。

---

## 5. Play 開始順序の解決（最重要）

Phase 0 で見つけた順序問題への対処です。

### 実際の呼び出し順（実装後）

```
RuntimeSceneService::SwapWorlds()
  ├ OnWorldUnloading(旧)        … 新規インスタンス生成を止める
  ├ Scene::Clear()              … OnDisable -> OnRuntimeDestroy -> OnDetach
  │    └ ScriptComponent::OnRuntimeDestroy
  │         ├ ユーザーの OnDestroy（インスタンスがある場合だけ）
  │         └ インスタンス破棄・ScriptWorld から解除
  ├ OnWorldUnloaded(旧)         … ScriptWorld 破棄・漏れ件数の記録
  ├ unique_ptr 差し替え / Rebind
  ├ OnWorldActivating(新)       ★ ここで Play Session を用意する
  │    ├ world.Services().SetScripts(this)
  │    └ 新しい ScriptWorld を作る（世代番号 +1）
  └ Scene::Start()
       └ ScriptComponent::OnRuntimeAwake
            ├ Schema 解決 → インスタンス生成 → Field 流し込み
            └ ユーザーの Awake
```

`ResetToEmptyWorld()`（Play 停止）でも同じ 3 点を同じ順序で呼びます。

### なぜ framework 側ではないのか

`SwapWorlds` は **`SceneFlowService` 経由のゲーム中 Scene 遷移でも通ります**。`enter_object_play_mode` にだけフックを置くと、Editor の Play では動くのに、ゲーム中にシーンを切り替えた瞬間だけスクリプトが動かなくなります。

**framework から `ScriptComponent` を走査する処理はどこにもありません。** `ScriptWorld` への登録は Component の自己申告です。

### 検証

`--validate-script-lifecycle` が、`OnWorldActivating` → `Awake` の順序、および Play 停止時に `OnDestroy` → `OnWorldUnloaded` の順で走り、終了後の生存インスタンスが 0 になることを確認しています。

---

## 6. Enable / Disable ライフサイクル 3 要件

`Component::DeferAwakeUntilObjectActive()` を追加し、`SyncEnableState()` の Awake ゲートだけを拡張しました。**スクリプト専用の第二状態管理は作っていません。** 判定に使うのは既存の `runtime_awake_called_` / `enable_state_applied_` / `started_` の 3 フラグのままです。

```cpp
const bool object_active = owner_ != nullptr && owner_->ActiveInHierarchy();

if (!runtime_awake_called_ &&
    (!DeferAwakeUntilObjectActive() || object_active))
{
    runtime_awake_called_ = true;
    OnRuntimeAwake();
}
```

ゲートに `ActiveInHierarchy()`（自分の `enabled_` を含む）ではなく `owner_->ActiveInHierarchy()`（GameObject 階層のみ）を使うのが要点です。

| 要件 | 実行での確認内容 |
|---|---|
| 1-a | 有効な GameObject 上の無効な ScriptComponent に `Awake` が 1 回 |
| 1-b | 同時に `OnEnable` / `Start` が 0 回 |
| 1-c | 有効化で `OnEnable` → `Start` が 1 回ずつ。`Awake` は増えない |
| 2-a | 無効な GameObject 上ではインスタンスすら作られない |
| 2-b | `Awake` / `OnEnable` / `Start` がすべて 0 回 |
| 2-c | GameObject を有効化した同期点で `Awake` → `OnEnable` → `Start` の順に走る |
| 2-d | **`Awake` していないスクリプトを破棄しても `OnDestroy` が呼ばれない** |
| 3-a/b | 10 回反復しても `Awake` 1 回・`Start` 1 回のまま |
| 3-c | `OnDisable` / `OnEnable` がちょうど 10 回ずつ、対を崩さない |
| 3-d | GameObject 側での反復（5 回）でも同じ |

`OnDestroy` のガードは `ScriptComponent` の内部で「インスタンスがあるか」を見るだけで実現しており、`Component` 側にも新しい状態変数にも手を入れていません。

---

## 7. Schema の共有

`PropertyDesc` の getter / setter は**インスタンスを捕捉せず、保存名の文字列だけを捕捉**します。

```cpp
desc.getter = [saved_name](const Core::Component& component) -> ScriptValue
{
    const ScriptComponent* script = ScriptComponent::From(component);   // TypeID 照合。dynamic_cast しない
    return script != nullptr ? script->ReadField(saved_name) : ScriptValue{};
};
```

これにより `ScriptTypeID` ごとに 1 セットの `PropertyDesc` 配列を全インスタンスで共有できます。

**検証:** 同じ型の `ScriptComponent` を 100 体作り、`DynamicProperties()` が返すポインタと `Schema()` の実体が**全て同一**であることを確認しています。

---

## 8. 保存形式

Scene バージョンは **v11 のまま**据え置きました。既存の PROPERTY 行だけで表現できています。

```
  COMPONENT "ScriptComponent" 1
    STABLE_ID 4
    TYPE_GUID "7a3e1c94b06d4f28ae5137c0d9b28e41"
    TYPE_MODULE "RePlayEngine.Scripting"
    TYPE_VERSION 1
    PROPERTY_COUNT 8
    PROPERTY "__script.language" enum 0
    PROPERTY "__script.asset" assetref "a1b2c3d4e5f60718293a4b5c6d7e8f90"
    PROPERTY "__script.class" string ""
    PROPERTY "__script.execution_order" int 0
    PROPERTY "__script.type_id" string "a1b2c3d4e5f60718293a4b5c6d7e8f90"
    PROPERTY "field.RotationSpeed" float 90
    PROPERTY "field.LocalSpace" bool 1
    PROPERTY "field.Target" objref 0
  END_COMPONENT
```

**予約接頭辞の検証:** `language` / `class_name` / `script_asset` / `execution_order` という名前の Field を宣言した Mock 型を用意し、保存名が `field.language` などになって管理情報と衝突しないこと、双方の値が互いに潰れないことを確認しています。

Inspector には接頭辞が出ません（`PropertyDesc::DisplayName()` が `display_name` を返すため）。

---

## 9. 値の保護

| 状況 | 挙動 | 検証 |
|---|---|---|
| 一度も読み込みに成功していない型 | Field 値を `pending_values_` へ預かる。**保存し直しても値が消えない** | 済 |
| 預かり状態から再解決 | 照合して `field_values_` へ流し込む | 済 |
| 一度成功した型が読めなくなった | **最後に成功した Schema を維持する**。Inspector から Field が消えない | 済 |
| Field の型が変わった | `ConvertTo` で寄せる。寄らなければ既定値を使い、元の値は預かりへ残す | 済 |
| Field が増えた | 既定値で埋める | 済 |
| Clone | Schema と Field 値の両方を引き継ぐ | 済 |
| Scene 往復 | `CaptureScene` → `ApplySceneData` で値と ScriptTypeID が保たれる | 済 |

`Component::RetainUnknownProperties` の仕組みとは**併用していません**（二重管理を避けるため）。`field.` 接頭辞の名前は `ScriptComponent` が全部引き受けます。

---

## 10. 設計上の判断（Phase 0 の方針からの補足）

Phase 0 の計画に無かった判断を 3 つ加えました。いずれも既存の流儀に沿っています。

1. **`SceneServices::Scripts()` を追加した。** `ScriptComponent` から `ScriptRuntime` へ辿る経路が要ります。Singleton を作らず、既存の `SetRuntime` / `SetPhysics` と同じ「非所有参照の束」へ 1 つ足す形にしました。接続は `ScriptRuntime::OnWorldActivating` が自分で行うので、World が入れ替わっても `Scene::Start()` に間に合います。

2. **`InspectorPanel` が編集確定時に `OnPropertyChanged(nullptr)` を呼ぶようにした。** これが無いと、Inspector で Script Asset を選び替えても Field の顔ぶれが入れ替わりません。呼ぶのは「どの入力欄も掴まれていないフレーム」だけなので、Drag 中に毎フレーム走ることも、`DrawAll` の走査中に配列が入れ替わることもありません。`OnPropertyChanged` の宣言コメントは元々「Editor がプロパティを書き換えた直後に呼ばれる」となっており、実装が追いついていなかった箇所です。

3. **`PropertyRegistry::CopyValues` が静的プロパティの反映直後に `OnPropertyChanged(nullptr)` を呼ぶようにした。** 複製先の動的プロパティの顔ぶれは `__script.asset` が入って初めて決まります。この 1 行が無いと、複製で Field 値が 1 つも移りません。

---

## 11. Windows 実機で確認していただきたいこと

サンドボックス（Linux）では代替できない項目です。

### 11.1 ビルド

- `Debug|x64` / `Release|x64` の両方
- `.vcxproj` / `.filters` を更新済みなので、Visual Studio でソリューションを開き直してください

### 11.2 Validation

```bat
start /wait "" x64\Debug\3dgp.exe --validate-script-core
echo ExitCode=%ERRORLEVEL%
start /wait "" x64\Debug\3dgp.exe --validate-script-lifecycle
echo ExitCode=%ERRORLEVEL%
start /wait "" x64\Debug\3dgp.exe --validate-script-serialization
echo ExitCode=%ERRORLEVEL%
```

既存 12 コマンドの再実行もお願いします。**`start /wait` を使わないと `%ERRORLEVEL%` が常に 0 になります**（SubSystem が Windows のため）。

### 11.3 D3D11 Live Object

今回 GPU リソースには一切触れていないため増減は無い見込みですが、通常起動 → 数秒描画 → 終了で Report を確認してください。

### 11.4 Editor の手動確認

1. GameObject を作る
2. Add Component → Scripting → **Script** を追加
3. Inspector に **Language / Script / Class / Execution Order** が出る
4. Script Asset は未登録なので選べない（Phase 6 で `.lua` / `.cs` のスキャンを入れるまで）
5. Scene を保存 → 再起動 → `Script` が残っている
6. Play / Stop で落ちない

**Phase 1 時点では Backend を挿していないので、スクリプトは実行されません。** Add Component / Inspector / 保存 / 復元 までが確認範囲です。

---

## 12. 残っている制限

| 項目 | Phase |
|---|---|
| Lua の組み込み（Backend が未接続） | 2 |
| .NET のホスト | 3 / 4 |
| `.lua` / `.cs` の Asset 登録と Script Asset Picker | 6 |
| Add Component へスクリプト名を並べる（`Scripts > C# > RotatingObject`） | 6 |
| Project Browser からのドラッグ & ドロップ | 6 |
| Inspector ヘッダーをスクリプト名にする（`ScriptComponent::DisplayLabel()` は実装済み。呼び出し側が未対応） | 6 |
| `execution_order` による実際のソート | 別作業 |
| Input API | 7 |
| 配列 / Dictionary の Field | 対象外 |

---

## 13. 推測と実確認の区別

### 実行で確認したこと

- 新規 143 検査・既存 337 検査の終了コードが 0（Linux g++ 11.4）
- ASan + LSan でエラー 0 / リーク 0（新規 3 コマンド）
- `--validate-behaviour` が 0 のまま＝既存 Component の Awake 挙動が変わっていない
- 100 インスタンスが Schema を共有している
- Enable / Disable 3 要件のすべて
- Play Session の準備が `Awake` より前であること

### 静的にだけ確認したこと

- 新規・変更ファイルの警告 0（`-Wall -Wextra`）
- `.vcxproj` / `.filters` の集合一致・重複なし・実体欠落なし
- 改行コードと BOM

### 未確認（Windows 実機が必要）

- MSVC でのコンパイル（g++ が通っても MSVC 特有のエラーは出うる）
- `Debug|x64` / `Release|x64` のビルドと Validation の終了コード
- D3D11 Live Object Report
- Editor UI の実際の表示と操作
- `Source/app/` の 4 ファイル（`framework.h` / `framework_runtime_scene.cpp` / `framework_gameobject_scene.cpp` / `main.cpp`）の変更。**これらは Windows / D3D11 依存のため Linux ハーネスでコンパイルできていません。** 目視での確認のみです

### 推測

- `Win32` 構成は今回のスクリプト機能の対象外です。既存コードが C++17 の機能を多用しており、`LanguageStandard` が x64 の 2 構成にしか指定されていないため、もともとビルドが通らない可能性が高いと見ています（実際にビルドして確かめてはいません）

---

## 14. 次のセッションへの引き継ぎ

**Phase 2（Lua バックエンド）に入る前に決めていただきたいこと:**

- Lua 5.4 ソースの入手方法。`lua.org` の `lua-5.4.x.tar.gz` をリポジトリ直下の `lua-5.4.x/` へ配置していただくのが確実です（`imgui` / `cereal-master` / `DirectXTK-main` と同じ流儀）
- 4 構成すべての `AdditionalIncludeDirectories` へ追加する必要があります

**Linux ハーネスの再構築:** `outputs/linux_harness/` に `DirectXMath.h` / `windows.h` のシムと `build.sh` を置いてあります。リポジトリの外なので MSVC ビルドには影響しません。

以上。
