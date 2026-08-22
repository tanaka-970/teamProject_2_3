using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;

namespace ReplayEngine;

public static unsafe class NativeBridge
{
    private static readonly ScriptRuntimeContext Context = new();
    private static readonly Dictionary<ulong, ManagedInstance> Instances = new();
    private static readonly Dictionary<TypeGuid, Type> Types = new();
    private static AssemblyLoadContext? scriptContext;
    private static Assembly? scriptAssembly;
    private static ulong nextHandle = 1;
    private static string lastError = string.Empty;
    private static string lastErrorFile = string.Empty;
    private static int lastErrorLine;


    [ThreadStatic]
    private static RuntimeEventPayload? pendingParsedEventPayload;
    private static NativeApi api;

    internal static float TimeDeltaTime { get; private set; }
    internal static float TimeFixedDeltaTime { get; private set; }
    internal static ulong TimeFrameIndex { get; private set; }

    [StructLayout(LayoutKind.Sequential)]
    private struct TypeGuid : IEquatable<TypeGuid>
    {
        public ulong High;
        public ulong Low;

        public bool Equals(TypeGuid other) => High == other.High && Low == other.Low;
        public override bool Equals(object? obj) => obj is TypeGuid other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(High, Low);
    }

    // 関数ポインタ表の互換番号。C++ の Detail::kNativeApiAbiVersion と必ず一致させる。
    public const uint NativeApiAbiVersion = 10;

    // 表の先頭に必ず置く自己記述ヘッダー。C++ の Detail::NativeApiHeader と同じ並び。
    [StructLayout(LayoutKind.Sequential)]
    public struct NativeApiHeader
    {
        public uint AbiVersion;
        public uint StructSize;
        public uint EntryCount;
        public uint Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct NativeApi
    {
        public NativeApiHeader Header;

        public delegate* unmanaged[Cdecl]<ulong, ObjectHandle*, int> FindGameObject;
        public delegate* unmanaged[Cdecl]<ObjectHandle, int> IsGameObjectValid;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector3*, int> GetLocalPosition;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector3, int> SetLocalPosition;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector3*, int> GetLocalRotationEuler;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector3, int> SetLocalRotationEuler;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector3*, int> GetLocalScale;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector3, int> SetLocalScale;
        public delegate* unmanaged[Cdecl]<ObjectHandle, uint, ComponentHandle*, int> GetComponent;
        public delegate* unmanaged[Cdecl]<ObjectHandle, int> DestroyGameObject;
        public delegate* unmanaged[Cdecl]<ComponentHandle, int> DestroyComponent;
        public delegate* unmanaged[Cdecl]<byte*, Vector3, Vector3, Vector3, ObjectHandle, ObjectHandle*, int> Instantiate;
        public delegate* unmanaged[Cdecl]<byte*, int> LoadScene;
        public delegate* unmanaged[Cdecl]<int> ReloadScene;
        public delegate* unmanaged[Cdecl]<int> ReturnToPreviousScene;
        public delegate* unmanaged[Cdecl]<ulong, ulong, ObjectHandle, ulong*, int> SubscribeEvent;
        public delegate* unmanaged[Cdecl]<ulong, int> UnsubscribeEvent;
        public delegate* unmanaged[Cdecl]<ulong, byte*, int, int> PollEvent;
        public delegate* unmanaged[Cdecl]<byte*, int> TriggerSceneFlow;
        public delegate* unmanaged[Cdecl]<byte*, int, int> SetSceneFlowBool;
        public delegate* unmanaged[Cdecl]<byte*, long, int> SetSceneFlowInt;
        public delegate* unmanaged[Cdecl]<byte*, double, int> SetSceneFlowFloat;
        public delegate* unmanaged[Cdecl]<Vector3, Vector3, float, int, int, ObjectHandle, RaycastHit*, int> Raycast;
        public delegate* unmanaged[Cdecl]<ObjectHandle, byte*, ComponentHandle*, int> FindMotionPlayer;
        public delegate* unmanaged[Cdecl]<ComponentHandle, int> MotionPlay;
        public delegate* unmanaged[Cdecl]<ComponentHandle, float, int> MotionPlayFrom;
        public delegate* unmanaged[Cdecl]<ComponentHandle, int> MotionPause;
        public delegate* unmanaged[Cdecl]<ComponentHandle, int> MotionResume;
        public delegate* unmanaged[Cdecl]<ComponentHandle, int> MotionStop;
        public delegate* unmanaged[Cdecl]<ComponentHandle, int> MotionReverse;
        public delegate* unmanaged[Cdecl]<ComponentHandle, float, int> MotionSetTime;
        public delegate* unmanaged[Cdecl]<ComponentHandle, float, int> MotionSetSpeed;
        public delegate* unmanaged[Cdecl]<ComponentHandle, float, int> MotionSetWeight;
        public delegate* unmanaged[Cdecl]<ComponentHandle, int*, int> MotionIsPlaying;
        public delegate* unmanaged[Cdecl]<ComponentHandle, float*, int> MotionGetTime;
        public delegate* unmanaged[Cdecl]<ComponentHandle, float*, int> MotionGetDuration;

        // v4 Runtime Service / Component / Runtime UI API。
        public delegate* unmanaged[Cdecl]<int> InputAvailable;
        public delegate* unmanaged[Cdecl]<byte*, int, int*, int> InputHeld;
        public delegate* unmanaged[Cdecl]<byte*, int, int*, int> InputPressed;
        public delegate* unmanaged[Cdecl]<byte*, int, int*, int> InputReleased;
        public delegate* unmanaged[Cdecl]<byte*, int, float*, int> InputAxis;
        public delegate* unmanaged[Cdecl]<float*, int> InputPointerDeltaX;
        public delegate* unmanaged[Cdecl]<float*, int> InputPointerDeltaY;
        public delegate* unmanaged[Cdecl]<int> AudioAvailable;
        public delegate* unmanaged[Cdecl]<byte*, int, float, float, int, Vector3, float, float, ulong*, int> AudioPlay;
        public delegate* unmanaged[Cdecl]<ulong, int> AudioStop;
        public delegate* unmanaged[Cdecl]<ulong, byte*, int, float, float, int, Vector3, float, float, int> AudioUpdate;
        public delegate* unmanaged[Cdecl]<int> SaveAvailable;
        public delegate* unmanaged[Cdecl]<byte*, byte*, int, int> SaveSetBool;
        public delegate* unmanaged[Cdecl]<byte*, byte*, long, int> SaveSetInt;
        public delegate* unmanaged[Cdecl]<byte*, byte*, double, int> SaveSetDouble;
        public delegate* unmanaged[Cdecl]<byte*, byte*, byte*, int> SaveSetString;
        public delegate* unmanaged[Cdecl]<byte*, byte*, int*, int> SaveGetBool;
        public delegate* unmanaged[Cdecl]<byte*, byte*, long*, int> SaveGetInt;
        public delegate* unmanaged[Cdecl]<byte*, byte*, double*, int> SaveGetDouble;
        public delegate* unmanaged[Cdecl]<byte*, byte*, byte*, int, int> SaveGetString;
        public delegate* unmanaged[Cdecl]<byte*, byte*, int*, int> SaveHasKey;
        public delegate* unmanaged[Cdecl]<byte*, byte*, int> SaveDeleteKey;
        public delegate* unmanaged[Cdecl]<byte*, int> SaveGame;
        public delegate* unmanaged[Cdecl]<byte*, int> LoadGame;
        public delegate* unmanaged[Cdecl]<byte*, int> DeleteSave;
        public delegate* unmanaged[Cdecl]<int> RuntimeUIAvailable;
        public delegate* unmanaged[Cdecl]<byte*, ObjectHandle, ObjectHandle*, int> CreateUIElement;
        public delegate* unmanaged[Cdecl]<ObjectHandle, byte*, int> SetUIText;
        public delegate* unmanaged[Cdecl]<ObjectHandle, byte*, int, int> GetUIText;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Color, int> SetUIImageColor;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector2, Vector2, Vector2, float, int, int> SetUIRect;
        public delegate* unmanaged[Cdecl]<ObjectHandle, int, int> SetUIButtonInteractable;
        public delegate* unmanaged[Cdecl]<ObjectHandle, uint, ComponentHandle*, int> AddComponent;
        public delegate* unmanaged[Cdecl]<ObjectHandle, uint, ComponentHandle*, int, int*, int> GetComponents;
        public delegate* unmanaged[Cdecl]<ComponentHandle, int, int> SetComponentEnabled;
        public delegate* unmanaged[Cdecl]<ComponentHandle, int*, int> GetComponentEnabled;
        // v5 Script Field / UI Focus / Event publish API. Keep in exact C++ tail order.
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, int*, int> GetScriptBool;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, int, int> SetScriptBool;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, int*, int> GetScriptInt;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, int, int> SetScriptInt;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, double*, int> GetScriptDouble;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, double, int> SetScriptDouble;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, byte*, int, int> GetScriptString;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, byte*, int> SetScriptString;
        public delegate* unmanaged[Cdecl]<ObjectHandle*, int> UIGetFocus;
        public delegate* unmanaged[Cdecl]<ObjectHandle, int> UISetFocus;
        public delegate* unmanaged[Cdecl]<ObjectHandle, int, ObjectHandle*, int> UIFindFocus;
        public delegate* unmanaged[Cdecl]<ulong, ulong, byte*, ObjectHandle, ObjectHandle, int> PublishEvent;

        // v6 Object / hierarchy / log API. Keep in exact C++ tail order.
        public delegate* unmanaged[Cdecl]<byte*, ObjectHandle, int> LogInfo;
        public delegate* unmanaged[Cdecl]<byte*, ObjectHandle, int> LogWarning;
        public delegate* unmanaged[Cdecl]<byte*, ObjectHandle, int> LogError;
        public delegate* unmanaged[Cdecl]<byte*, ObjectHandle*, int> CreateGameObject;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector3*, int> GetWorldPosition;
        public delegate* unmanaged[Cdecl]<ObjectHandle, ObjectHandle, int, int> SetParent;
        public delegate* unmanaged[Cdecl]<ObjectHandle, ObjectHandle*, int> GetParent;
        public delegate* unmanaged[Cdecl]<ObjectHandle, ObjectHandle*, int, int*, int> GetChildren;
        public delegate* unmanaged[Cdecl]<ObjectHandle, byte*, int, int> GetName;
        public delegate* unmanaged[Cdecl]<ObjectHandle, byte*, int> SetName;
        public delegate* unmanaged[Cdecl]<ObjectHandle, int*, int> GetGameObjectEnabled;
        public delegate* unmanaged[Cdecl]<ObjectHandle, int, int> SetGameObjectEnabled;

        // v7 Physics / deferred / runtime-state API. Keep in exact C++ tail order.
        public delegate* unmanaged[Cdecl]<Vector3, float, float, float, float, ObjectHandle, GroundHit*, int> QueryGround;
        public delegate* unmanaged[Cdecl]<Vector3, Vector3, float, float, ObjectHandle, SphereSweepHit*, int> SweepSphere;
        public delegate* unmanaged[Cdecl]<byte*, Vector3, Vector3, Vector3, ObjectHandle, int> InstantiatePrefabDeferred;
        public delegate* unmanaged[Cdecl]<int> FlushDeferredOperations;
        public delegate* unmanaged[Cdecl]<ulong*, int> PendingDeferredOperationCount;
        public delegate* unmanaged[Cdecl]<ObjectHandle, uint, int*, int> HasComponent;
        public delegate* unmanaged[Cdecl]<float*, int> GetTimeScale;
        public delegate* unmanaged[Cdecl]<int*, int> GetSceneTransitionInProgress;
        public delegate* unmanaged[Cdecl]<int> PhysicsAvailable;
        public delegate* unmanaged[Cdecl]<int> SceneFlowAvailable;

        // v8 Event payload API. Keep in exact C++ tail order.
        public delegate* unmanaged[Cdecl]<ulong, byte*, int, int*, int> PollEventWithPayload;
        public delegate* unmanaged[Cdecl]<ulong, ulong, byte*, ObjectHandle, ObjectHandle, byte*, int> PublishEventWithPayload;

        // v9 追加。名前で GameObject を探す。C++ 側の表も同じ位置（末尾）。
        public delegate* unmanaged[Cdecl]<byte*, ObjectHandle*, int> FindGameObjectByName;

        // v10 追加。Component 型・汎用プロパティ・World Transform・Rigidbody。
        public delegate* unmanaged[Cdecl]<byte*, uint*, int> ComponentTypeId;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, int, int> GetComponentTypeName;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, int*, int> GetPropertyBool;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, int, int> SetPropertyBool;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, long*, int> GetPropertyInt;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, long, int> SetPropertyInt;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, double*, int> GetPropertyDouble;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, double, int> SetPropertyDouble;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, byte*, int, int> GetPropertyString;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, byte*, int> SetPropertyString;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, Vector2*, int> GetPropertyVector2;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, Vector2, int> SetPropertyVector2;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, Vector3*, int> GetPropertyVector3;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, Vector3, int> SetPropertyVector3;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, Vector4*, int> GetPropertyVector4;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, Vector4, int> SetPropertyVector4;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector3, int> SetWorldPosition;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector4*, int> GetWorldRotation;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector4, int> SetWorldRotation;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector3*, int> GetWorldScale;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector3, int> SetWorldScale;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector3*, Vector3*, Vector3*, int> GetWorldAxes;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector3, Vector3, int> LookAt;
        public delegate* unmanaged[Cdecl]<ComponentHandle, Vector3, int> RigidbodyAddForce;
        public delegate* unmanaged[Cdecl]<ComponentHandle, Vector3, int> RigidbodyAddTorque;
        public delegate* unmanaged[Cdecl]<ComponentHandle, int> RigidbodyClearForces;
        public delegate* unmanaged[Cdecl]<ComponentHandle, Vector3, Vector3, int> RigidbodyTeleport;
        public delegate* unmanaged[Cdecl]<ComponentHandle, Vector3*, int> RigidbodyGetLinearVelocity;
        public delegate* unmanaged[Cdecl]<ComponentHandle, Vector3, int> RigidbodySetLinearVelocity;
        public delegate* unmanaged[Cdecl]<ComponentHandle, Vector3*, int> RigidbodyGetAngularVelocity;
        public delegate* unmanaged[Cdecl]<ComponentHandle, Vector3, int> RigidbodySetAngularVelocity;
    }

    private sealed class ManagedInstance
    {
        public ManagedInstance(ScriptBehaviour behaviour)
        {
            Behaviour = behaviour;
        }

        public ScriptBehaviour Behaviour { get; }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SetNativeApi(NativeApi* table)
    {
        if (table == null)
        {
            api = default;
            return 0;
        }

        // 関数ポインタ表は順番で結びついている。1 つずれると別関数を呼ぶので、
        // 受け取る前に「同じ版・同じ大きさ・同じ本数か」を必ず確かめる。
        var header = table->Header;
        var expectedSize = (uint)sizeof(NativeApi);
        var expectedEntries =
            (expectedSize - (uint)sizeof(NativeApiHeader)) / (uint)sizeof(void*);
        if (header.AbiVersion != NativeApiAbiVersion ||
            header.StructSize != expectedSize ||
            header.EntryCount != expectedEntries)
        {
            api = default;
            lastError =
                $"Native API table mismatch: got abi={header.AbiVersion} size={header.StructSize} " +
                $"entries={header.EntryCount}, expected abi={NativeApiAbiVersion} " +
                $"size={expectedSize} entries={expectedEntries}. " +
                "Rebuild RePlayEngine.Managed and the engine together.";
            lastErrorFile = string.Empty;
            lastErrorLine = 0;
            return 0;
        }

        api = *table;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int LoadAssembly(byte* assemblyPath, byte* output, int outputCapacity)
    {
        try
        {
            var path = FromUtf8(assemblyPath);
            if (string.IsNullOrWhiteSpace(path))
            {
                return Fail("C# assembly path is empty.", output, outputCapacity);
            }

            if (Instances.Count != 0)
            {
                return Fail("Managed instances are still alive. Stop Play or destroy instances before Assembly Reload.", output, outputCapacity);
            }

            var context = new ScriptLoadContext(path);
            Assembly? assembly = null;
            Dictionary<TypeGuid, Type>? discovered = null;
            try
            {
                assembly = context.LoadFromAssemblyPath(path);
                discovered = new Dictionary<TypeGuid, Type>();
                foreach (var type in DiscoverBehaviourTypes(assembly))
                {
                    var guid = ReadGuid(type);
                    if (guid.HasValue)
                    {
                        discovered[guid.Value] = type;
                    }
                }
            }
            catch
            {
                context.Unload();
                throw;
            }

            var oldContext = scriptContext;
            Types.Clear();
            foreach (var pair in discovered)
            {
                Types[pair.Key] = pair.Value;
            }

            scriptContext = context;
            scriptAssembly = assembly;
            if (oldContext != null)
            {
                oldContext.Unload();
                GC.Collect();
                GC.WaitForPendingFinalizers();
                GC.Collect();
            }
            WriteUtf8($"Loaded {Types.Count} C# Behaviour type(s).", output, outputCapacity);
            ClearLastError();
            return 1;
        }
        catch (Exception ex)
        {
            return Fail(ex, output, outputCapacity);
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int UnloadAssembly(byte* output, int outputCapacity)
    {
        if (Instances.Count != 0)
        {
            return Fail("Managed instances are still alive.", output, outputCapacity);
        }

        UnloadScriptContext();
        Types.Clear();
        WriteUtf8("Unloaded C# assembly.", output, outputCapacity);
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int DescribeType(ulong high, ulong low, byte* output, int outputCapacity)
    {
        try
        {
            var guid = new TypeGuid { High = high, Low = low };
            if (!Types.TryGetValue(guid, out var type))
            {
                return Fail("C# Behaviour type is not loaded.", output, outputCapacity);
            }

            var schema = BuildSchema(type);
            WriteUtf8(schema, output, outputCapacity);
            return 1;
        }
        catch (Exception ex)
        {
            return Fail(ex, output, outputCapacity);
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static ulong CreateInstance(ulong high, ulong low, ObjectHandle owner, ComponentHandle component)
    {
        try
        {
            var guid = new TypeGuid { High = high, Low = low };
            if (!Types.TryGetValue(guid, out var type))
            {
                SetLastError("C# Behaviour type is not loaded.");
                return 0;
            }

            if (Activator.CreateInstance(type) is not ScriptBehaviour behaviour)
            {
                SetLastError("C# Behaviour could not be created.");
                return 0;
            }

            behaviour.Attach(Context, owner, component);
            var handle = nextHandle++;
            Instances[handle] = new ManagedInstance(behaviour);
            ClearLastError();
            return handle;
        }
        catch (Exception ex)
        {
            SetLastError(ex);
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int DestroyInstance(ulong instance)
    {
        if (!Instances.Remove(instance, out var state)) return 0;
        state.Behaviour.ReleaseManagedSubscriptions();
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int Invoke(ulong instance, int callback, float deltaTime)
    {
        try
        {
            if (!Instances.TryGetValue(instance, out var state)) return 2;

            switch (callback)
            {
                case 0: state.Behaviour.Awake(); break;
                case 1: state.Behaviour.OnEnable(); break;
                case 2: state.Behaviour.Start(); break;
                case 3: state.Behaviour.FixedUpdate(deltaTime); break;
                case 4: state.Behaviour.Update(deltaTime); break;
                case 5: state.Behaviour.LateUpdate(deltaTime); break;
                case 6: state.Behaviour.OnDisable(); break;
                case 7: state.Behaviour.OnDestroy(); break;
                default: return 1;
            }

            ClearLastError();
            return 0;
        }
        catch (Exception ex)
        {
            SetLastError(ex);
            return 4;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SetField(ulong instance, byte* savedName, int valueType, byte* valueText)
    {
        try
        {
            if (!Instances.TryGetValue(instance, out var state)) return 0;
            var fieldName = StripFieldPrefix(FromUtf8(savedName));
            var field = FindSerializableField(state.Behaviour.GetType(), fieldName);
            if (field == null) return 0;

            var parsed = ParseValue(field.FieldType, FromUtf8(valueText));
            field.SetValue(state.Behaviour, parsed);
            return 1;
        }
        catch (Exception ex)
        {
            SetLastError(ex);
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetField(ulong instance, byte* savedName, byte* output, int outputCapacity)
    {
        try
        {
            if (!Instances.TryGetValue(instance, out var state)) return 0;
            var fieldName = StripFieldPrefix(FromUtf8(savedName));
            var field = FindSerializableField(state.Behaviour.GetType(), fieldName);
            if (field == null) return 0;

            WriteUtf8(FormatValue(field.GetValue(state.Behaviour), field.FieldType), output, outputCapacity);
            return 1;
        }
        catch (Exception ex)
        {
            SetLastError(ex);
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SetTime(float deltaTime, float fixedDeltaTime, ulong frameIndex)
    {
        TimeDeltaTime = deltaTime;
        TimeFixedDeltaTime = fixedDeltaTime;
        TimeFrameIndex = frameIndex;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int LiveInstanceCount()
    {
        return Instances.Count;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int LastError(byte* message, int messageCapacity, byte* file, int fileCapacity, int* line)
    {
        WriteUtf8(lastError, message, messageCapacity);
        WriteUtf8(lastErrorFile, file, fileCapacity);
        if (line != null) *line = lastErrorLine;
        return string.IsNullOrEmpty(lastError) ? 0 : 1;
    }

    internal static RuntimeResult<ObjectHandle> FindGameObject(ulong objectId)
    {
        if (api.FindGameObject == null) return new(RuntimeStatus.ServiceUnavailable);
        ObjectHandle result = default;
        var status = (RuntimeStatus)api.FindGameObject(objectId, &result);
        return new RuntimeResult<ObjectHandle>(status, result);
    }

    internal static RuntimeResult<ObjectHandle> FindGameObjectByName(string name)
    {
        if (api.FindGameObjectByName == null) return new(RuntimeStatus.ServiceUnavailable);
        if (name == null) return new(RuntimeStatus.InvalidArgument);
        ObjectHandle result = default;
        RuntimeStatus status;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
            status = (RuntimeStatus)api.FindGameObjectByName(text, &result);
        return new RuntimeResult<ObjectHandle>(status, result);
    }

    internal static RuntimeStatus IsGameObjectValid(ObjectHandle handle)
    {
        if (api.IsGameObjectValid == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.IsGameObjectValid(handle);
    }

    internal static RuntimeResult<Vector3> GetLocalPosition(ObjectHandle handle)
    {
        if (api.GetLocalPosition == null) return new(RuntimeStatus.ServiceUnavailable);
        Vector3 value = default;
        return new RuntimeResult<Vector3>((RuntimeStatus)api.GetLocalPosition(handle, &value), value);
    }

    internal static RuntimeStatus SetLocalPosition(ObjectHandle handle, Vector3 value)
    {
        if (api.SetLocalPosition == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.SetLocalPosition(handle, value);
    }

    internal static RuntimeResult<Vector3> GetLocalRotationEuler(ObjectHandle handle)
    {
        if (api.GetLocalRotationEuler == null) return new(RuntimeStatus.ServiceUnavailable);
        Vector3 value = default;
        return new RuntimeResult<Vector3>((RuntimeStatus)api.GetLocalRotationEuler(handle, &value), value);
    }

    internal static RuntimeStatus SetLocalRotationEuler(ObjectHandle handle, Vector3 value)
    {
        if (api.SetLocalRotationEuler == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.SetLocalRotationEuler(handle, value);
    }

    internal static RuntimeResult<Vector3> GetLocalScale(ObjectHandle handle)
    {
        if (api.GetLocalScale == null) return new(RuntimeStatus.ServiceUnavailable);
        Vector3 value = default;
        return new RuntimeResult<Vector3>((RuntimeStatus)api.GetLocalScale(handle, &value), value);
    }

    internal static RuntimeStatus SetLocalScale(ObjectHandle handle, Vector3 value)
    {
        if (api.SetLocalScale == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.SetLocalScale(handle, value);
    }

    internal static RuntimeResult<ComponentHandle> GetComponent(ObjectHandle handle, uint componentTypeId)
    {
        if (api.GetComponent == null) return new(RuntimeStatus.ServiceUnavailable);
        ComponentHandle value = default;
        return new RuntimeResult<ComponentHandle>((RuntimeStatus)api.GetComponent(handle, componentTypeId, &value), value);
    }

    internal static RuntimeResult<ComponentHandle> AddComponent(ObjectHandle handle, uint componentTypeId)
    {
        if (api.AddComponent == null) return new(RuntimeStatus.ServiceUnavailable);
        ComponentHandle value = default;
        return new RuntimeResult<ComponentHandle>(
            (RuntimeStatus)api.AddComponent(handle, componentTypeId, &value), value);
    }

    internal static RuntimeResult<ComponentHandle[]> GetComponents(ObjectHandle handle,
        uint componentTypeId)
    {
        if (api.GetComponents == null) return new(RuntimeStatus.ServiceUnavailable);
        int count = 0;
        var status = (RuntimeStatus)api.GetComponents(handle, componentTypeId, null, 0, &count);
        if (status != RuntimeStatus.Ok) return new(status);
        if (count <= 0) return new RuntimeResult<ComponentHandle[]>(RuntimeStatus.Ok,
            Array.Empty<ComponentHandle>());

        var values = new ComponentHandle[count];
        fixed (ComponentHandle* output = values)
        {
            status = (RuntimeStatus)api.GetComponents(handle, componentTypeId,
                output, values.Length, &count);
        }
        if (status != RuntimeStatus.Ok) return new(status);
        if (count != values.Length) Array.Resize(ref values, Math.Clamp(count, 0, values.Length));
        return new RuntimeResult<ComponentHandle[]>(RuntimeStatus.Ok, values);
    }

    internal static RuntimeStatus SetComponentEnabled(ComponentHandle handle, bool enabled)
    {
        if (api.SetComponentEnabled == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.SetComponentEnabled(handle, enabled ? 1 : 0);
    }

    internal static RuntimeResult<bool> IsComponentEnabled(ComponentHandle handle)
    {
        if (api.GetComponentEnabled == null) return new(RuntimeStatus.ServiceUnavailable);
        int value = 0;
        var status = (RuntimeStatus)api.GetComponentEnabled(handle, &value);
        return new RuntimeResult<bool>(status, value != 0);
    }

    internal static RuntimeStatus DestroyGameObject(ObjectHandle handle)
    {
        if (api.DestroyGameObject == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.DestroyGameObject(handle);
    }

    internal static RuntimeStatus DestroyComponent(ComponentHandle handle)
    {
        if (api.DestroyComponent == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.DestroyComponent(handle);
    }

    // ---- v4 Runtime Services -----------------------------------------------

    internal static bool InputAvailable()
    {
        return api.InputAvailable != null && api.InputAvailable() != 0;
    }

    internal static RuntimeResult<bool> InputHeld(string action, int playerSlot)
    {
        if (api.InputHeld == null) return new(RuntimeStatus.ServiceUnavailable);
        int value = 0;
        fixed (byte* text = Encoding.UTF8.GetBytes(action + "\0"))
        {
            var status = (RuntimeStatus)api.InputHeld(text, playerSlot, &value);
            return new RuntimeResult<bool>(status, value != 0);
        }
    }

    internal static RuntimeResult<bool> InputPressed(string action, int playerSlot)
    {
        if (api.InputPressed == null) return new(RuntimeStatus.ServiceUnavailable);
        int value = 0;
        fixed (byte* text = Encoding.UTF8.GetBytes(action + "\0"))
        {
            var status = (RuntimeStatus)api.InputPressed(text, playerSlot, &value);
            return new RuntimeResult<bool>(status, value != 0);
        }
    }

    internal static RuntimeResult<bool> InputReleased(string action, int playerSlot)
    {
        if (api.InputReleased == null) return new(RuntimeStatus.ServiceUnavailable);
        int value = 0;
        fixed (byte* text = Encoding.UTF8.GetBytes(action + "\0"))
        {
            var status = (RuntimeStatus)api.InputReleased(text, playerSlot, &value);
            return new RuntimeResult<bool>(status, value != 0);
        }
    }

    internal static RuntimeResult<float> InputAxis(string axis, int playerSlot)
    {
        if (api.InputAxis == null) return new(RuntimeStatus.ServiceUnavailable);
        float value = 0.0f;
        fixed (byte* text = Encoding.UTF8.GetBytes(axis + "\0"))
        {
            var status = (RuntimeStatus)api.InputAxis(text, playerSlot, &value);
            return new RuntimeResult<float>(status, value);
        }
    }

    internal static RuntimeResult<float> InputPointerDeltaX()
    {
        if (api.InputPointerDeltaX == null) return new(RuntimeStatus.ServiceUnavailable);
        float value = 0.0f;
        var status = (RuntimeStatus)api.InputPointerDeltaX(&value);
        return new RuntimeResult<float>(status, value);
    }

    internal static RuntimeResult<float> InputPointerDeltaY()
    {
        if (api.InputPointerDeltaY == null) return new(RuntimeStatus.ServiceUnavailable);
        float value = 0.0f;
        var status = (RuntimeStatus)api.InputPointerDeltaY(&value);
        return new RuntimeResult<float>(status, value);
    }

    internal static bool AudioAvailable()
    {
        return api.AudioAvailable != null && api.AudioAvailable() != 0;
    }

    internal static RuntimeResult<AudioVoice> PlayAudio(string clipPath, bool loop,
        float volume, float pitch, int spatialMode, Vector3 position,
        float minDistance, float maxDistance)
    {
        if (api.AudioPlay == null) return new(RuntimeStatus.ServiceUnavailable);
        ulong value = 0;
        fixed (byte* clip = Encoding.UTF8.GetBytes(clipPath + "\0"))
        {
            var status = (RuntimeStatus)api.AudioPlay(clip, loop ? 1 : 0, volume, pitch,
                spatialMode, position, minDistance, maxDistance, &value);
            return new RuntimeResult<AudioVoice>(status, new AudioVoice(value));
        }
    }

    internal static RuntimeStatus StopAudio(AudioVoice voice)
    {
        if (api.AudioStop == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.AudioStop(voice.Id);
    }

    internal static RuntimeStatus UpdateAudio(AudioVoice voice, string clipPath, bool loop,
        float volume, float pitch, int spatialMode, Vector3 position,
        float minDistance, float maxDistance)
    {
        if (api.AudioUpdate == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* clip = Encoding.UTF8.GetBytes(clipPath + "\0"))
        {
            return (RuntimeStatus)api.AudioUpdate(voice.Id, clip, loop ? 1 : 0,
                volume, pitch, spatialMode, position, minDistance, maxDistance);
        }
    }

    internal static bool SaveAvailable()
    {
        return api.SaveAvailable != null && api.SaveAvailable() != 0;
    }

    internal static RuntimeStatus SaveSetBool(string slot, string key, bool value)
    {
        if (api.SaveSetBool == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* slotText = Encoding.UTF8.GetBytes(slot + "\0"))
        fixed (byte* keyText = Encoding.UTF8.GetBytes(key + "\0"))
        {
            return (RuntimeStatus)api.SaveSetBool(slotText, keyText, value ? 1 : 0);
        }
    }

    internal static RuntimeStatus SaveSetInt(string slot, string key, long value)
    {
        if (api.SaveSetInt == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* slotText = Encoding.UTF8.GetBytes(slot + "\0"))
        fixed (byte* keyText = Encoding.UTF8.GetBytes(key + "\0"))
        {
            return (RuntimeStatus)api.SaveSetInt(slotText, keyText, value);
        }
    }

    internal static RuntimeStatus SaveSetDouble(string slot, string key, double value)
    {
        if (api.SaveSetDouble == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* slotText = Encoding.UTF8.GetBytes(slot + "\0"))
        fixed (byte* keyText = Encoding.UTF8.GetBytes(key + "\0"))
        {
            return (RuntimeStatus)api.SaveSetDouble(slotText, keyText, value);
        }
    }

    internal static RuntimeStatus SaveSetString(string slot, string key, string value)
    {
        if (api.SaveSetString == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* slotText = Encoding.UTF8.GetBytes(slot + "\0"))
        fixed (byte* keyText = Encoding.UTF8.GetBytes(key + "\0"))
        fixed (byte* valueText = Encoding.UTF8.GetBytes(value + "\0"))
        {
            return (RuntimeStatus)api.SaveSetString(slotText, keyText, valueText);
        }
    }

    internal static RuntimeResult<bool> SaveGetBool(string slot, string key)
    {
        if (api.SaveGetBool == null) return new(RuntimeStatus.ServiceUnavailable);
        int value = 0;
        fixed (byte* slotText = Encoding.UTF8.GetBytes(slot + "\0"))
        fixed (byte* keyText = Encoding.UTF8.GetBytes(key + "\0"))
        {
            var status = (RuntimeStatus)api.SaveGetBool(slotText, keyText, &value);
            return new RuntimeResult<bool>(status, value != 0);
        }
    }

    internal static RuntimeResult<long> SaveGetInt(string slot, string key)
    {
        if (api.SaveGetInt == null) return new(RuntimeStatus.ServiceUnavailable);
        long value = 0;
        fixed (byte* slotText = Encoding.UTF8.GetBytes(slot + "\0"))
        fixed (byte* keyText = Encoding.UTF8.GetBytes(key + "\0"))
        {
            var status = (RuntimeStatus)api.SaveGetInt(slotText, keyText, &value);
            return new RuntimeResult<long>(status, value);
        }
    }

    internal static RuntimeResult<double> SaveGetDouble(string slot, string key)
    {
        if (api.SaveGetDouble == null) return new(RuntimeStatus.ServiceUnavailable);
        double value = 0.0;
        fixed (byte* slotText = Encoding.UTF8.GetBytes(slot + "\0"))
        fixed (byte* keyText = Encoding.UTF8.GetBytes(key + "\0"))
        {
            var status = (RuntimeStatus)api.SaveGetDouble(slotText, keyText, &value);
            return new RuntimeResult<double>(status, value);
        }
    }

    internal static RuntimeResult<string> SaveGetString(string slot, string key)
    {
        if (api.SaveGetString == null) return new(RuntimeStatus.ServiceUnavailable);
        const int capacity = 64 * 1024 + 1;
        var buffer = new byte[capacity];
        fixed (byte* slotText = Encoding.UTF8.GetBytes(slot + "\0"))
        fixed (byte* keyText = Encoding.UTF8.GetBytes(key + "\0"))
        fixed (byte* output = buffer)
        {
            var status = (RuntimeStatus)api.SaveGetString(slotText, keyText, output, capacity);
            if (status != RuntimeStatus.Ok) return new(status);
        }
        int length = Array.IndexOf(buffer, (byte)0);
        if (length < 0) length = buffer.Length;
        return new RuntimeResult<string>(RuntimeStatus.Ok,
            Encoding.UTF8.GetString(buffer, 0, length));
    }

    internal static RuntimeResult<bool> SaveHasKey(string slot, string key)
    {
        if (api.SaveHasKey == null) return new(RuntimeStatus.ServiceUnavailable);
        int value = 0;
        fixed (byte* slotText = Encoding.UTF8.GetBytes(slot + "\0"))
        fixed (byte* keyText = Encoding.UTF8.GetBytes(key + "\0"))
        {
            var status = (RuntimeStatus)api.SaveHasKey(slotText, keyText, &value);
            return new RuntimeResult<bool>(status, value != 0);
        }
    }

    internal static RuntimeStatus SaveDeleteKey(string slot, string key)
    {
        if (api.SaveDeleteKey == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* slotText = Encoding.UTF8.GetBytes(slot + "\0"))
        fixed (byte* keyText = Encoding.UTF8.GetBytes(key + "\0"))
        {
            return (RuntimeStatus)api.SaveDeleteKey(slotText, keyText);
        }
    }

    internal static RuntimeStatus SaveGame(string slot)
    {
        if (api.SaveGame == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(slot + "\0"))
        {
            return (RuntimeStatus)api.SaveGame(text);
        }
    }

    internal static RuntimeStatus LoadGame(string slot)
    {
        if (api.LoadGame == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(slot + "\0"))
        {
            return (RuntimeStatus)api.LoadGame(text);
        }
    }

    internal static RuntimeStatus DeleteSave(string slot)
    {
        if (api.DeleteSave == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(slot + "\0"))
        {
            return (RuntimeStatus)api.DeleteSave(text);
        }
    }

    internal static bool RuntimeUIAvailable()
    {
        return api.RuntimeUIAvailable != null && api.RuntimeUIAvailable() != 0;
    }

    internal static RuntimeResult<ObjectHandle> CreateUIElement(string name, ObjectHandle parent)
    {
        if (api.CreateUIElement == null) return new(RuntimeStatus.ServiceUnavailable);
        ObjectHandle value = default;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
        {
            var status = (RuntimeStatus)api.CreateUIElement(text, parent, &value);
            return new RuntimeResult<ObjectHandle>(status, value);
        }
    }

    internal static RuntimeStatus SetUIText(ObjectHandle handle, string text)
    {
        if (api.SetUIText == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* value = Encoding.UTF8.GetBytes(text + "\0"))
        {
            return (RuntimeStatus)api.SetUIText(handle, value);
        }
    }

    internal static RuntimeResult<string> GetUIText(ObjectHandle handle)
    {
        if (api.GetUIText == null) return new(RuntimeStatus.ServiceUnavailable);
        const int capacity = 64 * 1024 + 1;
        var buffer = new byte[capacity];
        var status = (RuntimeStatus)api.GetUIText(handle, null, 0);
        if (status != RuntimeStatus.InvalidArgument && status != RuntimeStatus.Ok)
            return new(status);
        fixed (byte* output = buffer)
        {
            status = (RuntimeStatus)api.GetUIText(handle, output, capacity);
        }
        if (status != RuntimeStatus.Ok) return new(status);
        int length = Array.IndexOf(buffer, (byte)0);
        if (length < 0) length = buffer.Length;
        return new RuntimeResult<string>(RuntimeStatus.Ok,
            Encoding.UTF8.GetString(buffer, 0, length));
    }

    internal static RuntimeStatus SetUIImageColor(ObjectHandle handle, Color color)
    {
        if (api.SetUIImageColor == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.SetUIImageColor(handle, color);
    }

    internal static RuntimeStatus SetUIRect(ObjectHandle handle, Vector2 position,
        Vector2 size, Vector2 scale, float rotation, int sortOrder)
    {
        if (api.SetUIRect == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.SetUIRect(handle, position, size, scale, rotation, sortOrder);
    }

    internal static RuntimeStatus SetUIButtonInteractable(ObjectHandle handle, bool interactable)
    {
        if (api.SetUIButtonInteractable == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.SetUIButtonInteractable(handle, interactable ? 1 : 0);
    }

    internal static RuntimeResult<ObjectHandle> Instantiate(string guid, Vector3 position, Vector3 rotationEuler, Vector3 scale, ObjectHandle parent)
    {
        if (api.Instantiate == null) return new(RuntimeStatus.ServiceUnavailable);
        ObjectHandle value = default;
        fixed (byte* guidUtf8 = Encoding.UTF8.GetBytes(guid + "\0"))
        {
            return new RuntimeResult<ObjectHandle>(
                (RuntimeStatus)api.Instantiate(guidUtf8, position, rotationEuler, scale, parent, &value),
                value);
        }
    }

    internal static RuntimeStatus LoadScene(string sceneAssetGuid)
    {
        if (api.LoadScene == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* guidUtf8 = Encoding.UTF8.GetBytes(sceneAssetGuid + "\0"))
        {
            return (RuntimeStatus)api.LoadScene(guidUtf8);
        }
    }

    internal static RuntimeStatus ReloadScene()
    {
        if (api.ReloadScene == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.ReloadScene();
    }

    internal static RuntimeStatus ReturnToPreviousScene()
    {
        if (api.ReturnToPreviousScene == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.ReturnToPreviousScene();
    }

    internal static RuntimeStatus TriggerSceneFlow(string eventName)
    {
        if (api.TriggerSceneFlow == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(eventName + "\0"))
        {
            return (RuntimeStatus)api.TriggerSceneFlow(text);
        }
    }

    internal static RuntimeStatus SetSceneFlowBool(string key, bool value)
    {
        if (api.SetSceneFlowBool == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(key + "\0"))
        {
            return (RuntimeStatus)api.SetSceneFlowBool(text, value ? 1 : 0);
        }
    }

    internal static RuntimeStatus SetSceneFlowInt(string key, long value)
    {
        if (api.SetSceneFlowInt == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(key + "\0"))
        {
            return (RuntimeStatus)api.SetSceneFlowInt(text, value);
        }
    }

    internal static RuntimeStatus SetSceneFlowFloat(string key, double value)
    {
        if (api.SetSceneFlowFloat == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(key + "\0"))
        {
            return (RuntimeStatus)api.SetSceneFlowFloat(text, value);
        }
    }

    internal static RuntimeResult<RaycastHit> Raycast(Vector3 origin, Vector3 direction,
        float maxDistance, int layer, int mask, ObjectHandle ignore)
    {
        if (api.Raycast == null) return new(RuntimeStatus.ServiceUnavailable);
        RaycastHit hit = default;
        var status = (RuntimeStatus)api.Raycast(origin, direction, maxDistance, layer, mask, ignore, &hit);
        return new RuntimeResult<RaycastHit>(status, hit);
    }

    internal static RuntimeResult<ComponentHandle> FindMotionPlayer(ObjectHandle owner, string key)
    {
        if (api.FindMotionPlayer == null) return new(RuntimeStatus.ServiceUnavailable);
        ComponentHandle value = default;
        fixed (byte* text = Encoding.UTF8.GetBytes(key + "\0"))
        {
            return new RuntimeResult<ComponentHandle>(
                (RuntimeStatus)api.FindMotionPlayer(owner, text, &value),
                value);
        }
    }

    internal static RuntimeStatus MotionPlay(ComponentHandle player)
    {
        if (api.MotionPlay == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.MotionPlay(player);
    }

    internal static RuntimeStatus MotionPlayFrom(ComponentHandle player, float seconds)
    {
        if (api.MotionPlayFrom == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.MotionPlayFrom(player, seconds);
    }

    internal static RuntimeStatus MotionPause(ComponentHandle player)
    {
        if (api.MotionPause == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.MotionPause(player);
    }

    internal static RuntimeStatus MotionResume(ComponentHandle player)
    {
        if (api.MotionResume == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.MotionResume(player);
    }

    internal static RuntimeStatus MotionStop(ComponentHandle player)
    {
        if (api.MotionStop == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.MotionStop(player);
    }

    internal static RuntimeStatus MotionReverse(ComponentHandle player)
    {
        if (api.MotionReverse == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.MotionReverse(player);
    }

    internal static RuntimeStatus MotionSetTime(ComponentHandle player, float seconds)
    {
        if (api.MotionSetTime == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.MotionSetTime(player, seconds);
    }

    internal static RuntimeStatus MotionSetSpeed(ComponentHandle player, float speed)
    {
        if (api.MotionSetSpeed == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.MotionSetSpeed(player, speed);
    }

    internal static RuntimeStatus MotionSetWeight(ComponentHandle player, float weight)
    {
        if (api.MotionSetWeight == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.MotionSetWeight(player, weight);
    }

    internal static RuntimeResult<bool> MotionIsPlaying(ComponentHandle player)
    {
        if (api.MotionIsPlaying == null) return new(RuntimeStatus.ServiceUnavailable);
        int value = 0;
        var status = (RuntimeStatus)api.MotionIsPlaying(player, &value);
        return new RuntimeResult<bool>(status, value != 0);
    }

    internal static RuntimeResult<float> MotionGetTime(ComponentHandle player)
    {
        if (api.MotionGetTime == null) return new(RuntimeStatus.ServiceUnavailable);
        float value = 0.0f;
        return new RuntimeResult<float>(
            (RuntimeStatus)api.MotionGetTime(player, &value), value);
    }

    internal static RuntimeResult<float> MotionGetDuration(ComponentHandle player)
    {
        if (api.MotionGetDuration == null) return new(RuntimeStatus.ServiceUnavailable);
        float value = 0.0f;
        return new RuntimeResult<float>(
            (RuntimeStatus)api.MotionGetDuration(player, &value), value);
    }

    internal static RuntimeResult<EventSubscription> SubscribeEvent(string eventTypeGuid, ObjectHandle owner)
    {
        if (api.SubscribeEvent == null) return new(RuntimeStatus.ServiceUnavailable);
        if (!TryParseGuidText(eventTypeGuid, out var guid)) return new(RuntimeStatus.InvalidArgument);

        ulong id = 0;
        var status = (RuntimeStatus)api.SubscribeEvent(guid.High, guid.Low, owner, &id);
        return new RuntimeResult<EventSubscription>(status, new EventSubscription(id));
    }

    internal static RuntimeStatus UnsubscribeEvent(EventSubscription subscription)
    {
        if (!subscription.IsValid) return RuntimeStatus.InvalidHandle;
        if (api.UnsubscribeEvent == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.UnsubscribeEvent(subscription.Id);
    }

    internal static RuntimeResult<RuntimeEvent> PollEvent(EventSubscription subscription)
    {
        if (!subscription.IsValid) return new(RuntimeStatus.InvalidHandle);

        if (api.PollEventWithPayload != null)
        {
            int requiredCapacity = 0;
            var payloadStatus = (RuntimeStatus)api.PollEventWithPayload(
                subscription.Id, null, 0, &requiredCapacity);
            if (payloadStatus != RuntimeStatus.Ok) return new(payloadStatus);
            if (requiredCapacity <= 0) return new(RuntimeStatus.Ok);

            var payloadBuffer = new byte[requiredCapacity];
            fixed (byte* output = payloadBuffer)
            {
                payloadStatus = (RuntimeStatus)api.PollEventWithPayload(
                    subscription.Id, output, payloadBuffer.Length, &requiredCapacity);
                if (payloadStatus != RuntimeStatus.Ok) return new(payloadStatus);
                var payloadText = FromUtf8(output);
                if (string.IsNullOrEmpty(payloadText)) return new(RuntimeStatus.Ok);
                return new RuntimeResult<RuntimeEvent>(RuntimeStatus.Ok,
                    ParseRuntimeEvent(payloadText));
            }
        }
        if (api.PollEvent == null) return new(RuntimeStatus.ServiceUnavailable);

        const int capacity = 4096;
        byte* buffer = stackalloc byte[capacity];
        var status = (RuntimeStatus)api.PollEvent(subscription.Id, buffer, capacity);
        if (status != RuntimeStatus.Ok) return new(status);

        var text = FromUtf8(buffer);
        if (string.IsNullOrEmpty(text)) return new(RuntimeStatus.Ok);
        return new RuntimeResult<RuntimeEvent>(RuntimeStatus.Ok, ParseRuntimeEvent(text));
    }

    internal static RuntimeResult<bool> GetScriptFieldBool(ComponentHandle component, string fieldName)
    {
        if (api.GetScriptBool == null) return new(RuntimeStatus.ServiceUnavailable);
        int value = 0;
        fixed (byte* field = Encoding.UTF8.GetBytes(fieldName + "\0"))
        {
            var status = (RuntimeStatus)api.GetScriptBool(component, field, &value);
            return new RuntimeResult<bool>(status, value != 0);
        }
    }

    internal static RuntimeStatus SetScriptFieldBool(ComponentHandle component, string fieldName, bool value)
    {
        if (api.SetScriptBool == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* field = Encoding.UTF8.GetBytes(fieldName + "\0"))
            return (RuntimeStatus)api.SetScriptBool(component, field, value ? 1 : 0);
    }

    internal static RuntimeResult<int> GetScriptFieldInt(ComponentHandle component, string fieldName)
    {
        if (api.GetScriptInt == null) return new(RuntimeStatus.ServiceUnavailable);
        int value = 0;
        fixed (byte* field = Encoding.UTF8.GetBytes(fieldName + "\0"))
        {
            var status = (RuntimeStatus)api.GetScriptInt(component, field, &value);
            return new RuntimeResult<int>(status, value);
        }
    }

    internal static RuntimeStatus SetScriptFieldInt(ComponentHandle component, string fieldName, int value)
    {
        if (api.SetScriptInt == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* field = Encoding.UTF8.GetBytes(fieldName + "\0"))
            return (RuntimeStatus)api.SetScriptInt(component, field, value);
    }

    internal static RuntimeResult<double> GetScriptFieldDouble(ComponentHandle component, string fieldName)
    {
        if (api.GetScriptDouble == null) return new(RuntimeStatus.ServiceUnavailable);
        double value = 0.0;
        fixed (byte* field = Encoding.UTF8.GetBytes(fieldName + "\0"))
        {
            var status = (RuntimeStatus)api.GetScriptDouble(component, field, &value);
            return new RuntimeResult<double>(status, value);
        }
    }

    internal static RuntimeStatus SetScriptFieldDouble(ComponentHandle component, string fieldName, double value)
    {
        if (api.SetScriptDouble == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* field = Encoding.UTF8.GetBytes(fieldName + "\0"))
            return (RuntimeStatus)api.SetScriptDouble(component, field, value);
    }

    internal static RuntimeResult<string> GetScriptFieldString(ComponentHandle component, string fieldName)
    {
        if (api.GetScriptString == null) return new(RuntimeStatus.ServiceUnavailable);
        const int capacity = 4096;
        byte* output = stackalloc byte[capacity];
        fixed (byte* field = Encoding.UTF8.GetBytes(fieldName + "\0"))
        {
            var status = (RuntimeStatus)api.GetScriptString(component, field, output, capacity);
            return status == RuntimeStatus.Ok
                ? new RuntimeResult<string>(status, FromUtf8(output))
                : new RuntimeResult<string>(status);
        }
    }

    internal static RuntimeStatus SetScriptFieldString(ComponentHandle component, string fieldName, string value)
    {
        if (api.SetScriptString == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* field = Encoding.UTF8.GetBytes(fieldName + "\0"))
        fixed (byte* text = Encoding.UTF8.GetBytes(value + "\0"))
            return (RuntimeStatus)api.SetScriptString(component, field, text);
    }

    internal static RuntimeResult<ObjectHandle> GetUIFocus()
    {
        if (api.UIGetFocus == null) return new(RuntimeStatus.ServiceUnavailable);
        ObjectHandle value = default;
        return new RuntimeResult<ObjectHandle>((RuntimeStatus)api.UIGetFocus(&value), value);
    }

    internal static RuntimeStatus SetUIFocus(ObjectHandle target)
    {
        if (api.UISetFocus == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.UISetFocus(target);
    }

    internal static RuntimeResult<ObjectHandle> FindUIFocus(ObjectHandle from, UIFocusDirection direction)
    {
        if (api.UIFindFocus == null) return new(RuntimeStatus.ServiceUnavailable);
        ObjectHandle value = default;
        var status = (RuntimeStatus)api.UIFindFocus(from, (int)direction, &value);
        return new RuntimeResult<ObjectHandle>(status, value);
    }

    internal static RuntimeStatus PublishEvent(string eventTypeGuid, string typeName,
        ObjectHandle source, ObjectHandle target)
    {
        if (api.PublishEvent == null) return RuntimeStatus.ServiceUnavailable;
        if (!TryParseGuidText(eventTypeGuid, out var guid)) return RuntimeStatus.InvalidArgument;
        fixed (byte* name = Encoding.UTF8.GetBytes(typeName + "\0"))
            return (RuntimeStatus)api.PublishEvent(guid.High, guid.Low, name, source, target);
    }

    private static IEnumerable<Type> DiscoverBehaviourTypes(Assembly assembly)
    {
        return assembly.GetTypes().Where(type =>
            !type.IsAbstract &&
            typeof(ScriptBehaviour).IsAssignableFrom(type) &&
            type.GetCustomAttribute<ReplayGuidAttribute>() != null);
    }

    private static RuntimeEvent ParseRuntimeEvent(string text)
    {
        var typeGuid = string.Empty;
        var typeName = string.Empty;
        ulong frame = 0;
        ulong sourceWorld = 0;
        ulong sourceObject = 0;
        uint sourceGeneration = 0;
        ulong targetWorld = 0;
        ulong targetObject = 0;
        uint targetGeneration = 0;

        var payload = new RuntimeEventPayload();

        foreach (var rawLine in text.Split('\n'))
        {
            if (string.IsNullOrWhiteSpace(rawLine)) continue;
            var separator = rawLine.IndexOf('=');
            if (separator <= 0) continue;

            var key = rawLine[..separator];
            var value = UnescapeEventValue(rawLine[(separator + 1)..]);

            if (key == "payload")
            {
                payload.TryAddEncoded(value);
                continue;
            }
            switch (key)
            {
                case "type": typeGuid = value; break;
                case "name": typeName = value; break;
                case "frame": ulong.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out frame); break;
                case "source_world": ulong.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out sourceWorld); break;
                case "source_object": ulong.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out sourceObject); break;
                case "source_generation": uint.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out sourceGeneration); break;
                case "target_world": ulong.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out targetWorld); break;
                case "target_object": ulong.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out targetObject); break;
                case "target_generation": uint.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out targetGeneration); break;
            }
        }

        var source = new ObjectHandle
        {
            World = sourceWorld,
            Object = sourceObject,
            Generation = sourceGeneration,
        };
        var target = new ObjectHandle
        {
            World = targetWorld,
            Object = targetObject,
            Generation = targetGeneration,
        };

        pendingParsedEventPayload = payload;
        return new RuntimeEvent(typeGuid, typeName, source, target, frame);
    }

    private static string UnescapeEventValue(string text)
    {
        var builder = new StringBuilder(text.Length);
        var escaped = false;
        foreach (var c in text)
        {
            if (!escaped && c == '\\')
            {
                escaped = true;
                continue;
            }

            if (escaped)
            {
                builder.Append(c switch
                {
                    'n' => '\n',
                    'r' => '\r',
                    '\\' => '\\',
                    '=' => '=',
                    _ => c,
                });
                escaped = false;
            }
            else
            {
                builder.Append(c);
            }
        }
        if (escaped) builder.Append('\\');
        return builder.ToString();
    }

    private sealed class ScriptLoadContext : AssemblyLoadContext
    {
        private readonly AssemblyDependencyResolver resolver;

        public ScriptLoadContext(string assemblyPath)
            : base("RePlayEngine.CSharpScripts", isCollectible: true)
        {
            resolver = new AssemblyDependencyResolver(assemblyPath);
        }

        protected override Assembly? Load(AssemblyName assemblyName)
        {
            if (assemblyName.Name == typeof(ScriptBehaviour).Assembly.GetName().Name)
            {
                return typeof(ScriptBehaviour).Assembly;
            }

            var resolved = resolver.ResolveAssemblyToPath(assemblyName);
            return resolved != null ? LoadFromAssemblyPath(resolved) : null;
        }
    }

    private static TypeGuid? ReadGuid(Type type)
    {
        var text = type.GetCustomAttribute<ReplayGuidAttribute>()?.Value;
        if (string.IsNullOrWhiteSpace(text)) return null;
        return TryParseGuidText(text, out var guid) ? guid : null;
    }

    private static bool TryParseGuidText(string text, out TypeGuid guid)
    {
        guid = default;
        if (string.IsNullOrWhiteSpace(text)) return false;

        Span<char> digits = stackalloc char[32];
        var count = 0;
        foreach (var c in text)
        {
            if (c == '-' || c == '{' || c == '}') continue;
            if (!Uri.IsHexDigit(c) || count >= digits.Length) return false;
            digits[count++] = char.ToLowerInvariant(c);
        }

        if (count != 32) return false;
        var high = ulong.Parse(new string(digits[..16]), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
        var low = ulong.Parse(new string(digits[16..]), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
        guid = new TypeGuid { High = high, Low = low };
        return true;
    }

    private static string BuildSchema(Type type)
    {
        var builder = new StringBuilder();
        foreach (var field in SerializableFields(type))
        {
            var mapped = MapFieldType(field.FieldType);
            if (mapped == null) continue;

            var defaultValue = DefaultValue(type, field);
            builder.Append("FIELD\t");
            builder.Append(Escape(field.Name));
            builder.Append('\t');
            builder.Append(mapped);
            builder.Append('\t');
            builder.Append(Escape(Humanize(field.Name)));
            builder.Append('\t');
            builder.Append(Escape(string.Empty));
            builder.Append('\t');
            builder.Append(Escape(FormatValue(defaultValue, field.FieldType)));
            builder.Append('\n');
        }

        return builder.ToString();
    }

    private static IEnumerable<FieldInfo> SerializableFields(Type type)
    {
        const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
        return type.GetFields(flags)
            .Where(field =>
                !field.IsStatic &&
                !field.IsInitOnly &&
                !field.IsDefined(typeof(NonSerializedAttribute), true) &&
                (field.IsPublic || field.IsDefined(typeof(SerializeFieldAttribute), true)))
            .OrderBy(field => field.MetadataToken);
    }

    private static FieldInfo? FindSerializableField(Type type, string fieldName)
    {
        return SerializableFields(type).FirstOrDefault(field => field.Name == fieldName);
    }

    private static object? DefaultValue(Type type, FieldInfo field)
    {
        try
        {
            var instance = Activator.CreateInstance(type);
            return field.GetValue(instance);
        }
        catch
        {
            return field.FieldType.IsValueType ? Activator.CreateInstance(field.FieldType) : null;
        }
    }

    private static string? MapFieldType(Type type)
    {
        if (type == typeof(bool)) return "bool";
        if (type == typeof(int)) return "int";
        if (type == typeof(long)) return "int64";
        if (type == typeof(ulong)) return "uint64";
        if (type == typeof(float)) return "float";
        if (type == typeof(double)) return "double";
        if (type == typeof(string)) return "string";
        if (type == typeof(Vector2)) return "vector2";
        if (type == typeof(Vector3)) return "vector3";
        if (type == typeof(Vector4)) return "vector4";
        if (type == typeof(Quaternion)) return "quaternion";
        if (type == typeof(Color)) return "color";
        if (type == typeof(ObjectReference)) return "object";
        if (type == typeof(ComponentReference)) return "component";
        return null;
    }

    private static object? ParseValue(Type type, string text)
    {
        var parts = text.Split(',');
        if (type == typeof(bool)) return text == "1" || text.Equals("true", StringComparison.OrdinalIgnoreCase);
        if (type == typeof(int)) return int.Parse(text, CultureInfo.InvariantCulture);
        if (type == typeof(long)) return long.Parse(text, CultureInfo.InvariantCulture);
        if (type == typeof(ulong)) return ulong.Parse(text, CultureInfo.InvariantCulture);
        if (type == typeof(float)) return float.Parse(text, CultureInfo.InvariantCulture);
        if (type == typeof(double)) return double.Parse(text, CultureInfo.InvariantCulture);
        if (type == typeof(string)) return text;
        if (type == typeof(Vector2)) return new Vector2(ParseFloat(parts, 0), ParseFloat(parts, 1));
        if (type == typeof(Vector3)) return new Vector3(ParseFloat(parts, 0), ParseFloat(parts, 1), ParseFloat(parts, 2));
        if (type == typeof(Vector4)) return new Vector4(ParseFloat(parts, 0), ParseFloat(parts, 1), ParseFloat(parts, 2), ParseFloat(parts, 3));
        if (type == typeof(Quaternion)) return new Quaternion(ParseFloat(parts, 0), ParseFloat(parts, 1), ParseFloat(parts, 2), ParseFloat(parts, 3));
        if (type == typeof(Color)) return new Color(ParseFloat(parts, 0), ParseFloat(parts, 1), ParseFloat(parts, 2), ParseFloat(parts, 3));
        if (type == typeof(ObjectReference)) return new ObjectReference { ObjectId = ParseULong(parts, 0) };
        if (type == typeof(ComponentReference)) return new ComponentReference { OwnerObjectId = ParseULong(parts, 0), ComponentStableId = (uint)ParseULong(parts, 1) };
        return type.IsValueType ? Activator.CreateInstance(type) : null;
    }

    private static string FormatValue(object? value, Type type)
    {
        if (value == null) return string.Empty;
        if (type == typeof(bool)) return ((bool)value) ? "true" : "false";
        if (type == typeof(int)) return ((int)value).ToString(CultureInfo.InvariantCulture);
        if (type == typeof(long)) return ((long)value).ToString(CultureInfo.InvariantCulture);
        if (type == typeof(ulong)) return ((ulong)value).ToString(CultureInfo.InvariantCulture);
        if (type == typeof(float)) return ((float)value).ToString("R", CultureInfo.InvariantCulture);
        if (type == typeof(double)) return ((double)value).ToString("R", CultureInfo.InvariantCulture);
        if (type == typeof(string)) return (string)value;
        if (type == typeof(Vector2))
        {
            var v = (Vector2)value;
            return $"{F(v.X)},{F(v.Y)}";
        }
        if (type == typeof(Vector3))
        {
            var v = (Vector3)value;
            return $"{F(v.X)},{F(v.Y)},{F(v.Z)}";
        }
        if (type == typeof(Vector4))
        {
            var v = (Vector4)value;
            return $"{F(v.X)},{F(v.Y)},{F(v.Z)},{F(v.W)}";
        }
        if (type == typeof(Quaternion))
        {
            var v = (Quaternion)value;
            return $"{F(v.X)},{F(v.Y)},{F(v.Z)},{F(v.W)}";
        }
        if (type == typeof(Color))
        {
            var v = (Color)value;
            return $"{F(v.R)},{F(v.G)},{F(v.B)},{F(v.A)}";
        }
        if (type == typeof(ObjectReference)) return ((ObjectReference)value).ObjectId.ToString(CultureInfo.InvariantCulture);
        if (type == typeof(ComponentReference))
        {
            var reference = (ComponentReference)value;
            return $"{reference.OwnerObjectId.ToString(CultureInfo.InvariantCulture)},{reference.ComponentStableId.ToString(CultureInfo.InvariantCulture)}";
        }

        return string.Empty;
    }

    private static string F(float value) => value.ToString("R", CultureInfo.InvariantCulture);
    private static float ParseFloat(string[] parts, int index) => index < parts.Length ? float.Parse(parts[index], CultureInfo.InvariantCulture) : 0.0f;
    private static ulong ParseULong(string[] parts, int index) => index < parts.Length ? ulong.Parse(parts[index], CultureInfo.InvariantCulture) : 0UL;

    private static string Humanize(string name)
    {
        if (string.IsNullOrEmpty(name)) return name;

        var builder = new StringBuilder();
        for (var i = 0; i < name.Length; ++i)
        {
            var c = name[i];
            if (i > 0 && char.IsUpper(c) && (char.IsLower(name[i - 1]) || char.IsDigit(name[i - 1])))
            {
                builder.Append(' ');
            }
            else if (c == '_' || c == '-')
            {
                builder.Append(' ');
                continue;
            }

            builder.Append(builder.Length == 0 ? char.ToUpperInvariant(c) : c);
        }

        return builder.ToString();
    }

    private static void UnloadScriptContext()
    {
        scriptAssembly = null;
        if (scriptContext == null) return;
        scriptContext.Unload();
        scriptContext = null;
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
    }

    private static int Fail(Exception ex, byte* output, int outputCapacity)
    {
        SetLastError(ex);
        WriteUtf8(lastError, output, outputCapacity);
        return 0;
    }

    private static int Fail(string message, byte* output, int outputCapacity)
    {
        SetLastError(message);
        WriteUtf8(message, output, outputCapacity);
        return 0;
    }

    private static void SetLastError(Exception ex)
    {
        lastError = ex.Message;
        lastErrorFile = string.Empty;
        lastErrorLine = 0;
    }

    private static void SetLastError(string message)
    {
        lastError = message;
        lastErrorFile = string.Empty;
        lastErrorLine = 0;
    }

    private static void ClearLastError()
    {
        lastError = string.Empty;
        lastErrorFile = string.Empty;
        lastErrorLine = 0;
    }

    private static string StripFieldPrefix(string savedName)
    {
        const string prefix = "field.";
        return savedName.StartsWith(prefix, StringComparison.Ordinal) ? savedName[prefix.Length..] : savedName;
    }

    private static string FromUtf8(byte* text)
    {
        if (text == null) return string.Empty;
        var length = 0;
        while (text[length] != 0) ++length;
        return Encoding.UTF8.GetString(text, length);
    }

    private static void WriteUtf8(string text, byte* output, int capacity)
    {
        if (output == null || capacity <= 0) return;
        var bytes = Encoding.UTF8.GetBytes(text);
        var count = Math.Min(bytes.Length, capacity - 1);
        for (var i = 0; i < count; ++i) output[i] = bytes[i];
        output[count] = 0;
    }

    private static string Escape(string text)
    {
        var builder = new StringBuilder(text.Length);
        foreach (var c in text)
        {
            if (c == '%' || c == '\t' || c == '\r' || c == '\n')
            {
                builder.Append('%');
                builder.Append(((int)c).ToString("X2", CultureInfo.InvariantCulture));
            }
            else
            {
                builder.Append(c);
            }
        }

        return builder.ToString();
    }

    // ---- v6 Object / hierarchy / log ---------------------------------------

    internal static RuntimeStatus LogInfo(string message, ObjectHandle source)
    {
        if (api.LogInfo == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(message + "\0"))
            return (RuntimeStatus)api.LogInfo(text, source);
    }

    internal static RuntimeStatus LogWarning(string message, ObjectHandle source)
    {
        if (api.LogWarning == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(message + "\0"))
            return (RuntimeStatus)api.LogWarning(text, source);
    }

    internal static RuntimeStatus LogError(string message, ObjectHandle source)
    {
        if (api.LogError == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(message + "\0"))
            return (RuntimeStatus)api.LogError(text, source);
    }

    internal static RuntimeResult<ObjectHandle> CreateGameObject(string name)
    {
        if (api.CreateGameObject == null) return new(RuntimeStatus.ServiceUnavailable);
        ObjectHandle value = default;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
        {
            var status = (RuntimeStatus)api.CreateGameObject(text, &value);
            return new RuntimeResult<ObjectHandle>(status, value);
        }
    }

    internal static RuntimeResult<Vector3> GetWorldPosition(ObjectHandle handle)
    {
        if (api.GetWorldPosition == null) return new(RuntimeStatus.ServiceUnavailable);
        Vector3 value = default;
        var status = (RuntimeStatus)api.GetWorldPosition(handle, &value);
        return new RuntimeResult<Vector3>(status, value);
    }

    internal static RuntimeStatus SetParent(ObjectHandle child, ObjectHandle parent,
        bool preserveWorldTransform)
    {
        if (api.SetParent == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.SetParent(child, parent, preserveWorldTransform ? 1 : 0);
    }

    internal static RuntimeResult<ObjectHandle> GetParent(ObjectHandle handle)
    {
        if (api.GetParent == null) return new(RuntimeStatus.ServiceUnavailable);
        ObjectHandle value = default;
        var status = (RuntimeStatus)api.GetParent(handle, &value);
        return new RuntimeResult<ObjectHandle>(status, value);
    }

    internal static RuntimeResult<ObjectHandle[]> GetChildren(ObjectHandle handle)
    {
        if (api.GetChildren == null) return new(RuntimeStatus.ServiceUnavailable);
        int count = 0;
        var status = (RuntimeStatus)api.GetChildren(handle, null, 0, &count);
        if (status != RuntimeStatus.Ok) return new(status);
        if (count <= 0) return new RuntimeResult<ObjectHandle[]>(RuntimeStatus.Ok,
            Array.Empty<ObjectHandle>());

        var values = new ObjectHandle[count];
        fixed (ObjectHandle* output = values)
        {
            status = (RuntimeStatus)api.GetChildren(handle, output, values.Length, &count);
        }
        if (status != RuntimeStatus.Ok) return new(status);
        if (count != values.Length) Array.Resize(ref values, Math.Clamp(count, 0, values.Length));
        return new RuntimeResult<ObjectHandle[]>(RuntimeStatus.Ok, values);
    }

    internal static RuntimeResult<string> GetName(ObjectHandle handle)
    {
        if (api.GetName == null) return new(RuntimeStatus.ServiceUnavailable);
        const int capacity = 4096;
        byte* output = stackalloc byte[capacity];
        var status = (RuntimeStatus)api.GetName(handle, output, capacity);
        return status == RuntimeStatus.Ok
            ? new RuntimeResult<string>(status, FromUtf8(output))
            : new RuntimeResult<string>(status);
    }

    internal static RuntimeStatus SetName(ObjectHandle handle, string name)
    {
        if (api.SetName == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
            return (RuntimeStatus)api.SetName(handle, text);
    }

    internal static RuntimeResult<bool> IsGameObjectEnabled(ObjectHandle handle)
    {
        if (api.GetGameObjectEnabled == null) return new(RuntimeStatus.ServiceUnavailable);
        int value = 0;
        var status = (RuntimeStatus)api.GetGameObjectEnabled(handle, &value);
        return new RuntimeResult<bool>(status, value != 0);
    }

    internal static RuntimeStatus SetGameObjectEnabled(ObjectHandle handle, bool enabled)
    {
        if (api.SetGameObjectEnabled == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.SetGameObjectEnabled(handle, enabled ? 1 : 0);
    }


    // ---- v7 Physics / deferred / runtime state -----------------------------

    internal static RuntimeResult<GroundHit> QueryGround(Vector3 origin, float radius,
        float upOffset, float downDistance, float walkableNormalY, ObjectHandle ignore)
    {
        if (api.QueryGround == null) return new(RuntimeStatus.ServiceUnavailable);
        GroundHit value = default;
        var status = (RuntimeStatus)api.QueryGround(origin, radius, upOffset,
            downDistance, walkableNormalY, ignore, &value);
        return new RuntimeResult<GroundHit>(status, value);
    }

    internal static RuntimeResult<SphereSweepHit> SweepSphere(Vector3 start, Vector3 end,
        float radius, float maximumNormalY, ObjectHandle ignore)
    {
        if (api.SweepSphere == null) return new(RuntimeStatus.ServiceUnavailable);
        SphereSweepHit value = default;
        var status = (RuntimeStatus)api.SweepSphere(start, end, radius,
            maximumNormalY, ignore, &value);
        return new RuntimeResult<SphereSweepHit>(status, value);
    }

    internal static RuntimeStatus InstantiatePrefabDeferred(string guid, Vector3 position,
        Vector3 rotationEuler, Vector3 scale, ObjectHandle parent)
    {
        if (api.InstantiatePrefabDeferred == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(guid + "\0"))
            return (RuntimeStatus)api.InstantiatePrefabDeferred(
                text, position, rotationEuler, scale, parent);
    }

    internal static RuntimeStatus FlushDeferredOperations()
    {
        if (api.FlushDeferredOperations == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.FlushDeferredOperations();
    }

    internal static RuntimeResult<ulong> PendingDeferredOperationCount()
    {
        if (api.PendingDeferredOperationCount == null)
            return new(RuntimeStatus.ServiceUnavailable);
        ulong value = 0;
        var status = (RuntimeStatus)api.PendingDeferredOperationCount(&value);
        return new RuntimeResult<ulong>(status, value);
    }

    internal static RuntimeResult<bool> HasComponent(ObjectHandle handle, uint componentTypeId)
    {
        if (api.HasComponent == null) return new(RuntimeStatus.ServiceUnavailable);
        int value = 0;
        var status = (RuntimeStatus)api.HasComponent(handle, componentTypeId, &value);
        return new RuntimeResult<bool>(status, value != 0);
    }

    internal static RuntimeResult<float> TimeScale()
    {
        if (api.GetTimeScale == null) return new(RuntimeStatus.ServiceUnavailable);
        float value = 0.0f;
        var status = (RuntimeStatus)api.GetTimeScale(&value);
        return new RuntimeResult<float>(status, value);
    }

    internal static RuntimeResult<bool> SceneTransitionInProgress()
    {
        if (api.GetSceneTransitionInProgress == null)
            return new(RuntimeStatus.ServiceUnavailable);
        int value = 0;
        var status = (RuntimeStatus)api.GetSceneTransitionInProgress(&value);
        return new RuntimeResult<bool>(status, value != 0);
    }

    internal static bool PhysicsAvailable()
        => api.PhysicsAvailable != null && api.PhysicsAvailable() != 0;

    internal static bool SceneFlowAvailable()
        => api.SceneFlowAvailable != null && api.SceneFlowAvailable() != 0;


    internal static RuntimeEventPayload ConsumeParsedEventPayload()
    {
        var payload = pendingParsedEventPayload ?? new RuntimeEventPayload();
        pendingParsedEventPayload = null;
        return payload;
    }

    internal static RuntimeStatus PublishEvent(string eventTypeGuid, RuntimeEventPayload payload,
        string typeName, ObjectHandle source, ObjectHandle target)
    {
        if (api.PublishEventWithPayload == null) return RuntimeStatus.ServiceUnavailable;
        if (!TryParseGuidText(eventTypeGuid, out var guid)) return RuntimeStatus.InvalidArgument;
        var encodedPayload = payload.Serialize();
        fixed (byte* name = Encoding.UTF8.GetBytes(typeName + "\0"))
        fixed (byte* encoded = Encoding.UTF8.GetBytes(encodedPayload + "\0"))
            return (RuntimeStatus)api.PublishEventWithPayload(
                guid.High, guid.Low, name, source, target, encoded);
    }

    // ---- v10 Component 型・汎用プロパティ・World Transform・Rigidbody ----------

    internal static RuntimeResult<uint> ComponentTypeId(string typeName)
    {
        if (api.ComponentTypeId == null) return new(RuntimeStatus.ServiceUnavailable);
        uint value = 0;
        fixed (byte* text = Encoding.UTF8.GetBytes(typeName + "\0"))
            return new RuntimeResult<uint>((RuntimeStatus)api.ComponentTypeId(text, &value), value);
    }

    internal static RuntimeResult<string> GetComponentTypeName(ComponentHandle handle)
    {
        if (api.GetComponentTypeName == null) return new(RuntimeStatus.ServiceUnavailable);
        const int capacity = 256;
        byte* output = stackalloc byte[capacity];
        var status = (RuntimeStatus)api.GetComponentTypeName(handle, output, capacity);
        return status == RuntimeStatus.Ok
            ? new RuntimeResult<string>(status, FromUtf8(output))
            : new RuntimeResult<string>(status);
    }

    internal static RuntimeResult<bool> GetPropertyBool(ComponentHandle handle, string name)
    {
        if (api.GetPropertyBool == null) return new(RuntimeStatus.ServiceUnavailable);
        int value = 0;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
            return new RuntimeResult<bool>(
                (RuntimeStatus)api.GetPropertyBool(handle, text, &value), value != 0);
    }

    internal static RuntimeStatus SetPropertyBool(ComponentHandle handle, string name, bool value)
    {
        if (api.SetPropertyBool == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
            return (RuntimeStatus)api.SetPropertyBool(handle, text, value ? 1 : 0);
    }

    internal static RuntimeResult<long> GetPropertyInt(ComponentHandle handle, string name)
    {
        if (api.GetPropertyInt == null) return new(RuntimeStatus.ServiceUnavailable);
        long value = 0;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
            return new RuntimeResult<long>(
                (RuntimeStatus)api.GetPropertyInt(handle, text, &value), value);
    }

    internal static RuntimeStatus SetPropertyInt(ComponentHandle handle, string name, long value)
    {
        if (api.SetPropertyInt == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
            return (RuntimeStatus)api.SetPropertyInt(handle, text, value);
    }

    internal static RuntimeResult<double> GetPropertyDouble(ComponentHandle handle, string name)
    {
        if (api.GetPropertyDouble == null) return new(RuntimeStatus.ServiceUnavailable);
        double value = 0.0;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
            return new RuntimeResult<double>(
                (RuntimeStatus)api.GetPropertyDouble(handle, text, &value), value);
    }

    internal static RuntimeStatus SetPropertyDouble(ComponentHandle handle, string name, double value)
    {
        if (api.SetPropertyDouble == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
            return (RuntimeStatus)api.SetPropertyDouble(handle, text, value);
    }

    internal static RuntimeResult<string> GetPropertyString(ComponentHandle handle, string name)
    {
        if (api.GetPropertyString == null) return new(RuntimeStatus.ServiceUnavailable);
        const int capacity = 4096;
        byte* output = stackalloc byte[capacity];
        RuntimeStatus status;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
            status = (RuntimeStatus)api.GetPropertyString(handle, text, output, capacity);
        return status == RuntimeStatus.Ok
            ? new RuntimeResult<string>(status, FromUtf8(output))
            : new RuntimeResult<string>(status);
    }

    internal static RuntimeStatus SetPropertyString(ComponentHandle handle, string name, string value)
    {
        if (api.SetPropertyString == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
        fixed (byte* content = Encoding.UTF8.GetBytes(value + "\0"))
            return (RuntimeStatus)api.SetPropertyString(handle, text, content);
    }

    internal static RuntimeResult<Vector2> GetPropertyVector2(ComponentHandle handle, string name)
    {
        if (api.GetPropertyVector2 == null) return new(RuntimeStatus.ServiceUnavailable);
        Vector2 value = default;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
            return new RuntimeResult<Vector2>(
                (RuntimeStatus)api.GetPropertyVector2(handle, text, &value), value);
    }

    internal static RuntimeStatus SetPropertyVector2(ComponentHandle handle, string name, Vector2 value)
    {
        if (api.SetPropertyVector2 == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
            return (RuntimeStatus)api.SetPropertyVector2(handle, text, value);
    }

    internal static RuntimeResult<Vector3> GetPropertyVector3(ComponentHandle handle, string name)
    {
        if (api.GetPropertyVector3 == null) return new(RuntimeStatus.ServiceUnavailable);
        Vector3 value = default;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
            return new RuntimeResult<Vector3>(
                (RuntimeStatus)api.GetPropertyVector3(handle, text, &value), value);
    }

    internal static RuntimeStatus SetPropertyVector3(ComponentHandle handle, string name, Vector3 value)
    {
        if (api.SetPropertyVector3 == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
            return (RuntimeStatus)api.SetPropertyVector3(handle, text, value);
    }

    internal static RuntimeResult<Vector4> GetPropertyVector4(ComponentHandle handle, string name)
    {
        if (api.GetPropertyVector4 == null) return new(RuntimeStatus.ServiceUnavailable);
        Vector4 value = default;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
            return new RuntimeResult<Vector4>(
                (RuntimeStatus)api.GetPropertyVector4(handle, text, &value), value);
    }

    internal static RuntimeStatus SetPropertyVector4(ComponentHandle handle, string name, Vector4 value)
    {
        if (api.SetPropertyVector4 == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
            return (RuntimeStatus)api.SetPropertyVector4(handle, text, value);
    }

    internal static RuntimeStatus SetWorldPosition(ObjectHandle handle, Vector3 value)
    {
        if (api.SetWorldPosition == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.SetWorldPosition(handle, value);
    }

    internal static RuntimeResult<Quaternion> GetWorldRotation(ObjectHandle handle)
    {
        if (api.GetWorldRotation == null) return new(RuntimeStatus.ServiceUnavailable);
        Vector4 value = default;
        var status = (RuntimeStatus)api.GetWorldRotation(handle, &value);
        return new RuntimeResult<Quaternion>(status,
            new Quaternion(value.X, value.Y, value.Z, value.W));
    }

    internal static RuntimeStatus SetWorldRotation(ObjectHandle handle, Quaternion value)
    {
        if (api.SetWorldRotation == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.SetWorldRotation(handle,
            new Vector4(value.X, value.Y, value.Z, value.W));
    }

    internal static RuntimeResult<Vector3> GetWorldScale(ObjectHandle handle)
    {
        if (api.GetWorldScale == null) return new(RuntimeStatus.ServiceUnavailable);
        Vector3 value = default;
        return new RuntimeResult<Vector3>((RuntimeStatus)api.GetWorldScale(handle, &value), value);
    }

    internal static RuntimeStatus SetWorldScale(ObjectHandle handle, Vector3 value)
    {
        if (api.SetWorldScale == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.SetWorldScale(handle, value);
    }

    internal static RuntimeStatus GetWorldAxes(ObjectHandle handle,
        out Vector3 forward, out Vector3 right, out Vector3 up)
    {
        forward = new Vector3(0.0f, 0.0f, 1.0f);
        right = new Vector3(1.0f, 0.0f, 0.0f);
        up = new Vector3(0.0f, 1.0f, 0.0f);
        if (api.GetWorldAxes == null) return RuntimeStatus.ServiceUnavailable;
        Vector3 f = default, r = default, u = default;
        var status = (RuntimeStatus)api.GetWorldAxes(handle, &f, &r, &u);
        if (status != RuntimeStatus.Ok) return status;
        forward = f;
        right = r;
        up = u;
        return status;
    }

    internal static RuntimeStatus LookAt(ObjectHandle handle, Vector3 target, Vector3 worldUp)
    {
        if (api.LookAt == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.LookAt(handle, target, worldUp);
    }

    internal static RuntimeStatus RigidbodyAddForce(ComponentHandle handle, Vector3 force)
    {
        if (api.RigidbodyAddForce == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.RigidbodyAddForce(handle, force);
    }

    internal static RuntimeStatus RigidbodyAddTorque(ComponentHandle handle, Vector3 torque)
    {
        if (api.RigidbodyAddTorque == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.RigidbodyAddTorque(handle, torque);
    }

    internal static RuntimeStatus RigidbodyClearForces(ComponentHandle handle)
    {
        if (api.RigidbodyClearForces == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.RigidbodyClearForces(handle);
    }

    internal static RuntimeStatus RigidbodyTeleport(ComponentHandle handle,
        Vector3 position, Vector3 rotationEuler)
    {
        if (api.RigidbodyTeleport == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.RigidbodyTeleport(handle, position, rotationEuler);
    }

    internal static RuntimeResult<Vector3> RigidbodyGetLinearVelocity(ComponentHandle handle)
    {
        if (api.RigidbodyGetLinearVelocity == null) return new(RuntimeStatus.ServiceUnavailable);
        Vector3 value = default;
        return new RuntimeResult<Vector3>(
            (RuntimeStatus)api.RigidbodyGetLinearVelocity(handle, &value), value);
    }

    internal static RuntimeStatus RigidbodySetLinearVelocity(ComponentHandle handle, Vector3 value)
    {
        if (api.RigidbodySetLinearVelocity == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.RigidbodySetLinearVelocity(handle, value);
    }

    internal static RuntimeResult<Vector3> RigidbodyGetAngularVelocity(ComponentHandle handle)
    {
        if (api.RigidbodyGetAngularVelocity == null) return new(RuntimeStatus.ServiceUnavailable);
        Vector3 value = default;
        return new RuntimeResult<Vector3>(
            (RuntimeStatus)api.RigidbodyGetAngularVelocity(handle, &value), value);
    }

    internal static RuntimeStatus RigidbodySetAngularVelocity(ComponentHandle handle, Vector3 value)
    {
        if (api.RigidbodySetAngularVelocity == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.RigidbodySetAngularVelocity(handle, value);
    }
}
