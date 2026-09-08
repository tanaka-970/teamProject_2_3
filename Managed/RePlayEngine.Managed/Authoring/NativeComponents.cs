using System;

namespace ReplayEngine;

// C++ Component を Unity と同じ名前で扱うための Public ラッパ。
//
// 値の読み書きは既存の型付き Binding（RigidbodyComponent など）へそのまま流す。
// PropertyRegistry の名前を二重定義していないので、C++ 側でプロパティを
// 1 つ足せば Legacy 側と同時にここからも触れる。
public abstract class NativeComponent : Component
{
    private readonly GameObject owner;

    private protected NativeComponent(GameObject owner, ComponentHandle handle)
    {
        this.owner = owner;
        Handle = handle;
    }

    internal ComponentHandle Handle { get; }

    public override GameObject gameObject => owner;

    // GameObject が生きていても、その上の Component だけ壊れていることがある。
    // Owner の生死だけで判定すると、破棄済みの Rigidbody が生きて見える。
    // Handle の世代照合まで Native へ問い合わせる。
    //
    // owner.IsAlive も含めて、判定はすべて Handle だけで決まる。
    // 「いま Managed callback の中か」では答えが変わらない。
    internal override bool IsAlive
        => !Handle.IsEmpty && owner.IsAlive && NativeBridge.IsComponentAlive(Handle);

    internal override bool SameTarget(Object other)
        => other is NativeComponent component &&
            component.Handle.Instance == Handle.Instance &&
            component.Handle.TypeId == Handle.TypeId &&
            owner.SameTarget(component.owner);

    internal override int IdentityHash()
        => HashCode.Combine(owner.IdentityHash(), Handle.Instance, Handle.TypeId);

    internal override void DestroySelf() => ScriptExecutionContext.Runtime.Destroy(Handle);

    // Component の有効・無効。ScriptComponent と同じ入口を使う。
    public bool ComponentEnabled
    {
        get
        {
            var result = ScriptExecutionContext.Runtime.IsComponentEnabled(Handle);
            return result.Succeeded && result.Value;
        }
        set => ScriptExecutionContext.Runtime.SetComponentEnabled(Handle, value);
    }
}

// 有効・無効を持つ Native Component。Unity の Behaviour に相当する。
public abstract class NativeBehaviour : NativeComponent
{
    private protected NativeBehaviour(GameObject owner, ComponentHandle handle)
        : base(owner, handle) { }

    // Behaviour.enabled と同じ意味を Native Component にも与える。
    public bool enabled
    {
        get => ComponentEnabled;
        set => ComponentEnabled = value;
    }
}

// ---- 物理 -------------------------------------------------------------------

public sealed class Rigidbody : NativeComponent
{
    internal Rigidbody(GameObject owner, ComponentHandle handle) : base(owner, handle) { }

    private RigidbodyComponent Binding => RigidbodyComponent.FromHandle(Handle);

    public float mass { get => Binding.Mass; set { var binding = Binding; binding.Mass = value; } }
    public float drag { get => Binding.LinearDamping; set { var binding = Binding; binding.LinearDamping = value; } }
    public float angularDrag { get => Binding.AngularDamping; set { var binding = Binding; binding.AngularDamping = value; } }
    public Vector3 velocity
    {
        get => Binding.Velocity;
        set { WakeUp(); var binding = Binding; binding.Velocity = value; }
    }
    public Vector3 angularVelocity
    {
        get => Binding.AngularVelocity;
        set { WakeUp(); var binding = Binding; binding.AngularVelocity = value; }
    }

    // C++ の body_type は 0=Static / 1=Kinematic / 2=Dynamic。
    public bool isKinematic
    {
        get => Binding.BodyType == 1;
        set { var binding = Binding; binding.BodyType = value ? 1 : 2; }
    }

    // Unity の useGravity に相当するものは gravity_scale。
    // 0 で無重力、1 で通常。bool より表現力があるので両方出す。
    public float gravityScale { get => Binding.GravityScale; set { var binding = Binding; binding.GravityScale = value; } }
    public bool useGravity
    {
        get => Binding.GravityScale != 0.0f;
        set { var binding = Binding; binding.GravityScale = value ? 1.0f : 0.0f; }
    }

    public bool IsSleeping => Binding.IsSleeping;

    // 眠っている剛体を起こす。
    //
    // 【なぜ力を入れる前に必ず呼ぶか】
    //   Solver は is_sleeping の物体について、蓄積した力を積分せずに捨てる。
    //   一度床で静止して寝てしまうと、以後 AddForce も velocity も一切効かず
    //   「入力は読めているのに動かない」という形で詰まる。
    //   Unity と同じく「力を加えたら起きる」に揃える。
    public void WakeUp() => Binding.Accessor.SetBool("is_sleeping", false);

    public void AddForce(Vector3 force) { WakeUp(); Binding.AddForce(force); }
    public void AddTorque(Vector3 torque) { WakeUp(); Binding.AddTorque(torque); }
    public void ClearForces() => Binding.ClearForces();

    // Unity の ForceMode.Impulse に相当。質量ぶんの速度変化として与える。
    public void AddImpulse(Vector3 impulse) { WakeUp(); Binding.AddImpulse(impulse); }

    public void MovePosition(Vector3 position) { WakeUp(); Binding.Teleport(position); }
}

// Box / Sphere / Capsule / Mesh に共通する Collider。
public class Collider : NativeComponent
{
    internal Collider(GameObject owner, ComponentHandle handle) : base(owner, handle) { }

    private protected ColliderComponent Binding => new(Handle);

    public bool isTrigger { get => Binding.IsTrigger; set { var binding = Binding; binding.IsTrigger = value; } }
    public Vector3 center { get => Binding.CenterOffset; set { var binding = Binding; binding.CenterOffset = value; } }
    public int collisionLayer { get => Binding.CollisionLayer; set { var binding = Binding; binding.CollisionLayer = value; } }
    public int collisionMask { get => Binding.CollisionMask; set { var binding = Binding; binding.CollisionMask = value; } }

    // 接触イベントが持つ ColliderID から、当たった Collider そのものを引く。
    //
    // 【なぜ最初の Collider で代用しないか】
    //   敵が Head / Body / Foot の 3 つを持つとき、頭に当たったのか
    //   足に当たったのかが区別できなくなる。
    //   ID が指す Collider が配送前に消えていた場合も、代わりに Body を返すと
    //   「頭に当たった」を「体に当たった」と嘘の情報にしてしまう。
    //   引けなかったときは null を返す。別の Collider へは決して化けさせない。
    //
    //   colliderId == 0 は ID を持たない古い通知。そのときだけ、
    //   今までどおり最初の Collider で代用する。
    internal static Collider? For(GameObject target, uint colliderId)
    {
        if (colliderId == 0) return target.GetComponent<Collider>();

        var found = NativeBridge.FindColliderComponent(target.Handle, colliderId);
        if (!found.Succeeded || found.Value.IsEmpty) return null;

        var entry = NativeComponentRegistry.FindByNativeTypeName(found.Value);
        return entry?.Invoke(target, found.Value) as Collider;
    }
}

public sealed class BoxCollider : Collider
{
    internal BoxCollider(GameObject owner, ComponentHandle handle) : base(owner, handle) { }

    private BoxColliderComponent Typed => BoxColliderComponent.FromHandle(Handle);

    public Vector3 size { get => Typed.Size; set { var binding = Typed; binding.Size = value; } }
}

public sealed class SphereCollider : Collider
{
    internal SphereCollider(GameObject owner, ComponentHandle handle) : base(owner, handle) { }

    private SphereColliderComponent Typed => SphereColliderComponent.FromHandle(Handle);

    public float radius { get => Typed.Radius; set { var binding = Typed; binding.Radius = value; } }
}

public sealed class CapsuleCollider : Collider
{
    internal CapsuleCollider(GameObject owner, ComponentHandle handle) : base(owner, handle) { }

    private CapsuleColliderComponent Typed => CapsuleColliderComponent.FromHandle(Handle);

    public float radius { get => Typed.Radius; set { var binding = Typed; binding.Radius = value; } }
    public float height { get => Typed.Height; set { var binding = Typed; binding.Height = value; } }
}

public sealed class MeshCollider : Collider
{
    internal MeshCollider(GameObject owner, ComponentHandle handle) : base(owner, handle) { }
}

// ---- 描画 -------------------------------------------------------------------

public sealed class Camera : NativeBehaviour
{
    internal Camera(GameObject owner, ComponentHandle handle) : base(owner, handle) { }

    private CameraComponent Binding => CameraComponent.FromHandle(Handle);

    // degree。C++ 側も degree で持っているので変換しない。
    public float fieldOfView { get => Binding.FieldOfView; set { var binding = Binding; binding.FieldOfView = value; } }
    public float nearClipPlane { get => Binding.NearClip; set { var binding = Binding; binding.NearClip = value; } }
    public float farClipPlane { get => Binding.FarClip; set { var binding = Binding; binding.FarClip = value; } }
    public float orthographicSize
    {
        get => Binding.OrthographicSize;
        set { var binding = Binding; binding.OrthographicSize = value; }
    }
    public bool orthographic
    {
        get => Binding.ProjectionMode == 1;
        set { var binding = Binding; binding.ProjectionMode = value ? 1 : 0; }
    }
    public int depth { get => Binding.Priority; set { var binding = Binding; binding.Priority = value; } }
}

public sealed class MeshRenderer : NativeBehaviour
{
    internal MeshRenderer(GameObject owner, ComponentHandle handle) : base(owner, handle) { }

    private MeshRendererComponent Binding => MeshRendererComponent.FromHandle(Handle);

    public bool visible { get => Binding.Visible; set { var binding = Binding; binding.Visible = value; } }
    public Color color { get => Binding.Tint; set { var binding = Binding; binding.Tint = value; } }
    public bool castShadows { get => Binding.CastShadow; set { var binding = Binding; binding.CastShadow = value; } }
    public bool receiveShadows { get => Binding.ReceiveShadow; set { var binding = Binding; binding.ReceiveShadow = value; } }
}

public sealed class SkinnedMeshRenderer : NativeBehaviour
{
    internal SkinnedMeshRenderer(GameObject owner, ComponentHandle handle)
        : base(owner, handle) { }

    private SkinnedMeshRendererComponent Binding
        => SkinnedMeshRendererComponent.FromHandle(Handle);

    public bool visible { get => Binding.Visible; set { var binding = Binding; binding.Visible = value; } }
    public Color color { get => Binding.Tint; set { var binding = Binding; binding.Tint = value; } }
    public bool castShadows { get => Binding.CastShadow; set { var binding = Binding; binding.CastShadow = value; } }
    public bool receiveShadows { get => Binding.ReceiveShadow; set { var binding = Binding; binding.ReceiveShadow = value; } }
}

public sealed class Animator : NativeBehaviour
{
    internal Animator(GameObject owner, ComponentHandle handle) : base(owner, handle) { }

    private AnimatorComponent Binding => AnimatorComponent.FromHandle(Handle);

    public float speed { get => Binding.PlaybackSpeed; set { var binding = Binding; binding.PlaybackSpeed = value; } }
    public string currentState => Binding.CurrentState;

    public void Play(string stateName, float blendTime = 0.0f)
        => Binding.Play(stateName, blendTime);
    public void Pause() => Binding.Pause();
    public void Resume() => Binding.Resume();
    public void Stop() => Binding.Stop();

    public void SetBool(string name, bool value) => Binding.SetBool(name, value);
    public void SetFloat(string name, float value) => Binding.SetFloat(name, value);
    public void SetTrigger(string name) => Binding.SetTrigger(name);
    public void ResetTrigger(string name) => Binding.ResetTrigger(name);

    // SetInt は C++ 側の Animator パラメータに無い。
    // 見た目だけ作って何も起きない API にはしない。
}

public sealed class AudioSource : NativeBehaviour
{
    internal AudioSource(GameObject owner, ComponentHandle handle) : base(owner, handle) { }

    private AudioSourceComponent Binding => AudioSourceComponent.FromHandle(Handle);

    public float volume { get => Binding.Volume; set { var binding = Binding; binding.Volume = value; } }
    public float pitch { get => Binding.Pitch; set { var binding = Binding; binding.Pitch = value; } }
    public bool loop { get => Binding.Loop; set { var binding = Binding; binding.Loop = value; } }
    public bool isPlaying => Binding.IsPlaying;

    public void Play() => Binding.Play();
    public void Stop() => Binding.Stop();
}

// ---- UI ---------------------------------------------------------------------

public sealed class UIText : NativeBehaviour
{
    internal UIText(GameObject owner, ComponentHandle handle) : base(owner, handle) { }

    private UITextComponent Binding => UITextComponent.FromHandle(Handle);

    public string text
    {
        get => Binding.Text;
        set { var binding = Binding; binding.Text = value; }
    }
    public float fontSize
    {
        get => Binding.FontSize;
        set { var binding = Binding; binding.FontSize = value; }
    }
    public Color color
    {
        get => Binding.Color;
        set { var binding = Binding; binding.Color = value; }
    }
}

public sealed class UIImage : NativeBehaviour
{
    internal UIImage(GameObject owner, ComponentHandle handle) : base(owner, handle) { }

    private UIImageComponent Binding => UIImageComponent.FromHandle(Handle);

    public Color color
    {
        get => Binding.Color;
        set { var binding = Binding; binding.Color = value; }
    }
    // 0..1。ゲージに使う。
    public float fillAmount
    {
        get => Binding.FillAmount;
        set { var binding = Binding; binding.FillAmount = value; }
    }
}

public sealed class UIButton : NativeBehaviour
{
    internal UIButton(GameObject owner, ComponentHandle handle) : base(owner, handle) { }

    private UIButtonComponent Binding => UIButtonComponent.FromHandle(Handle);

    public bool interactable
    {
        get => Binding.Interactable;
        set { var binding = Binding; binding.Interactable = value; }
    }
}

// ---- ゲームプレイ -----------------------------------------------------------

// 歩く・跳ぶ・落ちるを持つ Native の移動部品。
//
// 【なぜ Rigidbody で自作しないか】
//   接地判定も段差登りも壁ずりも、C++ 側で既に解いてある。
//   同じものを C# の力積で作り直すと、寝た剛体や摩擦の扱いで必ず詰まる。
//   入力から動かすところは PlayerInput / PlayerController が native で担当する。
//   ここは「技で自分から動く」「ふっとぶ」「復帰させる」だけを外へ出す。
public sealed class CharacterMotor : NativeBehaviour
{
    internal CharacterMotor(GameObject owner, ComponentHandle handle) : base(owner, handle) { }

    private CharacterMotorComponent Binding => CharacterMotorComponent.FromHandle(Handle);

    public float moveSpeed
    {
        get => Binding.MoveSpeed;
        set { var binding = Binding; binding.MoveSpeed = value; }
    }

    public float jumpPower
    {
        get => Binding.JumpPower;
        set { var binding = Binding; binding.JumpPower = value; }
    }

    // 次の FixedUpdate で 1 回だけ跳ぶ。押しっぱなしで跳び続けない。
    public void Jump() { var binding = Binding; binding.RequestJump = true; }

    // 水平の向きへ歩かせる。y は使わない。倍率は走り・突進に使う。
    public RuntimeStatus Move(Vector3 direction, float speedMultiplier = 1.0f)
        => NativeBridge.InvokeComponentCommand(Handle, ComponentCommand.MotorMove,
            scalar: direction.X, secondaryScalar: direction.Z,
            integer: (int)(Mathf.Clamp(speedMultiplier, 0.0f, 20.0f) * 1000.0f));

    // ふっとばす。接地していても上へ抜ける。
    //
    // holdSeconds のあいだ、Motor は水平の加速・減速・上限を止める。
    // 0 のままだと次の更新で移動速度まで削られ、まったく飛ばない。
    public RuntimeStatus AddImpulse(Vector3 impulse, float holdSeconds = 0.0f)
        => NativeBridge.InvokeComponentCommand(Handle, ComponentCommand.MotorImpulse,
            Text(impulse), scalar: holdSeconds);

    // 速度を残したまま位置だけ移す。復帰に使う。
    public RuntimeStatus Teleport(Vector3 position)
        => NativeBridge.InvokeComponentCommand(Handle, ComponentCommand.MotorTeleport,
            Text(position));

    private static string Text(Vector3 value)
        => value.X.ToString(System.Globalization.CultureInfo.InvariantCulture) + "," +
            value.Y.ToString(System.Globalization.CultureInfo.InvariantCulture) + "," +
            value.Z.ToString(System.Globalization.CultureInfo.InvariantCulture);
}

// 今フレームの操作を保つ箱（Character Input）。
// input_source が外部のときだけ、ここから AI がスティックを倒せる。
public sealed class PlayerInput : NativeBehaviour
{
    internal PlayerInput(GameObject owner, ComponentHandle handle) : base(owner, handle) { }

    private PlayerInputComponent Binding => PlayerInputComponent.FromHandle(Handle);

    public bool inputEnabled
    {
        get => Binding.InputEnabled;
        set { var binding = Binding; binding.InputEnabled = value; }
    }

    // 外部入力にしてあるか。false の間は下の書き込みが全部弾かれる。
    public bool externalDriven
    {
        get => Binding.Accessor.GetInt("input_source") == 1;
        set => Binding.Accessor.SetInt("input_source", value ? 1 : 0);
    }

    // スティックを倒す。横だけ使うなら vertical は 0 でよい。
    public RuntimeStatus SetAxes(float horizontal, float vertical = 0.0f)
        => NativeBridge.InvokeComponentCommand(Handle, ComponentCommand.InputSetAxes,
            scalar: horizontal, secondaryScalar: vertical);

    // ダッシュの押しっ放し状態。
    public RuntimeStatus SetDash(bool held)
        => NativeBridge.InvokeComponentCommand(Handle, ComponentCommand.InputSetDash,
            integer: held ? 1 : 0);

    // ジャンプを 1 回分。人間の押下と同じラッチに入る。
    public RuntimeStatus Jump()
        => NativeBridge.InvokeComponentCommand(Handle, ComponentCommand.InputJump);
}

// ---- 演出 -------------------------------------------------------------------

// 画面全体に掛かる後処理の並び。1 枚ずつ Scene で置いてあるものを実行時に動かす。
//
// 効果の中身は effects[N].xxx という動的プロパティとして並んでいる。
// 種類ごとに専用のプロパティを生やすと C++ 側と二重定義になるので、
// 添字と名前で触る入口だけを出す。並び順は Scene が決める。
public sealed class ScreenEffectStack : NativeBehaviour
{
    internal ScreenEffectStack(GameObject owner, ComponentHandle handle) : base(owner, handle) { }

    private ScreenEffectStackComponent Binding => ScreenEffectStackComponent.FromHandle(Handle);

    // Stack 全体の入り切り。1 フレームで全部消したいときはこちら。
    public bool active
    {
        get => Binding.Enabled;
        set { var binding = Binding; binding.Enabled = value; }
    }

    public int effectCount => Binding.EffectCount;

    // 効果 1 枚の強さ。ほとんどの種類がこれで濃さを変えられる。
    public float GetIntensity(int index) => Binding.Accessor.GetFloat(Name(index, "intensity"));
    public void SetIntensity(int index, float value)
        => Binding.Accessor.SetFloat(Name(index, "intensity"), value);

    // ぼかし半径やずらし幅。種類によって意味が変わる。
    public void SetRadius(int index, float value)
        => Binding.Accessor.SetFloat(Name(index, "radius"), value);

    public void SetAmount(int index, float value)
        => Binding.Accessor.SetFloat(Name(index, "amount"), value);

    public void SetProgress(int index, float value)
        => Binding.Accessor.SetFloat(Name(index, "progress"), value);

    public void SetSpeed(int index, float value)
        => Binding.Accessor.SetFloat(Name(index, "speed"), value);

    public void SetColor(int index, Color value)
        => Binding.Accessor.SetColor(Name(index, "color"), value);

    public void SetEffectEnabled(int index, bool value)
        => Binding.Accessor.SetBool(Name(index, "enabled"), value);

    private static string Name(int index, string field)
        => "effects[" + index.ToString(System.Globalization.CultureInfo.InvariantCulture) +
            "]." + field;
}

// 動いた跡を残す帯。剣先に付けて斬撃の軌跡にする。
public sealed class Trail : NativeBehaviour
{
    internal Trail(GameObject owner, ComponentHandle handle) : base(owner, handle) { }

    private TrailComponent Binding => TrailComponent.FromHandle(Handle);

    // 出すのを止めても、すでに出ている帯は寿命ぶん残る。
    public bool emitting
    {
        get => Binding.Emitting;
        set { var binding = Binding; binding.Emitting = value; }
    }

    public float lifetime
    {
        get => Binding.Lifetime;
        set { var binding = Binding; binding.Lifetime = value; }
    }
}

// 粒。Emit で必要な瞬間にだけ撒く。
public sealed class ParticleEmitter : NativeBehaviour
{
    internal ParticleEmitter(GameObject owner, ComponentHandle handle) : base(owner, handle) { }

    private ParticleEmitterComponent Binding => ParticleEmitterComponent.FromHandle(Handle);

    public bool emitting
    {
        get => Binding.Emitting;
        set { var binding = Binding; binding.Emitting = value; }
    }

    public Color startColor
    {
        get => Binding.StartColor;
        set { var binding = Binding; binding.StartColor = value; }
    }

    public void Play() => Binding.Play();
    public void Stop() => Binding.Stop();
    public void Clear() => Binding.Clear();

    // その場で count 個だけ撒く。当たった瞬間の火花に使う。
    public void Emit(int count) => Binding.Emit(count);
}

// 点光源。当たった瞬間だけ強くする、といった演出に使う。
public sealed class PointLight : NativeBehaviour
{
    internal PointLight(GameObject owner, ComponentHandle handle) : base(owner, handle) { }

    private PointLightComponent Binding => PointLightComponent.FromHandle(Handle);

    public Color color
    {
        get => Binding.Color;
        set { var binding = Binding; binding.Color = value; }
    }

    public float intensity
    {
        get => Binding.Intensity;
        set { var binding = Binding; binding.Intensity = value; }
    }

    public float range
    {
        get => Binding.Range;
        set { var binding = Binding; binding.Range = value; }
    }
}

// 型と C++ Component 名の対応表。ここが唯一の登録場所。
internal static class NativeComponentBootstrap
{
    private static bool registered;

    internal static void EnsureRegistered()
    {
        if (registered) return;
        registered = true;

        NativeComponentRegistry.Register<Rigidbody>("RigidbodyComponent",
            (owner, handle) => new Rigidbody(owner, handle));

        // Collider は C++ 側で 4 つの型に分かれている。
        // GetComponent<Collider>() は見つかった順に 1 つ返す。
        NativeComponentRegistry.Register<Collider>(
            new[]
            {
                "BoxColliderComponent", "SphereColliderComponent",
                "CapsuleColliderComponent", "MeshColliderComponent",
            },
            (owner, handle) => new Collider(owner, handle));

        NativeComponentRegistry.Register<BoxCollider>("BoxColliderComponent",
            (owner, handle) => new BoxCollider(owner, handle));
        NativeComponentRegistry.Register<SphereCollider>("SphereColliderComponent",
            (owner, handle) => new SphereCollider(owner, handle));
        NativeComponentRegistry.Register<CapsuleCollider>("CapsuleColliderComponent",
            (owner, handle) => new CapsuleCollider(owner, handle));
        NativeComponentRegistry.Register<MeshCollider>("MeshColliderComponent",
            (owner, handle) => new MeshCollider(owner, handle));

        NativeComponentRegistry.Register<Camera>("CameraComponent",
            (owner, handle) => new Camera(owner, handle));
        NativeComponentRegistry.Register<MeshRenderer>("MeshRendererComponent",
            (owner, handle) => new MeshRenderer(owner, handle));
        NativeComponentRegistry.Register<SkinnedMeshRenderer>("SkinnedMeshRendererComponent",
            (owner, handle) => new SkinnedMeshRenderer(owner, handle));
        NativeComponentRegistry.Register<Animator>("AnimatorComponent",
            (owner, handle) => new Animator(owner, handle));
        NativeComponentRegistry.Register<AudioSource>("AudioSourceComponent",
            (owner, handle) => new AudioSource(owner, handle));

        NativeComponentRegistry.Register<UIText>("UITextComponent",
            (owner, handle) => new UIText(owner, handle));
        NativeComponentRegistry.Register<UIImage>("UIImageComponent",
            (owner, handle) => new UIImage(owner, handle));
        NativeComponentRegistry.Register<UIButton>("UIButtonComponent",
            (owner, handle) => new UIButton(owner, handle));

        NativeComponentRegistry.Register<Landscape>("LandscapeComponent",
            (owner, handle) => new Landscape(owner, handle));

        // ゲームプレイ。移動は C# ではなくこの Component が持つ。
        NativeComponentRegistry.Register<CharacterMotor>("CharacterMotorComponent",
            (owner, handle) => new CharacterMotor(owner, handle));
        NativeComponentRegistry.Register<PlayerInput>("PlayerInputComponent",
            (owner, handle) => new PlayerInput(owner, handle));

        // 演出まわり。値はすべて PropertyRegistry の名前をそのまま使う。
        NativeComponentRegistry.Register<ScreenEffectStack>("ScreenEffectStackComponent",
            (owner, handle) => new ScreenEffectStack(owner, handle));
        NativeComponentRegistry.Register<Trail>("TrailComponent",
            (owner, handle) => new Trail(owner, handle));
        NativeComponentRegistry.Register<ParticleEmitter>("ParticleEmitterComponent",
            (owner, handle) => new ParticleEmitter(owner, handle));
        NativeComponentRegistry.Register<PointLight>("PointLightComponent",
            (owner, handle) => new PointLight(owner, handle));
    }
}
