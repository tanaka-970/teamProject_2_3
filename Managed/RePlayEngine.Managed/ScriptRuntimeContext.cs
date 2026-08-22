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

    // 名前で探す。見つからなければ Status が Ok 以外になり、Handle は無効。
    //
    // 同じ名前が複数あるときは Scene の並び順で最初のものが返る。
    // 破棄予定のものは飛ばす。名前を一意にするのは呼び出し側の責任。
    public RuntimeResult<ObjectHandle> FindGameObject(string name)
    {
        return NativeBridge.FindGameObjectByName(name);
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

    public RuntimeResult<bool> GetScriptFieldBool(ComponentHandle component, string fieldName)
        => NativeBridge.GetScriptFieldBool(component, fieldName);
    public RuntimeStatus SetScriptFieldBool(ComponentHandle component, string fieldName, bool value)
        => NativeBridge.SetScriptFieldBool(component, fieldName, value);
    public RuntimeResult<int> GetScriptFieldInt(ComponentHandle component, string fieldName)
        => NativeBridge.GetScriptFieldInt(component, fieldName);
    public RuntimeStatus SetScriptFieldInt(ComponentHandle component, string fieldName, int value)
        => NativeBridge.SetScriptFieldInt(component, fieldName, value);
    public RuntimeResult<double> GetScriptFieldDouble(ComponentHandle component, string fieldName)
        => NativeBridge.GetScriptFieldDouble(component, fieldName);
    public RuntimeStatus SetScriptFieldDouble(ComponentHandle component, string fieldName, double value)
        => NativeBridge.SetScriptFieldDouble(component, fieldName, value);
    public RuntimeResult<string> GetScriptFieldString(ComponentHandle component, string fieldName)
        => NativeBridge.GetScriptFieldString(component, fieldName);
    public RuntimeStatus SetScriptFieldString(ComponentHandle component, string fieldName, string value)
        => NativeBridge.SetScriptFieldString(component, fieldName, value);

    public RuntimeResult<ObjectHandle> GetUIFocus() => NativeBridge.GetUIFocus();
    public RuntimeStatus SetUIFocus(ObjectHandle target) => NativeBridge.SetUIFocus(target);
    public RuntimeResult<ObjectHandle> FindUIFocus(ObjectHandle from, UIFocusDirection direction)
        => NativeBridge.FindUIFocus(from, direction);

    public RuntimeStatus PublishEvent(string eventTypeGuid, string typeName = "",
        ObjectHandle source = default, ObjectHandle target = default)
        => NativeBridge.PublishEvent(eventTypeGuid, typeName, source, target);

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

    // ---- v6 Object / hierarchy / log ---------------------------------------

    public RuntimeStatus LogInfo(string message, ObjectHandle source = default)
        => NativeBridge.LogInfo(message, source);
    public RuntimeStatus LogWarning(string message, ObjectHandle source = default)
        => NativeBridge.LogWarning(message, source);
    public RuntimeStatus LogError(string message, ObjectHandle source = default)
        => NativeBridge.LogError(message, source);

    public RuntimeResult<ObjectHandle> CreateGameObject(string name = "")
        => NativeBridge.CreateGameObject(name);
    public RuntimeResult<Vector3> GetWorldPosition(ObjectHandle handle)
        => NativeBridge.GetWorldPosition(handle);
    public RuntimeStatus SetParent(ObjectHandle child, ObjectHandle parent = default,
        bool preserveWorldTransform = true)
        => NativeBridge.SetParent(child, parent, preserveWorldTransform);
    public RuntimeResult<ObjectHandle> GetParent(ObjectHandle handle)
        => NativeBridge.GetParent(handle);
    public RuntimeResult<ObjectHandle[]> GetChildren(ObjectHandle handle)
        => NativeBridge.GetChildren(handle);
    public RuntimeResult<string> GetName(ObjectHandle handle)
        => NativeBridge.GetName(handle);
    public RuntimeStatus SetName(ObjectHandle handle, string name)
        => NativeBridge.SetName(handle, name);
    public RuntimeResult<bool> IsEnabled(ObjectHandle handle)
        => NativeBridge.IsGameObjectEnabled(handle);
    public RuntimeStatus SetEnabled(ObjectHandle handle, bool enabled)
        => NativeBridge.SetGameObjectEnabled(handle, enabled);


    // ---- v7 Physics / deferred / runtime state -----------------------------

    public RuntimeResult<GroundHit> QueryGround(Vector3 origin, float radius,
        float upOffset, float downDistance, float walkableNormalY,
        ObjectHandle ignore = default)
        => NativeBridge.QueryGround(origin, radius, upOffset, downDistance,
            walkableNormalY, ignore);

    public RuntimeResult<SphereSweepHit> SweepSphere(Vector3 start, Vector3 end,
        float radius, float maximumNormalY, ObjectHandle ignore = default)
        => NativeBridge.SweepSphere(start, end, radius, maximumNormalY, ignore);

    public RuntimeStatus InstantiatePrefabDeferred(string prefabAssetGuid, Vector3 position,
        Vector3 rotationEuler, Vector3 scale, ObjectHandle parent = default)
        => NativeBridge.InstantiatePrefabDeferred(
            prefabAssetGuid, position, rotationEuler, scale, parent);

    public RuntimeStatus FlushDeferredOperations()
        => NativeBridge.FlushDeferredOperations();

    public RuntimeResult<ulong> PendingDeferredOperationCount()
        => NativeBridge.PendingDeferredOperationCount();

    public RuntimeResult<bool> HasComponent(ObjectHandle handle, uint componentTypeId)
        => NativeBridge.HasComponent(handle, componentTypeId);

    public RuntimeResult<float> TimeScale => NativeBridge.TimeScale();
    public RuntimeResult<bool> SceneTransitionInProgress
        => NativeBridge.SceneTransitionInProgress();
    public bool PhysicsAvailable => NativeBridge.PhysicsAvailable();
    public bool SceneFlowAvailable => NativeBridge.SceneFlowAvailable();


    // ---- v8 Event payload --------------------------------------------------

    public RuntimeStatus PublishEvent(string eventTypeGuid, RuntimeEventPayload payload,
        string typeName = "", ObjectHandle source = default, ObjectHandle target = default)
        => NativeBridge.PublishEvent(eventTypeGuid, payload, typeName, source, target);

    // ---- v10 型付き Component API ------------------------------------------
    //
    // uint の Component Type ID を手で書かなくて済むようにする。
    // 型名から引いた ID は ComponentTypes が覚えるので、毎フレーム引き直さない。

    public RuntimeResult<T> GetComponent<T>(ObjectHandle handle)
        where T : IComponentBinding<T>
    {
        var typeId = ComponentTypes.IdOf<T>();
        if (typeId == 0) return new RuntimeResult<T>(RuntimeStatus.ComponentNotFound);
        var result = NativeBridge.GetComponent(handle, typeId);
        return new RuntimeResult<T>(result.Status, T.FromHandle(result.Value));
    }

    public bool TryGetComponent<T>(ObjectHandle handle, out T component)
        where T : IComponentBinding<T>
    {
        var result = GetComponent<T>(handle);
        component = result.Value;
        return result.Succeeded;
    }

    // 値をそのまま返す入口。戻り値を変数へ受ければプロパティへ代入できる。
    public T GetComponentOrDefault<T>(ObjectHandle handle) where T : IComponentBinding<T>
        => GetComponent<T>(handle).Value;

    public T AddComponentOrDefault<T>(ObjectHandle handle) where T : IComponentBinding<T>
        => AddComponent<T>(handle).Value;

    public RuntimeResult<T> AddComponent<T>(ObjectHandle handle)
        where T : IComponentBinding<T>
    {
        var typeId = ComponentTypes.IdOf<T>();
        if (typeId == 0) return new RuntimeResult<T>(RuntimeStatus.ComponentNotFound);
        var result = NativeBridge.AddComponent(handle, typeId);
        return new RuntimeResult<T>(result.Status, T.FromHandle(result.Value));
    }

    public bool HasComponent<T>(ObjectHandle handle) where T : IComponentBinding<T>
    {
        var typeId = ComponentTypes.IdOf<T>();
        if (typeId == 0) return false;
        var result = NativeBridge.HasComponent(handle, typeId);
        return result.Succeeded && result.Value;
    }

    public RuntimeResult<T[]> GetComponents<T>(ObjectHandle handle)
        where T : IComponentBinding<T>
    {
        var typeId = ComponentTypes.IdOf<T>();
        if (typeId == 0) return new RuntimeResult<T[]>(RuntimeStatus.ComponentNotFound, Array.Empty<T>());
        var result = NativeBridge.GetComponents(handle, typeId);
        if (!result.Succeeded || result.Value == null)
            return new RuntimeResult<T[]>(result.Status, Array.Empty<T>());

        var typed = new T[result.Value.Length];
        for (var index = 0; index < result.Value.Length; ++index)
            typed[index] = T.FromHandle(result.Value[index]);
        return new RuntimeResult<T[]>(result.Status, typed);
    }

    // 型付きの入口がまだ無い Component を、C++ の型名で直接触る。
    public RuntimeResult<GenericComponent> GetComponent(ObjectHandle handle, string nativeTypeName)
    {
        var typeId = ComponentTypes.IdOf(nativeTypeName);
        if (typeId == 0) return new RuntimeResult<GenericComponent>(RuntimeStatus.ComponentNotFound);
        var result = NativeBridge.GetComponent(handle, typeId);
        return new RuntimeResult<GenericComponent>(result.Status, new GenericComponent(result.Value));
    }

    public RuntimeResult<uint> ComponentTypeId(string nativeTypeName)
        => NativeBridge.ComponentTypeId(nativeTypeName);
    public RuntimeResult<string> GetComponentTypeName(ComponentHandle handle)
        => NativeBridge.GetComponentTypeName(handle);

    // ---- v10 Component プロパティ（型付きの入口が無い値へ直接触る） -------------

    public RuntimeResult<bool> GetComponentBool(ComponentHandle handle, string name)
        => NativeBridge.GetPropertyBool(handle, name);
    public RuntimeStatus SetComponentBool(ComponentHandle handle, string name, bool value)
        => NativeBridge.SetPropertyBool(handle, name, value);
    public RuntimeResult<long> GetComponentInt(ComponentHandle handle, string name)
        => NativeBridge.GetPropertyInt(handle, name);
    public RuntimeStatus SetComponentInt(ComponentHandle handle, string name, long value)
        => NativeBridge.SetPropertyInt(handle, name, value);
    public RuntimeResult<double> GetComponentFloat(ComponentHandle handle, string name)
        => NativeBridge.GetPropertyDouble(handle, name);
    public RuntimeStatus SetComponentFloat(ComponentHandle handle, string name, double value)
        => NativeBridge.SetPropertyDouble(handle, name, value);
    public RuntimeResult<string> GetComponentString(ComponentHandle handle, string name)
        => NativeBridge.GetPropertyString(handle, name);
    public RuntimeStatus SetComponentString(ComponentHandle handle, string name, string value)
        => NativeBridge.SetPropertyString(handle, name, value);
    public RuntimeResult<Vector3> GetComponentVector3(ComponentHandle handle, string name)
        => NativeBridge.GetPropertyVector3(handle, name);
    public RuntimeStatus SetComponentVector3(ComponentHandle handle, string name, Vector3 value)
        => NativeBridge.SetPropertyVector3(handle, name, value);
    public RuntimeResult<Vector4> GetComponentVector4(ComponentHandle handle, string name)
        => NativeBridge.GetPropertyVector4(handle, name);
    public RuntimeStatus SetComponentVector4(ComponentHandle handle, string name, Vector4 value)
        => NativeBridge.SetPropertyVector4(handle, name, value);

    // ---- v10 Transform ------------------------------------------------------

    public TransformAccess Transform(ObjectHandle handle) => new(handle);

    public RuntimeStatus SetWorldPosition(ObjectHandle handle, Vector3 value)
        => NativeBridge.SetWorldPosition(handle, value);
    public RuntimeResult<Quaternion> GetWorldRotation(ObjectHandle handle)
        => NativeBridge.GetWorldRotation(handle);
    public RuntimeStatus SetWorldRotation(ObjectHandle handle, Quaternion value)
        => NativeBridge.SetWorldRotation(handle, value);
    public RuntimeResult<Vector3> GetWorldScale(ObjectHandle handle)
        => NativeBridge.GetWorldScale(handle);
    public RuntimeStatus SetWorldScale(ObjectHandle handle, Vector3 value)
        => NativeBridge.SetWorldScale(handle, value);
    public RuntimeStatus LookAt(ObjectHandle handle, Vector3 target)
        => NativeBridge.LookAt(handle, target, new Vector3(0.0f, 1.0f, 0.0f));

    // ---- v11 Scene / 生成 ----------------------------------------------------

    // 現在の Runtime Scene の Asset GUID。未接続なら空。
    public RuntimeResult<string> CurrentSceneGuid() => NativeBridge.GetCurrentSceneGuid();

    // プロセスは落とさない。要求として記録するだけ。
    public RuntimeStatus QuitApplication(string reason = "") =>
        NativeBridge.QuitApplication(reason ?? string.Empty);

    // 遅延生成を積み、要求番号を返す。Flush 後に TakeSpawnResult で引き取る。
    public RuntimeResult<ulong> InstantiateDeferred(string prefabAssetGuid,
        Vector3 position, Vector3 rotationEuler, Vector3 scale, ObjectHandle parent = default)
        => NativeBridge.InstantiatePrefabTracked(
            prefabAssetGuid, position, rotationEuler, scale, parent);

    // 完了した遅延生成を 1 件引き取る。
    // まだ Flush されていなければ Status が TransitionInProgress になる。
    public RuntimeResult<ObjectHandle> TakeSpawnResult(ulong request)
        => NativeBridge.TakeSpawnResult(request);

    // Scene 遷移で破棄されないようにする。PersistentComponent を付けるだけ。
    public RuntimeStatus DontDestroyOnLoad(ObjectHandle handle)
    {
        var result = AddComponent<PersistentComponent>(handle);
        return result.Status;
    }

    // 取りこぼして捨てられたイベント数。Poll 忘れの検出に使う。
    public RuntimeResult<ulong> EventDroppedCount(EventSubscription subscription)
        => NativeBridge.EventDroppedCount(subscription);

    // ---- v11 生デバイス入力 --------------------------------------------------
    //
    // 実体は静的な Input クラス。Behaviour から Runtime 経由でも呼べるようにしておく。

    public bool GetKey(Key key) => Input.GetKey(key);
    public bool GetKeyDown(Key key) => Input.GetKeyDown(key);
    public bool GetKeyUp(Key key) => Input.GetKeyUp(key);
    public Vector2 MousePosition => Input.MousePosition;
    public float MouseScrollDelta => Input.MouseScrollDelta;
}
