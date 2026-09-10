using ReplayEngine;

namespace Game.SwordClash;

// アタッチ先: SwordClash.replayscene の CameraRig。
// 担当: 2 人が必ず画面へ入る位置まで引く。揺れと寄りもここが持つ。
//
// 横スクロールの対戦なので、追うのは中点だけでよい。
// 画角を動かして「引き」を作るのは Camera 側の field_of_view。
[ReplayGuid("b52d8f14e6c73a09d81f4b62c095a7e3")]
public class SwordClashCamera : MonoBehaviour
{
    [SerializeField]
    [Tooltip("追従の速さ")]
    [Range(1.0, 20.0)]
    float followSpeed = 6.5f;

    [SerializeField]
    [Tooltip("いちばん寄ったときの画角")]
    [Range(20.0, 90.0)]
    float nearFov = 32.0f;

    [SerializeField]
    [Tooltip("いちばん引いたときの画角")]
    [Range(30.0, 110.0)]
    float farFov = 52.0f;

    [SerializeField]
    [Tooltip("カメラを置く奥行き")]
    float depth = -12.5f;

    Camera? lens;
    SwordClashFighter[] fighters = System.Array.Empty<SwordClashFighter>();

    // 揺れは残り時間と強さで持つ。Coroutine より 2 変数の方が扱いやすい。
    float shake;
    float shakeDecay = 1.0f;
    float punch;

    // 疑似乱数。揺れの向きだけに使うので線形合同法で足りる。
    int randomState = 20260908;

    void Awake()
    {
        var lensObject = GameObject.Find("Camera");
        lens = lensObject != null ? lensObject.GetComponent<Camera>() : null;
    }

    void Start()
    {
        // Fighter は Director より後に並んでいても Awake 済み。ここで集める。
        var stage = GameObject.Find("Fighters");
        if (stage != null) fighters = stage.GetComponentsInChildren<SwordClashFighter>();
        SnapToFighters();
    }

    void LateUpdate()
    {
        // Fighter の移動が確定してから寄せる。Update だと 1 フレーム遅れる。
        if (fighters.Length == 0) return;

        var center = Vector3.Zero;
        var spread = 0.0f;
        var alive = 0;
        foreach (var fighter in fighters)
        {
            if (fighter == null) continue;
            center += fighter.transform.position;
            ++alive;
        }
        if (alive == 0) return;
        center *= 1.0f / alive;

        foreach (var fighter in fighters)
        {
            if (fighter == null) continue;
            var offset = fighter.transform.position - center;
            spread = Mathf.Max(spread, Mathf.Abs(offset.X) * 1.15f + Mathf.Abs(offset.Y));
        }

        // 上下は中点より少し上を見る。落下側が見切れないようにするため。
        // 目線はキャラの胸の高さ。上へ寄せすぎると足元が画面下へ落ちる。
        var target = new Vector3(Mathf.Clamp(center.X, -6.0f, 6.0f),
            Mathf.Clamp(center.Y + 0.9f, 1.4f, 8.0f), depth);

        if (shake > 0.0f)
        {
            shake = Mathf.Max(0.0f, shake - Time.deltaTime * shakeDecay);
            target += new Vector3(Signed() * shake, Signed() * shake, 0.0f);
        }

        transform.position = Vector3.Lerp(transform.position, target,
            Mathf.Clamp01(Time.deltaTime * followSpeed));

        if (lens == null) return;

        // 離れているほど引く。当たった瞬間だけ punch のぶん寄る。
        punch = Mathf.Max(0.0f, punch - Time.deltaTime * 3.4f);
        var wide = Mathf.Lerp(nearFov, farFov, Mathf.Clamp01(spread / 16.0f));
        lens.fieldOfView = Mathf.Lerp(lens.fieldOfView, wide - punch * 7.0f,
            Mathf.Clamp01(Time.deltaTime * 8.0f));
    }

    // 当たった / 撃墜した瞬間に Director から呼ぶ。
    public void Shake(float strength, float seconds = 0.35f)
    {
        shake = Mathf.Max(shake, strength);
        shakeDecay = strength / Mathf.Max(0.05f, seconds);
        punch = Mathf.Max(punch, Mathf.Min(1.0f, strength * 1.6f));
    }

    public void SnapToFighters()
    {
        shake = 0.0f;
        punch = 0.0f;
        transform.position = new Vector3(0.0f, 2.0f, depth);
        if (lens != null) lens.fieldOfView = farFov;
    }

    // -1..1 の乱数。揺れが片側へ寄らないようにするだけの用途。
    float Signed()
    {
        randomState = randomState * 1103515245 + 12345;
        return (((randomState >> 8) & 0xFFFF) / 32767.5f) - 1.0f;
    }
}
