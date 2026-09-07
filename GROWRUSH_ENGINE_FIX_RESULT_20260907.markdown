# Grow Rush 起動モード別の終了と C# 読み込みの修正結果

対象: `C:\Users\2250298\Desktop\teamProject_2_3`、ブランチ `sinotake`。
ゲーム固有の回避策は入れていない。すべてエンジン共通の経路を直している。

---

## 1. 原因（確定）

### 症状 2: 単体 EXE の終了ボタンで透明なエディターが出て閉じられない

確定。経路は 1 本で、単体ゲームでは必ずこうなる。

1. ゲームの終了ボタン / Esc → `Runtime.QuitApplication()`
   → `SceneFlowService::QuitApplication()`（プロセスは落とさない契約）。
2. `framework::tick_runtime_scene_flow()` が受け取り、起動モードを分けずに
   `request_object_scene_action(exit_application)` を呼んでいた。
3. `request_object_scene_action()` は `object_editor_context.Dirty()` を見て、
   Dirty なら未保存確認を要求し `editor_mode = true` / `set_edit_mode(true)` にする。
4. **単体ゲームでも起動時に既定の編集 Scene を作る**
   (`create_object_scene()` → `MarkDirty()`) ため、`Dirty()` は常に true。
   → 必ず編集用ダイアログの経路へ入り、ゲームの上に Editor UI が出る。
5. ウィンドウの × も同じ。`WM_CLOSE` → `confirm_object_scene_close()` →
   Dirty → `request_object_scene_action()` → false を返す → 閉じない。

**裏付け**: `%LOCALAPPDATA%\GROW RUSH\engine_log.txt` の 3 回の起動
(06:16 / 06:22 / 10:38) と `GrowRush_Title\engine_log.txt` の 2 回 (11:12) は、
どれも `WM_CLOSE` / `WM_DESTROY` / `メッセージループを抜けた` が 1 行も無い。
`PostMessage(WM_CLOSE)` へ到達していないことがログ側からも確認できる。

### 症状 1: エディターの Play で移動も UI ボタンも反応しない

確定。Grow Rush の移動もボタン処理も全部 C#（`GrowArena` / `GrowTitle` /
`GrowResult`）なので、C# Assembly が 1 つも載っていなければ両方とも死ぬ。
`Saved/engine_log.txt` の 10:13 / 11:07 / 11:12 の各起動で、Grow Rush を含む
16 型すべてが `C# Assembly is not loaded.` になっている。

`CSharpScriptBackend::Initialize()`（Editor 経路）の作りが原因:

```
if (!GameScriptsBuildRequired()) { ... ReloadLastBuiltAssembly(); return true; }
CompileAndReload(nullptr);   // ← 戻り値を見ていない
return true;
```

コメントは「コンパイル失敗時は直前に成功した Assembly を Managed 側が保持する」
としていたが、それが成り立つのは**一度ロードしたあとのホットリロードだけ**。
起動直後はまだ何もロードしていないので、維持する相手がいない。
起動時のビルドが失敗すると Assembly ゼロのまま Play へ入る。
単体 EXE は packaged 経路でビルドせずに Release の DLL を直接ロードするため
影響を受けない。「単体では動くがエディターでは動かない」の分かれ目はここ。

さらに、失敗の理由がどこにも残らなかった:

- `ScriptRuntime::Initialize()` の戻り値は捨てられていた。
- `Initialize()` は Editor を止めないために失敗しても true を返す。
- ログには型ごとの一般的な 1 行しか出ない。

このため「最初に何が失敗したのか」がログから分からない状態だった。

---

## 2. 修正（エンジン共通）

### 2-1. 終了要求の宛先を 3 つに分けた

`framework::handle_game_quit_request()` / `framework::request_application_quit()`
を新設し、判断を 1 か所へ集約した。

| 起動状態 | 「終了」の意味 | 動き |
|---|---|---|
| 単体ゲーム | プロセス終了 | 未保存確認を通さずそのまま閉じる |
| エディター内 Play | Play の終了 | `exit_object_play_mode()` で編集シーンへ戻す。アプリは閉じない |
| エディター本体 | アプリ終了 | 未保存編集があれば確認を出す（従来どおり） |

- `Source/app/Runtime/framework_runtime_scene.cpp`
  `tick_runtime_scene_flow()` が `handle_game_quit_request(QuitReason())` を呼ぶ。
- `Source/app/Runtime/framework_gameobject_scene_serialization.cpp`
  `request_object_scene_action()` と `confirm_object_scene_close()` に
  単体ゲームの分岐を追加。
- `Source/app/Editor/framework_editor.cpp`
  File > Exit を `request_application_quit()` へ。動きは従来と同じ。

編集内容の保護は落としていない。未保存のエディターは今までどおり確認を出し、
確認へ答えるまで閉じない。

### 2-2. 起動時の C# ビルド失敗から復旧する

`RePlayEngine/Scripting/CSharp/CSharpScriptBackendHost.cpp`

- `CompileAndReload()` が失敗し、かつ Assembly を 1 つもロードしていない場合だけ、
  ディスクに残っている前回のビルド結果を読み直す。
- ホットリロード時の既存仕様（失敗しても直前の Assembly を維持）は変えていない。
  ロード済みなら何もしない。
- `last_build_` は失敗したビルドの記録のまま残す。成功へ書き換えない。

### 2-3. 失敗の理由を記録する

- `CSharpScriptBackend::StartupDiagnostic()` / `StartupUsedExistingAssembly()` を追加。
- `framework::log_csharp_startup_state()` が起動ごとに `engine_log.txt` へ 1 行残す。
  - 正常 … `[Script] C# Assembly ロード済み`
  - 復旧 … `[Script] C# ビルド失敗。ディスクに残っていた前回の Assembly で起動しました…`
  - 失敗 … `[Script] C# を用意できませんでした。C# の Component はすべて動きません: <理由>`
- `LoadType()` の失敗メッセージに理由を付ける
  （`C# Assembly is not loaded: <理由>`）。
- `LoadGameAssembly()` が entry point 未解決のとき、理由なしの false を返さなくなった。

### 2-4. 起動に失敗した Backend を後から復旧できるようにした

`CSharpScriptBackendAssembly.cpp` の `CompileAndReload()` は、
未初期化なら先に `Initialize()` をやり直す。
以前は起動時に hostfxr / Managed API の接続へ失敗すると
`load_assembly_` が空のままで、Editor の「C# Build & Reload」を何度押しても
直らなかった。

---

## 3. 追加した回帰確認

| コマンド | 内容 |
|---|---|
| `3dgp.exe --validate-app-quit` | 終了要求の宛先 3 分岐。単体ゲーム / エディター内 Play / 未保存ありエディター / 未保存なしエディターの 4 状態を、実際の `framework` で確認（15 項目） |
| `3dgp.exe --validate-csharp-startup-recovery` | 起動時ビルド失敗 → ディスクの Assembly で復旧し、理由が残ることを確認 |

`--validate-csharp-startup-recovery` を別コマンドにしてあるのは、
`hostfxr_initialize_for_runtime_config` が 1 プロセス 1 回しか通らないため。
同じプロセスで Backend を作り直すと 0x80008081 で手前で止まり、
確かめたい「プロセス最初の Initialize()」を通せない。

---

## 4. 確認表

### 4-1. 確認済み（自動）

| 項目 | 方法 | 結果 |
|---|---|---|
| Release x64 ビルド | `MSBuild 3dgp.vcxproj /p:Configuration=Release /p:Platform=x64` | OK (exit 0) |
| Debug x64 ビルド | 同上 `/p:Configuration=Debug` | OK (exit 0) |
| C# Debug ビルド | `dotnet build Scripts/RePlayGameScripts.csproj -c Debug` | OK 0 警告 0 エラー |
| C# Release ビルド | 同上 `-c Release` | OK 0 警告 0 エラー |
| 終了要求の宛先 3 分岐 | `--validate-app-quit` (Release / Debug) | OK 15 項目 |
| 起動時 C# ビルド失敗からの復旧 | `--validate-csharp-startup-recovery` (Release / Debug) | OK 6 項目 |
| C# Behaviour 一式（既存） | `--validate-csharp-scripting` | OK 77 項目 |
| SceneFlow（終了要求の契約を含む） | `--validate-scene-flow` | OK 59 + 12 項目 |
| Runtime Scene | `--validate-runtime-scene` | OK 48 項目 |
| Script 基盤 | `--validate-script-core` / `--validate-script-lifecycle` | OK 49 / 53 項目 |

### 4-2. 確認済み（実機・単体 EXE）

パッケージは `Tools\GrowRush\Play-GrowRush.ps1 -NoBuild` で作った実運用の手順。
ログは `%LOCALAPPDATA%\GROW RUSH\engine_log.txt`。

| 項目 | 結果 |
|---|---|
| ウィンドウの × で終了する | OK。`WM_CLOSE` → `WM_DESTROY` → `メッセージループを抜けた (WM_QUIT)` が記録され、透明なエディターは出ない |
| ゲームの終了ボタンで終了する | OK。`GROWRUSH_TEST=1` の自動確認が 3 試合まわして Quit を押し、`終了要求 (GrowRush title)` → `WM_CLOSE` → 終了（12:30:29 開始 → 12:31:10 終了、exit code 0） |
| 放置しても勝手に終了しない | OK。60 秒間そのまま起動し続ける |
| 単体 EXE の C# ロード | OK。`[Script] C# Assembly ロード済み` |
| ゲーム 1 周（タイトル→60秒→結果→再戦→タイトル→120秒→結果→タイトル） | OK。`session.log` に `PASS title -> 60s -> result -> replay 60s -> title -> 120s -> result -> title -> quit` |

### 4-3. 確認済み（実機・エディター）

`Saved/engine_log.txt` に残った、Visual Studio から起動された実際の Editor セッション。

| 時刻 | ログ | 意味 |
|---|---|---|
| 11:55:38 / 12:17:09 | `[Script] C# Assembly ロード済み` | 通常経路。C# が載っている |
| 12:20:21 | `[Script] C# ビルド失敗。ディスクに残っていた前回の Assembly で起動しました。ソースより古い可能性があります: … error CS1002 …` | **修正前ならここで C# が全滅していた実例。** 検証用に一瞬置いた壊れた .cs をビルドが拾って失敗したが、ディスクの Assembly で復旧し、理由も残った |
| 11:56:50 | `[Script] Runtime Error [C#] … GrowRush missing object: Camera` | C# が実際に走っている証拠（内容はゲーム側の問題） |

### 4-4. 未確認（手動でお願いしたい項目）

自動化できないもの、または実機の操作が要るもの。

| 項目 | 手順 | 期待 |
|---|---|---|
| エディター Play での移動 | Editor で GrowRush_Title を開き F5 → 1/2 キーで試合開始 → WASD | キャラクターが動く |
| エディター Play での UI クリック | Play 中に Scene View（中央のドック領域）の中にあるボタンを押す | 反応する |
| エディター Play 中のゲーム内 Quit | Play 中にタイトルの終了ボタン、または Esc | **Play だけ止まって編集シーンへ戻る。エディターは閉じない** |
| 通常のエディター終了（未保存あり） | 何か編集して × または File > Exit | 未保存確認が出る。保存/破棄/キャンセルが従来どおり効く |
| 通常のエディター終了（未保存なし） | 保存してから × | 確認なしで閉じる |
| 単体 EXE の Alt+F4 | ゲーム中に Alt+F4 | そのまま終了する |

---

## 5. 修正の対象外だが見つけたこと

### 5-1. Play 中のゲーム UI は全画面へ出て、押せるのは Scene View の中だけ

`Source/app/Runtime/framework_gameobject_scene.cpp` の
`object_ui_viewport_target()` は、条件に `!object_scene_play_mode` があるため
**Play 中は Scene View の矩形ではなくクライアント全体**を返す。
描画 (`framework_dx12_ui.cpp:253`) と当たり判定
(`framework_gameobject_scene_runtime.cpp`) は同じ矩形を使うので座標はずれない。

ただしクリックの可否は別条件で、
`input_captured = ImGui::GetIO().WantCaptureMouse && !scene_view_hovered`。
そのため中央の Scene View の外（Inspector や Console の上）へ出たゲーム UI は、
エディターのパネルに隠れたうえに押せない。

今回の症状の原因ではない（C# が載っていなければ位置に関係なく全部反応しない）。
直すなら「Play 中も Scene View の矩形へ描く」か「Play 中はパネルを畳む」かの
設計判断が要るので、指示なしには変えていない。

### 5-2. キーボードは Scene View のフォーカスを要求していない

`framework_update.cpp` の
`keyboard_captured = WantCaptureKeyboard && !(editor_mode && play && scene_view_focused)`
は、ImGui が文字入力中でなければ `WantCaptureKeyboard` が false なので、
Scene View にフォーカスが無くても WASD は Gameplay へ届く。ここは問題なし。

### 5-3. 12:25 の単体 EXE が 9 秒で終了した件

一度だけ、単体 EXE が起動 9 秒後に終了した (`12:25:27` → `12:25:36`)。
同じ時間帯に別セッションがエンジンと C# を再ビルドしており、
`Saved/GrowRush` 配下の DLL が入れ替わっている最中だった可能性が高い。
理由記録を入れた版では再現しない（60 秒放置で終了せず、
Quit ボタンでは `終了要求 (GrowRush title)` が必ず残る）。
再発したら `engine_log.txt` の `終了要求 (...)` 行の有無で、
ゲームが要求したのか外から閉じられたのかを切り分けられる。

---

## 6. 作業中の注意点

- **同じ作業ツリーで別の Claude セッションが並行して作業していた。**
  12:15 頃に `framework_gameobject_scene_play.cpp` へ
  `ensure_csharp_ready_for_play()`（Play 前に C# を追いつかせる）が追加され、
  12:21 に `eebf1db 起動モードで終了の宛先を分けC#の起動失敗とPlay前の未保存を可視化する`
  として**こちらの変更ごと 1 コミットにまとめられている**。
  内容は競合せず補い合う関係だが、コミットは 2 セッション分が混ざっている。
  その後の `b9480d7 終了要求の理由をengine_logへ残す` はこちらの単独コミット。
- ビルドが同時に走ると MSBuild の tracker log (`3dgp.tlog`) を取り合って
  `MSB6003` / `LNK1104` で落ちる。実際に 3 回落ちた。
- ユーザー編集中の `Scripts/GrowRush/*.cs`、`resources/AssetDatabase.replaydb`、
  GrowRush の Title / Arena シーンには手を触れていない。
- `--validate-csharp-startup-recovery` はビルドを故意に失敗させるため、
  実行中の 1 秒ほど `Scripts/ValidationStartupBrokenBehaviour.cs` を置く。
  その最中に Editor を起動すると（12:20 に実際に起きた）そのビルド失敗を拾う。
  終了時に必ず削除するので後には残らない。
