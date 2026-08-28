using System.Collections;
using ReplayEngine;

namespace Game;

// C# API の使い方見本。型付き Component / Transform / 入力 / 接触 / Coroutine。
//
// 【見かた】
// Play 中に Inspector でこのコンポーネントを見る。
//
//   Frames      … Update が呼ばれた回数
//   FoundCamera … Camera Component を型で引けたら 1
//   Hits        … 接触した回数
//   LastStatus  … 直近の書き込み結果（0 が成功）
//
// 【型付き Component の呼び方】
// 型付き Component は readonly struct なので、プロパティ経由の代入は
// CS1612（値のコピーへの代入）で弾かれる。**必ずローカル変数へ受ける。**
//
//     var camera = GetComponentOrDefault<CameraComponent>();
//     camera.FieldOfView = 50.0f;                                 // OK
//     GetComponent<CameraComponent>().Value.FieldOfView = 50.0f;  // NG
//
// Transform は ScriptBehaviour のフィールドなので、そのまま代入できる。
[ReplayGuid("2f6b48d1c07e4a5d9b3e81a6c4d20f77")]
public sealed class ComponentApiSample : ScriptBehaviour
{
    public enum MoveMode { Idle = 0, Walk = 1, Dash = 2 }

    // ---- Inspector 属性つきのフィールド ----

    [Header("移動")]
    [Range(0.0, 20.0)]
    [Tooltip("1 秒あたりの回転量（ラジアン）")]
    public float TurnSpeed = 1.0f;

    public MoveMode Mode = MoveMode.Walk;

    [Range(1.0, 179.0)]
    public float TargetFieldOfView = 55.0f;

    [AssetType("Image")]
    public AssetReference Icon;

    [HideInInspector]
    public int InternalCounter = 0;

    // ---- Play 中に増えるのを見るためのもの ----

    public int Frames = 0;
    public int FoundCamera = 0;
    public int Hits = 0;
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

        // 2 秒後に 1 回だけ / 0.5 秒ごとに繰り返し。
        After(2.0f, () => Runtime.LogInfo("2 秒経った", GameObject));
        Every(0.5f, () => ++InternalCounter);

        // 明るさを 1 秒かけて動かす。
        TweenValue(0.0f, 1.0f, 1.0f, value =>
        {
            if (!TryGetComponent<SkinnedMeshRendererComponent>(out var target)) return;
            target.Tint = new Color(value, value, value);
        }, Easing.OutCubic);

        StartCoroutine(Blink());
    }

    public override void Update(float deltaTime)
    {
        ++Frames;

        // Transform はフィールドなので直接代入できる。
        Transform.Rotate(new Vector3(0.0f, TurnSpeed * deltaTime, 0.0f));

        // 生キーとマウス。Action を定義するほどでもない入力に使う。
        if (Input.GetKeyDown(Key.Space)) Mode = MoveMode.Dash;
        if (Input.GetMouseButtonDown(MouseButton.Left))
        {
            Runtime.LogInfo($"クリック位置 {Input.MousePosition.X}, {Input.MousePosition.Y}",
                GameObject);
        }

        // ゲームパッド。左スティックでそのまま動かす。
        if (Input.GamepadConnected())
        {
            var stick = Input.GetLeftStick();
            Transform.Translate(new Vector3(stick.X * deltaTime, 0.0f, stick.Y * deltaTime));
        }
    }

    public override void FixedUpdate(float fixedDeltaTime)
    {
        // 物理は FixedUpdate から積む。
        if (!TryGetComponent<RigidbodyComponent>(out var body)) return;
        if (Mode != MoveMode.Dash) return;

        var forward = Transform.Forward;
        LastStatus = (int)body.AddForce(new Vector3(
            forward.X * 10.0f, 0.0f, forward.Z * 10.0f));
    }

    // 接触は override するだけで届く。Poll も Subscribe も要らない。
    public override void OnCollisionEnter(CollisionInfo collision)
    {
        ++Hits;
        Runtime.LogInfo($"当たった 法線Y={collision.ContactNormal.Y}", GameObject);
    }

    public override void OnTriggerEnter(TriggerInfo trigger)
    {
        if (!trigger.OtherValid) return;
        var name = Runtime.GetName(trigger.Other);
        if (name.Succeeded) Runtime.LogInfo($"{name.Value} が入った", GameObject);
    }

    private IEnumerator Blink()
    {
        // yield return null で次のフレームまで待つ。
        while (Frames < 300)
        {
            if (TryGetComponent<SkinnedMeshRendererComponent>(out var renderer))
            {
                renderer.Visible = !renderer.Visible;
            }
            yield return new WaitForSeconds(0.25f);
        }
    }
}
