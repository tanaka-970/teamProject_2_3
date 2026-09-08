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

    Rigidbody? body;
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
        Move.Slash => new Spec(0.07f, 0.11f, 0.16f, 6.5f, 4.2f, 0.085f, 1.55f, 0.42f),
        Move.SideB => new Spec(0.12f, 0.26f, 0.30f, 12.0f, 5.6f, 0.105f, 1.75f, 0.30f),
        Move.UpB => new Spec(0.06f, 0.22f, 0.34f, 10.0f, 5.0f, 0.098f, 1.45f, 0.95f),
        Move.DownB => new Spec(0.05f, 0.30f, 0.24f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
        _ => new Spec(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
    };

    void Awake()
    {
        body = GetComponent<Rigidbody>();
        skin = GetComponent<MeshRenderer>();
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
        // 相手は Director が全 Fighter を集めてから配る。Awake では取りに行かない。
        rival = director != null ? director.RivalOf(this) : null;
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

        AdvanceMove();
        UpdateBlade();

        if (!ControlEnabled) return;
        ReadJump();
        ReadAttack();
    }

    void FixedUpdate()
    {
        if (body == null || hitStop > 0.0f) return;

        // Z へは動かさない。奥行きを使わない対戦にして間合いを読みやすくする。
        var position = transform.position;
        if (Mathf.Abs(position.Z) > 0.001f)
        {
            transform.position = new Vector3(position.X, position.Y, 0.0f);
        }

        var velocity = body.velocity;
        if (!ControlEnabled)
        {
            body.velocity = new Vector3(velocity.X * 0.86f, velocity.Y, 0.0f);
            return;
        }

        // 突進斬りの持続中は自分から前へ出る。技が移動を兼ねる。
        if (move == Move.SideB && InActiveWindow())
        {
            body.velocity = new Vector3(facing * moveSpeed * 1.55f, velocity.Y * 0.55f, 0.0f);
            return;
        }

        // 硬直中は足を止める。振ってから動けるまでの間を作る。
        if (move != Move.None && move != Move.DownB)
        {
            body.velocity = new Vector3(velocity.X * 0.78f, velocity.Y, 0.0f);
            return;
        }

        var input = ReadMove();
        if (Mathf.Abs(input) > 0.01f && move == Move.None) facing = Mathf.Sign(input);

        var control = Grounded ? 1.0f : airControl;
        var target = input * moveSpeed * control;
        var next = Mathf.Lerp(velocity.X, target, Grounded ? 0.55f : 0.22f);
        body.velocity = new Vector3(next, velocity.Y, 0.0f);
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
        var pressed = BrainControlled
            ? BrainJump
            : Input.GetKeyDown(secondPlayer ? KeyCode.RightShift : KeyCode.Space);
        BrainJump = false;
        if (!pressed || body == null) return;
        if (!Grounded && airJumps <= 0) return;
        if (!Grounded) --airJumps;

        body.velocity = new Vector3(body.velocity.X, jumpSpeed, 0.0f);
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

        if (value == Move.UpB && body != null)
        {
            upBUsed = true;
            airJumps = 0;
            body.velocity = new Vector3(body.velocity.X * 0.6f, jumpSpeed * 1.18f, 0.0f);
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
    void TryHit(Spec spec)
    {
        var center = transform.position +
            new Vector3(facing * spec.Reach * 0.75f, spec.Upward * 0.55f, 0.0f);
        var found = Physics.OverlapSphere(center, spec.Reach * 0.62f);

        foreach (var collider in found)
        {
            if (collider == null) continue;
            var target = collider.gameObject.GetComponent<SwordClashFighter>();
            if (target == null || ReferenceEquals(target, this)) continue;

            hitLanded = true;
            target.Receive(this, spec.Damage, spec.KnockBase, spec.KnockGrow, spec.Upward);
            return;
        }
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

        if (body != null)
        {
            body.velocity = Vector3.Zero;
            body.AddImpulse(new Vector3(away * power, power * (0.55f + upward), 0.0f));
        }

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

        // 技ごとに刃の角度を変えるだけ。モーション資産は要らない。
        var angle = -20.0f;
        if (move != Move.None)
        {
            var spec = SpecOf(move);
            var t = Mathf.Clamp01(moveTimer / Mathf.Max(0.01f, spec.Total));
            angle = move switch
            {
                Move.Slash => Mathf.Lerp(-95.0f, 75.0f, t),
                Move.SideB => Mathf.Lerp(-10.0f, 20.0f, t),
                Move.UpB => Mathf.Lerp(40.0f, -120.0f, t),
                Move.DownB => -160.0f,
                _ => -20.0f,
            };
        }
        blade.localPosition = new Vector3(facing * 0.62f, 0.18f, 0.0f);
        blade.localEulerAngles = new Vector3(0.0f, 0.0f, angle * facing);

        // 持続の間だけ刃を光らせ、帯を出す。振り終わりに切れて軌跡が残る。
        var live = move != Move.None && move != Move.DownB && InActiveWindow();
        if (bladeSkin != null)
        {
            bladeSkin.color = live
                ? new Color(1.0f, 0.96f, 0.62f, 1.0f)
                : new Color(0.78f, 0.84f, 0.94f, 1.0f);
        }
        if (bladeTrail != null) bladeTrail.emitting = live;

        // 下B の構えは黄色い光で分かるようにする。読み合いの材料になる。
        if (glow != null)
        {
            var guarding = move == Move.DownB && InActiveWindow();
            glow.intensity = guarding ? 6.5f : (live ? 3.2f : 1.1f);
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

    public SwordClashFighter? Rival => rival;
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
        if (body != null)
        {
            body.velocity = Vector3.Zero;
            body.angularVelocity = Vector3.Zero;
        }
    }

    public void Respawn() => RespawnAt(spawnPoint + Vector3.Up * 4.0f);
}
