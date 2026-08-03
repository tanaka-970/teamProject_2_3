# Pre-Scripting Editor & Runtime Stabilization — Phase A-1 完了報告

対象: `teamProject_2_3`（RePlayEngine / 3dgp）
実施日: 2026-08-03
実施範囲: **Phase A-1（Play 開始直後の Ctrl+Z で Animation が止まる）のみ**

コミットしていません。ブランチも切り替えていません。

---

## 0. 最初に — 進行状況の正直な報告

いただいた指示は Phase A〜F の 6 段階で、Camera System の再設計、Input Routing、View Cube、Debug Draw、Editor UI の全面整理と日本語化、Docking、Shader System の責務分離まで含みます。**これは Phase 0 + Phase 1 の 6 倍を超える分量で、1 セッションでは終わりません。**

そこで、ご指示の「Phase A のバグを残したまま先へ進まない」を最優先し、**A-1 の原因特定と修正、再発防止の Validation まで**を完了させました。A-2 / A-3 以降は未着手です。

中途半端に全 Phase へ手を広げて、どれも検証されていない状態にするより、1 件を確実に直しきる方が価値が高いと判断しました。**A-1 は今回の 3 件のうち最も深刻で、しかも Animation だけの問題ではありませんでした。**

---

## 1. A-1 の直接原因

### 見つかったもの

`Scene::Clear()` の最終行が `started_ = false;` です（`Scene.cpp`）。

`ApplySceneData()` は内部で必ず `Scene::Clear()` を通りますが、**自身では `Start()` を呼びません**。これは仕様で、`SceneData.h:216` にも「OnStart / OnEnable はここでは呼ばれない。呼び出し側が `Scene::Start()` を呼ぶ」と明記されています。

リポジトリ内で `ApplySceneData` を呼んでいるのは 4 か所です。

| 呼び出し元 | `Start()` を呼ぶか |
|---|---|
| `framework::load_object_scene`（`framework_gameobject_scene.cpp:661`） | **呼ぶ**（`:667`。「ApplySceneData の中では OnStart / OnEnable を呼ばないので」というコメント付き） |
| `RuntimeSceneService::BuildStagingWorld`（`:294`） | **呼ぶ**（`SwapWorlds` の `:415`） |
| `SceneEditHistory::Undo`（`:72`） | **呼ばない** ← 原因 |
| `SceneEditHistory::Redo`（`:85`） | **呼ばない** ← 原因 |

### 何が起きていたか

Editor で Ctrl+Z を一度でも押すと:

1. `ApplySceneData` が `Scene::Clear()` を通り、`started_` が `false` に落ちる
2. 誰も `Start()` を呼び直さない
3. 以降 `Scene::Update` / `FixedUpdate` / `LateUpdate` はすべて先頭の `if (!started_ || loading_) return;` で早期 return する
4. **Scene 上の全 Component が二度と更新されなくなる**

### 重要な訂正 — Animation だけの問題ではありません

止まっていたのは Animator だけではなく、**`Rotator` も `CharacterMotor` も `PlayerController` も、Scene 上の全 Component** です。`OnRuntimeAwake` / `OnEnable` / `OnStart` も走らなくなるため、Undo で作り直された Component は初期化すらされていませんでした。

Animation が最初に気づかれたのは、それが一番目に見える変化だからです。

「保存せず再起動すれば治る」のも説明がつきます。読み込み経路（`load_object_scene`）は `Start()` を対で呼ぶためです。

### Play 中の Ctrl+Z について

`EditorContext::Undo()` は `CanEdit()`（= `scene_ != nullptr && !play_mode_`）で弾いていたため、**Play 中の Ctrl+Z はもともと Undo 履歴へ流れていません**。

ただし `false` を返すだけで何も表示しなかったため、ユーザーからは「押したのに何も起きない」としか見えませんでした。報告にあった「Play 直後の Ctrl+Z」の症状は、**Play を押す前に Edit Mode で Undo していたことで編集 Scene が凍り、Stop 後にそれが表面化した**ものと考えられます。

---

## 2. 修正内容

### 2-1. `SceneEditHistory` — 実行状態を復元する

`Undo` / `Redo` の共通処理を `ApplySnapshot()` へ切り出し、**スナップショット適用の前後で `started_` を保存・復元**します。

```cpp
void ApplySnapshot(const SceneData& data, Scene::Scene& scene)
{
    const bool was_started = scene.Started();

    SceneLoadReport report;
    ApplySceneData(data, scene, report);

    if (was_started) scene.Start();
}
```

**無条件に `Start()` を呼ばない理由:** Editor で読み込んだ直後のまだ開始していない Scene を Undo しただけで動き出してしまいます。「元の状態へ戻す」のが Undo の意味なので、実行状態も元へ戻します。この挙動も Validation で確認しています。

### 2-2. `EditorContext` — 黙って無視しない

Play 中の Undo / Redo は引き続き実行しませんが、**理由を Status へ出す**ようにしました。履歴の端まで来た場合も同様です。

```
実行中は元に戻せません（停止してから操作してください）
これ以上元に戻せません
```

**Ctrl+Z 自体は無効化していません。** 禁止事項「Ctrl+Z を完全に無効化して原因を隠す」に該当しない形で、原因そのものを直しています。

また `EditorContext::Undo()` に残っていた書きかけのコメント（`// ApplySceneData は Scene を作り直すため、実行中なら開始し直す必要がある。`）を、実際の担当箇所を指す記述へ置き換えました。作者が問題に気づいて書き切らなかった痕跡です。

---

## 3. Validation

### `--validate-animation-undo`（終了コード帯 800-859 / 33 検査）

`AnimatorComponent` そのものではなく、**更新回数を数えるだけの検証用 Component** で確かめています。本物の Animator は Animation Clip / SkinnedMesh / GPU リソースを必要とし、ヘッドレスの検証に載らないためです。確かめたいのは Animator 固有の処理ではなく「Undo のあとも Scene が Component を更新し続けるか」なので、これで十分かつ確実です。

検査項目:

- 開始後は毎フレーム更新される
- **Undo のあとも `Started()` が true のまま**
- **Undo のあとも毎フレーム更新され続ける**（止まると Animation が凍る）
- Undo で作り直された Component へ `Awake` が届く
- Undo で保存値が戻る
- Redo でも同じ
- 履歴の先頭でさらに Undo を押しても状態が壊れない（報告された再現手順のひとつ）
- Undo / Redo を **20 往復**しても更新が続く
- **まだ開始していない Scene は Undo しても動き出さない**
- Play 中は Undo / Redo を実行せず、理由が Status へ出る
- Play 中の Undo 要求のあとも更新が続く

### 検証がバグを実際に捕まえることの確認

修正の 1 行（`if (was_started) scene.Start();`）を一時的に外して再ビルドし、**8 件が FAIL することを確認**しました。

```
[FAIL 807] Undo のあとも Scene の実行状態が保たれる（started_ が false のまま残らない）
[FAIL 810] 作り直された Component へ Awake が届く
[FAIL 811] Undo のあとも毎フレーム更新され続ける（これが止まると Animation が凍る）
[FAIL 812] Undo のあとも時間が進む
[FAIL 814] Redo のあとも実行状態が保たれる
[FAIL 816] Redo のあとも更新され続ける
[FAIL 819] 空振りの Undo で実行状態が壊れない
[FAIL 820] 空振りの Undo のあとも更新され続ける
```

外した変更は元に戻し、修正が復元されていることを確認済みです。

### 結果（Linux g++ 11.4）

| コマンド | 検査数 | 結果 |
|---|---|---|
| `--validate-animation-undo` | 33 | **ExitCode=0** |

### 回帰

| コマンド | 検査数 | 結果 |
|---|---|---|
| `--validate-behaviour` | 28 | **ExitCode=0** |
| `--validate-serialization` | 65 | **ExitCode=0** |

（Phase 1 で確認済みの 480 検査のうち、Editor 側の変更が影響しうるものを再実行しました。）

### 静的検査

| 項目 | 結果 |
|---|---|
| `-fsyntax-only -Wall -Wextra -std=c++17` | **警告 0** |
| `.vcxproj` / `.filters` の ClCompile 一致 | **176 / 176**、差分 0、重複 0、実体欠落 0 |
| `.vcxproj` / `.filters` の ClInclude 一致 | **192 / 192**、差分 0、重複 0、実体欠落 0 |
| 新規ファイルの BOM / CRLF / 末尾改行 | UTF-8 BOM + CRLF + 末尾改行 |
| 変更した既存ファイルの BOM | 元の有無を維持 |

---

## 4. 新規 / 変更ファイル

### 新規（2 件）

- `RePlayEngine/Editor/Validation/AnimationUndoValidation.h`
- `RePlayEngine/Editor/Validation/AnimationUndoValidation.cpp`

### 変更（4 件）

- `RePlayEngine/Editor/Commands/SceneEditHistory.cpp` — `ApplySnapshot()` の追加と実行状態の復元
- `RePlayEngine/Editor/Core/EditorContext.cpp` — Play 中の理由表示、書きかけコメントの整理
- `Source/app/Runtime/main.cpp` — `--validate-animation-undo` の分岐
- `3dgp.vcxproj` / `3dgp.vcxproj.filters` — 新規 2 件の登録

**削除: 0。**

---

## 5. Windows MSVC 用コマンド

### ビルド

Visual Studio でソリューションを開き直してから（`.vcxproj` を更新しているため）:

```bat
msbuild 3dgp.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m
```

### Validation（Phase A-1 + Phase 1 + 既存）

```bat
@echo off
for %%C in (
  --validate-animation-undo
  --validate-script-core
  --validate-script-lifecycle
  --validate-script-serialization
  --validate-handles
  --validate-serialization
  --validate-missing-component
  --validate-scene-version
  --validate-behaviour
  --validate-events
  --validate-runtime-api
  --validate-collision
  --validate-runtime-scene
  --validate-scene-flow
  --validate-editor-integration
  --validate-stress
  --validate-shutdown
) do (
  start /wait "" x64\Debug\3dgp.exe %%C
  echo %%C ExitCode=!ERRORLEVEL!
)
```

`setlocal enabledelayedexpansion` を先頭へ付けてください。**`start /wait` を外すと `%ERRORLEVEL%` が常に 0 になります**（SubSystem が Windows のため）。

### Release

```bat
msbuild 3dgp.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m
```

---

## 6. 手動確認手順（A-1）

1. Animation 付き GameObject を配置した Scene を開く
2. 何か 1 つプロパティを変更する
3. **Ctrl+Z を押す**
4. → Animation が動き続けること（従来はここで止まっていた）
5. さらに Ctrl+Z を押して履歴の先頭まで戻す
6. もう一度 Ctrl+Z を押す（空振り）
7. → Status に「これ以上元に戻せません」と出て、Animation は動き続けること
8. Play を押す → Animation が動くこと
9. Play 中に Ctrl+Z → Status に「実行中は元に戻せません」と出て、何も壊れないこと
10. Stop → 編集 Scene の Animation が動き続けること
11. Play / Stop を 20 回繰り返しても同じであること

---

## 7. 未実施項目

### Windows 実機

- MSVC でのビルド（g++ が通っても MSVC 固有のエラーは出うる）
- 上記 Validation の終了コード
- D3D11 Live Object Report（今回 GPU リソースには触れていないので増減は無い見込み）
- Editor UI の手動確認

### 未着手の Phase

| Phase | 内容 | 状態 |
|---|---|---|
| A-2 | CameraComponent の値が適用されない | **未着手** |
| A-3 | Player の設定速度と実速度が一致しない | **未着手** |
| B | リアルタイム編集 / Input Routing | **未着手** |
| C | Camera System 再整理 / Preset / View Cube / Culling | **未着手** |
| D | Debug 表示 | **未着手** |
| E | Editor UI 整理 / 日本語化 / 保存ログ / Docking | **未着手** |
| F | Shader System 責務分離 | **未着手** |

---

## 8. 次のセッションへの提案

A-2 と A-3 は独立した不具合なので、次に着手するならこの順です。

**A-2（CameraComponent）で最初に見るべき箇所（Phase 0 の調査から）:**
`PropertyRegistry::Apply` は `OnPropertyChanged(nullptr)` を呼びますが、**Inspector の編集経路はこれを呼んでいませんでした**。Phase 1 で `InspectorPanel` の編集確定時に呼ぶよう追加済みなので、CameraComponent 側が `OnPropertyChanged` で Projection を作り直す実装を持てば、A-2 の大半はその経路に乗る見込みです。ただし Runtime Camera と Editor Camera の状態共有の有無は未調査です。

**A-3（Player 速度）:** `PlayerControllerComponent` / `CharacterMotorComponent` / `PlayerInputComponent` の 3 つに速度係数が分散していないかの確認から始めるのが確実です。こちらも未調査です。

---

## 9. 推測と実確認の区別

### 実行で確認したこと

- `--validate-animation-undo` 33 検査が 0
- 修正を外すと 8 検査が FAIL する（＝検証が実際にこのバグを捕まえる）
- `--validate-behaviour` / `--validate-serialization` の回帰が 0

### 静的にだけ確認したこと

- `ApplySceneData` の全呼び出し 4 か所と、`Start()` の有無
- 警告 0、`.vcxproj` / `.filters` の整合、改行と BOM

### 推測

- 報告された「Play 直後の Ctrl+Z」の症状は、Edit Mode で先に Undo したことによる編集 Scene の凍結が Stop 後に表面化したもの、と考えています。Play 中の Ctrl+Z 自体は `CanEdit()` で弾かれており、コード上は Undo 履歴へ流れません。**実機で再現手順どおりに確かめてはいません。**
- MSVC でのビルド可否

以上。
