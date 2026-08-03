namespace ReplayEngine;

public abstract class ScriptBehaviour
{
    internal void Attach(ScriptRuntimeContext context, ObjectHandle gameObject, ComponentHandle component)
    {
        Runtime = context;
        GameObject = gameObject;
        Component = component;
    }

    protected ScriptRuntimeContext Runtime { get; private set; } = ScriptRuntimeContext.Unavailable;

    public ObjectHandle GameObject { get; private set; }
    public ComponentHandle Component { get; private set; }

    public virtual void Awake() { }
    public virtual void OnEnable() { }
    public virtual void Start() { }
    public virtual void FixedUpdate(float fixedDeltaTime) { }
    public virtual void Update(float deltaTime) { }
    public virtual void LateUpdate(float deltaTime) { }
    public virtual void OnDisable() { }
    public virtual void OnDestroy() { }
}
