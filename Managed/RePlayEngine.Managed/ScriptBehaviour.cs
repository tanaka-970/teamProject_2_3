using System.Collections.Generic;

namespace ReplayEngine;
// ビヘイビアスクリプトの基底クラス。ゲームオブジェクトにアタッチされるスクリプトは、このクラスを継承する必要があるよ。
public abstract class ScriptBehaviour
{
	// ゲームオブジェクトにアタッチされたスクリプトのライフサイクルイベントを定義
	private readonly List<EventSubscription> eventSubscriptions = new();
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

    // 自分が付いている GameObject の Transform。
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

    internal void ReleaseManagedSubscriptions()
    {
        foreach (var subscription in eventSubscriptions)
        {
            Runtime.UnsubscribeEvent(subscription);
        }
        eventSubscriptions.Clear();
    }

    public virtual void Awake() { }
    public virtual void OnEnable() { }
    public virtual void Start() { }
    public virtual void FixedUpdate(float fixedDeltaTime) { }
    public virtual void Update(float deltaTime) { }
    public virtual void LateUpdate(float deltaTime) { }
    public virtual void OnDisable() { }
    public virtual void OnDestroy() { }
}
