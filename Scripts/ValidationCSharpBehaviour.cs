using ReplayEngine;

namespace ValidationScripts;

public enum ValidationMode { First = 0, Second = 1, Third = 2 }
[System.Serializable] public struct ValidationSettings
{
    public int Lives;
    public Vector3 Spawn;
}

[ReplayGuid("c5a9c4a3d7914bb5a0b64b68de81d7f1")]
public sealed class ValidationCSharpBehaviour : ScriptBehaviour
{
    public float Speed = 2.5f;
    public int Counter = 7;
    public ObjectReference Target;
    public ComponentReference TargetComponent;
    public string LastEventType = string.Empty;
    public int ApiChecks = 0;
    public int RuntimeChecks = 0;
    public int TypedEventChecks = 0;
    [Range(0.0, 10.0)] public float RangedValue = 1.0f;
    [Tooltip("説明文")] [Header("見出し")] public int Described = 3;
    [HideInInspector] public int Hidden = 5;
    [AssetType("Image")] public AssetReference Picture;
    public ValidationMode Mode = ValidationMode.Second;
    public int[] Scores = new[] { 1, 2, 3 };
    public System.Collections.Generic.List<string> Tags = new() { "alpha", "日本語" };
    public System.Collections.Generic.Dictionary<string, float> Tuning = new() { ["speed"] = 2.5f };
    public ValidationSettings Settings = new() { Lives = 3, Spawn = new Vector3(1, 2, 3) };
    public AnimationCurve Curve = AnimationCurve.Linear(0, 0, 1, 1);
    private EventSubscription subscription;

    public override void Awake()
    {
        Counter += 1;
        var result = SubscribeEvent("a1000000000000000000000000000006");
        if (result.Succeeded) subscription = result.Value;
        if (Runtime.InputUnavailable() != RuntimeStatus.ServiceUnavailable) Counter = -100;
        if (Runtime.AudioUnavailable() != RuntimeStatus.ServiceUnavailable) Counter = -101;
        if (Runtime.RuntimeUIUnavailable() != RuntimeStatus.ServiceUnavailable) Counter = -102;
        if (Runtime.SaveGameUnavailable() != RuntimeStatus.ServiceUnavailable) Counter = -103;
        ApiChecks = RunComponentApiChecks();
        StartCoroutine(CountUp());
        After(0.0f, () => { RuntimeChecks += 1; });
        TweenValue(0.0f, 10.0f, 0.0f, v => { if (v >= 10.0f) RuntimeChecks += 1; });
        RuntimeChecks += RunRuntimeApiChecks();
    }

    private System.Collections.IEnumerator CountUp()
    {
        RuntimeChecks += 1;
        yield return null;
    }

    // v11 で足した入力 / Scene / イベント定数を確かめる。
    private int RunRuntimeApiChecks()
    {
        var passed = 0;
        // Input Service 未接続でも例外にならず false を返す。
        if (!Input.GetKey(Key.A)) ++passed;
        if (!Input.GamepadConnected()) ++passed;
        if (Input.MouseScrollDelta == 0.0f) ++passed;
        if (EngineEventIds.CollisionEnter.Length == 32) ++passed;
        if (EngineEventIds.ButtonClicked.Length == 32) ++passed;
        if (Vector3.Cross(Vector3.Right, Vector3.Up).Z > 0.99f) ++passed;
        var turned = Quaternion.AngleAxis(MathF.PI * 0.5f, Vector3.Up) * Vector3.Forward;
        if (turned.X > 0.99f) ++passed;
        if (Runtime.OverlapSphere(Vector3.Zero, 1.0f).Status ==
            RuntimeStatus.ServiceUnavailable) ++passed;
        if (Runtime.CurrentSceneGuid().Status != RuntimeStatus.Ok ||
            Runtime.CurrentSceneGuid().Value != null) ++passed;
        var spawn = Runtime.InstantiateDeferred("missing",
            new Vector3(0.0f, 0.0f, 0.0f), new Vector3(0.0f, 0.0f, 0.0f),
            new Vector3(1.0f, 1.0f, 1.0f));
        if (!spawn.Succeeded) ++passed;
        if (!Runtime.TakeSpawnResult(0).Succeeded) ++passed;
        return passed;
    }

    // v10 で足した型付き Component API を実行時に確かめる。
    private int RunComponentApiChecks()
    {
        var passed = 0;
        if (Runtime.ComponentTypeId("CameraComponent").Value != 0) ++passed;
        var cameraType = Runtime.ComponentTypeInfo<CameraComponent>();
        if (cameraType.Succeeded && cameraType.Value.TypeId != 0 &&
            cameraType.Value.NativeTypeName == "CameraComponent") ++passed;
        Transform.LocalPosition = new Vector3(1.0f, 2.0f, 3.0f);
        if (Transform.LocalPosition.Y == 2.0f) ++passed;
        var axes = Transform.Forward;
        if (axes.Z > 0.99f) ++passed;
        var made = Runtime.CreateGameObject("ApiProbe");
        if (!made.Succeeded) return passed;
        var addedCamera = Runtime.AddComponent<CameraComponent>(made.Value);
        if (addedCamera.Succeeded) ++passed;
        var camera = addedCamera.Value;
        camera.FieldOfView = 42.0f;
        if (camera.FieldOfView == 42.0f) ++passed;
        if (Runtime.TryGetComponent<CameraComponent>(made.Value, out var found) &&
            found.FieldOfView == 42.0f) ++passed;
        if (!Runtime.HasComponent<RigidbodyComponent>(made.Value)) ++passed;
        var body = Runtime.AddComponentOrDefault<RigidbodyComponent>(made.Value);
        if (body.IsValid && body.SetVelocity(new Vector3(0.0f, 5.0f, 0.0f)) ==
            RuntimeStatus.Ok && body.Velocity.Y == 5.0f) ++passed;
        if (body.IsValid && body.AddForce(new Vector3(0.0f, 1.0f, 0.0f)) ==
            RuntimeStatus.Ok) ++passed;
        var runtimeTransform = Runtime.Transform(made.Value);
        runtimeTransform.Position = new Vector3(4.0f, 5.0f, 6.0f);
        if (runtimeTransform.Position.Z == 6.0f) ++passed;
        var animator = Runtime.AddComponentOrDefault<AnimatorComponent>(made.Value);
        if (animator.IsValid) ++passed;
        if (animator.IsValid && animator.Pause() == RuntimeStatus.Ok &&
            animator.Resume() == RuntimeStatus.Ok && animator.Stop() == RuntimeStatus.Ok) ++passed;
        var particles = Runtime.AddComponentOrDefault<ParticleEmitterComponent>(made.Value);
        if (particles.IsValid) ++passed;
        if (particles.IsValid && particles.Emit(32) == RuntimeStatus.Ok &&
            particles.Stop() == RuntimeStatus.Ok && particles.Play() == RuntimeStatus.Ok &&
            particles.Clear() == RuntimeStatus.Ok) ++passed;
        var audio = Runtime.AddComponentOrDefault<AudioSourceComponent>(made.Value);
        if (audio.IsValid) ++passed;
        if (audio.IsValid && audio.Play() == RuntimeStatus.ServiceUnavailable &&
            audio.Stop() == RuntimeStatus.Ok) ++passed;
        var image = Runtime.AddComponentOrDefault<UIImageComponent>(made.Value);
        if (image.IsValid) ++passed;
        image.Sprite = "validation-image-guid";
        if (image.Sprite == "validation-image-guid") ++passed;
        var slider = Runtime.AddComponentOrDefault<UISliderComponent>(made.Value);
        if (slider.IsValid) ++passed;
        slider.Minimum = 0.0f; slider.Maximum = 10.0f; slider.Value = 25.0f;
        if (slider.Value == 10.0f && slider.NormalizedValue == 1.0f) ++passed;
        var imageReference = image.Accessor.Reference();
        if (imageReference.Succeeded) slider.FillImage = imageReference.Value;
        var resolvedImage = slider.FillImage.Resolve();
        if (resolvedImage.Succeeded && resolvedImage.Value.TypeId == image.Handle.TypeId) ++passed;
        return passed;
    }

    public override void OnWorldChanged(SceneEventInfo scene)
    {
        if (scene.SceneAssetGuid == "typed-event" &&
            scene.WorldInstance == ulong.MaxValue - 7) ++TypedEventChecks;
    }

    public override void Update(float deltaTime)
    {
        var result = PollEvent(subscription);
        if (result.Succeeded && !string.IsNullOrEmpty(result.Value.TypeGuid))
        {
            LastEventType = result.Value.TypeGuid;
        }
    }
}
