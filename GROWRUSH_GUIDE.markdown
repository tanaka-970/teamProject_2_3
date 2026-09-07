# Grow Rush：スクリプトとSceneFlow

## 起動と操作

ルートの `GrowRushで遊ぶ.bat` を実行する。Release版エンジンを基にC#をビルドし、`Saved/GrowRush` にゲーム用の構成を作って起動する。
変更したスクリプトやアセットは、このランチャーから次に起動したときに反映される。

| 操作 | 内容 |
|---|---|
| タイトルの1分／2分 | 試合時間の選択。数字キー1／2でも選べる |
| WASD | カメラの向きを基準に移動 |
| マウス | 肩越しカメラと照準。エディター内では右ドラッグ |
| Space | 水を連射 |
| Esc | 対戦中は一時停止。結果画面ではタイトルへ戻る |
| 結果の「もう一度」／R | 同じ試合時間で再戦 |

プレイヤーはミント色、敵はオレンジ色。所有マス1点に成長加点が付き、芽1点・草2点・茂み3点・樹5点になる。
樹は5秒ごとに近くの未所有マスへ芽を1つ作る。撃破されると倒された側の樹が足元に生え、2.5秒後に復活する。
得点は「面積＋成長」で決まり、撃破数の直接加点はない。

## スクリプトのアタッチ先

各ファイルの最上部にも記載している。全コード行に、その行が行う処理の説明を付けてある。
説明コメントはコードの直前の行に置く。波括弧だけの行にはコメントを付けない。

| ファイル／型 | オブジェクト名 | シーン | 担当 |
|---|---|---|---|
| `GrowScreens.cs` / `GrowTitle` | `Director` | `GrowRush_Title.replayscene` | 時間選択、開始、終了 |
| `GrowArena.cs` / `GrowArena` | `Director` | `GrowRush_Arena.replayscene` | 入力、カメラ、対戦、見た目、結果への要求 |
| `GrowScreens.cs` / `GrowResult` | `Director` | `GrowRush_Result.replayscene` | 集計表示、再戦、タイトルへの要求 |
| `GrowScreens.cs` / `GrowScreen` | 直接アタッチしない | 上の3つが継承 | UI・音・SceneFlowの共通操作 |
| `GrowScreens.cs` / `GrowDiagnostics` | アタッチ不要 | 確認用の補助 | 自動確認の設定とログ |
| `GrowMatch.cs` | アタッチ不要 | `GrowArena` が利用 | 水、成長、命中、復活、集計、既存AI。`GrowSession` は各画面が共有 |
| `GrowPointer.cs` | アタッチ不要 | 上記スクリプトが利用 | Windowsのマウスポインター固定と解除 |

## SceneFlowの設定

実体は `resources/Game/GrowRush/GrowRush_Flow.replaysceneflow`。
ルートのProject Settingsと、ランチャーが作るゲーム用Project Settingsの両方で、このアセットを有効にしている。
ルートプロジェクトのStartup Sceneは変更していない。ゲーム用構成のStartup SceneはGrow Rushのタイトル。

| 遷移元 | イベント名 | 遷移先 | 発火する処理 |
|---|---|---|---|
| Title | `StartMatch` | Arena | 1分／2分の選択 |
| Arena | `MatchFinished` | Result | 時間切れの表示後 |
| Result | `Replay` | Arena | 再戦ボタンまたはR |
| Arena | `BackToTitle` | Title | 一時停止メニューのタイトル |
| Result | `BackToTitle` | Title | タイトルボタンまたはEsc |

全行は有効、優先度0、追加条件なし。イベント名が同じでも、現在のシーンが一致する行だけが候補になる。
試合時間と確定得点はC#の `GrowSession` が引き継ぐ。SceneFlowは遷移先を選ぶ役割を持つ。

### エンジン画面で変更する手順

1. Projectで `GrowRush_Flow.replaysceneflow` を開く。
2. Scene Flow画面で遷移元、イベント名、遷移先、必要なら条件や優先度を編集する。
3. `Save` を押す。アセットが有効でなければ `Set Active` を押す。
4. Grow Rushのタイトルシーンを開いて再生するか、ゲームのランチャーから起動する。

C#は `Runtime.TriggerSceneFlow("StartMatch")` のようにイベント名だけを送る。
遷移先GUIDの定数と `LoadSceneAsync` による直接ロードはゲームスクリプトから除去した。
受付結果と `Runtime.SceneTransition` を確認し、失敗時はエラー表示とログを出す。
アセットが欠けている場合に、C#の固定遷移へ自動で切り替える処理はない。

`Tools/GrowRush/generate_content.py` はSceneFlowアセットの遷移内容を上書きしない。
このアセットはエンジン側で編集した設定を使い続けられる。

### エンジン側だけから遷移できるか

**可能。** 実際の遷移先の選択と読み込み要求はC++の `SceneFlowService::Trigger` が行い、
シーンの構築・入れ替えは `RuntimeSceneService` が担当する。

発火元もC#限定ではない。標準の `GoalComponent` は接触時に `completion_event` を発火する。
対応するTriggerや接触対象を設定し、SceneFlowにそのイベントの遷移を登録すれば、ゴール到達による遷移はC#なしで作れる。
Grow Rushは時間制のゲームなので、このGoalコンポーネントは使わず、C#の時間切れ判定からイベントを送っている。

アセットは「イベントが来たらどこへ行くか」を記述するもので、作成しただけでは自動発火しない。
現状のScene Flow画面は遷移定義の編集用で、任意イベントの実行ボタンは持たない。
今回のUIボタンと試合タイマーはC#でイベント発火につないでいる。

## エンジン機能とC#の分担・使用API

| 項目 | エンジン機能／API | C#で決めていること |
|---|---|---|
| スクリプト | `ScriptBehaviour.Start/Update/OnDisable/OnDestroy/OnApplicationQuit` | 各画面の進行と後始末 |
| 遷移 | SceneFlowアセット、`TriggerSceneFlow`、`SceneTransition`、`StartCoroutine` | 開始・終了・再戦などのイベントを送る時点 |
| UI | Canvas・RectTransform・UIText・UIImage・UIButton、`SetUIText`、`SetEnabled`、`UIImageComponent.FillAmount`、`UITextComponent.Color`、`SetUIImageColor` | 表示する数字、勝敗、比較ゲージの演出 |
| ボタン通知 | `SubscribeEvent`、`PollEvent`、`EngineEventIds.ButtonClicked` | ボタンとゲーム操作の対応。`PublishEvent` は自動確認用 |
| 入力 | `Input.GetKey/GetKeyDown/GetMouseButton`、`PointerDeltaX/Y` | 移動方向、散水、停止、エディター内での照準 |
| カメラ | Camera、`TransformAccess.Position/LookAt` | 肩越しの位置、感度、上下角の制限、狙う地点 |
| モデル | PrimitiveMeshRenderer、`GetComponent`、`Transform`、`FindGameObject`、`LocalScale/LocalRotationEuler`、`Tint/Visible` | 成長段階の見た目、チーム色、水弾の位置 |
| 音 | `AudioAvailable`、`PlayAudio` | 命中・成長・開始・終了のタイミング |
| 終了・診断 | `QuitApplication`、`LogError` | 終了操作と遷移失敗の通知 |

水弾の放物線と命中、マスの成長、得点、敵AIはC#で計算する。
地面はプリミティブの平面であり、Landscape・植生・Rigidbody・NavMeshはこのゲームでは使っていない。
一時停止はC#の試合更新を止める方式。画面のボタン処理は継続する。
単体ゲームのマウス固定だけはエンジンAPIにないため、`GrowPointer` がWindows APIを直接使用する。

## 確認結果と手動確認表

2026-09-07：C# Releaseビルド成功。既存の検証用スクリプトに廃止予定APIの警告4件、エラー0件。
エンジンのSceneFlow／遷移動作検証は合計71項目成功。
実際のゲームで上表の全5経路を通り、1分→再戦1分→2分の各試合と、結果・タイトル・終了まで確認した。
ボタンの自動確認はエンジンのクリック通知を使う。実際のマウスでの操作感は次の表で確認できる。

| 確認 | 期待する動作 | 結果 |
|---|---|---|
| 1分・2分を選ぶ | 選んだ時間でカウントダウン後に開始 | □ |
| マウスとWASD | 肩越しで狙った方向へ向け、カメラ基準で移動 | □ |
| Spaceで地面へ散水 | 自分の色の芽が生えて面積と得点が増える | □ |
| 同じ範囲へ散水 | 草→茂み→樹へ育ち、1マス最大5点 | □ |
| 樹を残して待つ | 周囲の未所有マスへ芽が増える | □ |
| 敵の陣地に散水 | 成長量を削り、未所有に戻してから奪える | □ |
| 敵に当てる／被弾する | HPが減り、0で樹が生えて復活する | □ |
| Esc→続行 | 停止中は時間が進まず、続行できる | □ |
| 停止メニュー→タイトル | SceneFlowでタイトルへ戻る | □ |
| 時間切れ | TIME UP後に結果へ移り、面積＋成長＝合計になる | □ |
| 結果→再戦 | 同じ試合時間、得点ゼロから始まる | □ |
| 結果→タイトル→別の時間 | 新しく選んだ試合時間で開始する | □ |
| 別アプリへ切替／終了 | マウス固定が解除される | □ |

自動確認は `Tools/GrowRush/Play-GrowRush.ps1 -Test`。
操作中のゲームを壊さないよう、別の `Saved/GrowRushValidation` に構成を作って実行する。
ログはその下の `Saved/GrowRush/session.log` に出る。各遷移の `FLOW ... Ok` と最後の `PASS` を確認する。
AIの強化とUIデザインの変更は保留している。
