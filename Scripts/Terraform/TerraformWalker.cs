using ReplayEngine;

namespace Game.Terraform;

// アタッチ先: Terraform.replayscene の Player。
// 担当: 三人称の移動。地形の高さに沿って歩く。
//
// 地形は毎フレーム形が変わるので、Rigidbody に任せず
// Landscape.SampleWorldHeight で足元を取って自分で乗せる。
// 変形の直後でも足が地面へめり込まないのが理由。
[ReplayGuid("3f9a5b7c02d84e16f9b3a58c7d2ae491")]
public class TerraformWalker : MonoBehaviour
{
    [SerializeField]
    [Tooltip("歩く速さ")]
    [Range(1.0, 30.0)]
    float moveSpeed = 8.5f;

    [SerializeField]
    [Tooltip("登れる斜面の高さ差の上限")]
    float stepLimit = 2.4f;

    [SerializeField]
    [Tooltip("地面へ吸い付く速さ")]
    float groundSharpness = 14.0f;

    [SerializeField]
    [Tooltip("移動できる範囲の半径")]
    float fieldRadius = 46.0f;

    [Header("状態")]
    [ReadOnly] public float GroundHeight;
    [ReadOnly] public bool Blocked;

    [HideInInspector] public bool ControlEnabled;

    Landscape? ground;
    Transform? view;
    float bodyOffset = 0.9f;

    void Awake()
    {
        var groundObject = GameObject.Find("Ground");
        ground = groundObject != null ? groundObject.GetComponent<Landscape>() : null;

        var rig = GameObject.Find("CameraRig");
        view = rig != null ? rig.transform : null;

        bodyOffset = transform.localScale.Y * 0.5f + 0.1f;
    }

    void Update()
    {
        if (ground == null) return;

        var position = transform.position;

        if (ControlEnabled && view != null)
        {
            // 移動はカメラ基準。三人称なので W はいつも画面の奥。
            var forward = Flatten(view.forward);
            var right = Flatten(view.right);
            var input =
                right * ((Input.GetKey(KeyCode.D) ? 1.0f : 0.0f) - (Input.GetKey(KeyCode.A) ? 1.0f : 0.0f)) +
                forward * ((Input.GetKey(KeyCode.W) ? 1.0f : 0.0f) - (Input.GetKey(KeyCode.S) ? 1.0f : 0.0f));

            if (input.SqrMagnitude > 0.0f)
            {
                var step = input.Normalized * moveSpeed * Time.deltaTime;
                var next = position + step;

                // 急すぎる崖は登らない。自分で作った坂を登る遊びにするため、
                // 「登れる高さ」に上限を置く。
                var nextGround = ground.SampleWorldHeight(next);
                Blocked = nextGround - GroundHeight > stepLimit;

                // 場外へは出さない。
                var flat = new Vector3(next.X, 0.0f, next.Z);
                if (flat.Magnitude > fieldRadius) Blocked = true;

                if (!Blocked)
                {
                    position = next;
                    // 進む向きへ体を向ける。
                    transform.LookAt(new Vector3(next.X + step.X, position.Y, next.Z + step.Z));
                }
            }
            else
            {
                Blocked = false;
            }
        }

        // 足元の高さへ寄せる。地形が持ち上がれば一緒に持ち上がる。
        GroundHeight = ground.SampleWorldHeight(position);
        var target = GroundHeight + bodyOffset;
        var blend = Mathf.Clamp01(groundSharpness * Time.deltaTime);
        transform.position = new Vector3(position.X,
            position.Y + (target - position.Y) * blend, position.Z);
    }

    static Vector3 Flatten(Vector3 value)
    {
        var flat = new Vector3(value.X, 0.0f, value.Z);
        return flat.SqrMagnitude > 0.0001f ? flat.Normalized : Vector3.Forward;
    }

    public void MoveTo(Vector3 worldPosition)
    {
        var height = ground != null ? ground.SampleWorldHeight(worldPosition) : 0.0f;
        transform.position = new Vector3(worldPosition.X, height + bodyOffset, worldPosition.Z);
        GroundHeight = height;
    }
}
