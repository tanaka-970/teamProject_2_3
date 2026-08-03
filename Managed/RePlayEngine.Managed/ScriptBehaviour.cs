using System.Collections.Generic;

namespace ReplayEngine;

public abstract class ScriptBehaviour
{
    private readonly List<EventSubscription> eventSubscriptions = new();

    internal void Attach(ScriptRuntimeContext context, ObjectHandle gameObject, ComponentHandle component)
    {
        Runtime = context;
        GameObject = gameObject;
        Component = component;
    }

    protected ScriptRuntimeContext Runtime { get; private set; } = ScriptRuntimeContext.Unavailable;

    public ObjectHandle GameObject { get; private set; }
    public ComponentHandle Component { get; private set; }

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
