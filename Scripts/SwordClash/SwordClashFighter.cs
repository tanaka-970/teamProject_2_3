using System.Collections;
using ReplayEngine;

namespace Game.SwordClash;

// アタッチ先: SwordClash.replayscene の Fighter1 / Fighter2。
// 担当: 移動・ジャンプ・剣を振る・食らう。ルールは Director が持つ。
//
// 攻撃判定は Physics.OverlapSphere。距離を自前で数えず、エンジンの
// 物理クエリへ通す。Collider を足せばそのまま当たるようになる。
[ReplayGuid("9c1f6a3d84b25e70af38d1c62b45e9f7")]
public class SwordClashFighter : MonoBehaviour
{
    public enum Move { None, Slash, SideB, UpB, DownB }

    [SerializeField]
    [Tooltip("地上の移動速度")]
    [Range(1.0, 20.0)]
    float moveSpeed = 7.4f;

    [SerializeField]
    [Tooltip("空中で効く操作の割合")]
    [Range(0.0, 1.0)]
    float airControl = 0.72f;

    [SerializeField]
    [Tooltip("ジャンプの初速")]
    [Range(1.0, 30.0)]
    float jumpSpeed = 11.5f;

    [SerializeField]
    [Tooltip("重いほど飛びにくい")]
    [Range(0.5, 2.0)]
    float weight = 1.0f;

    [SerializeField]
    [Tooltip("目標速度へ寄せる力の強さ。大きいほど機敏")]
    [Range(10.0, 400.0)]
    float drivePower = 120.0f;

    [SerializeField]
    [Tooltip("止まるときの力の強さ")]
    [Range(10.0, 400.0)]
    float brakePower = 90.0f;

    [SerializeField]
    [Tooltip("2 人目なら true。操作キーが矢印側になる")]
    public bool secondPlayer;

    [Header("状態")]
    [ReadOnly]
    public float Damage;

    [ReadOnly]
    public int Stocks = 3;

    [ReadOnly]
    public bool Grounded;

    [ReadOnly]
    public string Action = "None";

    // ふっとばされた高さの最大。撃力が効いているかを画面で読むため。
    [ReadOnly]
    public float PeakY;

    // 撃力がどこで死んでいるかを画面で読むための一時表示。
    [ReadOnly]
    public string Launch = "-";

    // 操作を受け付けるか。
    //
    // 既定を true にしてあるのは、Director が居なくても動かせるようにするため。
    // タイトル画面でも歩けて素振りできる方が、格闘ゲームとしては自然でもある。
    // Director は撃墜中と結果画面でだけ false にする。
    [HideInInspector]
    public bool ControlEnabled = true;

    // Brain が入れる仮想入力。人間が操作する側は使わない。
    [HideInInspector]
    public float BrainMove;

    [HideInInspector]
    public bool BrainJump;

    [HideInInspector]
    public Move BrainMove2 = Move.None;

    [HideInInspector]
    public bool BrainControlled;

    CharacterMotor? motor;
    PlayerInput? input;
    MeshRenderer? skin;
    AudioSource? voice;
    Transform? blade;
    MeshRenderer? bladeSkin;
    Trail? bladeTrail;
    ParticleEmitter? sparks;
    PointLight? glow;
    SwordClashGame? director;
    SwordClashFighter? rival;

    Color baseColor = new(0.30f, 0.62f, 0.98f, 1.0f);
    Vector3 spawnPoint;

    Move move = Move.None;
    float moveTimer;
    float facing = 1.0f;
    float hitStop;
    float hitStun;
    int airJumps;
    bool upBUsed;
    bool hitLanded;

    // 技ごとの表。発生 / 持続 / 硬直 / 威力 / ふっとび / 間合い。
    readonly struct Spec
    {
        public Spec(float startup, float active, float recover, float damage,
            float knockBase, float knockGrow, float reach, float upward)
        {
            Startup = startup; Active = active; Recover = recover;
            Damage = damage; KnockBase = knockBase; KnockGrow = knockGrow;
            Reach = reach; Upward = upward;
        }

        public float Startup { get; }
        public float Active { get; }
        public float Recover { get; }
        public float Damage { get; }
        public float KnockBase { get; }
        public float KnockGrow { get; }
        public float Reach { get; }
        public float Upward { get; }

        public float Total => Startup + Active + Recover;
    }

    static Spec SpecOf(Move value) => value switch
    {
        //                    発生   持続   硬直   威力  ふっとび 伸び  間合い 上方向
        Move.Slash => new Spec(0.06f, 0.12f, 0.14f, 8.0f, 7.0f, 0.13f, 1.7f, 0.45f),
        Move.SideB => new Spec(0.11f, 0.26f, 0.28f, 15.0f, 10.5f, 0.17f, 2.0f, 0.35f),
        Move.UpB => new Spec(0.05f, 0.24f, 0.30f, 12.0f, 9.0f, 0.15f, 1.6f, 1.25f),
        Move.DownB => new Spec(0.04f, 0.32f, 0.22f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
        _ => new Spec(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
    };

    void Awake()
    {
        // 移動と跳躍は CharacterMotor が持つ。C# はここへ指示を出すだけ。
        motor = GetComponent<CharacterMotor>();
        input = GetComponent<PlayerInput>();

        var bodyObject = transform.Find("Body");
        skin = bodyObject != null ? bodyObject.gameObject.GetComponent<MeshRenderer>() : null;
        voice = GetComponent<AudioSource>();
        if (skin != null) baseColor = skin.color;

        // 剣先の帯と火花と光。どれも無くても動くので null 許容のまま持つ。
        blade = transform.Find("Blade");
        bladeSkin = blade != null ? blade.gameObject.GetComponent<MeshRenderer>() : null;
        bladeTrail = blade != null ? blade.gameObject.GetComponent<Trail>() : null;

        var sparkObject = transform.Find("Sparks");
        sparks = sparkObject != null ? sparkObject.gameObject.GetComponent<ParticleEmitter>() : null;

        var glowObject = transform.Find("Glow");
        glow = glowObject != null ? glowObject.gameObject.GetComponent<PointLight>() : null;

        spawnPoint = transform.position;
        facing = secondPlayer ? -1.0f : 1.0f;

        var directorObject = GameObject.Find("Director");
        director = directorObject != null ? directorObject.GetComponent<SwordClashGame>() : null;
    }

    void Start()
    {
        // Director が居ればそこから、居なければ Rival が自分で名前を引く。
        if (director != null) rival = director.RivalOf(this);
        ResetForRound();
    }

    void Update()
    {
        // ヒットストップ中は時間を止める。当たった手応えはここで出す。
        if (hitStop > 0.0f)
        {
            hitStop -= Time.deltaTime;
            return;
        }

        // 足元の少し上から下へ。体の半分（0.85）より長くしないと床へ届かない。
        Grounded = Physics.Raycast(transform.position + Vector3.Up * 0.2f,
            new Vector3(0.0f, -1.0f, 0.0f), out _, 1.15f);
        if (Grounded)
        {
            airJumps = 1;
            upBUsed = false;
        }

        if (hitStun > 0.0f) hitStun -= Time.deltaTime;
        PeakY = Mathf.Max(PeakY, transform.position.Y);

        AdvanceMove();
        UpdateBlade();

        if (!ControlEnabled || hitStun > 0.0f) return;
        ReadJump();
        ReadAttack();
    }

    void FixedUpdate()
    {
        if (motor == null || hitStop > 0.0f) return;

        // 奥行きへ流れたら引き戻す。横スクロールの面から出さない。
        // Teleport は速度を保つので、戻しても動きが途切れない。
        var position = transform.position;
        if (Mathf.Abs(position.Z) > 0.05f)
        {
            motor.Teleport(new Vector3(position.X, position.Y, 0.0f));
        }

        // 突進斬りの持続中だけ、自分から前へ出る。技が移動を兼ねる。
        if (move == Move.SideB && InActiveWindow())
        {
            motor.Move(new Vector3(facing, 0.0f, 0.0f), 1.6f);
            return;
        }

        // CPU は Brain の指示で歩く。1P は PlayerController が native で動かすので
        // ここでは何もしない。入力の読み口を 2 つ持たない。
        if (!BrainControlled) return;

        // 【なぜ止まっているときも Move を呼ぶか】
        //   Motor は駆動された回だけ接地と壁を解き直す。呼ばない回は
        //   重力だけが積もるので、入力ゼロで放っておくと床をすり抜けて
        //   落ち続ける。PlayerController も毎フレーム呼んでいる。
        var walk = ControlEnabled && move == Move.None ? BrainMove : 0.0f;
        if (Mathf.Abs(walk) > 0.01f) facing = Mathf.Sign(walk);
        motor.Move(new Vector3(walk, 0.0f, 0.0f));
    }

    // ---- 入力 ------------------------------------------------------------

    float ReadMove()
    {
        if (BrainControlled) return BrainMove;
        var left = Input.GetKey(secondPlayer ? KeyCode.LeftArrow : KeyCode.A);
        var right = Input.GetKey(secondPlayer ? KeyCode.RightArrow : KeyCode.D);
        return (right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f);
    }

    bool ReadUp() => BrainControlled
        ? BrainMove2 == Move.UpB
        : Input.GetKey(secondPlayer ? KeyCode.UpArrow : KeyCode.W);

    bool ReadDown() => BrainControlled
        ? BrainMove2 == Move.DownB
        : Input.GetKey(secondPlayer ? KeyCode.DownArrow : KeyCode.S);

    void ReadJump()
    {
        // 1P のジャンプは PlayerController が native で処理する。
        // ここで二重に跳ばさない。空中ジャンプだけ C# が足す。
        var pressed = BrainControlled
            ? BrainJump
            : (Input.GetKeyDown(secondPlayer ? KeyCode.RightShift : KeyCode.Space) &&
                !Grounded);
        BrainJump = false;
        if (!pressed || motor == null) return;
        if (!Grounded && airJumps <= 0) return;
        if (!Grounded) --airJumps;

        motor.Jump();
        PlayVoice(1.35f, 0.32f);
    }

    void ReadAttack()
    {
        if (move != Move.None) return;

        if (BrainControlled)
        {
            if (BrainMove2 != Move.None) Begin(BrainMove2);
            BrainMove2 = Move.None;
            return;
        }

        if (Input.GetKeyDown(secondPlayer ? KeyCode.Keypad1 : KeyCode.J))
        {
            Begin(Move.Slash);
            return;
        }

        if (!Input.GetKeyDown(secondPlayer ? KeyCode.Keypad2 : KeyCode.K)) return;

        // 方向で 3 つの必殺を分ける。入力が無ければ横B。
        if (ReadUp()) Begin(Move.UpB);
        else if (ReadDown()) Begin(Move.DownB);
        else Begin(Move.SideB);
    }

    // ---- 技 --------------------------------------------------------------

    void Begin(Move value)
    {
        if (value == Move.UpB && upBUsed && !Grounded) return;

        move = value;
        moveTimer = 0.0f;
        hitLanded = false;
        Action = value.ToString();

        if (value == Move.UpB && motor != null)
        {
            upBUsed = true;
            airJumps = 0;
            motor?.AddImpulse(new Vector3(facing * 4.0f, jumpSpeed * 1.3f, 0.0f), 0.18f);
        }
        PlayVoice(value == Move.Slash ? 1.0f : 0.72f, 0.4f);
    }

    void AdvanceMove()
    {
        if (move == Move.None) return;

        moveTimer += Time.deltaTime;
        var spec = SpecOf(move);

        if (move != Move.DownB && !hitLanded && InActiveWindow()) TryHit(spec);

        if (moveTimer < spec.Total) return;
        move = Move.None;
        Action = "None";
    }

    bool InActiveWindow()
    {
        var spec = SpecOf(move);
        return moveTimer >= spec.Startup && moveTimer < spec.Startup + spec.Active;
    }

    // 剣先へ球を置いて、その中に居る Collider を engine へ聞く。
    // 物理クエリが使えない環境でも当たるよう、間合い判定の保険を後ろに置く。
    void TryHit(Spec spec)
    {
        var center = transform.position +
            new Vector3(facing * spec.Reach * 0.7f, spec.Upward * 0.5f, 0.0f);

        foreach (var collider in Physics.OverlapSphere(center, spec.Reach * 0.75f))
        {
            if (collider == null) continue;
            var found = collider.gameObject.GetComponent<SwordClashFighter>();
            if (found == null || ReferenceEquals(found, this)) continue;
            Land(found, spec);
            return;
        }

        // 保険。振っているのに一生当たらない、という状態を作らない。
        var target = Rival;
        if (target == null) return;
        var offset = target.transform.position - transform.position;
        if (Mathf.Abs(offset.Y) > spec.Reach) return;
        if (offset.X * facing < -0.4f) return;
        if (Mathf.Abs(offset.X) > spec.Reach + 0.8f) return;
        Land(target, spec);
    }

    void Land(SwordClashFighter target, Spec spec)
    {
        hitLanded = true;
        target.Receive(this, spec.Damage, spec.KnockBase, spec.KnockGrow, spec.Upward);
    }

    // 相手から食らう。下B 中なら受け流して倍で返す。
    public void Receive(SwordClashFighter from, float damage, float knockBase,
        float knockGrow, float upward)
    {
        if (move == Move.DownB && InActiveWindow())
        {
            PlayVoice(0.55f, 0.85f);
            StartCoroutine(Flash(new Color(1.0f, 0.92f, 0.35f, 1.0f), 0.22f));
            director?.OnCounter(this, from);
            from.Receive2(this, damage * 1.6f + 6.0f, 7.0f, 0.12f, 0.5f);
            return;
        }
        Receive2(from, damage, knockBase, knockGrow, upward);
    }

    // カウンターの往復で無限に戻らないよう、返し technique は受け流さない側から呼ぶ。
    public void Receive2(SwordClashFighter from, float damage, float knockBase,
        float knockGrow, float upward)
    {
        Damage += damage;

        // ふっとびは蓄積ダメージで伸びる。重い側ほど飛ばない。
        var power = (knockBase + Damage * knockGrow) / Mathf.Max(0.4f, weight);
        var away = Mathf.Sign(transform.position.X - from.transform.position.X);
        if (Mathf.Abs(away) < 0.01f) away = from.facing;

        // ふっとばしは Motor の撃力へ。接地していても上へ抜ける。
        // 蓄積が高いほど長く飛ぶ。この間は操作も減速も効かない。
        var hold = 0.25f + Mathf.Min(0.55f, Damage * 0.004f);
        if (motor == null)
        {
            Launch = "nomotor";
        }
        else
        {
            var status = motor.AddImpulse(
                new Vector3(away * power, power * (0.62f + upward), 0.0f), hold);
            Launch = status + " p" + ((int)power);
        }
        hitStun = hold;

        move = Move.None;
        Action = "Hit";
        hitStop = 0.07f;
        from.hitStop = 0.07f;

        // 当たった瞬間だけ火花を撒く。常時出しっぱなしにはしない。
        if (sparks != null)
        {
            sparks.startColor = new Color(1.0f, 0.86f, 0.42f, 1.0f);
            sparks.Emit(18 + (int)Mathf.Min(40.0f, Damage * 0.4f));
        }

        PlayVoice(0.9f, 0.7f);
        StartCoroutine(Flash(new Color(1.0f, 0.35f, 0.35f, 1.0f), 0.14f));
        director?.OnHit(from, this, damage);
    }

    // ---- 見た目 ----------------------------------------------------------

    void UpdateBlade()
    {
        if (blade == null) return;

        // 技ごとに軌道も長さも変える。全部同じ角度差だと見分けがつかない。
        var angle = -20.0f;
        var reach = 0.62f;
        var length = 1.0f;
        var lift = 0.18f;

        if (move != Move.None)
        {
            var spec = SpecOf(move);
            var t = Mathf.Clamp01(moveTimer / Mathf.Max(0.01f, spec.Total));
            switch (move)
            {
                case Move.Slash:
                    // 上段から下段への薙ぎ。振り切るまでで 1 往復させない。
                    angle = Mathf.Lerp(-110.0f, 85.0f, t);
                    reach = 0.62f + Mathf.Sin(t * Mathf.PI) * 0.35f;
                    length = 1.0f + Mathf.Sin(t * Mathf.PI) * 0.35f;
                    break;

                case Move.SideB:
                    // 突き。刃を前へ倒したまま、長く伸ばして押し出す。
                    angle = Mathf.Lerp(-30.0f, 5.0f, t);
                    reach = 0.70f + Mathf.Sin(t * Mathf.PI) * 0.95f;
                    length = 1.0f + Mathf.Sin(t * Mathf.PI) * 1.30f;
                    lift = 0.05f;
                    break;

                case Move.UpB:
                    // 昇り斬り。1 回転させて上へ抜ける。
                    angle = Mathf.Lerp(60.0f, -300.0f, t);
                    reach = 0.55f;
                    length = 1.0f + Mathf.Sin(t * Mathf.PI) * 0.55f;
                    lift = 0.18f + t * 0.55f;
                    break;

                case Move.DownB:
                    // 構え。動かさない代わりに手前へ立てて分かるようにする。
                    angle = -95.0f;
                    reach = 0.42f;
                    length = 0.85f;
                    lift = 0.30f;
                    break;
            }
        }

        blade.localPosition = new Vector3(facing * reach, lift, 0.0f);
        blade.localEulerAngles = new Vector3(0.0f, 0.0f, angle * facing);
        blade.localScale = new Vector3(1.35f * length, 0.10f, 0.10f);

        // 持続の間だけ刃を光らせ、帯を出す。振り終わりに切れて軌跡が残る。
        var live = move != Move.None && move != Move.DownB && InActiveWindow();
        if (bladeSkin != null)
        {
            bladeSkin.color = live
                ? new Color(1.0f, 0.98f, 0.72f, 1.0f)
                : new Color(0.78f, 0.84f, 0.94f, 1.0f);
        }
        if (bladeTrail != null) bladeTrail.emitting = live;

        // 下B の構えは黄色い光で分かるようにする。読み合いの材料になる。
        if (glow != null)
        {
            var guarding = move == Move.DownB && InActiveWindow();
            glow.intensity = guarding ? 9.0f : (live ? 5.0f : 1.4f);
            glow.color = guarding
                ? new Color(1.0f, 0.88f, 0.30f, 1.0f)
                : new Color(baseColor.R, baseColor.G, baseColor.B, 1.0f);
        }
    }

    IEnumerator Flash(Color color, float seconds)
    {
        if (skin == null) yield break;
        skin.color = color;
        yield return new WaitForSeconds(seconds);
        if (skin != null) skin.color = baseColor;
    }

    void PlayVoice(float pitch, float volume)
    {
        if (voice == null) return;
        voice.pitch = pitch;
        voice.volume = volume;
        voice.Play();
    }

    // ---- Director から ---------------------------------------------------

    // 相手。Director が居なくても自分で引けるようにしてある。
    //
    // Director 経由だけにすると、Director の初期化が 1 つでも失敗した瞬間に
    // CPU が相手を見つけられず、その場から一歩も動かなくなる。
    // 名前で引くのは最初の 1 回だけで、あとは覚えた値を返す。
    public SwordClashFighter? Rival
    {
        get
        {
            if (rival != null) return rival;
            var other = GameObject.Find(secondPlayer ? "Fighter1" : "Fighter2");
            rival = other != null ? other.GetComponent<SwordClashFighter>() : null;
            return rival;
        }
    }
    public float Facing => facing;
    public Move Current => move;

    public bool OutOfBounds() =>
        Mathf.Abs(transform.position.X) > 15.0f || transform.position.Y < -9.0f ||
        transform.position.Y > 22.0f;

    public void ResetForRound()
    {
        Damage = 0.0f;
        RespawnAt(spawnPoint);
    }

    public void RespawnAt(Vector3 point)
    {
        transform.position = point;
        move = Move.None;
        Action = "None";
        moveTimer = 0.0f;
        hitStop = 0.0f;
        airJumps = 1;
        upBUsed = false;
        facing = secondPlayer ? -1.0f : 1.0f;
        if (skin != null) skin.color = baseColor;
        motor?.Teleport(point);
    }

    public void Respawn() => RespawnAt(spawnPoint + Vector3.Up * 4.0f);
}
