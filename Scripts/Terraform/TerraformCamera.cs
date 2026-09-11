using ReplayEngine;

namespace Game.Terraform;

// アタッチ先: Terraform.replayscene の CameraRig。
// 担当: 三人称の背後追従。見下ろしにはしない。
//
// 地形が持ち上がってもカメラが地面へ潜らないよう、
// Landscape.SampleWorldHeight で足元を見て最低高さを保つ。
[ReplayGuid("6c3d8ea035ab7149c2e6d8bf0a5d1724")]
public class TerraformCamera : MonoBehaviour
{
    [SerializeField]
    [Tooltip("プレイヤーからの距離")]
    [Range(3.0, 24.0)]
    float distance = 8.6f;

    [SerializeField]
    [Tooltip("注視点をプレイヤーのどれだけ上に置くか")]
    float lookHeight = 1.5f;

    [SerializeField]
    float sensitivity = 0.30f;

    [SerializeField]
    float followSharpness = 10.0f;

    [SerializeField]
    [Tooltip("地面からこれ以上は下がらない")]
    float groundClearance = 1.1f;

    // 真下を向かせない。地形を狙うために少しだけ下を向ける範囲に留める。
    const float MinPitchDegrees = -8.0f;
    const float MaxPitchDegrees = 34.0f;

    [ReadOnly] public float Yaw;
    [ReadOnly] public float Pitch = 16.0f;

    Transform? target;
    Landscape? ground;
    Vector2 lastMouse;
    bool hasLastMouse;

    void Awake()
    {
        var player = GameObject.Find("Player");
        target = player != null ? player.transform : null;

        var groundObject = GameObject.Find("Ground");
        ground = groundObject != null ? groundObject.GetComponent<Landscape>() : null;
    }

    void Start() => SnapBehindTarget();

    // 位置合わせは LateUpdate。Player の移動と地形の変形が確定してから追う。
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

        // 盛った地面へカメラがめり込まないようにする。
        if (ground != null)
        {
            var floor = ground.SampleWorldHeight(desired) + groundClearance;
            if (desired.Y < floor) desired = new Vector3(desired.X, floor, desired.Z);
        }

        var blend = Mathf.Clamp01(followSharpness * Time.deltaTime);
        transform.position = Vector3.Lerp(transform.position, desired, blend);
        transform.LookAt(focus);
    }

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

    public void SnapBehindTarget()
    {
        if (target == null) return;
        var focus = target.position + Vector3.Up * lookHeight;
        transform.position = focus - Orbit() * distance;
        transform.LookAt(focus);
    }
}
