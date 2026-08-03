namespace ReplayEngine;

public sealed class ScriptRuntimeContext
{
    internal static readonly ScriptRuntimeContext Unavailable = new();

    internal ScriptRuntimeContext()
    {
    }

    public float DeltaTime => NativeBridge.TimeDeltaTime;
    public float FixedDeltaTime => NativeBridge.TimeFixedDeltaTime;
    public ulong FrameIndex => NativeBridge.TimeFrameIndex;

    public RuntimeResult<ObjectHandle> FindGameObject(ulong objectId)
    {
        return NativeBridge.FindGameObject(objectId);
    }

    public RuntimeStatus IsValid(ObjectHandle handle)
    {
        return NativeBridge.IsGameObjectValid(handle);
    }

    public RuntimeResult<Vector3> GetLocalPosition(ObjectHandle handle)
    {
        return NativeBridge.GetLocalPosition(handle);
    }

    public RuntimeStatus SetLocalPosition(ObjectHandle handle, Vector3 value)
    {
        return NativeBridge.SetLocalPosition(handle, value);
    }

    public RuntimeResult<Vector3> GetLocalRotationEuler(ObjectHandle handle)
    {
        return NativeBridge.GetLocalRotationEuler(handle);
    }

    public RuntimeStatus SetLocalRotationEuler(ObjectHandle handle, Vector3 value)
    {
        return NativeBridge.SetLocalRotationEuler(handle, value);
    }

    public RuntimeResult<Vector3> GetLocalScale(ObjectHandle handle)
    {
        return NativeBridge.GetLocalScale(handle);
    }

    public RuntimeStatus SetLocalScale(ObjectHandle handle, Vector3 value)
    {
        return NativeBridge.SetLocalScale(handle, value);
    }

    public RuntimeResult<ComponentHandle> GetComponent(ObjectHandle handle, uint componentTypeId)
    {
        return NativeBridge.GetComponent(handle, componentTypeId);
    }

    public RuntimeStatus Destroy(ObjectHandle handle)
    {
        return NativeBridge.DestroyGameObject(handle);
    }

    public RuntimeStatus Destroy(ComponentHandle handle)
    {
        return NativeBridge.DestroyComponent(handle);
    }

    public RuntimeResult<ObjectHandle> Instantiate(string prefabAssetGuid, Vector3 position, Vector3 rotationEuler, Vector3 scale, ObjectHandle parent = default)
    {
        return NativeBridge.Instantiate(prefabAssetGuid, position, rotationEuler, scale, parent);
    }

    public RuntimeStatus LoadScene(string sceneAssetGuid)
    {
        return NativeBridge.LoadScene(sceneAssetGuid);
    }

    public RuntimeStatus ReloadScene()
    {
        return NativeBridge.ReloadScene();
    }

    public RuntimeStatus ReturnToPreviousScene()
    {
        return NativeBridge.ReturnToPreviousScene();
    }

    public RuntimeStatus Subscribe(string eventName)
    {
        _ = eventName;
        return RuntimeStatus.ServiceUnavailable;
    }

    public RuntimeStatus InputUnavailable()
    {
        return RuntimeStatus.ServiceUnavailable;
    }
}
