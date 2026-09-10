using ReplayEngine;

namespace Game.SwordClash;

// アタッチ先: SwordClash.replayscene の Fighter2。
// 担当: 同じ GameObject の Fighter へ仮想入力を入れる CPU。
//
// Fighter 側は「誰が入力したか」を知らない。人間と同じ入口へ値を流すだけ。
// 判断は間合いと高さだけ。読み合いの下地になる程度に留める。
[ReplayGuid("3e7b25c9f0a648d1b93c7e5a2f81d604")]
public class SwordClashBrain : MonoBehaviour
{
    [SerializeField]
    [Tooltip("この距離まで詰めてから振る")]
    [Range(0.5, 6.0)]
    float strikeRange = 1.9f;

    [SerializeField]
    [Tooltip("判断の間隔。短いほど強い")]
    [Range(0.02, 1.0)]
    float thinkInterval = 0.13f;

    [SerializeField]
    [Tooltip("必殺を選ぶ割合")]
    [Range(0.0, 1.0)]
    float specialRate = 0.35f;

    [Header("状態")]
    [ReadOnly]
    public string Intent = "Idle";

    SwordClashFighter? self;
    SwordClashFighter? target;
    float thinkTimer;
    int randomState = 987654321;

    void Awake()
    {
        self = GetComponent<SwordClashFighter>();
        if (self != null) self.BrainControlled = true;
    }

    void Start()
    {
        target = self != null ? self.Rival : null;
    }

    void Update()
    {
        if (self == null || !self.ControlEnabled) return;
        if (target == null)
        {
            target = self.Rival;
            return;
        }

        thinkTimer -= Time.deltaTime;
        if (thinkTimer > 0.0f) return;
        thinkTimer = thinkInterval;

        var here = self.transform.position;
        var there = target.transform.position;
        var gap = there.X - here.X;
        var height = there.Y - here.Y;
        var distance = Mathf.Abs(gap);

        // 場外へ出ていたら何より先に戻る。落ちる CPU は相手にならない。
        if (Mathf.Abs(here.X) > 11.0f)
        {
            Intent = "Recover";
            self.BrainMove = -Mathf.Sign(here.X);
            if (here.Y < 1.2f) self.BrainMove2 = SwordClashFighter.Move.UpB;
            else self.BrainJump = true;
            return;
        }

        // 間合いの外なら詰める。段差があれば跳ぶ。
        if (distance > strikeRange)
        {
            Intent = "Approach";
            self.BrainMove = Mathf.Sign(gap);

            // 遠いときだけ横B で一気に詰める。近距離では暴発になる。
            if (distance > 5.0f && Mathf.Abs(height) < 1.5f && Roll() < specialRate * 0.5f)
            {
                self.BrainMove2 = SwordClashFighter.Move.SideB;
            }
            else if (height > 1.4f && self.Grounded)
            {
                self.BrainJump = true;
            }
            return;
        }

        // 間合いの中。相手が振っていたら下B で受ける択も混ぜる。
        self.BrainMove = 0.0f;
        var incoming = target.Current != SwordClashFighter.Move.None;
        var roll = Roll();

        if (incoming && roll < 0.30f)
        {
            Intent = "Counter";
            self.BrainMove2 = SwordClashFighter.Move.DownB;
            return;
        }

        if (height > 1.0f && roll < specialRate)
        {
            Intent = "UpB";
            self.BrainMove2 = SwordClashFighter.Move.UpB;
            return;
        }

        Intent = "Slash";
        self.BrainMove2 = roll < specialRate * 0.4f
            ? SwordClashFighter.Move.SideB
            : SwordClashFighter.Move.Slash;
    }

    // 0..1 の乱数。種を固定して、CPU の動きが毎回同じ筋になるようにする。
    float Roll()
    {
        randomState = randomState * 1103515245 + 12345;
        return ((randomState >> 8) & 0xFFFF) / 65535.0f;
    }
}
