// アタッチ先: GrowRush_Arena.replayscene の Director オブジェクト（スクリプト型: Game.GrowRush.GrowArena）。
// 担当: エンジンの入力・カメラ・プリミティブ・UIと、C#のGrowMatchをつなぐ。
// 遷移先は GrowRush_Flow.replaysceneflow で設定し、このクラスはイベント名だけを送る。

// 数学・乱数・例外などC#の基本機能を使えるようにする。
using System;
// 名前をキーにした参照の保存にDictionaryを使う。
using System.Collections.Generic;
// エンジンのスクリプト・入力・描画・UI・Runtime APIを使う。
using ReplayEngine;
// 計算用の2次元ベクトルをN2と略してエンジン型と区別する。
using N2 = System.Numerics.Vector2;
// 計算用の3次元ベクトルをN3と略してエンジン型と区別する。
using N3 = System.Numerics.Vector3;

// このファイルの型をゲーム専用のGame.GrowRush名前空間へまとめる。
namespace Game.GrowRush;

// シーンに保存されたGrowArenaとC#の型を対応させる固定ID。
// 対戦シーンのDirectorに付け、入力・ルール・描画を結び付ける。
[ReplayGuid("beea44001a1543a2bfe8807732410002")]
public sealed class GrowArena : GrowScreen
{
    // 1個の見た目に必要なエンジンの参照をまとめる。
    private sealed class Visual
    {
        // 位置・向き・大きさを変更するTransformの操作窓口。
        public TransformAccess Transform;
        // プリミティブの色や表示状態を変更するコンポーネント。
        public PrimitiveMeshRendererComponent Renderer;
        // 最後に設定した表示状態を記憶する。
        public bool Visible;
        // 表示状態が変わったときだけエンジンに書き込み、記憶した値も更新する。
        public void Show(bool visible) { if (Visible != visible) { Renderer.Visible = visible; Visible = visible; } }
    }
    // 名前ごとに見た目の参照を保存して再検索を減らす。
    private readonly Dictionary<string, Visual> visuals = new();
    // 各マスの描画に反映済みの更新番号を保存する。
    private readonly int[] revisions = new int[GrowMatch.CellCount];
    // 使い回す16個の着水演出それぞれの経過時間。
    private readonly float[] splashAges = new float[16];
    // このシーンの対戦ルール。Startで必ず作成するため初期値の警告を抑える。
    private GrowMatch match = null!;
    // 敵を操作する既存の簡易AIを作る。
    private GrowCpu enemy = new();
    // 自動確認時だけプレイヤーを代行する、別の乱数の種のAI。
    private GrowCpu autoPlayer = new(4);
    // プレイヤーを追従するエンジンのカメラTransform。
    private TransformAccess camera;
    // カメラの角度と、固定更新・HUD・命中表示・効果音・結果待ちの時間を保持する。
    private float yaw = .42f, pitch = .20f, accumulator, hudTimer, hitTimer, soundCooldown, resultDelay;
    // 次に使う着水演出枠と、直前に表示した残り秒数を保持する。
    private int nextSplash, lastSecond = -1;
    // 一時停止中か、時間切れ演出へ入ったかを保持する。
    private bool paused, ending;
    // 現在狙っているワールド座標。
    private N3 aim;
    // 誰も所有していない地面の色。
    private static readonly Color Soil = new(.30f, .38f, .29f, 1);
    // 計算用のSystem.Numerics.Vector3をエンジンのVector3へ変換する。
    private static Vector3 E(N3 v) => new(v.X, v.Y, v.Z);

    // エンジンが対戦シーンの開始時に呼ぶ初期化処理。
    public override void Start()
    {
        // 試合時間をログに含めて画面共通の準備を行う。
        BeginScreen("ARENA " + GrowSession.Seconds);
        // 選択された1分または2分の新しい試合を作る。
        match = new GrowMatch(GrowSession.Seconds);
        // ルール側の命中・撃破通知を、音と見た目の処理へつなぐ。
        match.Impact = OnImpact; match.Knockout = OnKnockout;
        // 全マスを初回更新対象にし、着水演出は終了済みの状態にする。
        Array.Fill(revisions, -1); Array.Fill(splashAges, 10);
        // シーンにあるCameraのTransformを取得する。
        camera = Runtime.Transform(Find("Camera"));
        // 一時停止画面と読み込み表示を隠す。
        Show("Pause", false); Show("Loading", false);
        // 単体ゲームはマウス操作、エディター内では右ドラッグと案内する。
        Text("ControlHint", GrowPointer.Standalone ? "WASD 移動   マウス 照準   SPACE 水   ESC 停止" : "WASD 移動   右ドラッグ 照準   SPACE 水   ESC 停止");
        // 全マスの地面・茎・葉の参照を開始時に取得しておく。
        for (int i = 0; i < GrowMatch.CellCount; i++) { V("Plot" + i); V("Stem" + i); V("Leaf" + i); }
        // 全水弾の見た目を取得し、最初は隠す。
        for (int i = 0; i < GrowMatch.DropCount; i++) V("Drop" + i).Show(false);
        // 全着水演出の見た目を取得し、最初は隠す。
        for (int i = 0; i < splashAges.Length; i++) V("Splash" + i).Show(false);
        // 初期の世界・カメラ・HUDを時間を進めずに反映する。
        RefreshWorld(0); UpdateCamera(0); UpdateHud();
    }

    // 毎フレームの入力、試合進行、表示、終了判定を行う。
    public override void Update(float deltaTime)
    {
        // 初期化前またはシーン遷移要求中なら対戦処理を止める。
        if (match == null || Loading) return;
        // 1フレームの処理時間を0〜0.1秒に制限し、極端な進行を防ぐ。
        float dt = Math.Clamp(deltaTime, 0, .1f);
        // 画面経過時間を進め、効果音の再生待ち時間を減らす。
        ScreenTime += dt; soundCooldown -= dt;
        // 一時停止画面の続行・タイトルボタン通知を受け取る。
        PollButtons(Click, "Resume", "BackTitle");
        // 時間切れ前ならEscで一時停止状態を反転する。
        if (Input.GetKeyDown(Key.Escape) && !ending) SetPaused(!paused);
        // 通常の単体プレイで別ウィンドウへ移ったかを確認する。
        if (GrowPointer.Standalone && !GrowPointer.HasFocus && !GrowDiagnostics.Auto && GrowDiagnostics.Capture.Length == 0 && !paused)
            // フォーカスが外れたら自動で一時停止する。
            SetPaused(true);
        // 一時停止中はマウスを解放し、試合時間や弾を進めない。
        if (paused) { GrowPointer.Release(); return; }
        // マウス入力を反映して、今回の照準を計算する。
        UpdateCamera(dt);
        // カメラの水平角から地面上の前方向を求める。
        var forward = new N2(MathF.Sin(yaw), MathF.Cos(yaw));
        // 前方向に直交する右方向を求める。
        var right = new N2(forward.Y, -forward.X);
        // Dを右、Aを左として左右入力を-1〜1で求める。
        float x = (Input.GetKey(Key.D) ? 1 : 0) - (Input.GetKey(Key.A) ? 1 : 0);
        // Wを前、Sを後ろとして前後入力を-1〜1で求める。
        float z = (Input.GetKey(Key.W) ? 1 : 0) - (Input.GetKey(Key.S) ? 1 : 0);
        // カメラ基準の移動と照準、Spaceの発射指示をルールへ渡す形にする。
        var command = new GrowCommand(right * x + forward * z, aim, Input.GetKey(Key.Space));
        // 自動確認または対戦・結果の撮影を行うモードか判定する。
        bool automated = GrowDiagnostics.Auto || GrowDiagnostics.Capture == "arena" || GrowDiagnostics.Capture == "result";
        // 確認・撮影中だけ試合を12倍速にし、通常プレイは等速にする。
        float rate = automated ? 12 : 1;
        // 対戦撮影では18秒進んだところでルールの時間を固定する。
        bool freezeCapture = GrowDiagnostics.Capture == "arena" && match.Elapsed > 18;
        // 撮影で固定していなければ、未処理の時間を蓄積する。
        if (!freezeCapture) accumulator += dt * rate;
        // 対戦ルールを1/60秒刻みで処理する。
        const float step = 1f / 60;
        // 蓄積時間がある限り、最大120回まで固定幅の更新を行う。
        for (int n = 0; accumulator >= step && n < 120; n++)
        {
            // 今回処理する1刻み分を蓄積時間から引く。
            accumulator -= step;
            // 両者の操作を渡してルールを進める。自動確認時だけプレイヤー入力をAIに置き換える。
            match.Tick(step, automated ? autoPlayer.Command(match, step, 1) : command, enemy.Command(match, step));
        }
        // 自動確認中でプレイヤーが生存している場合、カメラも照準を追う。
        if (automated && match.Fighters[0].Alive)
        {
            // 自動操作が狙っている方向を求める。
            var d = match.Fighters[0].Aim - match.Fighters[0].Position;
            // 有効な方向ならカメラの水平角をその方向へ向ける。
            if (d.LengthSquared() > .01f) yaw = MathF.Atan2(d.X, d.Z);
        }
        // 最新のルールを見た目へ反映し、マウス入力を重ねず追従位置だけ更新する。
        RefreshWorld(dt); UpdateCamera(0);
        // HUDを次に更新するまでの待ち時間を減らす。
        hudTimer -= dt;
        // 得点や残り時間の文字は0.1秒ごとに更新する。
        if (hudTimer <= 0) { UpdateHud(); hudTimer = .1f; }
        // ルール側で試合が終了したか調べる。
        if (match.Phase == MatchPhase.Finished)
        {
            // 時間切れになった最初のフレームだけ終了演出を準備する。
            if (!ending)
            {
                // 終了状態を記録し、マウスを解放して終了音を鳴らす。
                ending = true; GrowPointer.Release(); Sound("finish", .6f);
                // 両者の確定得点を次のシーンへ渡す共有データに保存する。
                GrowSession.You = match.FinalYou; GrowSession.Cpu = match.FinalCpu; GrowSession.HasResult = true;
                // 中央にTIME UPを表示する。
                Show("CenterMessage", true); Text("CenterMessage", "TIME UP");
                // 終了した試合時間と死亡回数を診断ログに記録する。
                GrowDiagnostics.Record($"FINISH {match.Duration} deaths={match.Fighters[0].Deaths}/{match.Fighters[1].Deaths}");
            }
            // 時間切れ表示の待ち時間を増やす。
            resultDelay += dt;
            // 1.25秒見せたらMatchFinishedを送り、遷移先はSceneFlowアセットに任せる。
            if (resultDelay >= 1.25f) TriggerFlow("MatchFinished");
        }
    }

    // 対戦の一時停止と一時停止メニューを切り替える。
    private void SetPaused(bool value)
    {
        // 停止フラグとメニュー表示をそろえ、停止中は照準を隠す。
        paused = value; Show("Pause", value); Show("Crosshair", !value);
        // 停止・再開の切替時はいったんポインターを解放する。
        GrowPointer.Release();
    }
    // 一時停止画面で押されたボタンを処理する。
    private void Click(string name)
    {
        // 停止中でなければこのメニュー操作を無視する。
        if (!paused) return;
        // 続行ボタンなら一時停止を解除する。
        if (name == "Resume") SetPaused(false);
        // タイトルボタンならSceneFlowにBackToTitleを送る。
        else TriggerFlow("BackToTitle");
    }

    // 肩越しカメラと狙う位置を更新する。dtが0ならマウス移動は取り込まない。
    private void UpdateCamera(float dt)
    {
        // 試合中・非停止・生存中のときだけ照準操作を有効にする。
        bool active = match.Phase != MatchPhase.Finished && !paused && match.Fighters[0].Alive;
        // 今回のマウス移動量を初期値0にする。
        Vector2 delta = default;
        // 通常の単体プレイで時間が進む更新だけ、Windowsのポインター移動を取得する。
        if (GrowPointer.Standalone && !GrowDiagnostics.Auto && GrowDiagnostics.Capture.Length == 0 && dt > 0)
            // ポインター固定を伴うマウス移動量を補助クラスから受け取る。
            delta = GrowPointer.Delta(active);
        // エディター内では、右ボタンを押している間だけ視点を操作する。
        else if (!GrowPointer.Standalone && active && Input.GetMouseButton(MouseButton.Right) && dt > 0)
        {
            // エンジンの入力APIからマウスの左右・上下移動量を取得する。
            var dx = Runtime.PointerDeltaX(); var dy = Runtime.PointerDeltaY();
            // 取得できた軸の値を採用し、取得失敗した軸は0にする。
            delta = new(dx.Succeeded ? dx.Value : 0, dy.Succeeded ? dy.Value : 0);
        }
        // 感度を掛けて左右角・上下角を更新し、上下の回しすぎを制限する。
        yaw += delta.X * .003f; pitch = Math.Clamp(pitch + delta.Y * .0027f, -.12f, .75f);
        // カメラの左右角から水平な前方向を求める。
        var flat = new N3(MathF.Sin(yaw), 0, MathF.Cos(yaw));
        // カメラを右肩へずらすための右方向を求める。
        var right = new N3(flat.Z, 0, -flat.X);
        // 上下角も含めた3次元の視線方向を求める。
        var f = new N3(flat.X * MathF.Cos(pitch), -MathF.Sin(pitch), flat.Z * MathF.Cos(pitch));
        // プレイヤーから高さ2.65、後方4.8、右0.8の肩越し位置を求める。
        var eye = match.Fighters[0].Position + N3.UnitY * 2.65f - flat * 4.8f + right * .8f;
        // エンジンのカメラをその位置へ置き、視線方向へ向ける。
        camera.Position = E(eye); camera.LookAt(E(eye + f * 20));
        // 下を見ていれば地面までの距離を求め、水平に近い場合は仮の距離20を使う。
        float distance = f.Y < -.02f ? Math.Clamp((eye.Y - .1f) / -f.Y, 2, 25) : 20;
        // 視線上の指定距離に照準位置を置く。
        aim = eye + f * distance;
        // 照準に敵が入っているか調べるため敵の状態を取得する。
        var cpu = match.Fighters[1];
        // カメラから敵の胴体までの差分を求める。
        var toCpu = cpu.Position + N3.UnitY - eye;
        // 敵が視線方向のどの距離にいるかを内積で求める。
        float projected = N3.Dot(toCpu, f);
        // 敵が視線前方かつ地面より手前で、視線から0.65以内にいるか調べる。
        if (cpu.Alive && projected > 0 && projected < distance && (toCpu - f * projected).LengthSquared() < .65f * .65f)
            // 条件を満たす場合は敵の胴体を水の目標にする。
            aim = cpu.Position + N3.UnitY;
        // 射程を測る基準としてプレイヤーの発射高さを求める。
        var muzzle = match.Fighters[0].Position + N3.UnitY * 1.05f;
        // 発射高さから目標までの差分と水平距離を求める。
        var diff = aim - muzzle; float range = new N2(diff.X, diff.Z).Length();
        // 射程を越えた照準は距離13まで縮める。
        if (range > 13) aim = muzzle + diff * (13 / range);
        // 着水位置を示すマーカーの見た目を取得する。
        var marker = V("AimMarker");
        // 操作可能で地面付近を狙っているときだけマーカーを出す。
        marker.Show(active && aim.Y < .7f);
        // マーカーを照準のX・Z位置と地面より少し上の高さへ置く。
        marker.Transform.Position = new(aim.X, .14f, aim.Z);
        // マーカーを薄い円盤の大きさにする。
        marker.Transform.LocalScale = new(.8f, .025f, .8f);
    }

    // 名前から見た目の操作窓口を取り出す。
    private Visual V(string name)
    {
	  // 取得済みなら保存していた見た目の参照を返す。
		if (visuals.TryGetValue(name, out var found)) return found;
		// 名前に対応するエンジンのオブジェクトを取得する。
		var h = Find(name);
		// そのオブジェクトのプリミティブ描画コンポーネントを取得する。
		var renderer = Runtime.GetComponent<PrimitiveMeshRendererComponent>(h);
		// 必要な描画コンポーネントがなければ名前付きで例外を出す。
		if (!renderer.Succeeded) throw new InvalidOperationException("GrowRush renderer missing: " + name);
		// Transformと描画コンポーネント、現在の表示状態をまとめる
		var v = new Visual { Transform = Runtime.Transform(h), Renderer = renderer.Value, Visible = renderer.Value.Visible };

        // まとめた参照を名前で保存し、呼び出し元へ返す。
        visuals[name] = v; return v;
    }

	// 対戦ルールの状態を、地面・草・キャラクター・水の見た目へ反映する。
	private void RefreshWorld(float dt)
    {
        // 各マスの更新が必要か順に調べる。
        for (int i = 0; i < GrowMatch.CellCount; i++)
        {
            // そのマスの所有者・成長量・更新番号を取得する。
            var cell = match.Cells[i];
            // 前に表示したときから変化がなければ描画設定を書き換えない。
            if (revisions[i] == cell.Revision) continue;
            // 今回反映する更新番号を記録する。
            revisions[i] = cell.Revision;
            // 所有チームに対応する表示色を選ぶ。
            var color = cell.Team == 1 ? Mint : Orange;
            // 草や地面を置くマスの中心座標を求める。
            var p = GrowMatch.CellCenter(i);
            // 未所有なら土の色、所有されていればチーム色で地面を塗る。
            V("Plot" + i).Renderer.Tint = cell.Team == 0 ? Soil : color;
            // そのマスの茎と葉の見た目を取得する。
            var stem = V("Stem" + i); var leaf = V("Leaf" + i);
            // 芽以上に成長していれば茎と葉を表示する。
            stem.Show(cell.Stage > 0); leaf.Show(cell.Stage > 0);
            // 未所有なら大きさや色の更新はここで終える。
            if (cell.Stage == 0) continue;
            // 芽・草・茂み・樹の各段階に対応した高さを選ぶ。
            float height = cell.Stage switch { 1 => .22f, 2 => .48f, 3 => .90f, _ => 2.15f };
            // 成長段階に対応した葉の幅を選ぶ。
            float width = cell.Stage switch { 1 => .20f, 2 => .36f, 3 => .55f, _ => .84f };
            // 茎をマス中央の適切な高さへ置く。
            stem.Transform.Position = new(p.X, height * .42f, p.Z);
            // 樹だけ幹を太くし、成長段階に合わせて茎を伸ばす。
            stem.Transform.LocalScale = new(cell.Stage == 4 ? .18f : .07f, height * .8f, cell.Stage == 4 ? .18f : .07f);
            // 樹の幹は茶色、草の茎はチーム色にする。
            stem.Renderer.Tint = cell.Stage == 4 ? new(.32f, .20f, .12f, 1) : color;
            // 葉を茎の上部へ置く。
            leaf.Transform.Position = new(p.X, height * .88f, p.Z);
            // 葉のプリミティブを成長段階に応じて拡大する。
            leaf.Transform.LocalScale = new(width, height * .55f, width);
            // 葉の色を所有チームに合わせる。
            leaf.Renderer.Tint = color;
        }
        // プレイヤーと敵の見た目を順に更新する。
        for (int i = 0; i < 2; i++)
        {
            // 状態と、対応オブジェクトの名前の接頭辞を選ぶ。
            var fighter = match.Fighters[i]; string prefix = i == 0 ? "You" : "Cpu";
            // 胴体・顔のバイザー・散水口の見た目を取得する。
            var body = V(prefix + "Body"); var visor = V(prefix + "Visor"); var gun = V(prefix + "Gun");
            // 死亡中は非表示にし、無敵中は時間に応じて点滅させる。
            bool visible = fighter.Alive && (fighter.Shield <= 0 || (int)(ScreenTime * 12) % 2 == 0);
            // 胴体・バイザー・散水口を同じ表示状態にする。
            body.Show(visible); visor.Show(visible); gun.Show(visible);
            // プレイヤーはカメラ角、敵は狙っている方向から水平角を求める。
            float angle = i == 0 ? yaw : MathF.Atan2(fighter.Aim.X - fighter.Position.X, fighter.Aim.Z - fighter.Position.Z);
            // 水平角から前方向を計算する。
            var forward = new N3(MathF.Sin(angle), 0, MathF.Cos(angle));
            // 胴体の中心を足元から高さ0.95に置く。
            body.Transform.Position = E(fighter.Position + N3.UnitY * .95f);
            // 顔のバイザーを高さ1.35、前方0.4に置く。
            visor.Transform.Position = E(fighter.Position + N3.UnitY * 1.35f + forward * .40f);
            // バイザーをキャラクターの前へ向ける。
            visor.Transform.LocalRotationEuler = new(0, angle, 0);
            // 散水口を高さ1.05、前方0.7に置く。
            gun.Transform.Position = E(fighter.Position + N3.UnitY * 1.05f + forward * .7f);
            // 円柱の散水口を横に倒して前へ向ける。角度の単位はラジアン。
            gun.Transform.LocalRotationEuler = new(MathF.PI / 2, angle, 0);
            // 頭上のHP表示を6個ぶん更新する。
            for (int k = 0; k < 6; k++)
            {
                // HPマーカーを取得し、生存中だけ表示する。
                var pip = V(prefix + "Hp" + k); pip.Show(fighter.Alive);
                // 頭上の高さ2.05に小さなマーカーを横並びに置く。
                pip.Transform.Position = E(fighter.Position + new N3((k - 2.5f) * .16f, 2.05f, 0));
                // 残りHP分をチーム色にし、失ったHP分を暗くする。
                pip.Renderer.Tint = k < fighter.Hp ? (i == 0 ? Mint : Orange) : Ink;
            }
        }
        // 全水弾の見た目をルール側の状態に合わせる。
        for (int i = 0; i < match.Drops.Length; i++)
        {
            // 水弾の状態と、その描画用オブジェクトを取得する。
            var drop = match.Drops[i]; var v = V("Drop" + i);
            // 飛行中の水弾だけ位置や色を更新する。
            if (drop.Active)
            {
                // 表示を始める水弾に、発射チームの明るい色を設定する。
                if (!v.Visible) v.Renderer.Tint = drop.Team == 1 ? new(.48f, 1, .91f, 1) : new(1, .76f, .36f, 1);
                // 水弾の計算済み位置をエンジンのTransformに反映する。
                v.Transform.Position = E(drop.Position);
            }
            // 飛行中なら表示し、未使用枠なら隠す。
            v.Show(drop.Active);
        }
        // 着水の円盤演出を順に更新する。
        for (int i = 0; i < splashAges.Length; i++)
        {
            // 着水からの経過時間を増やし、対象の円盤を取得する。
            splashAges[i] += dt; var splash = V("Splash" + i);
            // 着水から0.32秒以内だけ円盤を表示する。
            splash.Show(splashAges[i] < .32f);
            // 表示中は時間に応じて円盤を広げ、薄さを保つ。
            if (splashAges[i] < .32f) { float radius = .2f + splashAges[i] * 5; splash.Transform.LocalScale = new(radius, .015f, radius); }
        }
        // 命中マークの残り時間を減らし、時間内だけ画面に出す。
        hitTimer -= dt; Show("HitMark", hitTimer > 0);
    }

    // ルールから命中または着水の通知を受けて演出する。
    private void OnImpact(GrowImpact impact)
    {
        // 円盤演出の枠を順番に再利用し、その経過時間を0へ戻す。
        int slot = nextSplash++ % splashAges.Length; splashAges[slot] = 0;
        // 命中・着水地点より少し上へ演出の円盤を置く。
        var v = V("Splash" + slot); v.Transform.Position = E(impact.Position + N3.UnitY * .025f);
        // 演出の色を水を撃ったチームに合わせる。
        v.Renderer.Tint = impact.Team == 1 ? Mint : Orange;
        // プレイヤーの水が敵に当たった場合、命中マークを0.16秒表示する。
        if (impact.Fighter && impact.Team == 1) hitTimer = .16f;
        // 自動確認でなく、前の効果音から十分な時間がたっていれば音を鳴らす。
        if (soundCooldown <= 0 && !GrowDiagnostics.Auto)
        {
            // 対人命中と着水で音を選び、敵の効果音は小さめにする。
            Sound(impact.Fighter ? "hit" : "water", impact.Team == 1 ? .16f : .08f);
            // 短時間に音が重なりすぎないよう0.16秒待つ。
            soundCooldown = .16f;
        }
    }
    // どちらかが倒されたときの演出を処理する。
    private void OnKnockout(int team)
    {
        // 倒れた場所に樹が生える効果音を鳴らす。
        Sound("tree", .5f);
        // 倒された側のチーム番号を診断ログへ出す。
        GrowDiagnostics.Record("KNOCKOUT team =" + team);
    }
    // 残り時間・得点・面積・HP・中央表示を更新する。
    private void UpdateHud()
    {
        // 残り時間を秒単位で切り上げ、早く0秒と表示しないようにする。
        int seconds = (int)MathF.Ceiling(match.Remaining);
        // 残り時間を分:秒の形式で表示する。
        Text("Timer", $"{seconds / 60}:{seconds % 60:00}");
        // 残り10秒以下なら時計の文字をオレンジ色にする。
        TextColor("Timer", seconds <= 10 ? Orange : Cream);
        // 最後の10秒は秒の切り替わりごとに警告音を鳴らす。
        if (seconds != lastSecond && seconds <= 10 && seconds > 0) Sound("tick", .25f);
        // 今回表示した秒数を次回の比較用に保存する。
        lastSecond = seconds;
        // 両チームの現在の得点を集計する。
        var you = match.Score(1); var cpu = match.Score(2);
        // 両チームの合計点をHUDに表示する。
        Text("YouScore", you.Total.ToString()); Text("CpuScore", cpu.Total.ToString());
        // プレイヤーの所有面積を全320マスに対する百分率で表示する。
        Text("YouAreaHud", $"{100f * you.Area / GrowMatch.CellCount:0}%");
        // 敵の所有面積を同じ基準の百分率で表示する。
        Text("CpuAreaHud", $"{100f * cpu.Area / GrowMatch.CellCount:0}%");
        // 両者の面積比率をそれぞれのゲージの充填率へ反映する。
        Fill("YouTerritory", you.Area / (float)GrowMatch.CellCount); Fill("CpuTerritory", cpu.Area / (float)GrowMatch.CellCount);
        // プレイヤーのHP表示6個を更新する。
        for (int k = 0; k < 6; k++)
            // 残りHPに含まれるマーカーを明るく、失った分を暗くする。
            Runtime.SetUIImageColor(Find("HpPip" + k), k < match.Fighters[0].Hp ? Mint : new(.16f, .24f, .25f, 1));
        // 開始前・死亡中・時間切れ演出中に中央の大きな文字を出す。
        bool center = match.Phase == MatchPhase.Countdown || !match.Fighters[0].Alive || ending;
        // 中央メッセージの表示状態を切り替える。
        Show("CenterMessage", center);
        // 終了演出中のTIME UPは上書きしない。
        if (!ending)
            // 開始前ならカウントダウンの残り数を切り上げて表示する。
            Text("CenterMessage", match.Phase == MatchPhase.Countdown ? Math.Max(1, (int)MathF.Ceiling(match.Countdown)).ToString()
                // 死亡中なら復活までの秒数を表示し、通常時は文字を空にする。
                : !match.Fighters[0].Alive ? $"復活  {MathF.Ceiling(match.Fighters[0].Respawn):0}" : "");
    }
}
