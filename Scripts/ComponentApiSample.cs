using ReplayEngine;

namespace Game;

// 型付き Component API の使い方見本。
//
// 【見かた】
// Play 中に Inspector でこのコンポーネントを見る。
//
//   Frames      … Update が呼ばれた回数
//   FoundCamera … Camera Component を型で引けたら 1
//   FoundBody   … Rigidbody Component を型で引けたら 1
//   LastStatus  … 直近の書き込み結果（0 が成功）
//
// 【型付き Component の呼び方】
// C# の構造体はプロパティ経由だとコピーへの代入になり CS1612 で弾かれる。
// **必ず一度ローカル変数へ受けてから**プロパティを触ること。
//
//     var camera = GetComponentOrDefault<CameraComponent>();
//     camera.FieldOfView = 50.0f;              // OK
//     GetComponent<CameraComponent>().Value.FieldOfView = 50.0f;  // NG (CS1612)
//
// Transform は ScriptBehaviour のフィールドなので、そのまま代入できる。
//
//     Transform.Position = new Vector3(0.0f, 1.0f, 0.0f);
[ReplayGuid("2f6b48d1c07e4a5d9b3e81a6c4d20f77")]
public sealed class ComponentApiSample : ScriptBehaviour
{
    // Play 前に Inspector で変えられる。
    public float TurnSpeed = 1.0f;
    public float TargetFieldOfView = 55.0f;

    // ---- ここから下は Play 中に増えるのを見るためのもの ----

    public int Frames = 0;
    public int FoundCamera = 0;
    public int FoundBody = 0;
    public int LastStatus = 0;

    public override void Start()
    {
        // 型で引く。uint の Type ID を手で書く必要はない。
        if (TryGetComponent<CameraComponent>(out var camera))
        {
            FoundCamera = 1;
            camera.FieldOfView = TargetFieldOfView;
        }

        // Renderer の見た目を変える。プロパティ名は Inspector と同じ。
        if (TryGetComponent<SkinnedMeshRendererComponent>(out var renderer))
        {
            renderer.Tint = new Color(1.0f, 0.9f, 0.8f);
            renderer.CastShadow = true;
        }

        // 型付きの入口がまだ無い Component は、C++ の型名で直接触れる。
        var stack = Runtime.GetComponent(GameObject, "ModelEffectStackComponent");
        if (stack.Succeeded)
        {
            LastStatus = (int)stack.Value.Accessor.SetBool("enabled", true);
        }
    }

    public override void Update(float deltaTime)
    {
        ++Frames;

        // Transform はフィールドなので直接代入できる。
        Transform.Rotate(new Vector3(0.0f, TurnSpeed * deltaTime, 0.0f));
    }

    public override void FixedUpdate(float fixedDeltaTime)
    {
        // 物理は FixedUpdate から積む。
        if (!TryGetComponent<RigidbodyComponent>(out var body)) return;
        FoundBody = 1;

        // 前方向へ押す。Forward はワールド前方。
        var forward = Transform.Forward;
        LastStatus = (int)body.AddForce(new Vector3(
            forward.X * 10.0f, 0.0f, forward.Z * 10.0f));
    }
}
