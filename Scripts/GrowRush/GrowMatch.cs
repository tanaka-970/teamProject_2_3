// アタッチ先: なし（補助クラス）。GrowRush_Arena.replayscene の Director / GrowArena が対戦ルールを使用する。
// GrowSession は Title・Arena・Result 各シーンの Director が試合時間と結果の受け渡しに使用する。
// 担当: C#で成長・水弾・命中・復活・得点・既存AIを計算する。描画やシーン遷移は行わない。

// 数学・乱数・例外などC#の基本機能を使えるようにする。
using System;
// 描画に依存しない2次元・3次元ベクトル計算を使う。
using System.Numerics;

// このファイルの型をゲーム専用のGame.GrowRush名前空間へまとめる。
namespace Game.GrowRush;

// 試合の状態を開始カウント・対戦中・終了の3種類で表す。
public enum MatchPhase { Countdown, Running, Finished }
// 地面1マスの所有者と成長量を保持する。
public sealed class GrowCell
{
    // 所有チーム。0は未所有、1はプレイヤー、2は敵。
    public int Team;
    // 水で増える成長量。最大20で樹になる。
    public float Growth;
    // 描画側が変化を検出するための更新番号。
    public int Revision;
    // 未所有0、芽1、草2、茂み3、樹4を成長量から求める。
    public int Stage => Team == 0 ? 0 : Growth >= 20 ? 4 : Growth >= 10 ? 3 : Growth >= 4 ? 2 : 1;
}
// プレイヤーまたは敵1体の状態を保持する。
public sealed class GrowFighter
{
    // キャラクターの足元のワールド座標。
    public Vector3 Position;
    // 水を飛ばす目標のワールド座標。
    public Vector3 Aim;
    // 初期体力を6にする。
    public int Hp = 6;
    // 復活までの残り秒数。
    public float Respawn;
    // 復活後の無敵時間の残り秒数。
    public float Shield;
    // 次の水を発射できるまでの残り秒数。
    public float FireCooldown;
    // この試合で倒された回数。
    public int Deaths;
    // 復活待ち時間がなければ生存中とみなす。
    public bool Alive => Respawn <= 0;
}
// 使い回す水弾1個の状態を保持する。
public sealed class GrowDrop
{
    // この水弾が現在飛んでいるかを示す。
    public bool Active;
    // 水弾を発射したチーム番号。
    public int Team;
    // 水弾の現在のワールド座標。
    public Vector3 Position;
    // 水弾の各軸方向の速度。
    public Vector3 Velocity;
    // 発射してからの経過秒数。
    public float Age;
}
// 1回の更新に渡す移動方向・照準位置・発射指示をまとめる。
public readonly record struct GrowCommand(Vector2 Move, Vector3 Aim, bool Fire);
// 所有マス数・成長加点・樹の本数をまとめた集計値。
public readonly record struct GrowScore(int Area, int Growth, int Trees)
{
    // 勝敗に使う合計点を面積と成長加点から求める。
    public int Total => Area + Growth;
}
// 着水または命中の位置・チーム・対人命中かどうかを描画側へ通知する型。
public readonly record struct GrowImpact(Vector3 Position, int Team, bool Fighter);

// 描画や入力に依存せず、対戦ルールと進行を管理する。
public sealed class GrowMatch
{
    // 地面を20列×16行に分け、水弾を64個まで使い回す。
    public const int Columns = 20, Rows = 16, CellCount = Columns * Rows, DropCount = 64;
    // 1マスの幅と、原点から地面の端までの距離を定義する。
    public const float CellSize = 1.35f, HalfWidth = Columns * CellSize / 2, HalfDepth = Rows * CellSize / 2;
    // 水の水平速度・重力・散水範囲の半径を定義する。
    public const float WaterSpeed = 19, Gravity = 10, SplashRadius = 1.35f;
    // 全320マスの状態を入れる配列を用意する。
    public readonly GrowCell[] Cells = new GrowCell[CellCount];
    // 配列の0番にプレイヤー、1番に敵を作る。
    public readonly GrowFighter[] Fighters = { new(), new() };
    // 水弾64個の状態を入れる配列を用意する。
    public readonly GrowDrop[] Drops = new GrowDrop[DropCount];
    // 試合状態を開始前のカウントダウンにする。
    public MatchPhase Phase { get; private set; } = MatchPhase.Countdown;
    // 開始まで3秒待つ。
    public float Countdown { get; private set; } = 3;
    // 対戦の残り秒数。変更できるのはこのクラス内だけ。
    public float Remaining { get; private set; }
    // 選択された試合時間を保持する。
    public int Duration { get; }
    // 試合時間から残り時間を引いて対戦の経過秒数を求める。
    public float Elapsed => Duration - Remaining;
    // 時間切れ時点で確定したプレイヤーの得点。
    public GrowScore FinalYou { get; private set; }
    // 時間切れ時点で確定した敵の得点。
    public GrowScore FinalCpu { get; private set; }
    // 水が何かに当たったときに呼ぶ任意の通知先。
    public Action<GrowImpact>? Impact;
    // キャラクターが倒されたときに呼ぶ任意の通知先。
    public Action<int>? Knockout;
    // 次の種まきまでの経過時間を蓄積する。
    private float seedTimer;
    // 種が落ちるマスを選ぶ乱数生成器。
    private readonly Random random;

    // 試合時間と乱数の種を受け取り、新しい試合を作る。
    public GrowMatch(int seconds, int seed = 1451)
    {
        // 120秒が指定された場合は2分、それ以外は1分にする。
        Duration = seconds == 120 ? 120 : 60;
        // 残り時間を選択された試合時間に戻す。
        Remaining = Duration;
        // 同じ種から同じ散布結果を再現できる乱数生成器を作る。
        random = new Random(seed);
        // 各マスを未所有・成長ゼロで初期化する。
        for (int i = 0; i < Cells.Length; i++) Cells[i] = new();
        // 水弾の各再利用枠に状態オブジェクトを作る。
        for (int i = 0; i < Drops.Length; i++) Drops[i] = new();
        // プレイヤーを自陣の開始位置に置く。
        Fighters[0].Position = Spawn(1);
        // 敵を敵陣の開始位置に置く。
        Fighters[1].Position = Spawn(2);
    }

    // チーム番号から左右対角に配置した開始位置を返す。
    public static Vector3 Spawn(int team) => team == 1 ? new(-6, 0, -6) : new(6, 0, 6);
    // マス番号から列を求め、マス中心のX座標を計算する。
    public static Vector3 CellCenter(int index) => new((index % Columns + .5f) * CellSize - HalfWidth,
        // 地面の高さを0にし、行からマス中心のZ座標を計算する。
        0, (index / Columns + .5f) * CellSize - HalfDepth);
    // ワールド座標が含まれるマス番号を調べる。
    public static int CellAt(Vector3 p)
    {
        // X座標を地面左端基準に移し、列番号に変換する。
        int x = (int)MathF.Floor((p.X + HalfWidth) / CellSize);
        // Z座標を地面手前端基準に移し、行番号に変換する。
        int z = (int)MathF.Floor((p.Z + HalfDepth) / CellSize);
        // 地面の外なら-1、内側なら行と列を1次元のマス番号に変換して返す。
        return x < 0 || z < 0 || x >= Columns || z >= Rows ? -1 : z * Columns + x;
    }

    // 指定された秒数だけ、両者の操作を使って試合を進める。
    public void Tick(float dt, GrowCommand you, GrowCommand cpu)
    {
        // 無効な時間、時間ゼロ、終了済みの試合は更新しない。
        if (!float.IsFinite(dt) || dt <= 0 || Phase == MatchPhase.Finished) return;
        // 開始カウントダウン中の処理へ分岐する。
        if (Phase == MatchPhase.Countdown)
        {
            // 開始までの残り時間を減らし、負の値にはしない。
            Countdown = MathF.Max(0, Countdown - dt);
            // 丸め誤差を考慮して、ほぼ0秒になったら対戦を開始する。
            if (Countdown <= .00001f) Phase = MatchPhase.Running;
            // 開始前は移動・散水・得点処理を行わず戻る。
            return;
        }
        // 今回の更新時間が試合の残り時間を越えないようにする。
        dt = MathF.Min(dt, Remaining);
        // プレイヤーの移動、復活、発射を更新する。
        UpdateFighter(0, you, dt);
        // 敵の移動、復活、発射を更新する。
        UpdateFighter(1, cpu, dt);
        // 生存中の2体が重なった場合は押し離す。
        SeparateFighters();
        // 飛行中の水弾だけ移動と衝突を計算する。
        foreach (var drop in Drops) if (drop.Active) UpdateDrop(drop, dt);
        // 種まき用の経過時間を増やす。
        seedTimer += dt;
        // 5秒ごとに樹から種をまき、その分の時間を差し引く。
        if (seedTimer >= 5) { seedTimer -= 5; ScatterSeeds(); }
        // 試合の残り時間を減らし、0秒未満にはしない。
        Remaining = MathF.Max(0, Remaining - dt);
        // 残り時間がほぼ0になった場合に結果を確定する。
        if (Remaining <= .00001f)
        {
            // 表示上の残り時間を正確に0へそろえる。
            Remaining = 0;
            // 両チームの得点を時間切れ時点で保存する。
            FinalYou = Score(1); FinalCpu = Score(2);
            // 以後の更新を止める終了状態にする。
            Phase = MatchPhase.Finished;
            // 終了後に命中や散水が起きないよう全水弾を消す。
            foreach (var drop in Drops) drop.Active = false;
        }
    }

    // 指定した1体の移動・復活・発射を更新する。
    private void UpdateFighter(int index, GrowCommand command, float dt)
    {
        // 今回更新するキャラクターの状態を取得する。
        var f = Fighters[index];
        // 倒されて復活待ちの場合の処理へ進む。
        if (!f.Alive)
        {
            // 復活待ち時間を減らす。
            f.Respawn = MathF.Max(0, f.Respawn - dt);
            // 復活時に開始位置へ戻し、HP6・無敵2秒・発射待ち0.2秒を設定する。
            if (f.Alive) { f.Position = Spawn(index + 1); f.Hp = 6; f.Shield = 2; f.FireCooldown = .2f; }
            // 復活待ちの更新では通常の移動や攻撃を行わない。
            return;
        }
        // 残っている無敵時間を減らす。
        f.Shield = MathF.Max(0, f.Shield - dt);
        // 操作指示から平面上の移動方向を取り出す。
        var move = command.Move;
        // 移動方向がNaNや無限大なら移動を止める。
        if (!float.IsFinite(move.X) || !float.IsFinite(move.Y)) move = Vector2.Zero;
        // 斜め移動などで入力の長さが1を越えたら正規化する。
        if (move.LengthSquared() > 1) move = Vector2.Normalize(move);
        // プレイヤーの速さを5.8、敵を4.5にする。
        float speed = index == 0 ? 5.8f : 4.5f;
        // 方向×速度×時間を現在位置に加えて移動する。
        f.Position += new Vector3(move.X, 0, move.Y) * (speed * dt);
        // X座標を地面の内側に制限し、高さを0へ固定する。
        f.Position = new(Math.Clamp(f.Position.X, -HalfWidth + .65f, HalfWidth - .65f), 0,
            // Z座標も地面の内側に制限する。
            Math.Clamp(f.Position.Z, -HalfDepth + .65f, HalfDepth - .65f));
        // 今回の照準位置をキャラクターに記録する。
        f.Aim = command.Aim;
        // 次の発射までの待ち時間を減らす。
        f.FireCooldown -= dt;
        // 発射指示があり、連射の待ち時間が終わったか調べる。
        if (command.Fire && f.FireCooldown <= 0)
        {
            // チーム番号と照準位置を渡して水弾を発射する。
            Fire(index + 1, command.Aim);
            // 次の発射まで0.14秒待つ。
            f.FireCooldown = .14f;
        }
    }

    // 2体の重なりを平面上で解消する。
    private void SeparateFighters()
    {
        // プレイヤーと敵の状態を取り出す。
        var a = Fighters[0]; var b = Fighters[1];
        // どちらかが倒れている場合は押し合わない。
        if (!a.Alive || !b.Alive) return;
        // プレイヤーから敵へ向かう差分を求める。
        var d = b.Position - a.Position;
        // 2体の中心間距離を求める。
        float length = d.Length();
        // 1.1以上離れていれば押し戻さない。
        if (length >= 1.1f) return;
        // 重なりの半分を押す量にし、完全に同じ位置ならX方向へ分離する。
        var offset = (length > .001f ? d / length : Vector3.UnitX) * ((1.1f - length) * .5f);
        // 2体を互いに反対方向へ同じ量だけ移動する。
        a.Position -= offset; b.Position += offset;
    }

    // 発射位置から目標へ届く放物線の初速を計算する。
    public static Vector3 WaterVelocity(Vector3 origin, Vector3 target)
    {
        // 発射位置から目標への座標差を求める。
        var delta = target - origin;
        // 高さを除いた水平距離を求める。
        float horizontal = new Vector2(delta.X, delta.Z).Length();
        // 水平距離と水の速度から到達時間を求め、0.06〜0.75秒に制限する。
        float time = Math.Clamp(horizontal / WaterSpeed, .06f, .75f);
        // 水平速度と重力補正した上下速度を返し、上下の初速を制限する。
        return new(delta.X / time, Math.Clamp(delta.Y / time + .5f * Gravity * time, -14, 12), delta.Z / time);
    }

    // 空いている水弾枠を使って1発の水を発射する。
    private void Fire(int team, Vector3 target)
    {
        // 照準位置にNaNや無限大が含まれる場合は発射しない。
        if (!float.IsFinite(target.X) || !float.IsFinite(target.Y) || !float.IsFinite(target.Z)) return;
        // これから探す空き水弾枠の参照を用意する。
        GrowDrop? free = null;
        // 最初に見つかった非使用中の水弾を選ぶ。
        foreach (var d in Drops) if (!d.Active) { free = d; break; }
        // 64個すべて使用中なら今回は発射しない。
        if (free == null) return;
        // 発射元チームのキャラクターを取得する。
        var f = Fighters[team - 1];
        // キャラクター付近から照準へ向かう方向を求める。
        var forward = target - (f.Position + Vector3.UnitY);
        // 発射口の前後位置は水平な向きだけで決める。
        forward.Y = 0;
        // 真上や足元を狙って水平差がない場合は前方を補う。
        if (forward.LengthSquared() < .001f) forward = Vector3.UnitZ;
        // 前方向の長さを1にそろえる。
        forward = Vector3.Normalize(forward);
        // 足元から高さ1.05、前方0.7の発射口に水弾を置く。
        free.Position = f.Position + Vector3.UnitY * 1.05f + forward * .7f;
        // 発射口から目標への差分を計算する。
        var delta = target - free.Position;
        // 目標までの水平距離を求める。
        float range = new Vector2(delta.X, delta.Z).Length();
        // 目標が遠すぎる場合は水平射程13以内へ縮める。
        if (range > 13) target = free.Position + delta * (13 / range);
        // 目標へ向けた水弾の初速を設定する。
        free.Velocity = WaterVelocity(free.Position, target);
        // 所属チームと経過時間を初期化し、水弾を有効にする。
        free.Team = team; free.Age = 0; free.Active = true;
    }

    // 水弾を動かし、敵への命中と地面への着水を調べる。
    private void UpdateDrop(GrowDrop drop, float dt)
    {
        // 高速移動中のすり抜けを調べるため、移動前の位置を保存する。
        var old = drop.Position;
        // 初速と重力を使って、この時間内の移動後の位置を計算する。
        drop.Position += drop.Velocity * dt - Vector3.UnitY * (.5f * Gravity * dt * dt);
        // 落下による上下速度の変化と、水弾の経過時間を更新する。
        drop.Velocity.Y -= Gravity * dt; drop.Age += dt;
        // 水弾を撃った側から見た相手を取得する。
        var enemy = Fighters[drop.Team == 1 ? 1 : 0];
        // 生存中の相手と水弾の移動線分が交差したかを調べる。
        if (enemy.Alive && HitsCapsule(old, drop.Position, enemy.Position))
        {
            // 命中した水弾を再利用可能な状態に戻す。
            drop.Active = false;
            // 相手が無敵中でなければダメージ処理へ進む。
            if (enemy.Shield <= 0)
            {
                // 相手の体力を1減らす。
                enemy.Hp--;
                // 体力がなくなったら相手を倒す。
                if (enemy.Hp <= 0) Defeat(drop.Team == 1 ? 2 : 1);
            }
            // 命中の位置とチームを演出側へ通知する。
            Impact?.Invoke(new(drop.Position, drop.Team, true));
            // 命中済みの水弾について着水処理を重ねて行わない。
            return;
        }
        // 水弾が地面の当たり判定の高さまで落ちたか調べる。
        if (drop.Position.Y <= .10f)
        {
            // 移動前後の線分のどの割合で地面を通過したか求める。
            float t = old.Y > drop.Position.Y ? Math.Clamp((old.Y - .1f) / (old.Y - drop.Position.Y), 0, 1) : 1;
            // 線形補間で実際の着水地点を求め、高さを地面へそろえる。
            var hit = Vector3.Lerp(old, drop.Position, t); hit.Y = .10f;
            // 着水地点の周囲へ強さ1.6の水をまく。
            Splash(hit, drop.Team, 1.6f);
            // 着水を演出側へ通知し、水弾を非表示用の状態に戻す。
            Impact?.Invoke(new(hit, drop.Team, false)); drop.Active = false;
        }
        // 2秒経過または地面の外側へ飛びすぎた水弾を調べる。
        else if (drop.Age > 2 || MathF.Abs(drop.Position.X) > HalfWidth + 2 || MathF.Abs(drop.Position.Z) > HalfDepth + 2)
            // 寿命や範囲を越えた水弾を消す。
            drop.Active = false;
    }

    // 移動線分と、カプセルを近似する3つの球との接触を判定する。
    private static bool HitsCapsule(Vector3 a, Vector3 b, Vector3 feet)
    {
        // 移動前から移動後への線分の方向と長さをまとめる。
        var segment = b - a;
        // 線分の長さの2乗を計算する。
        float length2 = segment.LengthSquared();
        // 足・胴・頭付近に置いた3つの球を順に調べる。
        for (int n = 0; n < 3; n++)
        {
            // 足元から高さ0.45刻みで判定球の中心を決める。
            var center = feet + Vector3.UnitY * (.45f + n * .45f);
            // 球の中心に最も近い線分上の位置を0〜1の割合で求める。
            float t = length2 > .00001f ? Math.Clamp(Vector3.Dot(center - a, segment) / length2, 0, 1) : 0;
            // 最近点が半径0.52以内なら命中とする。
            if (Vector3.DistanceSquared(a + segment * t, center) <= .52f * .52f) return true;
        }
        // 3つの球すべてに当たらなければ未命中を返す。
        return false;
    }

    // 着水地点の周囲のマスへ水の成長効果を与える。
    public void Splash(Vector3 hit, int team, float strength)
    {
        // 試合中でない、チームが無効、水量が無効な場合は何もしない。
        if (Phase != MatchPhase.Running || (team != 1 && team != 2) || !float.IsFinite(strength) || strength <= 0) return;
        // 全マスを順に調べる。
        for (int i = 0; i < Cells.Length; i++)
        {
            // 調べているマスの中心座標を求める。
            var center = CellCenter(i);
            // 着水地点からマス中心までの水平距離を求める。
            float distance = new Vector2(hit.X - center.X, hit.Z - center.Z).Length();
            // 水が広がる半径の外にあるマスを飛ばす。
            if (distance > SplashRadius) continue;
            // 着水中心から離れるほど弱くなる水量をマスに与える。
            WaterCell(i, team, strength * (.55f + .45f * (1 - distance / SplashRadius)));
        }
    }

    // 1マスへ散水し、成長または敵の成長量の減少を処理する。
    public void WaterCell(int index, int team, float amount)
    {
        // 試合状態・マス番号・チーム・水量が不正なら変更しない。
        if (Phase != MatchPhase.Running || index < 0 || index >= CellCount || team < 1 || team > 2 || !float.IsFinite(amount) || amount <= 0) return;
        // 散水対象のマスを取得する。
        var c = Cells[index];
        // そのマスが敵チームの所有かを判定する。
        if (c.Team != 0 && c.Team != team)
        {
            // 敵の草の成長量を水量ぶん減らす。
            c.Growth -= amount;
            // 成長量を削り切ったら未所有へ戻す。奪うにはさらに水が必要。
            if (c.Growth <= 0) { c.Team = 0; c.Growth = 0; }
        }
        // 未所有または自陣なら所有者を設定し、最大20まで成長させる。
        else { c.Team = team; c.Growth = MathF.Min(20, c.Growth + amount); }
        // 描画側が色や大きさを更新できるよう変更番号を進める。
        c.Revision++;
    }

    // 指定したチームのキャラクターを倒し、その場に樹を植える。
    public void Defeat(int team)
    {
        // 試合中でない、またはチーム番号が不正なら倒さない。
        if (Phase != MatchPhase.Running || team < 1 || team > 2) return;
        // 倒されるキャラクターを取得する。
        var fighter = Fighters[team - 1];
        // 倒れている相手への重複処理を防ぐ。
        if (!fighter.Alive) return;
        // 倒れた足元にあるマスを特定する。
        int index = CellAt(fighter.Position);
        // 有効な地面のマスがある場合に樹を植える。
        if (index >= 0)
        {
            // 倒れた側の所有マスに変更し、成長量20の樹にする。
            var cell = Cells[index]; cell.Team = team; cell.Growth = 20; cell.Revision++;
        }
        // HPを0にし、死亡回数を増やして2.5秒後の復活を予約する。
        fighter.Hp = 0; fighter.Deaths++; fighter.Respawn = 2.5f; fighter.FireCooldown = .2f;
        // 倒れたチームを演出側へ通知する。
        Knockout?.Invoke(team);
    }

    // 各樹から周囲の未所有マスへ種を1つずつまく。
    private void ScatterSeeds()
    {
        // 樹があるか全マスを調べる。
        for (int i = 0; i < CellCount; i++)
        {
            // 成長段階4の樹以外は種をまかない。
            if (Cells[i].Stage != 4) continue;
            // 周囲5×5マスを調べ始める位置をランダムに決める。
            int start = random.Next(25);
            // 周囲の候補25マスを最大1周だけ調べる。
            for (int n = 0; n < 25; n++)
            {
                // ランダムな開始位置から循環する候補番号を求める。
                int k = (start + n) % 25;
                // 候補番号を樹からの列・行のずれに変換する。
                int dx = k % 5 - 2, dz = k / 5 - 2;
                // 樹の列・行にずれを加え、散布先のマス座標を求める。
                int x = i % Columns + dx, z = i / Columns + dz;
                // 地面の外、または散布距離を越えた候補を飛ばす。
                if (x < 0 || z < 0 || x >= Columns || z >= Rows || dx * dx + dz * dz > 8) continue;
                // 種を落とす候補マスの状態を取得する。
                var destination = Cells[z * Columns + x];
                // すでに誰かが所有している場所には種を落とさない。
                if (destination.Team != 0) continue;
                // 樹と同じチームの芽を成長量1で作り、描画更新を知らせる。
                destination.Team = Cells[i].Team; destination.Growth = 1; destination.Revision++;
                // この樹からの今回の散布は1マスだけで終える。
                break;
            }
        }
    }

    // 指定チームの現在の面積・成長加点・樹の本数を数える。
    public GrowScore Score(int team)
    {
        // 3種類の集計値を0から数え始める。
        int area = 0, growth = 0, trees = 0;
        // 指定チームが所有するマスだけを集計する。
        foreach (var c in Cells) if (c.Team == team)
        {
            // 所有マス1つにつき面積を1増やす。
            area++;
            // 芽0、草1、茂み2、樹4の成長加点を加える。
            growth += c.Stage == 4 ? 4 : c.Stage - 1;
            // 樹なら樹の本数も1増やす。
            if (c.Stage == 4) trees++;
        }
        // 3種類の集計値をまとめて返す。
        return new(area, growth, trees);
    }
}

// 既存の簡易AI。散水先の選択と近距離での攻撃を行う。
public sealed class GrowCpu
{
    // 散水候補と目標更新間隔を選ぶ乱数生成器。
    private readonly Random random;
    // 散水目標のマス番号。-1はまだ未選択。
    private int goal = -1;
    // 目標を選び直すまでの残り時間。
    private float chooseTimer;
    // 指定された種からAI用の乱数生成器を作る。
    public GrowCpu(int seed = 42) { random = new(seed); }

    // 現在の試合状態から、指定チームの次の操作指示を作る。
    public GrowCommand Command(GrowMatch match, float dt, int team = 2)
    {
        // 操作する側のキャラクターを取得する。
        var self = match.Fighters[team - 1];
        // 相手側のキャラクターを取得する。
        var other = match.Fighters[team == 1 ? 1 : 0];
        // 開始前・終了後・死亡中は操作しない。
        if (match.Phase != MatchPhase.Running || !self.Alive) return default;
        // 散水目標を選び直すまでの待ち時間を減らす。
        chooseTimer -= dt;
        // 相手までの距離を測る。
        float distance = Vector3.Distance(self.Position, other.Position);
        // 相手が生存・非無敵・距離8.5未満なら、9秒周期のうち6秒を対人攻撃に使う。
        if (other.Alive && other.Shield <= 0 && distance < 8.5f && ((int)(match.Elapsed / 3) % 3 != 0))
        {
            // 自分から相手への方向を計算する。
            var d = other.Position - self.Position;
            // 方向を地面上の2次元の移動入力へ変換する。
            var move = new Vector2(d.X, d.Z);
            // 距離がほぼ0でなければ移動方向を正規化する。
            if (move.LengthSquared() > .001f) move = Vector2.Normalize(move);
            // 相手が4.2未満まで近づいたら、ゆっくり後退する方向へ反転する。
            if (distance < 4.2f) move *= -.45f;
            // 移動速度を抑えつつ相手の胴体を狙って連射する。
            return new(move * .7f, other.Position + Vector3.UnitY, true);
        }
        // 時間切れ・目標未選択・育成完了のどれかで散水先を選び直す。
        if (chooseTimer <= 0 || goal < 0 || match.Cells[goal].Stage == 4)
        {
            // 次の目標更新を1.5〜2.8秒後にする。
            chooseTimer = 1.5f + (float)random.NextDouble() * 1.3f;
            // 候補の最高評価を、どの候補よりも低い値で初期化する。
            float best = float.NegativeInfinity;
            // 地面から45個の候補をランダムに調べる。
            for (int k = 0; k < 45; k++)
            {
                // 候補のマス番号を選ぶ。
                int i = random.Next(GrowMatch.CellCount);
                // 候補マスの所有者と成長量を参照する。
                var c = match.Cells[i];
                // 候補までの距離を計算する。
                float dist = Vector3.Distance(self.Position, GrowMatch.CellCenter(i));
                // 未所有・自陣・敵陣に評価を付け、遠いマスほど減点する。
                float value = (c.Team == 0 ? 7 : c.Team == team ? 5 : 3) - dist * .5f;
                // 完成した樹は散水の効果が小さいため大きく減点する。
                if (c.Stage == 4) value -= 12;
                // これまでで最良の候補なら目標を更新する。
                if (value > best) { best = value; goal = i; }
            }
        }
        // 選んだマスの中心を目標座標にする。
        var target = GrowMatch.CellCenter(goal);
        // 目標へ向かう平面上の移動方向を計算する。
        var direction = new Vector2(target.X - self.Position.X, target.Z - self.Position.Z);
        // 目標までの水平距離を求める。
        float length = direction.Length();
        // 4より遠ければ近づき、それ以内では立ち止まる。
        var movement = length > 4 ? direction / length : Vector2.Zero;
        // 目標の地面を狙い、距離12未満なら散水する操作指示を返す。
        return new(movement, target + Vector3.UnitY * .1f, length < 12);
    }
}

// シーン切替後も試合時間と直前の結果を共有する。ファイル保存は行わない。
public static class GrowSession
{
    // タイトルで選択した試合時間を保持し、初期値は60秒にする。
    public static int Seconds = 60;
    // 結果シーンへ渡す両チームの確定スコア。
    public static GrowScore You, Cpu;
    // 時間切れで確定した結果が存在するかを示す。
    public static bool HasResult;
}
