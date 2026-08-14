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

    public RuntimeResult<ComponentHandle> AddComponent(ObjectHandle handle, uint componentTypeId)
    {
        return NativeBridge.AddComponent(handle, componentTypeId);
    }

    public RuntimeResult<ComponentHandle[]> GetComponents(ObjectHandle handle, uint componentTypeId)
    {
        return NativeBridge.GetComponents(handle, componentTypeId);
    }

    public RuntimeStatus SetComponentEnabled(ComponentHandle handle, bool enabled)
    {
        return NativeBridge.SetComponentEnabled(handle, enabled);
    }

    public RuntimeResult<bool> IsComponentEnabled(ComponentHandle handle)
    {
        return NativeBridge.IsComponentEnabled(handle);
    }

    public RuntimeResult<MotionPlayer> FindMotionPlayer(ObjectHandle owner, string key = "")
    {
        var result = NativeBridge.FindMotionPlayer(owner, key);
        return new RuntimeResult<MotionPlayer>(result.Status, new MotionPlayer(result.Value));
    }

    public RuntimeStatus Destroy(ObjectHandle handle)
    {
        return NativeBridge.DestroyGameObject(handle);
    }

    public RuntimeStatus Destroy(ComponentHandle handle)
    {
        return NativeBridge.DestroyComponent(handle);
    }

    // ---- Runtime Services ---------------------------------------------------

    public bool InputAvailable => NativeBridge.InputAvailable();
    public RuntimeResult<bool> InputHeld(string action, int playerSlot = 0)
    {
        return NativeBridge.InputHeld(action, playerSlot);
    }

    public RuntimeResult<bool> InputPressed(string action, int playerSlot = 0)
    {
        return NativeBridge.InputPressed(action, playerSlot);
    }

    public RuntimeResult<bool> InputReleased(string action, int playerSlot = 0)
    {
        return NativeBridge.InputReleased(action, playerSlot);
    }

    public RuntimeResult<float> InputAxis(string axis, int playerSlot = 0)
    {
        return NativeBridge.InputAxis(axis, playerSlot);
    }

    public RuntimeResult<float> PointerDeltaX() => NativeBridge.InputPointerDeltaX();
    public RuntimeResult<float> PointerDeltaY() => NativeBridge.InputPointerDeltaY();

    public bool AudioAvailable => NativeBridge.AudioAvailable();
    public RuntimeResult<AudioVoice> PlayAudio(string clipPath, bool loop = false,
        float volume = 1.0f, float pitch = 1.0f, int spatialMode = 0,
        Vector3 position = default, float minDistance = 1.0f,
        float maxDistance = 30.0f)
    {
        return NativeBridge.PlayAudio(clipPath, loop, volume, pitch, spatialMode,
            position, minDistance, maxDistance);
    }

    public RuntimeStatus StopAudio(AudioVoice voice) => NativeBridge.StopAudio(voice);

    public RuntimeStatus UpdateAudio(AudioVoice voice, string clipPath, bool loop = false,
        float volume = 1.0f, float pitch = 1.0f, int spatialMode = 0,
        Vector3 position = default, float minDistance = 1.0f,
        float maxDistance = 30.0f)
    {
        return NativeBridge.UpdateAudio(voice, clipPath, loop, volume, pitch,
            spatialMode, position, minDistance, maxDistance);
    }

    public bool SaveGameAvailable => NativeBridge.SaveAvailable();
    public RuntimeStatus SetSaveBool(string slot, string key, bool value)
        => NativeBridge.SaveSetBool(slot, key, value);
    public RuntimeStatus SetSaveInt(string slot, string key, long value)
        => NativeBridge.SaveSetInt(slot, key, value);
    public RuntimeStatus SetSaveFloat(string slot, string key, double value)
        => NativeBridge.SaveSetDouble(slot, key, value);
    public RuntimeStatus SetSaveString(string slot, string key, string value)
        => NativeBridge.SaveSetString(slot, key, value);
    public RuntimeResult<bool> GetSaveBool(string slot, string key)
        => NativeBridge.SaveGetBool(slot, key);
    public RuntimeResult<long> GetSaveInt(string slot, string key)
        => NativeBridge.SaveGetInt(slot, key);
    public RuntimeResult<double> GetSaveFloat(string slot, string key)
        => NativeBridge.SaveGetDouble(slot, key);
    public RuntimeResult<string> GetSaveString(string slot, string key)
        => NativeBridge.SaveGetString(slot, key);
    public RuntimeResult<bool> HasSaveKey(string slot, string key)
        => NativeBridge.SaveHasKey(slot, key);
    public RuntimeStatus DeleteSaveKey(string slot, string key)
        => NativeBridge.SaveDeleteKey(slot, key);
    public RuntimeStatus SaveGame(string slot) => NativeBridge.SaveGame(slot);
    public RuntimeStatus LoadGame(string slot) => NativeBridge.LoadGame(slot);
    public RuntimeStatus DeleteSave(string slot) => NativeBridge.DeleteSave(slot);

    public bool RuntimeUIAvailable => NativeBridge.RuntimeUIAvailable();
    public RuntimeResult<ObjectHandle> CreateUIElement(string name,
        ObjectHandle parent = default)
        => NativeBridge.CreateUIElement(name, parent);
    public RuntimeStatus SetUIText(ObjectHandle handle, string text)
        => NativeBridge.SetUIText(handle, text);
    public RuntimeResult<string> GetUIText(ObjectHandle handle)
        => NativeBridge.GetUIText(handle);
    public RuntimeStatus SetUIImageColor(ObjectHandle handle, Color color)
        => NativeBridge.SetUIImageColor(handle, color);
    public RuntimeStatus SetUIRect(ObjectHandle handle, Vector2 position,
        Vector2 size, Vector2 scale, float rotation = 0.0f, int sortOrder = 0)
        => NativeBridge.SetUIRect(handle, position, size, scale, rotation, sortOrder);
    public RuntimeStatus SetUIButtonInteractable(ObjectHandle handle, bool interactable)
        => NativeBridge.SetUIButtonInteractable(handle, interactable);

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

    /// <summary>Fires an event in the active Scene Flow asset.</summary>
    public RuntimeStatus TriggerSceneFlow(string eventName)
    {
        return NativeBridge.TriggerSceneFlow(eventName);
    }

    public RuntimeStatus SetSceneFlowBool(string key, bool value)
    {
        return NativeBridge.SetSceneFlowBool(key, value);
    }

    public RuntimeStatus SetSceneFlowInt(string key, long value)
    {
        return NativeBridge.SetSceneFlowInt(key, value);
    }

    public RuntimeStatus SetSceneFlowFloat(string key, double value)
    {
        return NativeBridge.SetSceneFlowFloat(key, value);
    }

    /// <summary>Casts against the runtime collision world.</summary>
    public RuntimeResult<RaycastHit> Raycast(Vector3 origin, Vector3 direction,
        float maxDistance = 1000.0f, int layer = 0, int mask = -1, ObjectHandle ignore = default)
    {
        return NativeBridge.Raycast(origin, direction, maxDistance, layer, mask, ignore);
    }

    public RuntimeResult<EventSubscription> SubscribeEvent(string eventTypeGuid, ObjectHandle owner = default)
    {
        return NativeBridge.SubscribeEvent(eventTypeGuid, owner);
    }

    public RuntimeStatus UnsubscribeEvent(EventSubscription subscription)
    {
        return NativeBridge.UnsubscribeEvent(subscription);
    }

    public RuntimeResult<RuntimeEvent> PollEvent(EventSubscription subscription)
    {
        return NativeBridge.PollEvent(subscription);
    }

    [Obsolete("Use InputAvailable and the InputHeld/InputPressed/InputReleased APIs.")]
    public RuntimeStatus InputUnavailable()
        => InputAvailable ? RuntimeStatus.Ok : RuntimeStatus.ServiceUnavailable;

    [Obsolete("Use AudioAvailable and PlayAudio/StopAudio.")]
    public RuntimeStatus AudioUnavailable()
        => AudioAvailable ? RuntimeStatus.Ok : RuntimeStatus.ServiceUnavailable;

    [Obsolete("Use RuntimeUIAvailable and the Runtime UI APIs.")]
    public RuntimeStatus RuntimeUIUnavailable()
        => RuntimeUIAvailable ? RuntimeStatus.Ok : RuntimeStatus.ServiceUnavailable;

    [Obsolete("Use SaveGameAvailable and the SaveGame APIs.")]
    public RuntimeStatus SaveGameUnavailable()
        => SaveGameAvailable ? RuntimeStatus.Ok : RuntimeStatus.ServiceUnavailable;
}
