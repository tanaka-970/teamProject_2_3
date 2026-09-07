// アタッチ先: GrowRush_Title.replayscene の Director に GrowTitle、GrowRush_Result.replayscene の Director に GrowResult。
// GrowScreen は上記と GrowArena の共通基底クラス、GrowDiagnostics は補助クラスのため直接アタッチしない。
// 担当: エンジンのUI・音声・SceneFlowを呼び、タイトルと結果の進行をC#で制御する。

// 数学・乱数・例外などC#の基本機能を使えるようにする。
using System;
// コルーチンが返すIEnumeratorを使う。
using System.Collections;
// 名前をキーにした参照の保存にDictionaryを使う。
using System.Collections.Generic;
// 自動確認のログ用にフォルダー作成とファイル書き込みを使う。
using System.IO;
// エンジンのスクリプト・入力・描画・UI・Runtime APIを使う。
using ReplayEngine;

// このファイルの型をゲーム専用のGame.GrowRush名前空間へまとめる。
namespace Game.GrowRush;

// 自動確認と画面撮影の設定・記録をまとめる補助クラス。
public static class GrowDiagnostics
{
    // 環境変数が1のときだけ自動対戦を有効にする。
    public static bool Auto => Environment.GetEnvironmentVariable("GROWRUSH_TEST") == "1";
    // 撮影対象の画面名を読み、未指定なら空文字にする。
    public static string Capture => Environment.GetEnvironmentVariable("GROWRUSH_CAPTURE") ?? "";
    // 自動確認で結果まで到達した試合数。
    public static int Completed;
    // 自動確認用のログを1行追記する。
    public static void Record(string value)
    {
        // 通常プレイでは診断ファイルを書かない。
        if (!Auto && Capture.Length == 0) return;
        // 作業フォルダー内に診断ログ用の保存先を作る。
        Directory.CreateDirectory("Saved/GrowRush");
        // 指定されたメッセージと改行をログへ追記する。
        File.AppendAllText("Saved/GrowRush/session.log", value + Environment.NewLine);
    }
}

// 各画面が継承する共通処理。直接アタッチするクラスではない。
public abstract class GrowScreen : ScriptBehaviour
{
    // プレイヤー側のミント色を共通定義する。
    protected static readonly Color Mint = new(.13f, .87f, .64f, 1);
    // 敵側のオレンジ色を共通定義する。
    protected static readonly Color Orange = new(1, .42f, .17f, 1);
    // 背景や暗い文字に使う濃い色を定義する。
    protected static readonly Color Ink = new(.035f, .09f, .10f, 1);
    // 明るい文字に使うクリーム色を定義する。
    protected static readonly Color Cream = new(.97f, .98f, .90f, 1);
    // 一度見つけたオブジェクトのハンドルを名前ごとに保存する。
    private readonly Dictionary<string, ObjectHandle> objects = new();
    // 最後に設定した文字列を保存し、同じ文字の再設定を減らす。
    private readonly Dictionary<string, string> textCache = new();
    // エンジンのボタンクリック通知を受け取る購読情報。
    private EventSubscription clicks;
    // シーン切替を要求済みかどうか。連打による重複要求を防ぐ。
    protected bool Loading;
    // この画面が表示されてからの経過秒数。
    protected float ScreenTime;

    // 画面開始時のポインター・ボタン購読・ログを準備する。
    protected void BeginScreen(string name)
    {
        // 前の画面で固定していたマウスを解放する。
        GrowPointer.Release();
        // エンジン標準のボタンクリックイベントを購読する。
        var result = SubscribeEvent(EngineEventIds.ButtonClicked);
        // 購読に成功した場合に通知受信のハンドルを保存する。
        if (result.Succeeded) clicks = result.Value;
        // 開始した画面名を診断ログへ記録する。
        GrowDiagnostics.Record("SCENE " + name);
    }
    // シーン内のオブジェクトを名前で探し、結果を再利用する。
    protected ObjectHandle Find(string name)
    {
        // すでに見つけてある場合は保存済みのハンドルを返す。
        if (objects.TryGetValue(name, out var h)) return h;
        // エンジンAPIで現在のシーンから同名のオブジェクトを探す。
        var found = Runtime.FindGameObject(name);
        // 必須オブジェクトが欠けていたら名前付きの例外で原因を示す。
        if (!found.Succeeded || found.Value.IsEmpty) throw new InvalidOperationException("GrowRush missing object: " + name);
        // 今回見つかったハンドルを保存する。
        objects[name] = found.Value;
        // 呼び出し元へオブジェクトのハンドルを返す。
        return found.Value;
    }
    // 指定オブジェクトに表示する文字列を更新する。
    protected void Text(string name, string value)
    {
        // 表示内容が同じ場合はエンジンへの更新要求を省く。
        if (textCache.TryGetValue(name, out var previous) && previous == value) return;
        // 太字タグ付きで文字を設定し、元の文字列をキャッシュする。
        Runtime.SetUIText(Find(name), "<b>" + value + "</b>"); textCache[name] = value;
    }
    // オブジェクトの有効・無効を切り替えてUIを表示または非表示にする。
    protected void Show(string name, bool show) => Runtime.SetEnabled(Find(name), show);
    // 画像の充填率を変えてゲージの長さを更新する。
    protected void Fill(string name, float value)
    {
        // 対象オブジェクトからエンジンの画像コンポーネントを取得する。
        var image = Runtime.GetComponent<UIImageComponent>(Find(name));
        // 画像があれば0〜1に収めた充填率を設定する。構造体なので一度変数へ取り出す。
        if (image.Succeeded) { var item = image.Value; item.FillAmount = Math.Clamp(value, 0, 1); }
    }
    // 指定されたテキストの色を更新する。
    protected void TextColor(string name, Color color)
    {
        // 対象からエンジンの文字コンポーネントを取得する。
        var result = Runtime.GetComponent<UITextComponent>(Find(name));
        // 文字コンポーネントが見つかったら指定色を設定する。
        if (result.Succeeded) { var item = result.Value; item.Color = color; }
    }
    // ゲーム専用フォルダーの効果音を指定音量で鳴らす。
    protected void Sound(string name, float volume = .4f)
    {
        // 音声サービスが利用可能なら、エンジンAPIでWAVを再生する。
        if (Runtime.AudioAvailable) Runtime.PlayAudio("resources/Game/GrowRush/Audio/" + name + ".wav", volume: volume);
    }
    // 受信したボタン通知を名前に対応する処理へ渡す。
    protected void PollButtons(Action<string> handle, params string[] names)
    {
        // ボタンイベントを購読できていなければ処理しない。
        if (!clicks.IsValid) return;
        // 1フレームに処理する通知を最大16件に制限する。
        for (int n = 0; n < 16; n++)
        {
            // エンジンから次のボタンクリック通知を1つ取り出す。
            var e = PollEvent(clicks);
            // 取得失敗または未受信なら読み取りを終える。
            if (!e.Succeeded || string.IsNullOrEmpty(e.Value.TypeGuid)) break;
            // シーン切替中に届いたクリックは消費だけして無視する。
            if (Loading) continue;
            // この画面が受け付けるボタン名を順に照合する。
            foreach (string name in names)
                // 通知元がそのボタンなら対応処理を呼び、同じ通知の照合を終える。
                if (e.Value.Source.Object == Find(name).Object) { handle(name); break; }
        }
    }
    // 遷移先をC#に書かず、SceneFlowアセットへイベント名を送る。
    protected void TriggerFlow(string eventName)
    {
        // 遷移要求をすでに出している場合は重ねて発火しない。
        if (Loading) return;
        // エンジンに現在のシーンとイベント名で遷移先を選ばせる。
        var status = Runtime.TriggerSceneFlow(eventName);
        // イベント名とエンジンの受付結果を診断ログに残す。
        GrowDiagnostics.Record("FLOW " + eventName + " " + status);
        // ルール不一致や利用不可の場合はエラーを表示して戻る。
        if (status != RuntimeStatus.Ok) { TransitionFailed(status); return; }
        // 重複入力を止め、画面切替前にポインター固定を解く。
        Loading = true; GrowPointer.Release();
        // 既存の読み込み表示を出す。
        Show("Loading", true); Text("LoadingText", "…");
        // エンジンのコルーチン機能で遷移の失敗を監視する。
        StartCoroutine(WaitForTransition());
    }
    // 自動確認用にボタンクリック通知をエンジンへ発行する。
    protected void TestClick(string name)
    {
        // 自動確認で選んだボタン名を記録する。
        GrowDiagnostics.Record("BUTTON " + name);
        // 実際のUIと同じ通知経路へ、対象ボタンを発生元として送る。
        Runtime.PublishEvent(EngineEventIds.ButtonClicked, source: Find(name));
    }
    // SceneFlowが進める遷移の終了状態をフレームごとに調べる。
    private IEnumerator WaitForTransition()
    {
        // 遷移要求をエンジンが処理するため、次のフレームまで待つ。
        yield return null;
        // 終了または失敗が分かるまで状態の取得を続ける。
        while (true)
        {
            // エンジンから読み込み中かどうかと結果を取得する。
            var transition = Runtime.SceneTransition;
            // 状態取得自体に失敗したらエラーを示し、監視を終える。
            if (!transition.Succeeded) { TransitionFailed(transition.Status); yield break; }
            // もう読み込み中ではない場合に最終結果を確認する。
            if (!transition.Value.InProgress)
            {
                // 読み込みが失敗していたら再試行できる表示へ戻す。
                if (transition.Value.Status != RuntimeStatus.Ok) TransitionFailed(transition.Value.Status);
                // 状態監視を終える。通常は成功時の旧シーン破棄でこのコルーチンも終了する。
                yield break;
            }
            // 読み込み中なら次のフレームまで待って再確認する。
            yield return null;
        }
    }
    // 遷移を受理できない場合や読み込みに失敗した場合の処理。
    private void TransitionFailed(RuntimeStatus status)
    {
        // 再操作できる状態へ戻し、エラーを載せる表示を有効にする。
        Loading = false; Show("Loading", true);
        // 読み込みに失敗したことを既存の文字欄に示す。
        Text("LoadingText", "読み込み失敗 — 再試行できます");
        // エンジンのログへエラー種別と発生元オブジェクトを渡す。
        Runtime.LogError("GrowRush SceneFlow failed: " + status, GameObject);
        // 自動確認用のファイルにも失敗を記録する。
        GrowDiagnostics.Record("ERROR flow " + status);
    }
    // スクリプトが無効になったときにマウス固定を解く。
    public override void OnDisable() => GrowPointer.Release();
    // オブジェクトが破棄されたときにもマウス固定を解く。
    public override void OnDestroy() => GrowPointer.Release();
    // アプリ終了の通知でもマウスを元に戻す。
    public override void OnApplicationQuit(ApplicationQuitEventInfo info) => GrowPointer.Release();
}

// シーンに保存されたGrowTitleとC#の型を対応させる固定IDですわ。
// タイトルシーンのDirectorに付けるスクリプト。
[ReplayGuid("beea44001a1543a2bfe8807732410001")]
public sealed class GrowTitle : GrowScreen
{
    // 自動確認で同じ画面のボタンを何度も押さないためのフラグ。
    private bool testClicked;
    // エンジンがタイトルの開始時に呼ぶ処理。
    public override void Start()
    {
        // タイトル用の共通初期化を行う。
        BeginScreen("TITLE");
        // Cameraオブジェクトを取得し、背景の庭へ向ける。
        var cam = Runtime.Transform(Find("Camera")); cam.LookAt(new Vector3(0, 1, 0));
        // 読み込み表示を隠してタイトルを表示する。
        Show("Loading", false);
    }
    // エンジンから毎フレーム呼ばれ、入力と自動確認を処理する。
    public override void Update(float dt)
    {
        // タイトルを開いてからの時間を増やす。
        ScreenTime += dt;
        // 1分・2分・終了のボタン通知を処理する。
        PollButtons(Click, "OneMinute", "TwoMinutes", "Quit");
        // 遷移中または表示直後0.3秒のキー入力を受け付けない。
        if (Loading || ScreenTime < .3f) return;
        // 数字の1キーで60秒の試合を開始する。
        if (Input.GetKeyDown(Key.Alpha1)) StartMatch(60);
        // 数字の2キーで120秒の試合を開始する。
        else if (Input.GetKeyDown(Key.Alpha2)) StartMatch(120);
        // Escキーでエンジンへアプリ終了を要求する。
        else if (Input.GetKeyDown(Key.Escape)) Runtime.QuitApplication("GrowRush title");
        // 自動確認中だけ、表示から1秒後にボタンを1回選ぶ。
        if (GrowDiagnostics.Auto && !testClicked && ScreenTime > 1)
        {
            // この画面で自動ボタンを発行済みにする。
            testClicked = true;
            // 3試合ぶん確認済みなら終了確認へ進む。
            if (GrowDiagnostics.Completed >= 3)
            {
                // タイトル・再戦・1分と2分の周回が完了したことをログへ残す。
                GrowDiagnostics.Record("PASS title -> 60s -> result -> replay 60s -> title -> 120s -> result -> title -> quit");
                // エンジンのボタン通知で終了を選ぶ。
                TestClick("Quit");
            }
            // 完了済みの試合数に応じて1分または2分の開始ボタンを選ぶ。
            else TestClick(GrowDiagnostics.Completed == 2 ? "TwoMinutes" : "OneMinute");
        }
            // 対戦または結果の撮影モードでも、タイトルから通常の開始経路へ進む。
            if ((GrowDiagnostics.Capture == "arena" || GrowDiagnostics.Capture == "result") && ScreenTime > 1)
                // 撮影用の60秒対戦を開始する。
                StartMatch(60);
    }
    // 押されたタイトルのボタン名に応じた操作を行う。
    private void Click(string name)
    {
        // 終了ボタンならエンジンへ終了要求を送る。
        if (name == "Quit") Runtime.QuitApplication("GrowRush title");
        // 2分ボタンなら120秒、それ以外の開始ボタンなら60秒を選ぶ。
        else StartMatch(name == "TwoMinutes" ? 120 : 60);
    }
    // 試合時間を引き継ぐ準備をして開始イベントを送る。
    private void StartMatch(int seconds)
    {
        // 選択した時間を保存し、前の試合の結果を無効にする。
        GrowSession.Seconds = seconds; GrowSession.HasResult = false;
        // 開始音を鳴らし、SceneFlowのStartMatchを発火する。
        Sound("start"); TriggerFlow("StartMatch");
    }
}

// シーンに保存されたGrowResultとC#の型を対応させる固定ID。
// 結果シーンのDirectorに付けるスクリプト。
[ReplayGuid("beea44001a1543a2bfe8807732410003")]
public sealed class GrowResult : GrowScreen
{
    // この結果画面で表示する両チームの得点を保持する。
    private GrowScore you, cpu;
    // 自動確認のボタンを1回だけ発行するためのフラグ。
    private bool testClicked;
    // エンジンが結果シーンの開始時に呼ぶ処理。
    public override void Start()
    {
        // 試合時間付きの画面名で共通初期化を行う。
        BeginScreen("RESULT " + GrowSession.Seconds);
        // 背景カメラを庭の中央へ向ける。
        var cam = Runtime.Transform(Find("Camera")); cam.LookAt(new Vector3(0, 1, 0));
        // 前の遷移の読み込み表示を隠す。
        Show("Loading", false);
        // 対戦シーンで確定した両者の得点を共有データから取り出す。
        you = GrowSession.You; cpu = GrowSession.Cpu;
        // 結果の有無と合計点を調べ、勝ち・負け・引き分けを表示する。
        Text("ResultTitle", !GrowSession.HasResult ? "結果なし" : you.Total == cpu.Total ? "DRAW" : you.Total > cpu.Total ? "YOU WIN!" : "CPU WINS");
        // 勝敗表示を勝った側の色にする。引き分けはミント色。
        TextColor("ResultTitle", you.Total >= cpu.Total ? Mint : Orange);
        // 今回の試合が1分か2分かを表示する。
        Text("Duration", GrowSession.Seconds == 120 ? "2分 MATCH" : "1分 MATCH");
        // 両者の所有マス数を表示する。
        Text("YouArea", you.Area.ToString()); Text("CpuArea", cpu.Area.ToString());
        // 両者の成長加点をプラス記号付きで表示する。
        Text("YouGrowth", "+ " + you.Growth); Text("CpuGrowth", "+ " + cpu.Growth);
        // 両者の樹の本数を表示する。
        Text("YouTrees", you.Trees.ToString()); Text("CpuTrees", cpu.Trees.ToString());
        // 再戦ボタンに引き継ぐ試合時間を表示する。
        Text("RetryLabel", GrowSession.Seconds == 120 ? "もう一度  2分" : "もう一度  1分");
        // 合計点のカウントアップを0点から始める。
        Text("YouTotal", "0"); Text("CpuTotal", "0");
        // 勝ちまたは引き分けなら勝利音、負けなら終了音を鳴らす。
        Sound(you.Total >= cpu.Total ? "win" : "finish", .6f);
        // 自動確認時だけ結果の数値を記録する。
        if (GrowDiagnostics.Auto)
        {
            // 試合時間・合計点・面積・成長加点をログへ出す。
            GrowDiagnostics.Record($"RESULT seconds={GrowSession.Seconds} you={you.Total} cpu={cpu.Total} area={you.Area}/{cpu.Area} growth={you.Growth}/{cpu.Growth}");
            // 自動確認で完了した試合数を1増やす。
            GrowDiagnostics.Completed++;
        }
    }
    // 合計点の表示演出と、再戦・タイトル操作を毎フレーム更新する。
    public override void Update(float dt)
    {
        // 結果画面の経過時間を増やす。
        ScreenTime += dt;
        // 1.1秒で0から1へ進み、終盤がゆっくりになる演出係数を求める。
        float t = Math.Clamp(ScreenTime / 1.1f, 0, 1); t = 1 - (1 - t) * (1 - t);
        // 演出係数を掛けたプレイヤーの合計点を整数で表示する。
        Text("YouTotal", ((int)MathF.Round(you.Total * t)).ToString());
        // 演出係数を掛けた敵の合計点を整数で表示する。
        Text("CpuTotal", ((int)MathF.Round(cpu.Total * t)).ToString());
        // 大きい方の得点をゲージの基準にする。0除算を防ぐため最低1。
        int max = Math.Max(1, Math.Max(you.Total, cpu.Total));
        // 同じ基準と演出係数で両者の比較ゲージを伸ばす。
        Fill("YouBar", you.Total / (float)max * t); Fill("CpuBar", cpu.Total / (float)max * t);
        // 再戦とタイトルのボタン通知を処理する。
        PollButtons(Click, "Retry", "Title");
        // 読み込み中でなく、結果を0.5秒以上見せたらキー入力を受け付ける。
        if (!Loading && ScreenTime > .5f)
        {
            // Rキーで再戦ボタンと同じ処理を呼ぶ。
            if (Input.GetKeyDown(Key.R)) Click("Retry");
            // Escキーでタイトルボタンと同じ処理を呼ぶ。
            if (Input.GetKeyDown(Key.Escape)) Click("Title");
        }
        // 自動確認では結果を1.5秒表示してから次の操作を1回だけ行う。
        if (GrowDiagnostics.Auto && !Loading && !testClicked && ScreenTime > 1.5f)
        {
            // この結果画面での自動クリックを発行済みにする。
            testClicked = true;
            // 最初の結果では再戦、それ以降の結果ではタイトルを選ぶ。
            TestClick(GrowDiagnostics.Completed == 1 ? "Retry" : "Title");
        }
    }
    // 結果画面で選ばれた操作をSceneFlowのイベントに変換する。
    private void Click(string name)
    {
        // 控えめな音量で決定音を鳴らす。
        Sound("start", .25f);
        // 再戦の場合は前の確定結果を無効にする。
        if (name == "Retry") GrowSession.HasResult = false;
        // 再戦ならReplay、タイトルならBackToTitleをエンジンへ送る。
        TriggerFlow(name == "Retry" ? "Replay" : "BackToTitle");
    }
}
