using System.Collections;
using System.Collections.Generic;
using System.Reflection;

namespace ReplayEngine;
// ビヘイビアスクリプトの基底クラス。ゲームオブジェクトにアタッチされるスクリプトは、このクラスを継承する必要があるよ。
public abstract class ScriptBehaviour
{
	// ゲームオブジェクトにアタッチされたスクリプトのライフサイクルイベントを定義
	private readonly List<EventSubscription> eventSubscriptions = new();
	private readonly CoroutineRunner runner = new();
	private ContactEventPump? contactPump;
	// ゲームオブジェクトとコンポーネントのハンドルを保持するためのプロパティ
	internal void Attach(ScriptRuntimeContext context, ObjectHandle gameObject, ComponentHandle component)
    {
        Runtime = context;
        GameObject = gameObject;
        Component = component;
        Transform = new TransformAccess(gameObject);
    }

    protected ScriptRuntimeContext Runtime { get; private set; } = ScriptRuntimeContext.Unavailable;

    public ObjectHandle GameObject { get; private set; }
    public ComponentHandle Component { get; private set; }

    // プロパティにすると Transform.Position = v が CS1612 で弾かれる。
    // 代入先が「値のコピー」になるため。フィールドなら変数なので代入できる。
    public TransformAccess Transform;

    // 自分と同じ GameObject から型で Component を引く。
    protected RuntimeResult<T> GetComponent<T>() where T : IComponentBinding<T>
        => Runtime.GetComponent<T>(GameObject);

    protected bool TryGetComponent<T>(out T component) where T : IComponentBinding<T>
        => Runtime.TryGetComponent(GameObject, out component);

    protected RuntimeResult<T> GetComponent<T>(ObjectHandle target)
        where T : IComponentBinding<T>
        => Runtime.GetComponent<T>(target);

    protected RuntimeResult<T> AddComponent<T>() where T : IComponentBinding<T>
        => Runtime.AddComponent<T>(GameObject);

    protected bool HasComponent<T>() where T : IComponentBinding<T>
        => Runtime.HasComponent<T>(GameObject);

    // 値をそのまま返す入口。戻り値を変数へ受ければプロパティへ代入できる。
    //   var camera = GetComponentOrDefault<CameraComponent>();
    //   camera.FieldOfView = 42.0f;
    protected T GetComponentOrDefault<T>() where T : IComponentBinding<T>
        => Runtime.GetComponent<T>(GameObject).Value;

    protected T GetComponentOrDefault<T>(ObjectHandle target) where T : IComponentBinding<T>
        => Runtime.GetComponent<T>(target).Value;

	// イベントの購読を行うメソッド。指定されたイベントタイプの購読を開始し、成功した場合は購読情報を保持
	protected RuntimeResult<EventSubscription> SubscribeEvent(string eventTypeGuid)
    {
        var result = Runtime.SubscribeEvent(eventTypeGuid, GameObject);
        if (result.Succeeded && result.Value.IsValid)
        {
            eventSubscriptions.Add(result.Value);
        }
        return result;
    }

    // World 全体のイベントを購読する。Scene Loaded のように
    // 特定の GameObject へ紐づかないものはこちらを使う。
    protected RuntimeResult<EventSubscription> SubscribeGlobalEvent(string eventTypeGuid)
    {
        var result = Runtime.SubscribeEvent(eventTypeGuid);
        if (result.Succeeded && result.Value.IsValid)
        {
            eventSubscriptions.Add(result.Value);
        }
        return result;
    }

    protected RuntimeStatus UnsubscribeEvent(EventSubscription subscription)
    {
        eventSubscriptions.RemoveAll(entry => entry.Id == subscription.Id);
        return Runtime.UnsubscribeEvent(subscription);
    }

    protected RuntimeResult<RuntimeEvent> PollEvent(EventSubscription subscription)
    {
        return Runtime.PollEvent(subscription);
    }

    protected RuntimeStatus PublishEvent(string eventTypeGuid, string typeName = "",
        ObjectHandle target = default)
    {
        return Runtime.PublishEvent(eventTypeGuid, typeName, GameObject, target);
    }

    // ---- Coroutine / Timer / Tween ------------------------------------------

    protected Coroutine StartCoroutine(IEnumerator body) => runner.Start(body);

    protected void StopCoroutine(Coroutine coroutine) => coroutine?.Cancel();

    protected void StopAllCoroutines() => runner.CancelAll();

    // seconds 後に 1 回だけ呼ぶ。
    protected Timer After(float seconds, System.Action callback)
        => runner.AddTimer(new Timer(seconds, callback, repeat: false));

    // interval ごとに繰り返し呼ぶ。止めるときは Cancel()。
    protected Timer Every(float interval, System.Action callback)
        => runner.AddTimer(new Timer(interval, callback, repeat: true));

    protected Tween TweenValue(float from, float to, float duration,
        System.Action<float> apply, System.Func<float, float>? easing = null)
        => runner.AddTween(new Tween(from, to, duration, apply, easing));

    internal void ReleaseManagedSubscriptions()
    {
        foreach (var subscription in eventSubscriptions)
        {
            Runtime.UnsubscribeEvent(subscription);
        }
        eventSubscriptions.Clear();
        runner.CancelAll();
        contactPump = null;
    }

    // ---- 接触イベント ---------------------------------------------------------
    //
    // 継承側が override したものだけを購読する。
    // 使っていない Behaviour に購読と Poll のコストを掛けないため。

    public virtual void OnCollisionEnter(CollisionInfo collision) { }
    public virtual void OnCollisionStay(CollisionInfo collision) { }
    public virtual void OnCollisionExit(CollisionInfo collision) { }
    public virtual void OnTriggerEnter(TriggerInfo trigger) { }
    public virtual void OnTriggerStay(TriggerInfo trigger) { }
    public virtual void OnTriggerExit(TriggerInfo trigger) { }

    // Engine が毎フレーム呼ぶ。Update の直前に接触と時間を進める。
    internal void PumpFrame(float deltaTime)
    {
        contactPump ??= new ContactEventPump(this);
        contactPump.Pump();
        runner.Advance(deltaTime);
    }

    public virtual void Awake() { }
    public virtual void OnEnable() { }
    public virtual void Start() { }
    public virtual void FixedUpdate(float fixedDeltaTime) { }
    public virtual void Update(float deltaTime) { }
    public virtual void LateUpdate(float deltaTime) { }
    public virtual void OnDisable() { }
    public virtual void OnDestroy() { }

    // 接触イベントを Poll して仮想メソッドへ配る。
    private sealed class ContactEventPump
    {
        private readonly ScriptBehaviour owner;
        private readonly List<(EventSubscription subscription, int kind)> subscriptions = new();

        internal ContactEventPump(ScriptBehaviour owner)
        {
            this.owner = owner;
            Subscribe(nameof(OnCollisionEnter), EngineEventIds.CollisionEnter, 0);
            Subscribe(nameof(OnCollisionStay), EngineEventIds.CollisionStay, 1);
            Subscribe(nameof(OnCollisionExit), EngineEventIds.CollisionExit, 2);
            Subscribe(nameof(OnTriggerEnter), EngineEventIds.TriggerEnter, 3);
            Subscribe(nameof(OnTriggerStay), EngineEventIds.TriggerStay, 4);
            Subscribe(nameof(OnTriggerExit), EngineEventIds.TriggerExit, 5);
        }

        private void Subscribe(string methodName, string eventGuid, int kind)
        {
            if (!Overrides(methodName)) return;
            var result = owner.SubscribeEvent(eventGuid);
            if (result.Succeeded && result.Value.IsValid)
            {
                subscriptions.Add((result.Value, kind));
            }
        }

        private bool Overrides(string methodName)
        {
            var method = owner.GetType().GetMethod(methodName,
                BindingFlags.Public | BindingFlags.Instance);
            return method != null && method.DeclaringType != typeof(ScriptBehaviour);
        }

        internal void Pump()
        {
            foreach (var (subscription, kind) in subscriptions)
            {
                // 1 フレームで積まれたぶんをすべて配る。
                // 上限は C++ 側の購読キューが持っている。
                while (true)
                {
                    var polled = owner.Runtime.PollEvent(subscription);
                    if (!polled.Succeeded || string.IsNullOrEmpty(polled.Value.TypeGuid)) break;
                    Dispatch(kind, polled.Value);
                }
            }
        }

        private void Dispatch(int kind, RuntimeEvent record)
        {
            switch (kind)
            {
            case 0: owner.OnCollisionEnter(new CollisionInfo(record)); break;
            case 1: owner.OnCollisionStay(new CollisionInfo(record)); break;
            case 2: owner.OnCollisionExit(new CollisionInfo(record)); break;
            case 3: owner.OnTriggerEnter(new TriggerInfo(record)); break;
            case 4: owner.OnTriggerStay(new TriggerInfo(record)); break;
            default: owner.OnTriggerExit(new TriggerInfo(record)); break;
            }
        }
    }
}
