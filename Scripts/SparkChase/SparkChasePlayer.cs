using ReplayEngine;

namespace Game.SparkChase;

// アタッチ先: SparkChase.replayscene の Player。
// 担当: 入力を読み、Rigidbody で球を転がす。接地判定は Physics.Raycast。
//
// 新しい Authoring API だけで書いてある。
// ObjectHandle / ComponentHandle / RuntimeResult / ScriptRuntimeContext は出てこない。
[ReplayGuid("5a3b9d2f16c45e78baf29d406e831c52")]
public class SparkChasePlayer : MonoBehaviour
{
    [SerializeField]
    [Tooltip("地上での加速の強さ")]
    [Range(1.0, 200.0)]
    float moveForce = 46.0f;

    [SerializeField]
    [Tooltip("ジャンプの初速")]
    [Range(1.0, 30.0)]
    float jumpSpeed = 7.2f;

    [SerializeField]
    [Tooltip("水平方向の最大速度")]
    float maxSpeed = 11.0f;

    [Header("状態")]
    [ReadOnly]
    public bool Grounded;

    [ReadOnly]
    public float Speed;

    // 生きている間だけ操作を受け付ける。Director が切り替える。
    [HideInInspector]
    public bool ControlEnabled;

    Rigidbody? body;
    SparkChaseCamera? camera;
    Vector3 spawnPoint;

    void Awake()
    {
        // Unity と同じ書き味。見つからなければ null が返る。
        body = GetComponent<Rigidbody>();
        spawnPoint = transform.position;
        if (body == null) Debug.LogWarning("Player に Rigidbody がありません", this);

        // 別の GameObject に付いた MonoBehaviour も同じ入口で引ける。
        var rig = GameObject.Find("CameraRig");
        camera = rig != null ? rig.GetComponent<SparkChaseCamera>() : null;
    }

    // 水平成分だけ取り出して正規化する。カメラが下を向いていても
    // 前進が地面に潜らないようにするため。
    static Vector3 Flatten(Vector3 value)
    {
        var flat = new Vector3(value.X, 0.0f, value.Z);
        return flat.SqrMagnitude > 0.0001f ? flat.Normalized : Vector3.Forward;
    }

    void Update()
    {
        Speed = body != null ? Horizontal(body.velocity).Magnitude : 0.0f;

        // 足元へ短いレイを飛ばして接地を見る。
        Grounded = Physics.Raycast(transform.position + Vector3.Up * 0.1f,
            new Vector3(0.0f, -1.0f, 0.0f), out _, 0.75f);

        if (!ControlEnabled || body == null) return;

        if (Grounded && Input.GetKeyDown(KeyCode.Space))
        {
            var velocity = body.velocity;
            body.velocity = new Vector3(velocity.X, jumpSpeed, velocity.Z);
        }
    }

    // 物理は FixedUpdate で積む。Update で読んだ入力をここで消費する。
    void FixedUpdate()
    {
        if (!ControlEnabled || body == null) return;

        // 移動はカメラ基準。W はいつも「画面の奥」へ進む。
        // 見下ろしではなく背後視点なので、ワールド軸で動かすと
        // 視点を回した瞬間に操作の向きが変わってしまう。
        var forward = Flatten(camera != null ? camera.transform.forward : Vector3.Forward);
        var right = Flatten(camera != null ? camera.transform.right : Vector3.Right);

        var direction =
            right * ((Input.GetKey(KeyCode.D) ? 1.0f : 0.0f) - (Input.GetKey(KeyCode.A) ? 1.0f : 0.0f)) +
            forward * ((Input.GetKey(KeyCode.W) ? 1.0f : 0.0f) - (Input.GetKey(KeyCode.S) ? 1.0f : 0.0f));

        if (direction.SqrMagnitude > 0.0f)
        {
            body.AddForce(direction.Normalized * moveForce);
        }

        // 速すぎると操作できなくなるので水平だけ頭打ちにする。
        var flat = Horizontal(body.velocity);
        if (flat.Magnitude > maxSpeed)
        {
            var clamped = flat.Normalized * maxSpeed;
            body.velocity = new Vector3(clamped.X, body.velocity.Y, clamped.Z);
        }
    }

    // 落ちたら開始位置へ戻す。Director から呼ぶ。
    public void Respawn()
    {
        transform.position = spawnPoint;
        if (body != null)
        {
            body.velocity = Vector3.Zero;
            body.angularVelocity = Vector3.Zero;
        }
    }

    public bool FellOffStage() => transform.position.Y < -6.0f;

    static Vector3 Horizontal(Vector3 value) => new(value.X, 0.0f, value.Z);
}
