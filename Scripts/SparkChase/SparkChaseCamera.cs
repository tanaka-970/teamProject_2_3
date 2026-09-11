using ReplayEngine;

namespace Game.SparkChase;

// アタッチ先: SparkChase.replayscene の CameraRig。
// 担当: プレイヤーの背後へ回り込む三人称カメラ。見下ろしにはしない。
//
// マウス左右でヨー、上下でピッチ。ピッチは水平より少しだけ下を向く範囲に留める。
[ReplayGuid("8d6ec05249f78bab0d25cf7391b64f85")]
public class SparkChaseCamera : MonoBehaviour
{
    [SerializeField]
    [Tooltip("プレイヤーからの距離")]
    [Range(2.0, 20.0)]
    float distance = 7.4f;

    [SerializeField]
    [Tooltip("注視点をプレイヤーのどれだけ上に置くか")]
    float lookHeight = 1.35f;

    [SerializeField]
    [Tooltip("マウスの感度")]
    float sensitivity = 0.32f;

    [SerializeField]
    [Tooltip("追従の追いつき速度")]
    float followSharpness = 9.0f;

    // 真下を向かせない。見下ろし視点になるのを構造的に防ぐ。
    const float MinPitchDegrees = -6.0f;
    const float MaxPitchDegrees = 26.0f;

    [ReadOnly]
    public float Yaw;

    [ReadOnly]
    public float Pitch = 12.0f;

    Transform? target;
    Camera? view;
    Vector2 lastMouse;
    bool hasLastMouse;

    void Awake()
    {
        var player = GameObject.Find("Player");
        target = player != null ? player.transform : null;

        // 子オブジェクトから Camera を探す。GetComponentInChildren も同じ入口。
        view = GetComponentInChildren<Camera>();
        if (view == null) Debug.LogWarning("CameraRig の下に Camera がありません", this);
    }

    void Start() => SnapBehindTarget();

    // 位置合わせは LateUpdate。Player の物理が確定してから追従する。
    // Update で追うと 1 フレーム遅れて画面が揺れる。
    void LateUpdate()
    {
        if (target == null) return;

        var mouse = Input.mousePosition;
        if (hasLastMouse)
        {
            var delta = mouse - lastMouse;
            Yaw += delta.X * sensitivity;
            Pitch = Mathf.Clamp(Pitch - delta.Y * sensitivity, MinPitchDegrees, MaxPitchDegrees);
        }
        lastMouse = mouse;
        hasLastMouse = true;

        var focus = target.position + Vector3.Up * lookHeight;
        var desired = focus - Orbit() * distance;

        // 地面へめり込まない高さは保つ。
        if (desired.Y < focus.Y - distance * 0.35f)
            desired = new Vector3(desired.X, focus.Y - distance * 0.35f, desired.Z);

        var blend = Mathf.Clamp01(followSharpness * Time.deltaTime);
        transform.position = Vector3.Lerp(transform.position, desired, blend);
        transform.LookAt(focus);
    }

    // ヨーとピッチから、カメラがプレイヤーを見る向きを作る。
    Vector3 Orbit()
    {
        var yaw = Yaw * Mathf.Deg2Rad;
        var pitch = Pitch * Mathf.Deg2Rad;
        var horizontal = Mathf.Cos(pitch);
        return new Vector3(
            Mathf.Sin(yaw) * horizontal,
            -Mathf.Sin(pitch),
            Mathf.Cos(yaw) * horizontal).Normalized;
    }

    // 開始時と復帰時は補間せずに真後ろへ置く。
    public void SnapBehindTarget()
    {
        if (target == null) return;
        var focus = target.position + Vector3.Up * lookHeight;
        transform.position = focus - Orbit() * distance;
        transform.LookAt(focus);
    }
}
