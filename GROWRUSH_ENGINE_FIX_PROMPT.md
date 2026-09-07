# エンジン共通機能の調査結果と修正依頼

RePlayEngine の次の不具合を、ゲーム固有の回避策ではなくエンジン側から修正してください。
作業場所: `C:\Users\2250298\Desktop\teamProject_2_3`、既存ブランチ: `sinotake`。

## 症状

1. Grow Rush は単体 EXE で操作できるが、編集エディターの Play では移動も UI ボタンも反応しない。ユーザーは F1＋F5 でも改善しないと報告。
2. 単体 EXE の終了ボタンで背景が透明の編集エディターが現れ、通常操作で閉じられない。

## 調査で確認した事実

### 終了経路

- `Source/app/Runtime/framework_runtime_scene.cpp` の `tick_runtime_scene_flow()` は `QuitRequested()` を受けると、起動モードを分けず `request_object_scene_action(exit_application)` を呼ぶ。
- `Source/app/Runtime/framework_gameobject_scene_serialization.cpp` の同関数は、Play を止め、編集コンテキストが Dirty なら未保存確認を要求して `editor_mode = true` と `set_edit_mode(true)` を実行する。単体ゲームを除外していない。
- 同ファイルの `confirm_object_scene_close()` にも単体ゲームの除外がない。
- このため、単体ゲームで編集用の未保存確認に入る経路はコード上存在する。報告された透明画面との整合性は高いが、実際の終了クリック時に Dirty になった起点、透明描画、終了不能の全過程はまだ実機で追跡していない。
- エディター内ゲームの Quit は、編集アプリ終了ではなく Play 停止へ分けるべきか、SceneFlowService の既存契約も確認する。

### C# の読み込み

- `Saved/engine_log.txt` の 2026-09-07 11:12 頃に、GrowArena / GrowTitle / GrowResult を含む複数の C# 型で `C# Assembly is not loaded.` が記録されている。Grow Rush だけの入力処理とは限らない。
- `RePlayEngine/Scripting/CSharp/CSharpProject.h` ではエディター用ビルドの既定構成が Debug。単体パッケージは Release を利用する。
- 調査開始時の Debug C# DLL は 9 月 5 日のものだった。調査中の手動 `dotnet build Scripts/RePlayGameScripts.csproj -c Debug --no-restore` は成功し、DLL を更新した。既存の非推奨 API 警告 4 件、エラー 0 件。
- 調査時のネイティブ EXE 更新日時は Debug が 9 月 4 日、Release が 9 月 7 日。ユーザーが実際にどちらを起動したかは未確認。古い実行物との不整合を断定しない。
- `CSharpScriptBackendHost.cpp` の `Initialize()` はホスト接続後のゲーム C# コンパイル失敗でも true を返す設計。起動を継続する意図はあるが、Assembly 未ロードのまま Play に入る可能性を追う。
- `CSharpScriptBackendInstances.cpp` の `LoadType()` は未ロード時に上記の一般的メッセージを返す。初回ビルド／ロード失敗の具体的理由がログから分かりにくい。
- `Source/app/Editor/framework_editor_scripting.cpp` の `build_and_reload_csharp_scripts()` は `CompileAndReload()` を呼ぶが、起動時に初期化失敗した Backend の再初期化経路も点検が必要。
- `Source/app/Runtime/framework_gameobject_scene_play.cpp` の Play 開始時に、C# が実行可能か、ソース変更が反映済みかを確認する経路を調べる。
- 読み込み失敗の最初の原因は未確定。手動ビルド成功だけで操作不能が直ったと判断しない。

### 入力と C# 検出の追加調査点

- C# の動作を確認した後、`framework_update.cpp` の ImGui 入力捕捉と SceneView の focus / hover、`framework_gameobject_scene_runtime.cpp` の UI 入力座標、`framework_gameobject_scene.cpp` の `object_ui_viewport_target()` の矩形が一致するか調べる。Play 中の全画面 UI と中央 SceneView の捕捉範囲の差が疑わしいが、まだ原因とは断定していない。
- `RePlayEngine/Scripting/CSharp/CSharpProjectDiscovery.cpp` は正規表現で型を検出し、`[ReplayGuid]` とクラス宣言の間にコメントがあると検出できない。現在の Grow Rush は説明を属性の前へ置き、3 型すべてがこの検出式に一致することを確認済み。この検出制約は Assembly 全体の未ロードとは別問題として扱う。

## 修正・確認の要求

1. 起動時の C# ビルド／ロードの実エラーを取得して原因を確定し、再読み込みによる復旧と Play 開始時の扱いを整える。C# を使わないシーンや、コンパイル失敗時に直前の正常 DLL を維持する既存仕様も考慮する。
2. 単体ゲームの Quit とウィンドウの閉じる操作は編集用保存確認を開かず終了させる。通常のエディター終了では未保存編集の保護を維持する。
3. エディターのゲーム内 Quit は既存契約に沿って Play を停止し、編集シーンへ戻す。
4. Debug / Release とエディター / 単体 EXE の両方を確認する。ゲーム入力、UI クリック、SceneFlow 遷移、終了、通常の未保存編集確認を確認表にする。
5. 意味のある回帰確認を追加し、ビルドと自動確認の結果、未確認の手動項目を分けて報告する。

## 作業制約と現在の状態

- 今回は調査とコメント修正まで。試しに入れたエンジン修正は取り消しており、エンジンソースへの機能変更は残していない。
- Grow Rush の 4 つの C# ファイルに、日本語の説明をコードの直前へ付け、ファイル先頭にアタッチ先を記載。波括弧だけの行にはコメントを付けない。コメントの配置変更の前後で実行コードを保持した。HEAD との差分には既存の編集もあるため、一括復元しない。
- ユーザー編集中の `resources/AssetDatabase.replaydb` と GrowRush の Title / Arena シーンの差分は保持する。シーン再生成や無断復元、ユーザー操作中のプロセス停止をしない。
- 敵 AI 強化・UI デザイン変更は対象外。シーン遷移には既存の SceneFlow アセットを使う。
- 作業の区切りで、一行メッセージのコミットを行う。無関係なユーザー差分を含めない。
