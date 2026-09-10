using ReplayEngine;

namespace Game.Terraform;

// アタッチ先: Terraform.replayscene の Player。
// 担当: 画面中央のレイで地形を狙い、盛る / 削る / 均す。
//
// 地形を変える実装は engine 既存の LandscapeEditorTool。
// ここは「どこを、どのモードで、どれだけ」を決めているだけ。
[ReplayGuid("2e8f4a6b91c73d05e8a2f47b6c19d380")]
public class TerraformSculptor : MonoBehaviour
{
    [SerializeField]
    [Tooltip("届く距離")]
    [Range(4.0, 60.0)]
    float reach = 26.0f;

    [SerializeField]
    [Tooltip("1 秒あたりの強さ")]
    [Range(0.5, 30.0)]
    float strength = 7.0f;

    [SerializeField]
    [Tooltip("ブラシの半径。ホイールで変わる")]
    float radius = 3.4f;

    [SerializeField]
    float minRadius = 1.2f;

    [SerializeField]
    float maxRadius = 9.0f;

    [Header("状態")]
    [ReadOnly] public bool Aiming;
    [ReadOnly] public Vector3 AimPoint;
    [ReadOnly] public float Radius;

    // 自動確認から読む集計。
    [HideInInspector] public int RaiseTicks;
    [HideInInspector] public int LowerTicks;
    [HideInInspector] public int SmoothTicks;

    [HideInInspector] public bool ControlEnabled;

    Landscape? ground;
    Transform? view;
    GameObject? marker;

    void Awake()
    {
        var groundObject = GameObject.Find("Ground");
        ground = groundObject != null ? groundObject.GetComponent<Landscape>() : null;
        if (ground == null) Debug.LogError("Ground に Landscape がありません", this);

        var rig = GameObject.Find("CameraRig");
        view = rig != null ? rig.transform : null;

        marker = GameObject.Find("AimMarker");
        Radius = radius;
    }

    void Update()
    {
        if (ground == null || view == null) return;

        // 画面中央から地形へレイ。Collider ではなく地形の三角形へ直接当てるので、
        // 彫った直後の形にもそのフレームのうちに当たる。
        var found = ground.RaycastWorld(view.position, view.forward, out var hit, reach);
        Aiming = ControlEnabled && found;

        if (Aiming)
        {
            AimPoint = ground.LocalToWorld(hit.point);
            ShowMarker(true);
        }
        else
        {
            ShowMarker(false);
            return;
        }

        // ホイールでブラシの大きさ。
        var wheel = Input.mouseScrollDelta;
        if (wheel != 0.0f)
        {
            radius = Mathf.Clamp(radius + wheel * 0.6f, minRadius, maxRadius);
            Radius = radius;
        }

        if (Input.GetMouseButton(0)) Apply(SculptMode.Raise, ref RaiseTicks);
        else if (Input.GetMouseButton(1)) Apply(SculptMode.Lower, ref LowerTicks);
        else if (Input.GetKey(KeyCode.F)) Apply(SculptMode.Smooth, ref SmoothTicks);
    }

    void Apply(SculptMode mode, ref int counter)
    {
        if (ground == null) return;
        // strength は 1 秒あたり。deltaTime は Sculpt 側が掛ける。
        if (ground.SculptWorld(AimPoint, mode, radius, strength)) ++counter;
    }

    // 狙っている場所へ印を出す。地形の高さに合わせて置く。
    void ShowMarker(bool visible)
    {
        if (marker == null) return;
        if (marker.activeSelf != visible) marker.SetActive(visible);
        if (!visible) return;

        marker.transform.position = AimPoint + Vector3.Up * 0.12f;
        var scale = Radius * 2.0f;
        marker.transform.localScale = new Vector3(scale, 0.06f, scale);
    }

    // 自動確認から地形を操作するための入口。
    public bool SculptAt(Vector3 worldPoint, SculptMode mode, float brushRadius,
        float brushStrength, float deltaTime)
        => ground != null &&
            ground.SculptWorld(worldPoint, mode, brushRadius, brushStrength,
                deltaTime: deltaTime);

    public float SampleGroundHeight(Vector3 worldPoint)
        => ground != null ? ground.SampleWorldHeight(worldPoint) : 0.0f;
}
