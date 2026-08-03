# Phase 6〜9 最終報告

作業ブランチ: `sinotake`（切り替えていません）
HEAD: `018dbf4ae324257381f9b657e8f48c74dcc61973`（変更していません）
コミット: していません

前段の報告: `HANDOVER_Phase6-7_RESULT.md` / `HANDOVER_Phase8-9_RESULT.md`
本書はそれらの**最終確定版**です。食い違う記述があれば本書が正です。

---

## 1. 完了状況

| Phase | 状態 | 実機確認 |
|---|---|---|
| Phase 6 — RuntimeSceneService Validation | **完了** | `--validate-runtime-scene` = 0 |
| Phase 7 — SceneFlowService / Startup Scene / SceneTransitionBehaviour | **完了** | `--validate-scene-flow` = 0 |
| Phase 8A — World 所有権の統一 | **完了** | 下記 8C/8D に含む |
| Phase 8B — framework への SceneFlow 接続 | **完了** | 同上 |
| Phase 8C — Editor 統合 | **完了** | 同上 |
| Phase 8D — Editor Integration Validation | **完了** | `--validate-editor-integration` = 0 |
| Phase 9 — 最終 QA / 耐久 / D3D11 | **完了** | `--validate-stress` = 0 / `--validate-shutdown` = 0 / Live Object クリーン |

---

## 2. Windows 実機の確認結果

Visual Studio 2026 / MSVC / Debug x64。GUI サブシステムのため `start /wait` 経由で測定。

| コマンド | 終了コード |
|---|---|
| `--validate-runtime-scene` | **0** |
| `--validate-scene-flow` | **0** |
| `--validate-editor-integration` | **0** |
| `--validate-stress` | **0** |
| `--validate-shutdown` | **0** |

### D3D11 Live Object Report（通常起動 → 数秒描画 → 終了）

```
D3D11 WARNING: Live ID3D11Device at 0x000001AA604A00B0, Refcount: 2
               [ STATE_CREATION WARNING #441: LIVE_DEVICE ]
プログラム '[46964] 3dgp.exe' はコード 0 (0x0) で終了しました。
```

| 項目 | 結果 |
|---|---|
| Live ID3D11ShaderResourceView | **0** |
| Live ID3D11Query | **0** |
| 意図しない Live Buffer / Texture / Shader | **0** |
| Live ID3D11Device | Refcount 2 のみ（Report 実行に必要。許容） |
| `SETPRIVATEDATA_CHANGINGPARAMS` | **0** |
| `DEVICE_DRAW_CONSTANT_BUFFER_TOO_SMALL` | **0** |
| 終了コード | **0** |

### 測定方法についての注意

`SubSystem` が全構成 `Windows` のため、`cmd` から直接起動すると
**cmd はプロセスの終了を待たず、`%ERRORLEVEL%` は起動の成否（0）になります**。
必ず次の形で測ってください。

```
start /wait "" x64\Debug\3dgp.exe --validate-xxx
```

```
echo %ERRORLEVEL%
```

（この落とし穴により、途中経過で報告した一部の「0」は無効でした。本書の数値は
`start /wait` で測り直した実測値です。）

---

## 3. 主要クラス一覧

| クラス / 型 | 場所 | 役割 |
|---|---|---|
| `RuntimeSceneService` | Runtime/Scene | Runtime World の**唯一の所有者**。Staging 方式で入れ替える |
| `ISceneAssetResolver` | Runtime/Scene | AssetGUID → Scene パス。Runtime が AssetDatabase へ依存しない境界 |
| `SceneLoadState` / `SceneRequestResult` | Runtime/Scene | 読み込み工程の状態と要求の受理結果 |
| `SceneFlowService` | Runtime/Scene | 遷移要求・履歴・Startup Scene・Quit。実読込はしない上位サービス |
| `SceneTransitionState` / `StartupSceneState` / `SceneTransitionKind` | Runtime/Scene | 遷移と起動の診断状態 |
| `ISceneFlow` | Runtime/API | Behaviour から遷移を要求する口。未接続なら ServiceUnavailable |
| `SceneTransitionBehaviour` / `SceneTransitionMode` | Source/game/Behaviours | Trigger から遷移を要求する Behaviour |
| `ProjectSettings`（拡張） | Project | `startup_scene_guid` と `ResolveStartupScene()` |
| `AssetReferenceStatus` | Project | Unset / Missing / Resolved を種類ごとに増やさないための別名 |
| `framework::scene_asset_resolver` | Source/app | AssetDatabase を使った GUID → パス解決 |
| `framework::runtime_prefab_instantiator` | Source/app | Behaviour からの Prefab 生成の受け口 |

---

## 4. Runtime Scene Load の処理順

```
RequestLoad(guid) / RequestAdopt(SceneData, guid)
  ├ 空 GUID         -> InvalidRequest
  ├ 進行中          -> Busy（進行中の要求を壊さない）
  └ 受理            -> state=Loading / SceneLoadRequested を Global Bus へ発行

Tick() 1 回目 : BuildStagingWorld()
  1. SceneLoadStarted を発行
  2. AssetGuid 経路なら Resolver でパス解決 → SceneSerializer::LoadFromFile
     InMemory 経路（Play 開始）ならファイルも GUID も経由しない
  3. ApplySceneData で Staging World を構築
     GameObject / Component 生成、StableID 復元、Property 反映、
     Missing Component 保持、Unknown Property 保持、参照解決
  4. skipped_components != 0 なら失敗（Missing は失敗にしない）
  -> 成功: ReadyToSwap ／ 失敗: Failed（現在の World は 1 バイトも触らない）

Tick() 2 回目 : SwapWorlds()
  1. BeforeSceneUnload を発行し、旧 World が生きているうちに配送
  2. RuntimeContext::Events().Clear()
  3. CollisionEventDispatcher::Reset()
  4. 旧 World の Services().SetRuntime(nullptr)
  5. 旧 World の Clear()（OnDisable -> OnRuntimeDestroy -> OnDetach）
  6. unique_ptr 差し替え（旧 World の実体をここで解放）
  7. RuntimeContext::Rebind(new) / new.Services().SetRuntime(runtime)
  8. Scene::Start()（OnRuntimeAwake -> OnEnable -> OnStart）
  9. WorldChanged / SceneLoaded を発行
  -> Completed

Tick() 末尾: EventBus::Global().Dispatch(nullptr)
```

失敗時は `Fail()` が `SceneLoadFailed` を発行。`Clear()` は呼ばれず、
旧 World も旧 ObjectHandle も有効なまま残ります。

---

## 5. Scene Flow の処理順

```
LoadScene / ReloadCurrentScene / ReturnToPreviousScene
  ├ 空            -> InvalidArgument
  ├ 遷移中        -> TransitionInProgress
  └ 受理          -> previous を控えて RuntimeSceneService へ委譲

Tick()
  1. RuntimeSceneService::Tick()（実読込は下位が行う）
  2. 下位の State を観測して成功 / 失敗 / 取り消しを判定

履歴規則
  Load   : 直前の GUID を積む（切替先と同じ・空は積まない）
  Reload : 積まない
  Return : 積まず末尾を 1 つ取り出す（往復で伸びない）
  上限 16 件。超えたら先頭（古い方）から捨てる
```

`QuitApplication()` はプロセスを終了せず、要求として記録し
`ApplicationQuitRequested` を Global Bus へ発行します。

---

## 6. Startup Scene の起動手順

```
framework::initialize_object_scene()
  1. RegisterBuiltInComponents() / RegisterGameBehaviours()
  2. load_project_settings()
  3. initialize_runtime_services()
  4. if (object_boot_from_startup_scene) begin_startup_scene()   ← --game のときだけ

framework::begin_startup_scene()
  1. object_runtime_world_active = true
  2. StartupSceneGuid() 取得
       空 / 未解決 / 型違い -> Blocked（別 Scene を読まない）
  3. SceneFlowService::BeginStartupScene(guid)

framework::update_object_scene()  ← フレーム先頭 = 安全点
  tick_runtime_scene_flow() → SceneFlowService::Tick() のみ（二重 Tick しない）
```

**重要**: `editor_mode` は「Editor UI を表示しているか」の切り替えで既定 false です。
これを「Editor なしのゲーム起動」と読むと、通常の Editor 起動でも Runtime World が
有効になり、配置先（Runtime World）と保存元（編集 Scene）が食い違って
**配置内容が保存されません**。実際に一度その不具合を作り込みました。
起動 Scene から始めるのは `--game` を明示したときだけです。

---

## 7. Editor 統合の内容

- **Project Settings**: Startup Scene の選択（`.replayscene` のみ候補）／AssetGUID で保存／
  Missing 表示（GUID は保持）／解除ボタン／保存失敗の理由表示
- **Add Component**: `ComponentRegistry` 列挙のまま。Behaviour は登録するだけで一覧へ出る。
  型ごとの if / switch は 1 つも追加していない。`BehaviourRegistry` から供給元・
  Type GUID・Module をツールチップへ表示
- **Inspector**: `SceneReference`（Scene Asset のみ候補）／`ComponentReference`
  （GameObject → Component 一覧 → StableID で保存）／`Array`（追加・削除・並べ替え・要素編集）
  を完成。未対応の要素型は編集欄を出さず読み取り専用表示
- **Missing Component**: 型名 / Type GUID / Module ID / Type Version / Serialized Property を
  読み取り専用で表示。削除は明示操作のときだけ
- **Runtime 診断**: Active / Pending Scene GUID、Service State、Flow State、Startup State、
  Blocked と理由、QuitRequested、Play/Edit、Last Error、履歴、Load/Swap/失敗回数
- **Play Mode**: 編集 Scene は不変／切替時に Selection と Undo を破棄／終了時に復元／
  Runtime の変更を編集 Scene へ書き戻す経路そのものを置かない

---

## 8. Version 変更と Migration

| 形式 | 変更前 | 変更後 |
|---|---|---|
| Scene（`.replayscene`） | current 11 / minimum 7 | **変更なし** |
| ProjectSettings（`.replayproject`） | current 1 | **current 2 / minimum 1** |

- v1 → v2: `STARTUP_SCENE` 行が無いので未設定のまま読み込む（値を推測しない）。保存すると v2
- 未対応バージョン / 壊れたファイル: 読み込み失敗＋`ApplySafeDefaults()` で既定値へ

---

## 9. Missing / Unknown Data の保持

Phase 2 の仕組みをそのまま通し、経路を増やしていません。

- 型が解決できない Component は `MissingComponent` が型名 / TypeGUID / module_id /
  type_version / PropertyBag を丸ごと預かる
- 型が知らないプロパティは PropertyRegistry が預かり、保存時に書き戻す
- `RuntimeSceneService` は Missing / Unknown を**失敗にしない**。件数を診断へ残すだけ
- 復元できなかった Component（`skipped_components`）だけを失敗として扱う

---

## 10. Validation 一覧

| コマンド | 終了コード帯 | 検査数 | 実機 |
|---|---|---|---|
| `--validate-handles` | 80-139 | 53 | 未測定※ |
| `--validate-serialization` | 140-179 | 65 | 未測定※ |
| `--validate-missing-component` | 180-209 | 35 | 未測定※ |
| `--validate-scene-version` | 210-249 | 71 | 未測定※ |
| `--validate-behaviour` | 250-289 | 28 | 未測定※ |
| `--validate-events` | 290-329 | 20 | 未測定※ |
| `--validate-runtime-api` | 330-369 | 32 | 未測定※ |
| `--validate-collision` | 370-409 | 33 | 未測定※ |
| **`--validate-runtime-scene`** | 410-457 | 48 | **0** |
| **`--validate-scene-flow`** | 460-507 / 508-519 | 48 + 12 | **0** |
| **`--validate-editor-integration`** | 520-579 | 60 | **0** |
| **`--validate-stress`** | 580-604 | 25 | **0** |
| **`--validate-shutdown`** | Live Object で判定 | — | **0** |

※ Linux + g++ + ASan では全件 PASS。Windows 実機では `start /wait` での測り直しが未了。

合計検査数: **530**

---

## 11. 実行済みの検証

### Windows 実機（MSVC Debug x64）

- 新規 5 コマンドの終了コード（すべて 0）
- D3D11 Live Object Report（子リソース 0）
- D3D11 警告 0（SetPrivateData / Constant Buffer とも）
- 通常起動・終了の動作確認

### Linux + g++ 11.4（MSVC の代わりにはなりません）

- `-fsyntax-only -Wall -Wextra -std=c++17`：触った Engine / Editor / Game 側の全ファイルで警告 0
- **実際にビルドして 12 種 530 検査を実行し全件 PASS**
- **AddressSanitizer + LeakSanitizer で同 12 種を再実行し全件 PASS**（メモリエラー 0 / リーク 0）
- ImGui を実際に動かして PropertyDrawer を 8 フレーム描画し、
  入力の無いフレームで変更を報告しないことを確認（追加した全型を含め 0 件）

### 静的検査

- vcxproj / filters 登録集合の一致（ClCompile 164/164、ClInclude 177/177）
- 重複登録 0 / 実体欠落 0 / XML 妥当
- Runtime → Editor 逆依存 **0**、Engine → Game 逆依存 **0**、Runtime からの imgui 参照 **0**
- Scene の二重所有 **0**、Scene へのムーブ追加 **無し**、生 Scene* の長期キャッシュ **0**
- 変更・新規ファイルすべて CRLF・末尾改行あり。BOM は元の有無を維持
- `git diff --check` 空白エラー 0

---

## 12. 未実施の検証

- **既存 8 コマンドの Windows 実機での測り直し**（上表※）
- **Release x64 構成のビルドと Validation**
- **Windows 上の ASan**
- Editor UI の手動操作確認（`HANDOVER_Phase8-9_RESULT.md` の 15 章に手順あり）

---

## 13. 途中で作り込み、修正した不具合

いずれも実機・実行・ASan で見つかったものです。静的確認だけでは出ませんでした。

1. **配置内容が保存されない（重大）** — `editor_mode` を「Editor なしの起動」と誤読し、
   通常起動でも Runtime World を有効化。配置先と保存元が食い違っていた。
   `object_boot_from_startup_scene` を導入して修正。コミット済みデータの消失は無し
2. **終了できない** — 未保存確認の後に再度 Dirty を判定する構造。
   `object_exit_confirmed` で「選んだ結果を覆さない」形へ。ボタン名も「〜終了」に修正
3. **C2027（MSVC）** — `EventBus` の完全型が必要な箇所。使用側 .cpp へ局所 include
4. **GPU リソースリーク** — `texture.cpp` の static テクスチャキャッシュ（SRV 6）と
   `RenderStats` の Meyers singleton（Query 3）。どちらも `main()` 終了後に破棄されるため
   Report に残っていた。`uninitialize()` から明示解放
5. **Constant Buffer 80/160 不一致** — G-Buffer の PS が `compute_motion_vector()` 経由で
   b6 を読むのに、VS にしか Bind していなかった。PS b6 へも Bind
6. **SetPrivateData 二重命名** — 外部 Loader が命名済みの SRV へ再命名していた。
   ロード経路の命名を廃止し、自前生成の Dummy Texture だけを命名する設計へ
7. **RuntimeContext と World の破棄順** — ASan が検出した解放済みメモリ参照
8. **既存 BehaviourValidation の解放後参照 2 件** — ASan が検出

### 訂正

途中で「SetPrivateData 二重命名は DirectXTK 単独の問題」と報告しましたが、**誤りでした。**
静的検索で vendor 側に複数の呼び出しが見つかったことから推測しましたが、
同一実行経路で同一 SRV へ呼ばれる保証はありません。切り分け実験により、
原因は `Source/core/texture.cpp` 側の再命名であることが確定しています。

---

## 14. 既知の制限

- `RuntimeSceneService::Tick()` の末尾で `EventBus::Global().Dispatch(nullptr)` を呼ぶため、
  他所が Global Bus へ積んだイベントもこの同期点で配送されます
- `SceneFlowService::Tick()` は内部で `RuntimeSceneService::Tick()` を呼びます。二重 Tick 禁止
- Play 開始時だけ `Tick()` を 2 回続けて呼び、構築と入れ替えをその場で済ませます
- Editor 起動時は Startup Scene を自動で読み込みません（Play か `--game` のときだけ）
- `EditorSelection::PruneMissing()` は World 跨ぎの ObjectID 衝突を検出できません。
  World 入れ替え時は `ResetSceneState()` が必須です
- 一般 RigidBody の衝突は実装していません（`CharacterGround` / `CharacterWall` のみ）
- Audio / Input Action / SaveGame / Runtime UI は未実装。`〜Available()` は常に false
- C# は実装していません。拡張点（`IBehaviourProvider` / `TypeGUID` / Missing Behaviour 保持 /
  Handle Facade / WorldInstanceID / Runtime API 境界 / Scene 切り替え通知）は維持
- `3dgp.vcxproj.filters` に `Source\app\Editor` 等の `<Filter Include>` 宣言が無い
  （**今回の変更以前から**の大文字小文字不一致。実害なし）
- `resources\cube.obj` が無いという起動時メッセージは既存仕様（デバッグ用静的メッシュ）

---

## 15. Debug x64 ビルドコマンド

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

## 16. Debug 全 Validation コマンド

**GUI サブシステムのため `start /wait` が必須です。**

```
start /wait "" x64\Debug\3dgp.exe --validate-handles
```

```
echo HANDLES=%ERRORLEVEL%
```

```
start /wait "" x64\Debug\3dgp.exe --validate-serialization
```

```
echo SERIALIZATION=%ERRORLEVEL%
```

```
start /wait "" x64\Debug\3dgp.exe --validate-missing-component
```

```
echo MISSING=%ERRORLEVEL%
```

```
start /wait "" x64\Debug\3dgp.exe --validate-scene-version
```

```
echo SCENE_VERSION=%ERRORLEVEL%
```

```
start /wait "" x64\Debug\3dgp.exe --validate-behaviour
```

```
echo BEHAVIOUR=%ERRORLEVEL%
```

```
start /wait "" x64\Debug\3dgp.exe --validate-events
```

```
echo EVENTS=%ERRORLEVEL%
```

```
start /wait "" x64\Debug\3dgp.exe --validate-runtime-api
```

```
echo RUNTIME_API=%ERRORLEVEL%
```

```
start /wait "" x64\Debug\3dgp.exe --validate-collision
```

```
echo COLLISION=%ERRORLEVEL%
```

```
start /wait "" x64\Debug\3dgp.exe --validate-runtime-scene
```

```
echo RUNTIME_SCENE=%ERRORLEVEL%
```

```
start /wait "" x64\Debug\3dgp.exe --validate-scene-flow
```

```
echo SCENE_FLOW=%ERRORLEVEL%
```

```
start /wait "" x64\Debug\3dgp.exe --validate-editor-integration
```

```
echo EDITOR_INTEGRATION=%ERRORLEVEL%
```

```
start /wait "" x64\Debug\3dgp.exe --validate-stress
```

```
echo STRESS=%ERRORLEVEL%
```

```
start /wait "" x64\Debug\3dgp.exe --validate-shutdown
```

```
echo SHUTDOWN=%ERRORLEVEL%
```

```
start /wait "" x64\Debug\3dgp.exe --validate-prefab
```

```
echo PREFAB=%ERRORLEVEL%
```

```
start /wait "" x64\Debug\3dgp.exe --validate-large-scene
```

```
echo LARGE_SCENE=%ERRORLEVEL%
```

```
start /wait "" x64\Debug\3dgp.exe --validate-material
```

```
echo MATERIAL=%ERRORLEVEL%
```

```
start /wait "" x64\Debug\3dgp.exe --validate-landscape
```

```
echo LANDSCAPE=%ERRORLEVEL%
```

---

## 17. Release x64 Clean Rebuild

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

## 18. Release 全 Validation

16 章と同じ順序で、パスだけ `x64\Release\3dgp.exe` に読み替えてください。
`start /wait ""` は同様に必要です。

---

## 19. 手動確認手順

`HANDOVER_Phase8-9_RESULT.md` の 15 章（A〜H）を参照してください。
とくに次の 2 つは必ず通してください。

- **A**: GameObject を置く → Ctrl+S → 開き直して残っているか（配置と保存の一致）
- **D**: Play 中の変更が編集 Scene へ戻らないこと／Undo に混ざらないこと

---

## 20. 変更量

### 新規（16 ファイル）

```
RePlayEngine/Runtime/Scene/RuntimeSceneService.h / .cpp
RePlayEngine/Runtime/Scene/SceneFlowService.h / .cpp
RePlayEngine/Runtime/Validation/RuntimeSceneValidation.h / .cpp
RePlayEngine/Runtime/Validation/SceneFlowValidation.h / .cpp
RePlayEngine/Runtime/Validation/StressValidation.h / .cpp
RePlayEngine/Editor/Validation/EditorIntegrationValidation.h / .cpp
Source/app/Runtime/framework_runtime_scene.cpp
Source/app/Runtime/framework_shutdown_regression.cpp
Source/game/Behaviours/SceneTransitionBehaviour.h / .cpp
```

### 変更（主なもの）

```
3dgp.vcxproj / 3dgp.vcxproj.filters
RePlayEngine/Editor/ComponentBrowser/AddComponentPanel.cpp
RePlayEngine/Editor/Inspector/InspectorPanel.h / .cpp
RePlayEngine/Editor/Inspector/PropertyDrawer.cpp
RePlayEngine/Project/ProjectSettings.h / .cpp
RePlayEngine/Project/ProjectSettingsSerializer.h / .cpp
RePlayEngine/Rendering/RenderStats.h / .cpp
RePlayEngine/Runtime/API/RuntimeContext.h / .cpp
RePlayEngine/Runtime/Validation/BehaviourValidation.cpp
Shader/motion_vector_common.hlsli
Source/app/framework.h
Source/app/Editor/framework_editor.cpp
Source/app/Editor/framework_project_settings.cpp
Source/app/Runtime/framework.cpp
Source/app/Runtime/framework_gameobject_scene.cpp
Source/app/Runtime/main.cpp
Source/core/texture.cpp
Source/mesh/static_mesh.cpp / skinned_mesh.cpp / gltf_model.cpp
Source/game/Behaviours/ValidationBehaviours.h / .cpp
```

### 合計

- 新規: **16 ファイル / 約 4,900 行**
- 変更: **26 ファイル**
- 削除: **0 ファイル**
- vendor（`DirectXTK-main`）の変更: **無し**
