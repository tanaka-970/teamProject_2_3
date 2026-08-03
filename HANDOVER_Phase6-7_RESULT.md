# Phase 6 / Phase 7 実装報告（Phase 8・9 は未実施）

作業ブランチ: `sinotake`（切り替えていません）
HEAD: `018dbf4ae324257381f9b657e8f48c74dcc61973`（変更していません）
コミット: していません（指示どおり自動コミットなし）

---

## 0. 最初に確認した実際の状態

引き継ぎ資料の記述と、実際のリポジトリ状態には次の差がありました。

| 項目 | 資料の記述 | 実際 |
|---|---|---|
| 未コミット変更 | 「Phase 1〜6 の成果が大量に未コミット」 | Phase 1〜5 は **HEAD にコミット済み**。未コミットは Phase 6 の途中成果のみ（4 ファイル） |
| 削除ファイル | — | 0 件 |

作業開始時の未コミット差分（`RePlayEngine` / `Source` / vcxproj 範囲）:

```
 M 3dgp.vcxproj                                 |  2 ++
 M 3dgp.vcxproj.filters                         |  9 +++++++++
 M RePlayEngine/Runtime/API/RuntimeContext.cpp  | 15 +++++++++++++
 M RePlayEngine/Runtime/API/RuntimeContext.h    | 12 ++++++++++
?? RePlayEngine/Runtime/Scene/RuntimeSceneService.cpp
?? RePlayEngine/Runtime/Scene/RuntimeSceneService.h
```

ブランチ／HEAD は想定どおりだったため作業を継続しました。

---

## 1. Phase 6〜9 の完了概要

| Phase | 状態 | 内容 |
|---|---|---|
| Phase 6 | **完了** | `--validate-runtime-scene`（410-457、48 検査）を新設。あわせて RuntimeSceneService へ Scene 遷移通知の発行を追加 |
| Phase 7 | **完了（framework への結線を除く）** | SceneFlowService / Startup Scene（ProjectSettings v2 + v1 移行）/ SceneTransitionBehaviour / `--validate-scene-flow`（460-519、60 検査） |
| Phase 8 | **未着手** | Editor 統合・framework の World 所有移行はいずれも手を付けていません |
| Phase 9 | **部分実施** | g++／ASan による回帰は実施。MSVC・Windows 実機・D3D11 Live Object は **未実施** |

Phase 7 のうち「起動時の流れ」は、`SceneFlowService::BeginStartupScene()` と
`ProjectSettings::StartupSceneGuid()` として実装しヘッドレス検証も通っていますが、
**framework からの呼び出しは未接続**です。framework が Scene を値メンバで所有している
構造の変更（Phase 8 の項目 8）と不可分なため、中途半端に着手せず残しました。

---

## 2. 新規／変更／削除ファイル数

- 新規: **10**（うち Phase 6 引き継ぎ分の RuntimeSceneService.h/.cpp を含む）
- 変更: **12**
- 削除: **0**

### 新規

```
RePlayEngine/Runtime/Scene/RuntimeSceneService.h     235 行（引き継ぎ分＋今回追記）
RePlayEngine/Runtime/Scene/RuntimeSceneService.cpp   354 行（同上）
RePlayEngine/Runtime/Scene/SceneFlowService.h        220 行
RePlayEngine/Runtime/Scene/SceneFlowService.cpp      402 行
RePlayEngine/Runtime/Validation/RuntimeSceneValidation.h    21 行
RePlayEngine/Runtime/Validation/RuntimeSceneValidation.cpp 787 行
RePlayEngine/Runtime/Validation/SceneFlowValidation.h       20 行
RePlayEngine/Runtime/Validation/SceneFlowValidation.cpp    488 行
Source/game/Behaviours/SceneTransitionBehaviour.h          118 行
Source/game/Behaviours/SceneTransitionBehaviour.cpp        143 行
```

### 変更

```
3dgp.vcxproj                                         +10
3dgp.vcxproj.filters                                 +33
RePlayEngine/Project/ProjectSettings.h               +49/-2
RePlayEngine/Project/ProjectSettings.cpp             +35
RePlayEngine/Project/ProjectSettingsSerializer.h     +16/-2
RePlayEngine/Project/ProjectSettingsSerializer.cpp   +41/-6
RePlayEngine/Runtime/API/RuntimeContext.h            +66
RePlayEngine/Runtime/API/RuntimeContext.cpp          +90/-2
RePlayEngine/Runtime/Validation/BehaviourValidation.cpp +23/-10（ASan で見つけた解放済みメモリ参照の修正）
Source/app/Runtime/main.cpp                          +29
Source/game/Behaviours/ValidationBehaviours.h        +13
Source/game/Behaviours/ValidationBehaviours.cpp      +323
```

---

## 3. 主要クラス一覧

| クラス / 型 | 場所 | 役割 |
|---|---|---|
| `RuntimeSceneService` | Runtime/Scene | Runtime World の所有と Staging 方式の入れ替え（Phase 6 引き継ぎ＋通知発行を追加） |
| `ISceneAssetResolver` | Runtime/Scene | AssetGUID → Scene パス。Runtime が AssetDatabase へ依存しないための境界 |
| `SceneLoadState` / `SceneRequestResult` | Runtime/Scene | 読み込み工程の状態と要求の受理結果 |
| `SceneFlowService` | Runtime/Scene | Scene 遷移要求・履歴・Startup Scene・Quit 要求。実読込は行わない上位サービス |
| `SceneTransitionState` / `StartupSceneState` / `SceneTransitionKind` | Runtime/Scene | 遷移と起動の診断状態 |
| `ISceneFlow` | Runtime/API | Behaviour から遷移を要求する口。未接続なら ServiceUnavailable |
| `SceneTransitionBehaviour` / `SceneTransitionMode` | Source/game/Behaviours | Trigger から遷移を要求する Behaviour |
| `ProjectSettings`（拡張） | Project | `startup_scene_guid` の追加と `ResolveStartupScene()` |
| `AssetReferenceStatus` | Project | `PrefabReferenceStatus` の別名。Unset / Missing / Resolved を種類ごとに増やさないため |

---

## 4. Runtime Scene Load の処理順

```
RequestLoad(guid)
  ├ guid が空            -> InvalidRequest（状態を変えない）
  ├ 進行中               -> Busy（進行中の要求を壊さない）
  └ 受理                 -> state = Loading / SceneLoadRequested を Global Bus へ発行

Tick() 1 回目 : BuildStagingWorld()
  1. SceneLoadStarted を発行
  2. ISceneAssetResolver で GUID -> パス（未接続なら ServiceUnavailable）
  3. SceneSerializer::LoadFromFile（Version 判定・旧形式移行はここ）
  4. ApplySceneData で Staging World を構築
     GameObject / Component 生成、StableID 復元、Property 反映、
     Missing Component 保持、Unknown Property 保持、参照解決
  5. skipped_components != 0 なら失敗（Missing は失敗にしない）
  -> 成功: state = ReadyToSwap ／ 失敗: state = Failed（現在の World は 1 バイトも触らない）

Tick() 2 回目 : SwapWorlds()
  1. BeforeSceneUnload を発行し、その場で Global Bus を配送（旧 World がまだ生きている間）
  2. RuntimeContext::Events().Clear()（Scene 単位の購読を破棄）
  3. CollisionEventDispatcher::Reset()（接触状態を破棄）
  4. 旧 World の Services().SetRuntime(nullptr)
  5. 旧 World の Clear()（OnDisable -> OnRuntimeDestroy -> OnDetach）
  6. unique_ptr 差し替え（旧 World の実体をここで解放）
  7. RuntimeContext::Rebind(new)／new.Services().SetRuntime(runtime)
  8. Scene::Start()（OnRuntimeAwake -> OnEnable -> OnStart）
  9. WorldChanged / SceneLoaded を発行
  -> state = Completed

Tick() 末尾: EventBus::Global().Dispatch(nullptr)
  自分が積んだ通知を同じ同期点で配り切る（待ち行列が伸び続けないため）
```

失敗時に `Fail()` が `SceneLoadFailed` を発行します。失敗しても `Clear()` は呼ばれず、
旧 World も旧 ObjectHandle も有効なまま残ります。

---

## 5. Scene Flow の処理順

```
LoadScene(SceneReference | AssetGUID)
  ├ 空              -> InvalidArgument（履歴も現在の Scene も動かさない）
  ├ 遷移中          -> TransitionInProgress
  └ 受理            -> previous = 現在の GUID を控え、RuntimeSceneService へ委譲
                       state = Requested

ReloadCurrentScene()  現在の GUID が空なら SceneMissing
ReturnToPreviousScene() 履歴が空なら SceneMissing。履歴はこの時点では取り出さない

Tick()
  1. RuntimeSceneService::Tick()（実読込は下位が行う）
  2. 下位の State を観測
       Loading/ReadyToSwap/Swapping -> state = Loading
       Completed -> 成功処理
       Failed    -> 失敗処理（履歴を 1 つも動かさない）
       Idle      -> 下位が CancelPending された。遷移も無かったことにする

成功処理の履歴規則
  Load   : 直前の GUID を積む（切替先と同じ GUID・空 GUID は積まない）
  Reload : 積まない
  Return : 積まず、末尾を 1 つ取り出す（往復で伸びない）
  上限 16 件。超えたら先頭（古い方）から捨てる
```

`QuitApplication(reason)` はプロセスを終了せず、
`QuitRequested()` / `QuitReason()` / `QuitRequestCount()` として記録し、
`ApplicationQuitRequested` を Global Bus へ発行します。
アプリケーション層が受け取ったら `ClearQuitRequest()` を呼ぶ形です。

---

## 6. Startup Scene の起動手順

```
Engine Boot
  -> ProjectSettingsSerializer::LoadFromFile()
       失敗時は ApplySafeDefaults()（Reset）で安全な既定値へ
  -> ProjectSettings::StartupSceneGuid()
  -> SceneFlowService::BeginStartupScene(guid)
       guid が空 : StartupSceneState::NotConfigured / SceneMissing を返す
                   別の Scene を勝手に読むことはしない
       guid あり : 履歴を空にして RuntimeSceneService へ Load 要求
  -> SceneFlowService::Tick() を安全点で回す
       成功 : StartupSceneState::Ready（ここからゲーム開始）
       失敗 : StartupSceneState::Failed（AssetMissing / SceneLoadFailed など）
              StartupBlocked() が true。無言のフォールバックはしない
```

**未接続の部分**: 上記を framework の起動処理から呼ぶコードはまだありません。
現状 framework は `object_scene` / `object_scene_runtime` を値メンバとして所有しており、
RuntimeSceneService が World を所有する形への移行（Phase 8 項目 8）が前提になります。

---

## 7. Editor 統合内容

**未実施**です。Phase 8 は 1 行も書いていません。残作業は「13. 未実施検証・残作業」に列挙しました。

---

## 8. Scene / ProjectSettings の Version 変更

| 形式 | 変更前 | 変更後 |
|---|---|---|
| Scene（`.replayscene`） | current 11 / minimum 7 | **変更なし**（11 / 7 のまま） |
| ProjectSettings（`.replayproject`） | current 1 | **current 2 / minimum_supported 1** |

ProjectSettings v2 で追加した行:

```
STARTUP_SCENE "<AssetGUID>"
```

空 GUID も行ごと書き出します。行を省略すると
「v1 から移行しただけ」と「明示的に未設定へ戻した」を区別できなくなるためです。

---

## 9. Migration 対応範囲

- **v1 -> v2**: `STARTUP_SCENE` 行が無いので Startup Scene は未設定のまま読み込まれます。
  値を推測して埋めることはしません。読み込んだあと保存すると v2 形式になります。
- **未対応バージョン（v0 以下 / v3 以上）**: 読み込みを失敗させ、
  `ApplySafeDefaults()` で全項目を既定値へ戻します。壊れたファイルの断片が
  設定として残らないようにしています。
- **プロジェクト設定でないファイル**: 同様に失敗＋既定値。
- **Scene 側**: 今回は形式を変えていないため移行処理の追加はありません。

---

## 10. Missing / Unknown Data の保持方法

Phase 2 の仕組みをそのまま通しており、今回の変更で経路を増やしていません。

- Runtime の Scene 読み込みは `ApplySceneData()` を通るため、Editor 読み込みと同じ規則。
- 型が解決できない Component は `MissingComponent` が
  型名 / TypeGUID / module_id / type_version / PropertyBag を丸ごと預かる。
- 型が知らないプロパティは PropertyRegistry が預かり、保存時に書き戻す。
- `RuntimeSceneService` は Missing / Unknown を**失敗にしない**。
  `LastLoadReport()` に件数を残し、`RuntimeContext::LogWarning` で診断へ出す。
- 復元できなかった Component（`skipped_components`）だけを失敗として扱う。
  Missing は「保持できている」、skipped は「復元できていない」で意味が違うため。

`--validate-runtime-scene` の 2 検査（Missing の内容保持・Unknown の件数記録）で確認しています。

---

## 11. Validation 一覧と検査数

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
| **`--validate-runtime-scene`（新規）** | **410-457** | **48** |
| **`--validate-scene-flow`（新規）** | **460-507 / 508-519** | **48 + 12 = 60** |

`--validate-scene-flow` は 2 段構成です。
前半（460-507）が Engine 側、後半（508-519）が Game Module の
`SceneTransitionBehaviour`。Engine の Validation から Game の型を参照しないよう、
呼び分けを `main.cpp` で行っています。

### `--validate-runtime-scene`（48 検査）の内訳

一時 Scene の書き出し / 初期状態 / Resolver 未接続 / 正常 Load /
ReadyToSwap と Completed の 2 段 / 親子復元 / Property 復元 / OnAwake 到達 /
旧 Handle 無効化 / 新 Handle 有効 / Event 購読の作成と切替後の消失 /
Collision 接触状態の作成と切替後の消失 / 削除予約中の切替 /
A→B 切替 / 同一 Scene Reload / AssetMissing / InvalidAssetType /
壊れた Scene / 未対応 Version / 失敗時の World・Handle・GUID 維持 /
失敗段階と件数の記録 / 空 GUID / Missing Component 保持 /
Unknown Property 保持 / ObjectReference 解決 / ComponentReference 解決 /
Busy 拒否（Loading 中・ReadyToSwap 中）/ 連続要求後の結果 / CancelPending

Scene Asset は `Saved/Validation/RuntimeScene/` へその場で書き出し、
`ISceneAssetResolver` のテスト実装（`TestSceneAssetResolver`）で GUID → パスを
検証側から差し替えています。既存の Scene 原本は読みも書きもしていません。

### `--validate-scene-flow`（60 検査）の内訳

ProjectSettings v2 往復 / 空 GUID 往復 / Startup と Prefab の独立 /
v1 読み込み / v1→v2 保存 / 未対応版の拒否と既定値 / 壊れたファイルの拒否 /
SceneFlow 未接続の ServiceUnavailable（Load / Reload / Return / Quit / 状態問い合わせ）/
Startup 未設定 / Startup 無効 / Startup 正常 / Startup 後の履歴 /
Load / 履歴の積み上がり / CanReturn / Reload で履歴が増えない /
失敗 Load の履歴保持・World 維持・理由の記録 / Return ×2 / 履歴枯渇 /
戻り先が無い Return / 連続要求 / 遷移中の CanReturn / 空 GUID /
履歴上限 / 履歴の健全性 / Quit 要求の記録・保持・受け取り
＋ Behaviour 側 12 検査（Runtime 未接続 / 遷移先未設定 / Trigger からの要求 /
即時 Swap しない / 同一フレーム 2 件目の拒否 / 再発火抑止 / Layer 判定 /
Trigger 側限定 / 失敗後の Behaviour 生存 / Quit モード / Trigger 経由の遷移完了）

---

## 12. 実行済み検証

すべて **Linux + g++ 11.4** 上での検証です。**MSVC ではありません。**

1. **g++ `-fsyntax-only -Wall -Wextra -std=c++17`**
   触った 19 の .cpp と、新規・変更した 9 つのヘッダ（単独 include）で警告 0・エラー 0。
   DirectXMath / windows.h は検証専用のスタブを `/tmp` に置いて解決しています
   （リポジトリには 1 ファイルも追加していません）。

2. **実際にビルドして実行**
   `RuntimeSceneService` / `SceneFlowService` / `Scene` / `SceneData` / `SceneSerializer` /
   `RuntimeContext` / `EventBus` / `CollisionEventDispatcher` / `ComponentRegistry` /
   `PropertyRegistry` / 組み込み Component / Game Behaviour をリンクした実行ファイルを作り、
   **既存 8 種＋新規 2 種の Validation を全部実行**しました。

   ```
   handles          PASS 53 checks
   serialization    PASS 65 checks
   missing          PASS 35 checks
   scene-version    PASS 71 checks
   behaviour        PASS 28 checks
   events           PASS 20 checks
   runtime-api      PASS 32 checks
   collision        PASS 33 checks
   runtime-scene    PASS 48 checks
   scene-flow       PASS 48 + 12 checks
   ```

3. **AddressSanitizer + LeakSanitizer**（`-fsanitize=address`、`detect_leaks=1`）
   上記 10 種すべてを ASan 付きで実行し、全件 PASS。
   この過程で **3 件の解放済みメモリ参照を検出し修正**しました（下記 14 参照）。

4. **ProjectSettings v1→v2 移行の単体実行**
   実際に書き出し・読み戻しを行い、出力テキストも目視確認しました。

5. **静的な整合確認**
   - `3dgp.vcxproj` / `3dgp.vcxproj.filters` の XML 妥当性（`xmllint --noout`）
   - 登録集合の一致: ClCompile 160/160、ClInclude 175/175
   - 重複登録 0、実体欠落 0
   - Runtime → Editor 逆依存 0、Engine → Game 逆依存 0、Runtime からの imgui 参照 0
   - 変更・新規 22 ファイルすべて CRLF・末尾改行あり。BOM は元の有無を維持
     （`RePlayEngine/Project/*` は元から BOM 無しなので BOM 無しのまま）
   - `git diff --check` で空白エラー 0

---

## 13. 未実施検証・残作業

### 未実施の検証

- **MSVC v145 / Visual Studio 2026 でのビルド** — 実行していません
- **Windows 実機での Validation 実行** — 実行していません
- **Release x64 構成のビルドと実行** — 実行していません
- **D3D11 Live Object Report** — 実行していません
- **Windows 上での ASan** — 実行していません（Linux の ASan のみ）
- **Editor（ImGui）を伴う手動確認** — 実行していません

Linux/g++ で通っても MSVC で通る保証はありません。特に次は実機確認が必要です。

- 関数ローカル `constexpr` のラムダ捕捉（C3493）
  → 新規コードの定数はすべて名前空間スコープへ置いてありますが、未確認です
- 不完全型 `unique_ptr` メンバのデストラクタ位置
  → `RuntimeSceneService` / `SceneFlowService` とも `.cpp` で定義済みですが、未確認です
- `std::quoted` / `std::filesystem` まわりの差異

### 残作業（Phase 8）

1. Project Settings の Startup Scene 選択 UI（`.replayscene` のみ選択可・Missing 表示・Clear・保存失敗表示）
2. Add Component から Native Behaviour を追加（BehaviourRegistry / TypeGUID を利用、型ごとの分岐を増やさない）
3. Inspector の未完成部分
   - `ComponentReference` … 所有 GameObject の選択のみで Component 一覧が未実装
   - `Array` … 表示のみで追加・削除・並べ替え UI が未実装
4. Missing Behaviour / Missing Component の表示と保持（データを壊さないこと）
5. Runtime 診断パネル（Current / Pending / Service State / Flow State / Last Error / Startup Scene / WorldInstanceID / Play・Edit）
6. Play Mode の安全性（Selection の付け替え、終了時の復元、Undo 履歴の分離）
7. SceneReference の Asset Database 経由の選択
8. **framework 統合** — `RuntimeSceneService` が World を所有する形への移行。
   現状 `framework` は `object_scene` / `object_scene_runtime` を**値メンバ**で持っており、
   `Scene` はムーブ禁止なので単純な差し替えができません。
   `Source/app/Runtime/framework_gameobject_scene.cpp`（1205 行）を中心に、
   参照箇所をすべて `RuntimeSceneService::ActiveWorld()` 経由へ置き換える必要があります。
   ここが済むと Phase 7 の「起動時の流れ」も接続できます。
9. `--validate-editor-integration`（520-579）

### 残作業（Phase 9）

- MSVC Debug / Release でのビルドと全 Validation 実行
- Scene v7〜v11 の実ファイル読み込み（現状は `--validate-scene-version` の合成データのみ）
- 100 回以上の Scene Load / Reload、1000 以上の GameObject での負荷確認
  （SceneFlow 検証では 24 回の連続遷移まで確認済み）
- D3D11 Live Object / Windows ASan

---

## 14. 既知の制限・今回見つけて直した不具合

### ASan で見つけて修正したもの（3 件）

1. **`RuntimeContext` と World の破棄順による解放済みメモリ参照**
   `RuntimeContext` が先に消えると、World の破棄中に走る
   `BehaviourComponent::OnRuntimeDestroy()` が `Services().Runtime()` 経由で
   破棄済みの Context を触り、`EventBus::UnsubscribeOwner` でクラッシュしました。
   - `~RuntimeContext()` が World 側の back-pointer を外すようにしました。
   - `RuntimeSceneService` も、World を壊す前（デストラクタと SwapWorlds）に
     `Services().SetRuntime(nullptr)` を行うようにしました。
   - 寿命の約束（World を先に宣言し RuntimeContext を後に宣言する）をコメントで明示しました。

2. **`BehaviourValidation.cpp:249` の解放済み GameObject 参照**
   破棄後に `doomed_object->ID()` を読んでいました。ID を破棄前に控える形へ直し、
   併せて `|| true` で常に成功していた検査を実のある検査へ置き換えました。

3. **`BehaviourValidation.cpp:916` の解放済み Component 参照**
   削除予約 → `step()`（FixedUpdate の同期点で実体が解放される）→ `probe->records`
   の順で読んでいました。実体がある間に `dispatcher.Dispatch()` を直接呼ぶ形へ直しました。

いずれも MSVC Debug では ASan を通していなかったため表面化していなかったものです。

### 検証中に直した自分の誤り（1 件）

`--validate-scene-flow` で「履歴のどこにも現在の Scene が現れないこと」を検査していましたが、
A→B→A→B と往復すれば深い位置に現在と同じ Scene が残るのが**正しい戻り先の並び**です。
実行して初めて失敗し、「空 GUID が無いこと」と「1 つ前が自分自身でないこと」へ直しました。
実行していなければ MSVC で失敗していた検査です。

### 既知の制限

- `RuntimeSceneService::Tick()` の末尾で `EventBus::Global().Dispatch(nullptr)` を呼びます。
  自分が積んだ通知を確実に配り、待ち行列が伸び続けないようにするためですが、
  他所が Global Bus へ積んだイベントもこの同期点で配送されます。
- Scene 遷移通知（`BeforeSceneUnload` など）は Global Bus のみ。Scene 単位の Bus は
  入れ替えで捨てられるため使っていません。
- `SceneFlowService::Tick()` は内部で `RuntimeSceneService::Tick()` を呼びます。
  SceneFlowService を使う場合、下位を別に Tick しないでください（2 段進んでしまいます）。
- 一般 RigidBody の衝突は実装していません。Collision で取れるのは
  `CharacterGround` と `CharacterWall` だけという Phase 3〜5 の方針をそのまま維持しています。
- Audio / Input Action / SaveGame / Runtime UI は未実装のまま。`〜Available()` は常に false。
- C# は実装していません。`BehaviourRegistry` の `IBehaviourProvider`、`TypeGUID`、
  Missing Behaviour 保持、Handle Facade、WorldInstanceID、Runtime API 境界、
  そして Scene 切り替え通知（Global Bus の `BeforeSceneUnload` / `WorldChanged`）は
  いずれも将来の Managed Provider 用の拡張点として残してあります。
- `3dgp.vcxproj.filters` に `Source\app\Editor` / `Source\app\Runtime` / `Source\mesh` の
  `<Filter Include>` 宣言がありません（**今回の変更以前から**）。Visual Studio は
  自動生成するので実害はありませんが、無関係な整形を避けるため触っていません。

---

## 15. Windows 実機で実行する Debug x64 ビルドコマンド

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

## 17. Release x64 Clean Rebuild コマンド

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

## 18. Release 全 Validation コマンド

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

## 19. 手動確認手順

Editor 統合が未実施なので、確認できるのは次までです。

1. Debug ビルド後、`--validate-runtime-scene` と `--validate-scene-flow` を実行し、
   終了コードが 0、標準エラーに `OK: 48 checks passed` /
   `OK: 48 checks passed` と `OK: 12 checks passed` が出ることを確認する。

2. `Saved/Validation/RuntimeScene/` と `Saved/Validation/SceneFlow/` に
   一時 Scene が作られていることを確認する。既存の Scene 原本
   （`resources/` などに置いてある `.replayscene`）の更新日時が変わっていないことも確認する。

3. Editor を通常起動し、Validation 用のコードが通常起動へ影響していないことを確認する。
   Validation は `WinMain` の先頭でコマンドライン一致時だけ走り、
   一致しなければ `-1` を返して通常経路へ抜けます。

4. Scene を 1 つ保存し、`resources/Project.replayproject` を開いて
   `REPLAY_PROJECT 2` と `STARTUP_SCENE ""` の行があることを確認する。
   （Startup Scene の設定 UI はまだ無いので、値は空のままです。）

5. v1 形式の `.replayproject`（`STARTUP_SCENE` 行が無いもの）を用意して起動し、
   落ちないこと・Default Controlled Character Prefab の設定が保たれることを確認する。

**確認できないこと**: Startup Scene の選択 UI、Runtime 診断パネル、
Play Mode での Scene 遷移、Inspector の Array / ComponentReference 編集。
いずれも Phase 8 が未実施のためです。

---

## 20. git diff --stat 相当の変更量

追跡済みファイルの差分:

```
 3dgp.vcxproj                                       |  10 +
 3dgp.vcxproj.filters                               |  33 +++
 RePlayEngine/Project/ProjectSettings.cpp           |  35 +++
 RePlayEngine/Project/ProjectSettings.h             |  49 +++-
 RePlayEngine/Project/ProjectSettingsSerializer.cpp |  41 ++-
 RePlayEngine/Project/ProjectSettingsSerializer.h   |  16 +-
 RePlayEngine/Runtime/API/RuntimeContext.cpp        |  90 +++++-
 RePlayEngine/Runtime/API/RuntimeContext.h          |  66 +++++
 RePlayEngine/Runtime/Validation/BehaviourValidation.cpp | 23 +-
 Source/app/Runtime/main.cpp                        |  29 ++
 Source/game/Behaviours/ValidationBehaviours.cpp    | 323 +++++++++++++++++++++
 Source/game/Behaviours/ValidationBehaviours.h      |  13 +
 12 files changed, 718 insertions(+), 10 deletions(-)
```

未追跡（新規）ファイル: 10 ファイル、合計 2788 行。

合計の変更量: **22 ファイル / 約 3500 行の追加、10 行の削除、削除ファイル 0**。
