using System;
using System.Collections;
using System.Collections.Generic;

namespace ReplayEngine;

// MonoBehaviour を既存の ScriptComponent / Script Backend から駆動するアダプタ。
//
// C++ から見ると、これはただの ScriptBehaviour。
// Backend も ScriptComponent も Scene も、新旧の区別を知らない。
// 二つ目の Update システムは作っていない。
//
//   C++ Scene::Update
//        ↓
//   ScriptComponent
//        ↓
//   CSharpScriptBackend::Invoke
//        ↓
//   NativeBridge.Invoke
//        ↓
//   ScriptBehaviour の仮想メソッド   ← Legacy はここでユーザーコード
//        ↓
//   MonoBehaviourHost（このクラス）
//        ↓
//   BehaviourTypeDescriptor のキャッシュ済み delegate
//        ↓
//   ユーザーの MonoBehaviour
internal sealed class MonoBehaviourHost : ScriptBehaviour
{
    private readonly MonoBehaviour target;
    private readonly BehaviourTypeDescriptor descriptor;
    private GameObject? ownerGameObject;
    private bool destroyed;
    private bool diagnosticsReported;

    internal MonoBehaviourHost(MonoBehaviour target)
    {
        this.target = target;
        descriptor = BehaviourTypeDescriptor.For(target.GetType());
        target.Bind(this);
    }

    internal MonoBehaviour Target => target;
    internal bool IsInstanceAlive => !destroyed;

    internal GameObject OwnerGameObject
        => ownerGameObject ??= ReplayEngine.GameObject.Wrap(GameObject)
            ?? throw new InvalidOperationException("GameObject を解決できません。");

    // ---- GameObject ごとの索引 ------------------------------------------------
    //
    // GetComponent<T>() が全 Script を走査しないための表。
    // Unity と同じく「自分の GameObject の上」を探すだけで済む。

    private static readonly Dictionary<ObjectKey, List<MonoBehaviourHost>> ByObject = new();

    private readonly struct ObjectKey : IEquatable<ObjectKey>
    {
        internal ObjectKey(ObjectHandle handle)
        {
            World = handle.World;
            Object = handle.Object;
            Generation = handle.Generation;
        }

        private ulong World { get; }
        private ulong Object { get; }
        private uint Generation { get; }

        public bool Equals(ObjectKey other) => World == other.World &&
            Object == other.Object && Generation == other.Generation;
        public override bool Equals(object? obj) => obj is ObjectKey other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(World, Object, Generation);
    }

    // NativeBridge が Attach 直後に呼ぶ。索引へ載せるのはここ 1 か所。
    internal void OnAttached()
    {
        var key = new ObjectKey(GameObject);
        if (!ByObject.TryGetValue(key, out var list))
        {
            list = new List<MonoBehaviourHost>(1);
            ByObject[key] = list;
        }
        list.Add(this);
        ReportDiagnostics();
    }

    // NativeBridge が DestroyInstance で呼ぶ。
    internal void OnDetached()
    {
        destroyed = true;
        var key = new ObjectKey(GameObject);
        if (!ByObject.TryGetValue(key, out var list)) return;
        list.Remove(this);
        if (list.Count == 0) ByObject.Remove(key);
    }

    internal static void ClearAll() => ByObject.Clear();

    internal static object? FindOn(ObjectHandle owner, Type type)
    {
        if (!ByObject.TryGetValue(new ObjectKey(owner), out var list)) return null;
        foreach (var host in list)
        {
            if (type.IsInstanceOfType(host.target)) return host.target;
        }
        return null;
    }

    internal static T[] FindAllOn<T>(ObjectHandle owner) where T : class
    {
        if (!ByObject.TryGetValue(new ObjectKey(owner), out var list)) return Array.Empty<T>();
        var found = new List<T>(list.Count);
        foreach (var host in list)
        {
            if (host.target is T typed) found.Add(typed);
        }
        return found.Count == 0 ? Array.Empty<T>() : found.ToArray();
    }

    // GameObject を無効にしたときの保険。
    //
    // 通常は Component の OnDisable が来て、そこで止まる。
    // ただし OnDisable は次の同期点まで呼ばれないので、
    // SetActive(false) を呼んだその場でも止めておく。
    // 二重に呼んでも CancelAll は何度でも安全。
    internal static void StopCoroutinesOn(ObjectHandle owner)
    {
        if (!ByObject.TryGetValue(new ObjectKey(owner), out var list)) return;
        foreach (var host in list) host.StopRoutines();
    }

    // ---- MonoBehaviour から使う入口 -------------------------------------------

    internal bool ComponentEnabled
    {
        get
        {
            var result = Runtime.IsComponentEnabled(Component);
            return result.Succeeded && result.Value;
        }
        set => Runtime.SetComponentEnabled(Component, value);
    }

    internal void DestroyComponent() => Runtime.Destroy(Component);

    // Awake 済みなら Unity と同じく最初の yield まで即時実行する。
    // AddComponent 直後など、まだ Awake 前の instance だけは queue に留め、
    // 初期化前のユーザーコードを走らせない。
    internal Coroutine StartRoutine(IEnumerator body)
        => RuntimeLifecycleStarted ? StartCoroutinePrimed(body) : StartCoroutine(body);

    internal void StopRoutines() => StopAllCoroutines();

    // ---- 接触イベントの購読判定 -----------------------------------------------
    //
    // ScriptBehaviour の既定は「仮想メソッドを override したか」で決める。
    // Host は必ず override するので、そのままだと全 Host が購読してしまう。
    // ユーザーの MonoBehaviour が実際に持っているかで決め直す。

    internal override bool HandlesMessage(string methodName) => methodName switch
    {
        nameof(OnCollisionEnter) => descriptor.Collision != null,
        nameof(OnCollisionStay) => descriptor.CollisionStay != null,
        nameof(OnCollisionExit) => descriptor.CollisionExit != null,
        nameof(OnTriggerEnter) => descriptor.TriggerEnter != null,
        nameof(OnTriggerStay) => descriptor.TriggerStay != null,
        nameof(OnTriggerExit) => descriptor.TriggerExit != null,
        _ => false,
    };

    // ---- ライフサイクル -------------------------------------------------------
    //
    // ここが Legacy と New の合流点。呼ばれ方は今までと同じで、
    // 呼ぶ先がキャッシュ済み delegate になっただけ。

    public override void Awake() => RunAwake();
    public override void OnEnable() => Run(descriptor.OnEnable, nameof(OnEnable));
    public override void Start()
    {
        if (descriptor.Start != null)
        {
            Run(descriptor.Start, nameof(Start));
            return;
        }
        if (descriptor.StartCoroutineMessage == null || destroyed) return;
        using (ScriptExecutionContext.Enter(Runtime, this))
        {
            try
            {
                var routine = descriptor.StartCoroutineMessage(target);
                if (routine != null) StartRoutine(routine);
            }
            catch (Exception exception)
            {
                Report(nameof(Start), exception);
            }
        }
    }
    public override void FixedUpdate(float fixedDeltaTime)
        => Run(descriptor.FixedUpdate, nameof(FixedUpdate));
    public override void Update(float deltaTime) => Run(descriptor.Update, nameof(Update));
    public override void LateUpdate(float deltaTime)
        => Run(descriptor.LateUpdate, nameof(LateUpdate));
    public override void OnDisable()
    {
        Run(descriptor.OnDisable, nameof(OnDisable));

        // 無効になった理由で扱いを分ける。
        //
        //   GameObject が非 Active   -> Coroutine を止める
        //   behaviour.enabled = false -> Coroutine は続ける
        //
        // Component の OnDisable は「GameObject が非 Active」でも
        // 「自分が無効」でも同じように来る。ここで GameObject 側を見て
        // どちらなのかを判別する。親が非 Active になった場合も
        // ActiveInHierarchy が false になるので同じ経路で拾える。
        //
        // Unity と同じ意味論にしている。再度 Active にしても
        // 前の IEnumerator の途中からは再開しない。
        if (destroyed) return;
        var owner = ownerGameObject ?? ReplayEngine.GameObject.Wrap(GameObject);
        if (owner != null && !owner.activeInHierarchy) StopRoutines();
    }

    public override void OnDestroy()
    {
        Run(descriptor.OnDestroy, nameof(OnDestroy));
        destroyed = true;
    }

    public override void OnCollisionEnter(CollisionInfo collision)
        => Run(descriptor.Collision, new Collision(collision), nameof(OnCollisionEnter));
    public override void OnCollisionStay(CollisionInfo collision)
        => Run(descriptor.CollisionStay, new Collision(collision), nameof(OnCollisionStay));
    public override void OnCollisionExit(CollisionInfo collision)
        => Run(descriptor.CollisionExit, new Collision(collision), nameof(OnCollisionExit));

    public override void OnTriggerEnter(TriggerInfo trigger)
        => RunTrigger(descriptor.TriggerEnter, trigger, nameof(OnTriggerEnter));
    public override void OnTriggerStay(TriggerInfo trigger)
        => RunTrigger(descriptor.TriggerStay, trigger, nameof(OnTriggerStay));
    public override void OnTriggerExit(TriggerInfo trigger)
        => RunTrigger(descriptor.TriggerExit, trigger, nameof(OnTriggerExit));

    // ---- 実行 -----------------------------------------------------------------
    //
    // 1 つの Script の例外で、他の Script や以降のフレームを止めない。
    // 例外はここで捕まえて Script Console へ出し、次のフレームも呼び続ける。


    private void RunAwake()
    {
        if (descriptor.Awake == null || destroyed) return;
        using (ScriptExecutionContext.Enter(Runtime, this))
        {
            try
            {
                descriptor.Awake(target);
            }
            catch (Exception exception)
            {
                Report(nameof(Awake), exception);

                // 初期化に失敗した Behaviour を半端な状態のまま Start / Update へ
                // 進めない。Component lifecycle 側はこの enabled=false を見て
                // OnEnable / Start を抑止する。
                try { ComponentEnabled = false; } catch { }
            }
        }
    }

    private void Run(Action<MonoBehaviour>? invoker, string messageName)
    {
        if (invoker == null || destroyed) return;
        using (ScriptExecutionContext.Enter(Runtime, this))
        {
            try
            {
                invoker(target);
            }
            catch (Exception exception)
            {
                Report(messageName, exception);
            }
        }
    }

    private void Run<TArgument>(Action<MonoBehaviour, TArgument>? invoker, TArgument argument,
        string messageName)
    {
        if (invoker == null || destroyed) return;
        using (ScriptExecutionContext.Enter(Runtime, this))
        {
            try
            {
                invoker(target, argument);
            }
            catch (Exception exception)
            {
                Report(messageName, exception);
            }
        }
    }

    private void RunTrigger(Action<MonoBehaviour, Collider>? invoker, TriggerInfo trigger,
        string messageName)
    {
        if (invoker == null || destroyed) return;

        // 相手を特定できないトリガー通知は配らない。
        // null を渡すと、ユーザーの other.gameObject が必ず例外になる。
        var other = ReplayEngine.GameObject.Wrap(trigger.Other);
        if (other == null) return;
        var collider = Collider.For(other, unchecked((uint)trigger.OtherColliderId));
        if (collider == null) return;
        Run(invoker, collider, messageName);
    }

    private void Report(string messageName, Exception exception)
    {
        var objectName = string.Empty;
        try { objectName = OwnerGameObject.name; } catch { }
        Runtime.LogError(
            $"[{target.GetType().Name}] {messageName} で例外: {exception.Message}\n" +
            $"GameObject: {objectName}\n{exception.StackTrace}", GameObject);
    }

    // 名前は合っているが形が違うメソッドを 1 回だけ報告する。
    // 「Update と書いたのに呼ばれない」を黙って通さない。
    private void ReportDiagnostics()
    {
        if (diagnosticsReported || descriptor.Diagnostics.Length == 0) return;
        diagnosticsReported = true;
        foreach (var line in descriptor.Diagnostics)
        {
            Runtime.LogWarning(line, GameObject);
        }
    }
}
