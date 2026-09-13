using System.Collections;
using System.Collections.Generic;
using System.Reflection;

namespace ReplayEngine;
// ビヘイビアスクリプトの基底クラス。ゲームオブジェクトにアタッチされるスクリプトは、このクラスを継承する必要があるよ。
public abstract class ScriptBehaviour
{
    internal bool RuntimeLifecycleStarted { get; private set; }
    internal void BeginRuntimeLifecycle() => RuntimeLifecycleStarted = true;

	// ゲームオブジェクトにアタッチされたスクリプトのライフサイクルイベントを定義
	private readonly List<EventSubscription> eventSubscriptions = new();
	private readonly CoroutineRunner runner = new();
	private EngineEventPump? eventPump;
	// ゲームオブジェクトとコンポーネントのハンドルを保持するためのプロパティ
	internal void Attach(ScriptRuntimeContext context, ObjectHandle gameObject, ComponentHandle component)
    {
        Runtime = context;
        GameObject = gameObject;
        Component = component;
        Transform = new TransformAccess(gameObject);
        eventPump = new EngineEventPump(this);
    }

    protected ScriptRuntimeContext Runtime { get; private set; } = ScriptRuntimeContext.Unavailable;

    public ObjectHandle GameObject { get; private set; }
    public ComponentHandle Component { get; private set; }

    // プロパティにすると Transform.Position = v が CS1612 で弾かれる。
    // 代入先が「値のコピー」になるため。フィールドなら変数なので代入できる。
    public TransformAccess Transform;

    // 名前で引いた GameObject を覚えておく入れ物と、その World 番号。
    private readonly Dictionary<string, ObjectHandle> objectsByName = new();
    private ulong objectCacheWorld;

    // 名前で GameObject を引く。見つかった分は覚えて毎フレームの走査を避ける。
    // World が入れ替わったら覚えた分は捨てる。前の World のハンドルは別物を指すため。
    // 見つからなかった分は覚えない。あとから生成される場合があるから。
    protected bool TryFindObject(string name, out ObjectHandle handle)
    {
        if (objectCacheWorld != GameObject.World)
        {
            objectsByName.Clear();
            objectCacheWorld = GameObject.World;
        }
        if (objectsByName.TryGetValue(name, out handle)) return true;

        var found = Runtime.FindGameObject(name);
        handle = found.Succeeded ? found.Value : default;
        if (handle.IsEmpty) return false;
        objectsByName[name] = handle;
        return true;
    }

    // 無いと成立しない GameObject を引く。欠けていたら名前付きの例外で止める。
    // 黙って何もしないと、後続の無反応からは原因が追えない。
    protected ObjectHandle RequireObject(string name)
    {
        if (TryFindObject(name, out var handle)) return handle;
        throw new System.InvalidOperationException("GameObject が見つかりません: " + name);
    }

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

    protected RuntimeResult<T> GetBehaviour<T>() where T : ScriptBehaviour
        => Runtime.GetBehaviour<T>(GameObject);

    protected RuntimeResult<T> GetBehaviour<T>(ObjectHandle target)
        where T : ScriptBehaviour => Runtime.GetBehaviour<T>(target);

    protected RuntimeResult<T[]> GetBehaviours<T>() where T : ScriptBehaviour
        => Runtime.GetBehaviours<T>(GameObject);

    protected bool TryGetBehaviour<T>(out T behaviour) where T : ScriptBehaviour
        => Runtime.TryGetBehaviour(GameObject, out behaviour);

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
        var result = Runtime.SubscribeEvent(eventTypeGuid, scope: EventScope.Global);
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

    // 新 MonoBehaviour が、Unity と同じく「呼んだ時点で最初の yield まで」
    // Coroutine を進めるための内部入口。Legacy StartCoroutine は Start() のまま。
    internal Coroutine StartCoroutinePrimed(IEnumerator body) => runner.StartPrimed(body);

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
        RuntimeLifecycleStarted = false;
        foreach (var subscription in eventSubscriptions)
        {
            Runtime.UnsubscribeEvent(subscription);
        }
        eventSubscriptions.Clear();
        runner.CancelAll();
        eventPump = null;
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

    // ---- Engine 型付きイベント --------------------------------------------

    public virtual void OnBeforeSceneUnload(SceneEventInfo scene) { }
    public virtual void OnSceneLoadRequested(SceneEventInfo scene) { }
    public virtual void OnSceneLoadStarted(SceneEventInfo scene) { }
    public virtual void OnSceneLoaded(SceneEventInfo scene) { }
    public virtual void OnSceneLoadFailed(SceneEventInfo scene) { }
    public virtual void OnWorldChanged(SceneEventInfo scene) { }
    public virtual void OnApplicationQuit(ApplicationQuitEventInfo application) { }
    public virtual void OnButtonClicked(ButtonEventInfo button) { }
    // 自分以外のボタンの Click も受け取る。メニューを1つの Director で捌くための入口。
    // OnButtonClicked は発生元が自分自身の場合しか呼ばれないため、
    // ボタンを持たない制御用スクリプトはこちらを override する。
    // どのボタンかは button.Button を RequireObject() の戻り値と比べて判別する。
    public virtual void OnAnyButtonClicked(ButtonEventInfo button) { }
    public virtual void OnButtonStateChanged(ButtonEventInfo button) { }
    public virtual void OnInputFieldValueChanged(InputFieldEventInfo input) { }
    public virtual void OnInputFieldSubmitted(InputFieldEventInfo input) { }
    public virtual void OnInputFieldCanceled(InputFieldEventInfo input) { }
    public virtual void OnAnimationEvent(MotionEventInfo animationEvent) { }
    public virtual void OnCompositionMarker(MotionEventInfo marker) { }
    // Motion / Composition が終端へ達したときに 1 度だけ呼ばれる。Loop 中は呼ばれない。
    public virtual void OnAnimationFinished(MotionEventInfo animation) { }
    public virtual void OnAnimatorStateChanged(AnimatorStateEventInfo animator) { }
    public virtual void OnSliderValueChanged(SliderEventInfo slider) { }

    // 接触などの Engine イベントを配る。
    //
    // Update からではなく、物理と EventBus の配送が終わった同期点から呼ぶ。
    // Update で読むと、読めるのは前フレームぶんになり、
    // Native Behaviour より 1 フレーム遅れて届いてしまう。
    internal void PumpEngineEvents(bool contacts = true, bool engineEvents = true)
    {
        eventPump ??= new EngineEventPump(this);
        eventPump.Pump(contacts, engineEvents);
    }

    // Engine イベントを 1 つでも購読しているか。
    //
    // 初回にここで購読を作る。以降は bool を見るだけなので、
    // 接触も Scene イベントも使っていない Behaviour には毎フレームの
    // 追加コストが乗らない。
    internal bool HasEngineEventSubscriptions
    {
        get
        {
            eventPump ??= new EngineEventPump(this);
            return eventPump.SubscriptionCount != 0;
        }
    }

    // Coroutine / Timer / Tween が 1 本でも走っているか。
    internal bool HasPendingRoutines => runner.HasPending;

    // Coroutine / Timer / Tween を進める。
    //
    // イベント配送と分けてあるのは、enabled = false の Behaviour でも
    // Coroutine は進められるようにするため。Update 経由だと、
    // Component が無効なあいだ Invoke が来ず Coroutine まで止まる。
    internal void AdvanceCoroutines(float deltaTime) => runner.Advance(deltaTime);

    // GameObject が非 Active になったときに呼ぶ。途中から再開させない。
    internal void StopCoroutinesForInactive() => runner.CancelAll();

    [System.Obsolete("イベント配送と Coroutine は分離した。呼び出し互換のために残す。")]
    internal void PumpFrame(float deltaTime)
    {
        PumpEngineEvents();
        AdvanceCoroutines(deltaTime);
    }

    // この Behaviour がその Engine イベントを受け取るか。
    // 既定は「仮想メソッドを override したか」。今までと同じ判定。
    // MonoBehaviour を載せる Host は必ず override してしまう都合で、
    // ここを差し替えてユーザーのスクリプトが実際に持つかで決める。
    internal virtual bool HandlesMessage(string methodName)
    {
        var method = GetType().GetMethod(methodName,
            BindingFlags.Public | BindingFlags.Instance);
        return method != null && method.DeclaringType != typeof(ScriptBehaviour);
    }

    public virtual void Awake() { }
    public virtual void OnEnable() { }
    public virtual void Start() { }
    public virtual void FixedUpdate(float fixedDeltaTime) { }
    public virtual void Update(float deltaTime) { }
    public virtual void LateUpdate(float deltaTime) { }
    public virtual void OnDisable() { }
    public virtual void OnDestroy() { }

    // Engine Event を Poll して型付き仮想メソッドへ配る。
    private sealed class EngineEventPump
    {
        private readonly ScriptBehaviour owner;
        private readonly List<(EventSubscription subscription, int kind, bool ownSource)> subscriptions = new();

        internal EngineEventPump(ScriptBehaviour owner)
        {
            this.owner = owner;
            Subscribe(nameof(OnCollisionEnter), EngineEventIds.CollisionEnter, 0, ownSource: true);
            Subscribe(nameof(OnCollisionStay), EngineEventIds.CollisionStay, 1, ownSource: true);
            Subscribe(nameof(OnCollisionExit), EngineEventIds.CollisionExit, 2, ownSource: true);
            Subscribe(nameof(OnTriggerEnter), EngineEventIds.TriggerEnter, 3, ownSource: true);
            Subscribe(nameof(OnTriggerStay), EngineEventIds.TriggerStay, 4, ownSource: true);
            Subscribe(nameof(OnTriggerExit), EngineEventIds.TriggerExit, 5, ownSource: true);
            Subscribe(nameof(OnBeforeSceneUnload), EngineEventIds.BeforeSceneUnload, 6, global: true);
            Subscribe(nameof(OnSceneLoadRequested), EngineEventIds.SceneLoadRequested, 7, global: true);
            Subscribe(nameof(OnSceneLoadStarted), EngineEventIds.SceneLoadStarted, 8, global: true);
            Subscribe(nameof(OnSceneLoaded), EngineEventIds.SceneLoaded, 9, global: true);
            Subscribe(nameof(OnSceneLoadFailed), EngineEventIds.SceneLoadFailed, 10, global: true);
            Subscribe(nameof(OnWorldChanged), EngineEventIds.WorldChanged, 11, global: true);
            Subscribe(nameof(OnApplicationQuit), EngineEventIds.ApplicationQuitRequested, 12, global: true);
            Subscribe(nameof(OnButtonClicked), EngineEventIds.ButtonClicked, 13, ownSource: true);
            Subscribe(nameof(OnAnyButtonClicked), EngineEventIds.ButtonClicked, 22);
            Subscribe(nameof(OnButtonStateChanged), EngineEventIds.ButtonStateChanged, 14, ownSource: true);
            Subscribe(nameof(OnInputFieldValueChanged), EngineEventIds.InputFieldValueChanged, 15,
                ownSource: true);
            Subscribe(nameof(OnInputFieldSubmitted), EngineEventIds.InputFieldSubmitted, 16,
                ownSource: true);
            Subscribe(nameof(OnInputFieldCanceled), EngineEventIds.InputFieldCanceled, 17,
                ownSource: true);
            Subscribe(nameof(OnAnimationEvent), EngineEventIds.MotionEvent, 18, ownSource: true);
            Subscribe(nameof(OnCompositionMarker), EngineEventIds.CompositionMarker, 19,
                ownSource: true);
            Subscribe(nameof(OnAnimationFinished), EngineEventIds.MotionFinished, 23,
                ownSource: true);
            Subscribe(nameof(OnAnimatorStateChanged), EngineEventIds.AnimatorStateChanged, 20,
                ownSource: true);
            Subscribe(nameof(OnSliderValueChanged), EngineEventIds.SliderValueChanged, 21,
                ownSource: true);
        }

        private void Subscribe(string methodName, string eventGuid, int kind,
            bool global = false, bool ownSource = false)
        {
            if (!Overrides(methodName)) return;
            var result = global
                ? owner.SubscribeGlobalEvent(eventGuid)
                : owner.SubscribeEvent(eventGuid);
            if (result.Succeeded && result.Value.IsValid)
            {
                subscriptions.Add((result.Value, kind, ownSource));
            }
        }

        internal int SubscriptionCount => subscriptions.Count;

        private bool Overrides(string methodName)
            => owner.HandlesMessage(methodName);

        internal void Pump(bool contacts, bool engineEvents)
        {
            // Awake 前でも非 Active 期間のキューを捨てるだけの Poll は許す。
            if (!owner.RuntimeLifecycleStarted && (contacts || engineEvents)) return;
            foreach (var (subscription, kind, ownSource) in subscriptions)
            {
                // 1 フレームで積まれたぶんをすべて配る。
                // 上限は C++ 側の購読キューが持っている。
                while (true)
                {
                    var polled = owner.Runtime.PollEvent(subscription);
                    if (!polled.Succeeded || string.IsNullOrEmpty(polled.Value.TypeGuid)) break;
                    if (ownSource && !SameObject(polled.Value.Source, owner.GameObject)) continue;
                    // Poll は続け、配送しない期間のイベントを次の有効化へ持ち越さない。
                    if (kind <= 5 ? !contacts : !engineEvents) continue;
                    if (!NativeBridge.IsComponentAlive(owner.Component) ||
                        !NativeBridge.ActiveInHierarchy(owner.GameObject)) continue;
                    // 接触で自身を有効化できる MonoBehaviour のみ disabled 配送を許す。
                    if (kind > 5 || owner is not MonoBehaviourHost)
                    {
                        var enabled = NativeBridge.IsComponentEnabled(owner.Component);
                        if (!enabled.Succeeded || !enabled.Value) continue;
                    }
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
            case 5: owner.OnTriggerExit(new TriggerInfo(record)); break;
            case 6: owner.OnBeforeSceneUnload(new SceneEventInfo(record)); break;
            case 7: owner.OnSceneLoadRequested(new SceneEventInfo(record)); break;
            case 8: owner.OnSceneLoadStarted(new SceneEventInfo(record)); break;
            case 9: owner.OnSceneLoaded(new SceneEventInfo(record)); break;
            case 10: owner.OnSceneLoadFailed(new SceneEventInfo(record)); break;
            case 11: owner.OnWorldChanged(new SceneEventInfo(record)); break;
            case 12: owner.OnApplicationQuit(new ApplicationQuitEventInfo(record)); break;
            case 13: owner.OnButtonClicked(new ButtonEventInfo(record)); break;
            case 14: owner.OnButtonStateChanged(new ButtonEventInfo(record)); break;
            case 15: owner.OnInputFieldValueChanged(new InputFieldEventInfo(record)); break;
            case 16: owner.OnInputFieldSubmitted(new InputFieldEventInfo(record)); break;
            case 17: owner.OnInputFieldCanceled(new InputFieldEventInfo(record)); break;
            case 18: owner.OnAnimationEvent(new MotionEventInfo(record)); break;
            case 19: owner.OnCompositionMarker(new MotionEventInfo(record)); break;
            case 20: owner.OnAnimatorStateChanged(new AnimatorStateEventInfo(record)); break;
            case 21: owner.OnSliderValueChanged(new SliderEventInfo(record)); break;
            case 22: owner.OnAnyButtonClicked(new ButtonEventInfo(record)); break;
            case 23: owner.OnAnimationFinished(new MotionEventInfo(record)); break;
            }
        }

        private static bool SameObject(ObjectHandle left, ObjectHandle right)
            => left.World == right.World && left.Object == right.Object &&
                left.Generation == right.Generation;
    }
}
