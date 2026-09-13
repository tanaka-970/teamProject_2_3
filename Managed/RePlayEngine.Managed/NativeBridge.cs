using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;

namespace ReplayEngine;

public static unsafe class NativeBridge
{
    private static readonly JsonSerializerOptions FieldJsonOptions = new()
    {
        IncludeFields = true,
        PropertyNameCaseInsensitive = false,
        IgnoreReadOnlyProperties = true,
    };
    private static readonly ScriptRuntimeContext Context = new();
    private static readonly Dictionary<ulong, ManagedInstance> Instances = new();
    private static readonly Dictionary<ComponentInstanceKey, ScriptBehaviour> InstancesByComponent = new();
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
    public const uint NativeApiAbiVersion = 21;

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

        // v17 additions. Landscape。C++ の NativeApiTable と同じ並びで末尾へ足す。
        public delegate* unmanaged[Cdecl]<ComponentHandle, int*, int*, float*, int> LandscapeInfo;
        public delegate* unmanaged[Cdecl]<ComponentHandle, int, int, float*, int> LandscapeGetHeight;
        public delegate* unmanaged[Cdecl]<ComponentHandle, int, int, float, int> LandscapeSetHeight;
        public delegate* unmanaged[Cdecl]<ComponentHandle, float, float, float*, int> LandscapeSampleHeight;
        public delegate* unmanaged[Cdecl]<ComponentHandle, Vector3, int, int, float, float, float, float, float, float, int> LandscapeSculpt;
        public delegate* unmanaged[Cdecl]<ComponentHandle, Vector3, Vector3, float, Vector3*, Vector3*, float*, int> LandscapeRaycast;

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

        // v11 追加。生デバイス入力 / Scene / 診断。
        public delegate* unmanaged[Cdecl]<int, int*, int> InputKeyHeld;
        public delegate* unmanaged[Cdecl]<int, int*, int> InputKeyPressed;
        public delegate* unmanaged[Cdecl]<int, int*, int> InputKeyReleased;
        public delegate* unmanaged[Cdecl]<int, int*, int> InputMouseHeld;
        public delegate* unmanaged[Cdecl]<int, int*, int> InputMousePressed;
        public delegate* unmanaged[Cdecl]<int, int*, int> InputMouseReleased;
        public delegate* unmanaged[Cdecl]<float*, float*, int> InputPointerPosition;
        public delegate* unmanaged[Cdecl]<float*, int> InputWheelDelta;
        public delegate* unmanaged[Cdecl]<int, int*, int> InputPadConnected;
        public delegate* unmanaged[Cdecl]<int, int, int*, int> InputPadButtonHeld;
        public delegate* unmanaged[Cdecl]<int, int, int*, int> InputPadButtonPressed;
        public delegate* unmanaged[Cdecl]<int, int, int*, int> InputPadButtonReleased;
        public delegate* unmanaged[Cdecl]<int, int, float*, int> InputPadAxis;
        public delegate* unmanaged[Cdecl]<int, float, float, int> InputSetVibration;
        public delegate* unmanaged[Cdecl]<byte*, Vector3, Vector3, Vector3, ObjectHandle, ulong*, int> InstantiatePrefabTracked;
        public delegate* unmanaged[Cdecl]<ulong, ObjectHandle*, int> TakeSpawnResult;
        public delegate* unmanaged[Cdecl]<byte*, int, int> GetCurrentSceneGuid;
        public delegate* unmanaged[Cdecl]<byte*, int> QuitApplication;
        public delegate* unmanaged[Cdecl]<ulong, ulong*, int> EventDroppedCount;

        // v12 複数 Hit Physics Query。
        public delegate* unmanaged[Cdecl]<PhysicsQueryRequestNative, PhysicsHit*, int, int*, int> PhysicsQuery;

        // v13 Global/Scene Event 購読と Component 実行命令。
        public delegate* unmanaged[Cdecl]<ulong, ulong, ObjectHandle, int, ulong*, int> SubscribeEventScoped;
        public delegate* unmanaged[Cdecl]<ComponentHandle, int, byte*, float, float, int, int> ComponentCommand;

        // v14 Component 型メタデータ。
        public delegate* unmanaged[Cdecl]<byte*, byte*, int, int> ComponentTypeInfo;

        // v15 Component プロパティの Object / Component 参照。
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, ulong*, int> GetPropertyObjectReference;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, ulong, int> SetPropertyObjectReference;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, ulong*, uint*, int> GetPropertyComponentReference;
        public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, ulong, uint, int> SetPropertyComponentReference;
        public delegate* unmanaged[Cdecl]<ComponentHandle, ulong*, uint*, int> ComponentToReference;
        public delegate* unmanaged[Cdecl]<ulong, uint, ComponentHandle*, int> ResolveComponentReference;

        // v16 Scene 遷移の進捗・実行中・最終結果。
        public delegate* unmanaged[Cdecl]<float*, int*, int*, int> GetSceneTransitionState;

        // v18 additions. 表の本当の末尾。既存 entry の offset は動かさない。
        // C++ 側の NativeApiTable も v18 以降を同じ順で末尾へ append している。
        public delegate* unmanaged[Cdecl]<ComponentHandle, int*, int> ComponentAlive;
        public delegate* unmanaged[Cdecl]<ObjectHandle, uint, ComponentHandle*, int> FindColliderComponent;

        // v19 addition. 同じく本当の末尾。
        public delegate* unmanaged[Cdecl]<ObjectHandle, ulong, ulong, ComponentHandle*, int> AddScriptComponent;

        // v20 addition. Public GameObject.Find 用の activeInHierarchy 限定検索。
        public delegate* unmanaged[Cdecl]<byte*, ObjectHandle*, int> FindActiveGameObjectByName;

        // v21 addition. CompositionPlayer。C++ の composition_* と同じ並び。
        public delegate* unmanaged[Cdecl]<ObjectHandle, byte*, ComponentHandle*, int> FindCompositionPlayer;
        public delegate* unmanaged[Cdecl]<ComponentHandle, int> CompositionPlay;
        public delegate* unmanaged[Cdecl]<ComponentHandle, int> CompositionPause;
        public delegate* unmanaged[Cdecl]<ComponentHandle, int> CompositionResume;
        public delegate* unmanaged[Cdecl]<ComponentHandle, int> CompositionStop;
        public delegate* unmanaged[Cdecl]<ComponentHandle, float, int> CompositionSetTime;
        public delegate* unmanaged[Cdecl]<ComponentHandle, float, int> CompositionSetSpeed;
        public delegate* unmanaged[Cdecl]<ComponentHandle, float, int> CompositionSetWeight;
        public delegate* unmanaged[Cdecl]<ComponentHandle, int*, int> CompositionIsPlaying;
        public delegate* unmanaged[Cdecl]<ComponentHandle, float*, int> CompositionGetTime;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct PhysicsQueryRequestNative
    {
        public PhysicsQueryKind Kind;
        public Vector3 PointA;
        public Vector3 PointB;
        public Vector3 Direction;
        public Quaternion Rotation;
        public Vector3 HalfExtents;
        public float Radius;
        public float MaxDistance;
        public int Layer;
        public int Mask;
        public ObjectHandle Ignore;
    }

    private sealed class ManagedInstance
    {
        public ManagedInstance(ScriptBehaviour behaviour, ComponentHandle component)
        {
            Behaviour = behaviour;
            Component = component;
        }

        public ScriptBehaviour Behaviour { get; }
        public ComponentHandle Component { get; }
    }

    private readonly struct ComponentInstanceKey : IEquatable<ComponentInstanceKey>
    {
        internal ComponentInstanceKey(ComponentHandle handle)
        {
            World = handle.Owner.World;
            Object = handle.Owner.Object;
            Generation = handle.Owner.Generation;
            Instance = handle.Instance;
            TypeId = handle.TypeId;
        }

        private ulong World { get; }
        private ulong Object { get; }
        private uint Generation { get; }
        private ulong Instance { get; }
        private uint TypeId { get; }

        public bool Equals(ComponentInstanceKey other) =>
            World == other.World && Object == other.Object &&
            Generation == other.Generation && Instance == other.Instance &&
            TypeId == other.TypeId;
        public override bool Equals(object? obj) =>
            obj is ComponentInstanceKey other && Equals(other);
        public override int GetHashCode() =>
            HashCode.Combine(World, Object, Generation, Instance, TypeId);
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
                        if (!discovered.TryAdd(guid.Value, type))
                            throw new InvalidOperationException(
                                $"Duplicate ReplayGuid on C# Behaviour: {type.FullName}.");
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
            InstancesByComponent.Clear();
            PendingReferences.Clear();
            // 型解析と wrapper のキャッシュも捨てる。
            // 古い Type を握ったままだと AssemblyLoadContext が解放されない。
            BehaviourTypeDescriptor.ClearCache();
            MonoBehaviourHost.ClearAll();
            GameObject.ClearCache();
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
        InstancesByComponent.Clear();
        PendingReferences.Clear();
        BehaviourTypeDescriptor.ClearCache();
        MonoBehaviourHost.ClearAll();
        GameObject.ClearCache();
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

            var created = Activator.CreateInstance(type);

            // MonoBehaviour は ScriptBehaviour ではないので、駆動用の Host で包む。
            // C++ から見えるのは今までどおり ScriptBehaviour 1 種類だけ。
            MonoBehaviourHost? host = null;
            if (created is MonoBehaviour authored)
            {
                NativeComponentBootstrap.EnsureRegistered();
                host = new MonoBehaviourHost(authored);
            }

            if ((object?)(host ?? created as ScriptBehaviour) is not ScriptBehaviour behaviour)
            {
                SetLastError("C# Behaviour could not be created.");
                return 0;
            }

            behaviour.Attach(Context, owner, component);
            host?.OnAttached();
            var handle = nextHandle++;
            Instances[handle] = new ManagedInstance(behaviour, component);
            InstancesByComponent[new ComponentInstanceKey(component)] = behaviour;
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
        InstancesByComponent.Remove(new ComponentInstanceKey(state.Component));
        (state.Behaviour as MonoBehaviourHost)?.OnDetached();
        state.Behaviour.ReleaseManagedSubscriptions();
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int Invoke(ulong instance, int callback, float deltaTime)
    {
        try
        {
            if (!Instances.TryGetValue(instance, out var state)) return 2;

            // 非 Active の相手など、Scene の復元パスで未解決だった参照だけを引き直す。
            DrainPendingReferences();

            // Managed callback を実行している間だけ実行文脈を立てる。
            // Time / Physics / GameObject.Find などはここから World を解決する。
            // using なので、例外で抜けても必ず元へ戻る。
            using (ScriptExecutionContext.Enter(Context, state.Behaviour as MonoBehaviourHost))
            {
                switch (callback)
                {
                    case 0:
                        state.Behaviour.BeginRuntimeLifecycle();
                        state.Behaviour.Awake();
                        break;
                    case 1: state.Behaviour.OnEnable(); break;
                    case 2: state.Behaviour.Start(); break;
                    case 3: state.Behaviour.FixedUpdate(deltaTime); break;
                    case 4:
                        // Legacy の Coroutine / Timer / Tween は従来の Update 直前で進める。
                        if (state.Behaviour is not MonoBehaviourHost)
                            state.Behaviour.AdvanceCoroutines(TimeDeltaTime);
                        state.Behaviour.Update(deltaTime);
                        break;
                    case 5: state.Behaviour.LateUpdate(deltaTime); break;
                    case 6: state.Behaviour.OnDisable(); break;
                    case 7: state.Behaviour.OnDestroy(); break;
                    default: return 1;
                }
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
            // Inspector の値は Host ではなくユーザーのオブジェクトが持っている。
            var owner = SerializationTargetOf(state.Behaviour);
            var field = FindSerializableField(owner.GetType(), fieldName);
            if (field == null) return 0;

            // Inspector / Serialization は callback の外から来る。
            // 参照の解決だけは Handle で決まるので文脈に依らないが、
            // ユーザーの field 型が engine API を触る場合に備えて同じ文脈を張る。
            using (ScriptExecutionContext.Enter(Context,
                state.Behaviour as MonoBehaviourHost))
            {
                var text = FromUtf8(valueText);
                var parsed = ParseValue(field.FieldType, text);
                field.SetValue(owner, parsed);
                RememberUnresolvedReference(instance, owner, field, text, parsed);
            }
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
            var owner = SerializationTargetOf(state.Behaviour);
            var field = FindSerializableField(owner.GetType(), fieldName);
            if (field == null) return 0;

            using (ScriptExecutionContext.Enter(Context,
                state.Behaviour as MonoBehaviourHost))
            {
                WriteUtf8(FormatValue(field.GetValue(owner), field.FieldType),
                    output, outputCapacity);
            }
            return 1;
        }
        catch (Exception ex)
        {
            SetLastError(ex);
            return 0;
        }
    }

    // 物理と Event の配送が終わった同期点で、C++ が 1 回だけ呼ぶ。
    //
    // ここで接触イベントを配ると、Native Behaviour と同じフレームで届く。
    // deltaTime は Update と同じゲーム時間（Time.timeScale を掛けた後）。
    //
    // 【何を止めて何を進めるか】
    //
    //   GameObject が非 Active
    //     MonoBehaviour … イベントも Coroutine も止める。Coroutine は捨てる。
    //                     再 Active でも前の IEnumerator の途中からは再開しない。
    //     Legacy        … 従来どおり「Update が来ない」だけ。途中位置は保つ。
    //                     ここを捨てる側に変えると既存スクリプトの意味が変わる。
    //
    //   Component が enabled = false
    //     MonoBehaviour … Coroutine と接触イベントは進める。
    //     Legacy        … Coroutine も止まる。以前は Update 経由で進めていたので、
    //                     無効なあいだは進まなかった。その意味を保つ。
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int PumpEvents(float deltaTime)
    {
        try
        {
            DrainPendingReferences();

            // 走査中に Instances が変わることがある（Destroy / AddComponent）。
            // 変更に強い形へ写してから回す。
            // 写す先は使い回す。毎フレーム配列を確保しない。
            var count = Instances.Count;
            if (count == 0) return 1;
            if (pumpBuffer.Length < count)
                pumpBuffer = new ManagedInstance[Math.Max(count, pumpBuffer.Length * 2)];
            Instances.Values.CopyTo(pumpBuffer, 0);

            for (var index = 0; index < count; ++index) PumpOne(pumpBuffer[index], deltaTime);

            // 写した参照を残さない。破棄済みの Behaviour を 1 フレーム余分に掴まない。
            Array.Clear(pumpBuffer, 0, count);
            return 1;
        }
        catch (Exception ex)
        {
            SetLastError(ex);
            return 0;
        }
    }

    private static ManagedInstance[] pumpBuffer = Array.Empty<ManagedInstance>();

    private static void PumpOne(ManagedInstance state, float deltaTime)
    {
        var behaviour = state.Behaviour;
        if (!IsComponentAlive(state.Component)) return;
        var host = behaviour as MonoBehaviourHost;
        using (ScriptExecutionContext.Enter(Context, host))
        {
            try
            {
                // 配るものも進めるものも無いなら、状態を聞きに行かない。
                var wantsEvents = behaviour.HasEngineEventSubscriptions;
                var wantsRoutines = host != null && behaviour.HasPendingRoutines;
                if (!wantsEvents && !wantsRoutines) return;

                if (!ActiveInHierarchy(behaviour.GameObject))
                {
                    if (wantsEvents) behaviour.PumpEngineEvents(false, false);
                    if (host != null) behaviour.StopCoroutinesForInactive();
                    return;
                }

                if (!behaviour.RuntimeLifecycleStarted) return;
                var enabled = IsComponentEnabled(state.Component);
                var componentEnabled = !enabled.Succeeded || enabled.Value;
                // Scene / Button / Motion は従来の有効時限定とし、無効期間の通知を捨てる。
                if (wantsEvents) behaviour.PumpEngineEvents(host != null || componentEnabled, componentEnabled);
                if (!ActiveInHierarchy(behaviour.GameObject))
                {
                    if (host != null) behaviour.StopCoroutinesForInactive();
                    return;
                }
                if (wantsRoutines && IsComponentAlive(state.Component))
                    behaviour.AdvanceCoroutines(deltaTime);
            }
            catch (Exception inner)
            {
                // 1 つの Script の失敗で、残りの配送を止めない。
                SetLastError(inner);
            }
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

    internal static RuntimeResult<ObjectHandle> FindActiveGameObjectByName(string name)
    {
        if (api.FindActiveGameObjectByName == null)
            return new(RuntimeStatus.ServiceUnavailable);
        if (name == null) return new(RuntimeStatus.InvalidArgument);
        ObjectHandle result = default;
        RuntimeStatus status;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
            status = (RuntimeStatus)api.FindActiveGameObjectByName(text, &result);
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

    internal static RuntimeResult<PhysicsHit[]> PhysicsQuery(PhysicsQueryRequestNative request)
    {
        if (api.PhysicsQuery == null)
            return new RuntimeResult<PhysicsHit[]>(RuntimeStatus.ServiceUnavailable,
                Array.Empty<PhysicsHit>());

        int count = 0;
        var status = (RuntimeStatus)api.PhysicsQuery(request, null, 0, &count);
        if (status != RuntimeStatus.Ok || count <= 0)
            return new RuntimeResult<PhysicsHit[]>(status, Array.Empty<PhysicsHit>());

        var hits = new PhysicsHit[count];
        fixed (PhysicsHit* output = hits)
        {
            int written = count;
            status = (RuntimeStatus)api.PhysicsQuery(request, output, count, &written);
            if (status != RuntimeStatus.Ok)
                return new RuntimeResult<PhysicsHit[]>(status, Array.Empty<PhysicsHit>());
            if (written < hits.Length) Array.Resize(ref hits, written);
        }
        return new RuntimeResult<PhysicsHit[]>(status, hits);
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

    internal static RuntimeResult<ComponentHandle> FindCompositionPlayer(ObjectHandle owner, string key)
    {
        if (api.FindCompositionPlayer == null) return new(RuntimeStatus.ServiceUnavailable);
        ComponentHandle value = default;
        fixed (byte* text = Encoding.UTF8.GetBytes(key + "\0"))
        {
            return new RuntimeResult<ComponentHandle>(
                (RuntimeStatus)api.FindCompositionPlayer(owner, text, &value),
                value);
        }
    }

    internal static RuntimeStatus CompositionPlay(ComponentHandle player)
    {
        if (api.CompositionPlay == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.CompositionPlay(player);
    }

    internal static RuntimeStatus CompositionPause(ComponentHandle player)
    {
        if (api.CompositionPause == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.CompositionPause(player);
    }

    internal static RuntimeStatus CompositionResume(ComponentHandle player)
    {
        if (api.CompositionResume == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.CompositionResume(player);
    }

    internal static RuntimeStatus CompositionStop(ComponentHandle player)
    {
        if (api.CompositionStop == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.CompositionStop(player);
    }

    internal static RuntimeStatus CompositionSetTime(ComponentHandle player, float seconds)
    {
        if (api.CompositionSetTime == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.CompositionSetTime(player, seconds);
    }

    internal static RuntimeStatus CompositionSetSpeed(ComponentHandle player, float speed)
    {
        if (api.CompositionSetSpeed == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.CompositionSetSpeed(player, speed);
    }

    internal static RuntimeStatus CompositionSetWeight(ComponentHandle player, float weight)
    {
        if (api.CompositionSetWeight == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.CompositionSetWeight(player, weight);
    }

    internal static RuntimeResult<bool> CompositionIsPlaying(ComponentHandle player)
    {
        if (api.CompositionIsPlaying == null) return new(RuntimeStatus.ServiceUnavailable);
        int value = 0;
        var status = (RuntimeStatus)api.CompositionIsPlaying(player, &value);
        return new RuntimeResult<bool>(status, value != 0);
    }

    internal static RuntimeResult<float> CompositionGetTime(ComponentHandle player)
    {
        if (api.CompositionGetTime == null) return new(RuntimeStatus.ServiceUnavailable);
        float value = 0.0f;
        var status = (RuntimeStatus)api.CompositionGetTime(player, &value);
        return new RuntimeResult<float>(status, value);
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

    internal static RuntimeResult<EventSubscription> SubscribeEvent(string eventTypeGuid,
        ObjectHandle owner, EventScope scope = EventScope.Scene)
    {
        if (api.SubscribeEventScoped == null) return new(RuntimeStatus.ServiceUnavailable);
        if (!TryParseGuidText(eventTypeGuid, out var guid)) return new(RuntimeStatus.InvalidArgument);

        ulong id = 0;
        var status = (RuntimeStatus)api.SubscribeEventScoped(guid.High, guid.Low,
            owner, (int)scope, &id);
        return new RuntimeResult<EventSubscription>(status, new EventSubscription(id));
    }

    internal static RuntimeStatus InvokeComponentCommand(ComponentHandle handle,
        ComponentCommand command, string text = "", float scalar = 0.0f,
        float secondaryScalar = 0.0f, int integer = 0)
    {
        if (api.ComponentCommand == null) return RuntimeStatus.ServiceUnavailable;
        if (text == null) return RuntimeStatus.InvalidArgument;
        fixed (byte* pointer = Encoding.UTF8.GetBytes(text + "\0"))
        {
            return (RuntimeStatus)api.ComponentCommand(handle, (int)command, pointer,
                scalar, secondaryScalar, integer);
        }
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

    // Inspector の値を持っているオブジェクト。
    // Legacy はその ScriptBehaviour 自身、新 API は Host が抱える MonoBehaviour。
    private static object SerializationTargetOf(ScriptBehaviour behaviour)
        => behaviour is MonoBehaviourHost host ? host.Target : behaviour;

    private static IEnumerable<Type> DiscoverBehaviourTypes(Assembly assembly)
    {
        // Legacy の ScriptBehaviour と、新しい MonoBehaviour の両方を拾う。
        // どちらも同じ ScriptComponent から駆動するので、扱いはここから先で分かれない。
        return assembly.GetTypes().Where(type =>
            !type.IsAbstract &&
            (typeof(ScriptBehaviour).IsAssignableFrom(type) ||
                typeof(MonoBehaviour).IsAssignableFrom(type)) &&
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

        // Field とは別の型メタデータ。新しい MonoBehaviour だけ inactive 時にも
        // instance を用意し、Legacy ScriptBehaviour の constructor 時期は変えない。
        builder.Append("TYPE\tAUTHORING_MONO\t");
        builder.Append(typeof(MonoBehaviour).IsAssignableFrom(type) ? '1' : '0');
        builder.Append('\n');

        foreach (var field in SerializableFields(type))
        {
            var mapped = MapFieldType(field.FieldType);
            if (mapped == null) continue;

            var defaultValue = DefaultValue(type, field);
            var range = field.GetCustomAttribute<RangeAttribute>();
            var tooltip = field.GetCustomAttribute<TooltipAttribute>();
            var header = field.GetCustomAttribute<HeaderAttribute>();
            var display = field.GetCustomAttribute<DisplayNameAttribute>();
            var assetType = field.GetCustomAttribute<AssetTypeAttribute>();
            var hidden = field.IsDefined(typeof(HideInInspectorAttribute), true);
            var readOnly = field.IsDefined(typeof(ReadOnlyAttribute), true);

            // 6 列目までは v1 と同じ並び。7 列目以降は読めない側が無視できる追加分。
            builder.Append("FIELD\t");
            builder.Append(Escape(field.Name));
            builder.Append('\t');
            builder.Append(mapped);
            builder.Append('\t');
            builder.Append(Escape(display != null ? display.Text : Humanize(field.Name)));
            builder.Append('\t');
            builder.Append(Escape(tooltip != null ? tooltip.Text : string.Empty));
            builder.Append('\t');
            builder.Append(Escape(FormatValue(defaultValue, field.FieldType)));
            builder.Append('\t');
            // flags: h=Inspector非表示 r=読み取り専用
            builder.Append(Escape((hidden ? "h" : string.Empty) + (readOnly ? "r" : string.Empty)));
            builder.Append('\t');
            builder.Append(range != null
                ? range.Minimum.ToString(CultureInfo.InvariantCulture) : string.Empty);
            builder.Append('\t');
            builder.Append(range != null
                ? range.Maximum.ToString(CultureInfo.InvariantCulture) : string.Empty);
            builder.Append('\t');
            builder.Append(Escape(assetType != null
                ? assetType.Kind : TypedAssetKind(field.FieldType)));
            builder.Append('\t');
            builder.Append(Escape(header != null ? header.Text : string.Empty));
            builder.Append('\t');
            builder.Append(Escape(EnumLabels(field.FieldType)));
            builder.Append('\t');
            builder.Append(Escape(CollectionElementTypeName(field.FieldType)));
            builder.Append('\n');
        }

        return builder.ToString();
    }

    // enum のラベルを "," 区切りで並べる。値が 0..n-1 の連番でないときは空にする。
    // Inspector は添字でラベルを引くので、飛び番だと別の名前が出てしまう。
    private static string EnumLabels(Type type)
    {
        if (!type.IsEnum) return string.Empty;
        var names = Enum.GetNames(type);
        var values = Enum.GetValues(type);
        var labels = new string[names.Length];
        for (var index = 0; index < names.Length; ++index)
        {
            if (Convert.ToInt64(values.GetValue(index), CultureInfo.InvariantCulture) != index)
                return string.Empty;
            labels[index] = names[index].Replace(",", " ");
        }
        return string.Join(",", labels);
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

    // ---- Inspector の Public 参照 -----------------------------------------
    //
    // 保存されるのは今までどおり ObjectReference / ComponentReference。
    // ここでやるのは、その ID と Public wrapper の間の変換だけ。

    // 非 Active の相手や Runtime 生成・再ロードで遅れて現れる Component を待つ。
    private sealed class PendingReference
    {
        internal PendingReference(ulong instance, object owner, FieldInfo field, string text, object? value)
        {
            Instance = instance;
            Owner = owner;
            Field = field;
            Text = text;
            OriginalValue = value;
            CreatedFrame = TimeFrameIndex;
            if (value is System.Collections.IList collection)
            {
                OriginalElements = new object?[collection.Count];
                collection.CopyTo(OriginalElements, 0);
            }
        }

        internal ulong Instance { get; }
        internal object Owner { get; }
        internal FieldInfo Field { get; }
        internal string Text { get; }
        internal ulong CreatedFrame { get; set; }
        private object? OriginalValue { get; }
        private object?[]? OriginalElements { get; }

        internal bool IsUnchanged()
        {
            var current = Field.GetValue(Owner);
            if (!ReferenceEquals(current, OriginalValue)) return false;
            if (OriginalElements == null) return true;
            if (current is not System.Collections.IList collection ||
                collection.Count != OriginalElements.Length) return false;
            for (var index = 0; index < collection.Count; ++index)
                if (!ReferenceEquals(collection[index], OriginalElements[index])) return false;
            return true;
        }
    }

    private static readonly List<PendingReference> PendingReferences = new();

    // 新しい SetField は古い待機を置き換え、未生成の Component 参照だけを覚える。
    private static void RememberUnresolvedReference(ulong instance, object owner,
        FieldInfo field, string text, object? value)
    {
        PendingReferences.RemoveAll(entry => entry.Instance == instance && entry.Field == field);
        if (!HasUnresolvedReference(field.FieldType, text, value)) return;
        if (PendingReferences.Count >= 4096) return;
        PendingReferences.Add(new PendingReference(instance, owner, field, text, value));
    }

    private static bool HasUnresolvedReference(Type type, string text, object? value)
    {
        if (typeof(Component).IsAssignableFrom(type))
        {
            if (value != null || string.IsNullOrEmpty(text)) return false;
            var parts = text.Split(',');
            return parts.Length >= 2 && ParseULong(parts, 0) != 0 && ParseULong(parts, 1) != 0;
        }
        if (!TryGetCollectionElementType(type, out var elementType) ||
            !typeof(Component).IsAssignableFrom(elementType) ||
            value is not System.Collections.IList collection) return false;
        var fields = text.Split('|', 3);
        var encoded = fields.Length == 3 && fields[2].Length != 0
            ? fields[2].Split(';') : Array.Empty<string>();
        for (var index = 0; index < collection.Count && index < encoded.Length; ++index)
        {
            var itemText = Encoding.UTF8.GetString(Convert.FromHexString(encoded[index]));
            if (HasUnresolvedReference(elementType, itemText, collection[index])) return true;
        }
        return false;
    }

    // 積んである参照を引き直す。callback の入口とフレーム末尾で呼ぶ。
    // 空なら分岐 1 つで終わるので、通常のフレームには何も足さない。
    private static void DrainPendingReferences()
    {
        if (PendingReferences.Count == 0) return;
        for (var index = PendingReferences.Count - 1; index >= 0; --index)
        {
            var entry = PendingReferences[index];

            // 持ち主が消えていたら追いかけない。
            if (!Instances.ContainsKey(entry.Instance) || !entry.IsUnchanged())
            {
                PendingReferences.RemoveAt(index);
                continue;
            }

            // World の切り替えで frame index が戻った場合は最初の同期フレームを起点にする。
            if (TimeFrameIndex < entry.CreatedFrame) entry.CreatedFrame = TimeFrameIndex;
            if (TimeFrameIndex - entry.CreatedFrame >= 64)
            {
                PendingReferences.RemoveAt(index);
                continue;
            }

            try
            {
                var resolved = ParseValue(entry.Field.FieldType, entry.Text);
                if (HasUnresolvedReference(entry.Field.FieldType, entry.Text, resolved)) continue;
                entry.Field.SetValue(entry.Owner, resolved);
                PendingReferences.RemoveAt(index);
            }
            catch (Exception ex)
            {
                SetLastError(ex);
                PendingReferences.RemoveAt(index);
            }
        }
    }

    private static GameObject? ResolvePublicGameObject(ObjectReference reference)
    {
        if (!reference.IsAssigned) return null;
        var handle = FindGameObject(reference.ObjectId);
        return handle.Succeeded ? GameObject.Wrap(handle.Value) : null;
    }

    private static object? ResolvePublicComponent(Type type, ComponentReference reference)
    {
        if (!reference.IsAssigned) return null;
        var handle = ResolveComponentReference(reference);
        if (!handle.Succeeded || handle.Value.IsEmpty) return null;

        // Managed Behaviour への参照は、保存した ScriptComponent の Stable ID から
        // 復元した ComponentHandle でそのまま引く。
        //
        // 【なぜ owner + 型で探さないか】
        //   同じ GameObject に同じ型の MonoBehaviour を 2 つ付けられる。
        //   型で探すと必ず 1 つ目が返るので、Inspector で 2 つ目を指しても
        //   Scene を読み直した瞬間に 1 つ目へ化ける。
        //   ここは field の宣言型を見ずに先に引く。宣言型が基底の Component でも
        //   MonoBehaviour を入れられるようにするため。
        var managed = FindManagedTarget(handle.Value);
        if (managed != null) return type.IsInstanceOfType(managed) ? managed : null;

        // ScriptComponent 以外は Native の型名から wrapper を作る。
        var owner = GameObject.Wrap(handle.Value.Owner);
        if (owner == null) return null;
        var factory = NativeComponentRegistry.FindByNativeTypeName(handle.Value);
        if (factory == null) return null;
        var created = factory(owner, handle.Value);
        return type.IsInstanceOfType(created) ? created : null;
    }

    private static string ObjectIdTextOf(GameObject value)
    {
        // ObjectHandle の Object が、そのまま保存用の ObjectID。
        return value.Handle.Object.ToString(CultureInfo.InvariantCulture);
    }

    private static string ComponentReferenceTextOf(Component value)
    {
        // Managed Behaviour は、その instance を持つ ScriptComponent を指す。
        var handle = value is MonoBehaviour authored
            ? (authored.Host != null ? authored.Host.Component : default)
            : (value is NativeComponent native ? native.Handle : default);
        if (handle.IsEmpty) return string.Empty;

        var reference = ComponentToReference(handle);
        if (!reference.Succeeded || !reference.Value.IsAssigned) return string.Empty;
        return reference.Value.OwnerObjectId.ToString(CultureInfo.InvariantCulture) + "," +
            reference.Value.ComponentStableId.ToString(CultureInfo.InvariantCulture);
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
        // Public Authoring 型も、保存形式は既存の object / component と同じ。
        // Inspector から見える型を増やすだけで、新しい保存形式は作らない。
        if (type == typeof(GameObject)) return "object";
        if (typeof(Component).IsAssignableFrom(type)) return "component";
        if (typeof(IAssetReference).IsAssignableFrom(type)) return "asset";
        // enum は内部 int。ラベルは EnumLabels が別列で渡す。
        if (type.IsEnum && Enum.GetUnderlyingType(type) == typeof(int)) return "enum";
        if (TryGetCollectionElementType(type, out var elementType) &&
            MapScalarFieldType(elementType) != null) return "array";
        if (IsJsonFieldType(type)) return "json";
        return null;
    }

    private static string? MapScalarFieldType(Type type)
    {
        var mapped = MapFieldType(type);
        return mapped is "array" or "json" ? null : mapped;
    }

    private static bool TryGetCollectionElementType(Type type, out Type elementType)
    {
        if (type.IsArray && type.GetArrayRank() == 1)
        {
            elementType = type.GetElementType()!;
            return true;
        }
        if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(List<>))
        {
            elementType = type.GetGenericArguments()[0];
            return true;
        }
        elementType = typeof(void);
        return false;
    }

    private static string CollectionElementTypeName(Type type)
    {
        return TryGetCollectionElementType(type, out var elementType)
            ? MapScalarFieldType(elementType) ?? string.Empty
            : string.Empty;
    }

    private static string TypedAssetKind(Type type)
    {
        if (!type.IsGenericType ||
            type.GetGenericTypeDefinition() != typeof(AssetReference<>))
            return string.Empty;
        var marker = type.GetGenericArguments()[0];
        return marker.GetProperty("Kind", BindingFlags.Public | BindingFlags.Static)?
            .GetValue(null) as string ?? string.Empty;
    }

    private static bool IsJsonFieldType(Type type)
    {
        if (type == typeof(AnimationCurve)) return true;
        if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(Dictionary<,>))
            return type.GetGenericArguments()[0] == typeof(string);
        return type.IsDefined(typeof(SerializableAttribute), true) &&
            type != typeof(string) && !typeof(ScriptBehaviour).IsAssignableFrom(type);
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
        // Public GameObject / Component は、保存されている ID から解決して返す。
        // 解決できないときは null。Inspector 未設定と同じ扱いになる。
        if (type == typeof(GameObject))
            return ResolvePublicGameObject(new ObjectReference { ObjectId = ParseULong(parts, 0) });
        if (typeof(Component).IsAssignableFrom(type))
            return ResolvePublicComponent(type, new ComponentReference
            {
                OwnerObjectId = ParseULong(parts, 0),
                ComponentStableId = (uint)ParseULong(parts, 1),
            });
        if (type == typeof(ComponentReference)) return new ComponentReference { OwnerObjectId = ParseULong(parts, 0), ComponentStableId = (uint)ParseULong(parts, 1) };
        if (typeof(IAssetReference).IsAssignableFrom(type))
            return Activator.CreateInstance(type, text);
        if (type.IsEnum) return Enum.ToObject(type, int.Parse(text, CultureInfo.InvariantCulture));
        if (TryGetCollectionElementType(type, out var elementType))
            return ParseCollection(type, elementType, text);
        if (IsJsonFieldType(type))
            return string.IsNullOrEmpty(text) ? Activator.CreateInstance(type) :
                JsonSerializer.Deserialize(text, type, FieldJsonOptions);
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
        // Public wrapper は、保存時に既存の ID 形式へ戻す。
        if (value is GameObject publicObject)
            return publicObject.IsAlive ? ObjectIdTextOf(publicObject) : string.Empty;
        if (value is Component publicComponent)
            return ComponentReferenceTextOf(publicComponent);
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
        if (value is IAssetReference assetReference) return assetReference.AssetGuid;
        if (type.IsEnum)
            return Convert.ToInt32(value, CultureInfo.InvariantCulture)
                .ToString(CultureInfo.InvariantCulture);
        if (TryGetCollectionElementType(type, out var elementType))
            return FormatCollection(value, elementType);
        if (IsJsonFieldType(type))
            return JsonSerializer.Serialize(value, type, FieldJsonOptions);

        return string.Empty;
    }

    private static string FormatCollection(object value, Type elementType)
    {
        if (value is not System.Collections.IEnumerable enumerable) return string.Empty;
        var values = new List<string>();
        foreach (var element in enumerable)
        {
            var text = FormatValue(element, elementType);
            values.Add(Convert.ToHexString(Encoding.UTF8.GetBytes(text)));
        }
        return $"{MapScalarFieldType(elementType)}|{values.Count.ToString(CultureInfo.InvariantCulture)}|{string.Join(';', values)}";
    }

    private static object ParseCollection(Type collectionType, Type elementType, string text)
    {
        var fields = text.Split('|', 3);
        var encoded = fields.Length == 3 && fields[2].Length != 0
            ? fields[2].Split(';') : Array.Empty<string>();
        var count = fields.Length > 1 && int.TryParse(fields[1], NumberStyles.None,
            CultureInfo.InvariantCulture, out var parsedCount)
            ? Math.Clamp(parsedCount, 0, 65536) : 0;
        count = Math.Min(count, encoded.Length);

        var array = Array.CreateInstance(elementType, count);
        for (var index = 0; index < count; ++index)
        {
            var itemText = Encoding.UTF8.GetString(Convert.FromHexString(encoded[index]));
            array.SetValue(ParseValue(elementType, itemText), index);
        }
        if (collectionType.IsArray) return array;

        var list = (System.Collections.IList)Activator.CreateInstance(collectionType)!;
        foreach (var item in array) list.Add(item);
        return list;
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

    internal static RuntimeResult<ComponentTypeMetadata> ComponentTypeInfo(string typeName)
    {
        if (api.ComponentTypeInfo == null) return new(RuntimeStatus.ServiceUnavailable);
        const int capacity = 2048;
        byte* output = stackalloc byte[capacity];
        RuntimeStatus status;
        fixed (byte* text = Encoding.UTF8.GetBytes(typeName + "\0"))
            status = (RuntimeStatus)api.ComponentTypeInfo(text, output, capacity);
        if (status != RuntimeStatus.Ok) return new(status);
        var fields = FromUtf8(output).Split('\t');
        if (fields.Length < 10 ||
            !uint.TryParse(fields[1], NumberStyles.None, CultureInfo.InvariantCulture,
                out var typeId) ||
            !int.TryParse(fields[6], NumberStyles.Integer, CultureInfo.InvariantCulture,
                out var version))
        {
            return new(RuntimeStatus.TypeMismatch);
        }
        return new(status, new ComponentTypeMetadata(fields[0], typeId, fields[2],
            fields[3], fields[4], fields[5], version, fields[7] == "1",
            fields[8] == "1", fields[9] == "1"));
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

    internal static RuntimeResult<ObjectReference> GetPropertyObjectReference(
        ComponentHandle handle, string name)
    {
        if (api.GetPropertyObjectReference == null)
            return new(RuntimeStatus.ServiceUnavailable);
        ulong owner = 0;
        RuntimeStatus status;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
            status = (RuntimeStatus)api.GetPropertyObjectReference(handle, text, &owner);
        return new RuntimeResult<ObjectReference>(status,
            new ObjectReference { ObjectId = owner });
    }

    internal static RuntimeStatus SetPropertyObjectReference(ComponentHandle handle,
        string name, ObjectReference value)
    {
        if (api.SetPropertyObjectReference == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
            return (RuntimeStatus)api.SetPropertyObjectReference(handle, text, value.ObjectId);
    }

    internal static RuntimeResult<ComponentReference> GetPropertyComponentReference(
        ComponentHandle handle, string name)
    {
        if (api.GetPropertyComponentReference == null)
            return new(RuntimeStatus.ServiceUnavailable);
        ulong owner = 0;
        uint component = 0;
        RuntimeStatus status;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
            status = (RuntimeStatus)api.GetPropertyComponentReference(
                handle, text, &owner, &component);
        return new RuntimeResult<ComponentReference>(status, new ComponentReference
        {
            OwnerObjectId = owner,
            ComponentStableId = component,
        });
    }

    internal static RuntimeStatus SetPropertyComponentReference(ComponentHandle handle,
        string name, ComponentReference value)
    {
        if (api.SetPropertyComponentReference == null)
            return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(name + "\0"))
            return (RuntimeStatus)api.SetPropertyComponentReference(handle, text,
                value.OwnerObjectId, value.ComponentStableId);
    }

    internal static RuntimeResult<ComponentReference> ComponentToReference(ComponentHandle handle)
    {
        if (api.ComponentToReference == null) return new(RuntimeStatus.ServiceUnavailable);
        ulong owner = 0;
        uint component = 0;
        var status = (RuntimeStatus)api.ComponentToReference(handle, &owner, &component);
        return new RuntimeResult<ComponentReference>(status, new ComponentReference
        {
            OwnerObjectId = owner,
            ComponentStableId = component,
        });
    }

    internal static RuntimeResult<ComponentHandle> ResolveComponentReference(
        ComponentReference reference)
    {
        if (api.ResolveComponentReference == null)
            return new(RuntimeStatus.ServiceUnavailable);
        ComponentHandle handle = default;
        var status = (RuntimeStatus)api.ResolveComponentReference(
            reference.OwnerObjectId, reference.ComponentStableId, &handle);
        return new RuntimeResult<ComponentHandle>(status, handle);
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

    // ---- v11 生デバイス入力 / Scene / 診断 ------------------------------------

    private static RuntimeResult<bool> IntFlag(
        delegate* unmanaged[Cdecl]<int, int*, int> call, int argument)
    {
        if (call == null) return new(RuntimeStatus.ServiceUnavailable);
        int value = 0;
        return new RuntimeResult<bool>((RuntimeStatus)call(argument, &value), value != 0);
    }

    private static RuntimeResult<bool> IntFlag2(
        delegate* unmanaged[Cdecl]<int, int, int*, int> call, int a, int b)
    {
        if (call == null) return new(RuntimeStatus.ServiceUnavailable);
        int value = 0;
        return new RuntimeResult<bool>((RuntimeStatus)call(a, b, &value), value != 0);
    }

    internal static RuntimeResult<bool> InputKeyHeld(int key) => IntFlag(api.InputKeyHeld, key);
    internal static RuntimeResult<bool> InputKeyPressed(int key) => IntFlag(api.InputKeyPressed, key);
    internal static RuntimeResult<bool> InputKeyReleased(int key) => IntFlag(api.InputKeyReleased, key);
    internal static RuntimeResult<bool> InputMouseHeld(int button) => IntFlag(api.InputMouseHeld, button);
    internal static RuntimeResult<bool> InputMousePressed(int button) => IntFlag(api.InputMousePressed, button);
    internal static RuntimeResult<bool> InputMouseReleased(int button) => IntFlag(api.InputMouseReleased, button);

    internal static RuntimeResult<Vector2> InputPointerPosition()
    {
        if (api.InputPointerPosition == null) return new(RuntimeStatus.ServiceUnavailable);
        float x = 0.0f, y = 0.0f;
        var status = (RuntimeStatus)api.InputPointerPosition(&x, &y);
        return new RuntimeResult<Vector2>(status, new Vector2(x, y));
    }

    internal static RuntimeResult<float> InputWheelDelta()
    {
        if (api.InputWheelDelta == null) return new(RuntimeStatus.ServiceUnavailable);
        float value = 0.0f;
        return new RuntimeResult<float>((RuntimeStatus)api.InputWheelDelta(&value), value);
    }

    internal static RuntimeResult<bool> InputPadConnected(int slot)
        => IntFlag(api.InputPadConnected, slot);
    internal static RuntimeResult<bool> InputPadButtonHeld(int slot, int button)
        => IntFlag2(api.InputPadButtonHeld, slot, button);
    internal static RuntimeResult<bool> InputPadButtonPressed(int slot, int button)
        => IntFlag2(api.InputPadButtonPressed, slot, button);
    internal static RuntimeResult<bool> InputPadButtonReleased(int slot, int button)
        => IntFlag2(api.InputPadButtonReleased, slot, button);

    internal static RuntimeResult<float> InputPadAxis(int slot, int axis)
    {
        if (api.InputPadAxis == null) return new(RuntimeStatus.ServiceUnavailable);
        float value = 0.0f;
        return new RuntimeResult<float>((RuntimeStatus)api.InputPadAxis(slot, axis, &value), value);
    }

    internal static RuntimeStatus InputSetVibration(int slot, float low, float high)
    {
        if (api.InputSetVibration == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.InputSetVibration(slot, low, high);
    }

    internal static RuntimeResult<ulong> InstantiatePrefabTracked(string prefabAssetGuid,
        Vector3 position, Vector3 rotationEuler, Vector3 scale, ObjectHandle parent)
    {
        if (api.InstantiatePrefabTracked == null) return new(RuntimeStatus.ServiceUnavailable);
        ulong request = 0;
        fixed (byte* guid = Encoding.UTF8.GetBytes(prefabAssetGuid + "\0"))
            return new RuntimeResult<ulong>((RuntimeStatus)api.InstantiatePrefabTracked(
                guid, position, rotationEuler, scale, parent, &request), request);
    }

    internal static RuntimeResult<ObjectHandle> TakeSpawnResult(ulong request)
    {
        if (api.TakeSpawnResult == null) return new(RuntimeStatus.ServiceUnavailable);
        ObjectHandle value = default;
        return new RuntimeResult<ObjectHandle>(
            (RuntimeStatus)api.TakeSpawnResult(request, &value), value);
    }

    internal static RuntimeResult<string> GetCurrentSceneGuid()
    {
        if (api.GetCurrentSceneGuid == null) return new(RuntimeStatus.ServiceUnavailable);
        const int capacity = 256;
        byte* output = stackalloc byte[capacity];
        var status = (RuntimeStatus)api.GetCurrentSceneGuid(output, capacity);
        return status == RuntimeStatus.Ok
            ? new RuntimeResult<string>(status, FromUtf8(output))
            : new RuntimeResult<string>(status);
    }

    internal static RuntimeStatus QuitApplication(string reason)
    {
        if (api.QuitApplication == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* text = Encoding.UTF8.GetBytes(reason + "\0"))
            return (RuntimeStatus)api.QuitApplication(text);
    }

    internal static RuntimeResult<SceneTransitionInfo> GetSceneTransitionState()
    {
        if (api.GetSceneTransitionState == null)
            return new(RuntimeStatus.ServiceUnavailable);
        float progress = 0.0f;
        int inProgress = 0;
        int transitionStatus = (int)RuntimeStatus.ServiceUnavailable;
        var status = (RuntimeStatus)api.GetSceneTransitionState(
            &progress, &inProgress, &transitionStatus);
        return new RuntimeResult<SceneTransitionInfo>(status,
            new SceneTransitionInfo(progress, inProgress != 0,
                (RuntimeStatus)transitionStatus));
    }

    // ---- v19 Managed Behaviour の追加 --------------------------------------

    // 型 GUID を指定して ScriptComponent を足す。
    // Managed instance は既存のライフサイクルが作る。
    // TypeGuid は内部表現なので外へ出さない。呼び出し側は Type を渡す。
    internal static RuntimeResult<ComponentHandle> AddScriptComponentFor(ObjectHandle owner,
        Type behaviourType)
    {
        if (!TryGetScriptTypeGuid(behaviourType, out var type))
            return new(RuntimeStatus.ComponentNotFound);
        return AddScriptComponent(owner, type);
    }

    private static RuntimeResult<ComponentHandle> AddScriptComponent(ObjectHandle owner,
        TypeGuid type)
    {
        if (api.AddScriptComponent == null) return new(RuntimeStatus.ServiceUnavailable);
        ComponentHandle handle = default;
        var status = (RuntimeStatus)api.AddScriptComponent(owner, type.High, type.Low, &handle);
        return new RuntimeResult<ComponentHandle>(status, handle);
    }

    // 型から ReplayGuid を逆引きする。Assembly をロードしたときに作った表を使う。
    private static bool TryGetScriptTypeGuid(Type type, out TypeGuid guid)
    {
        foreach (var pair in Types)
        {
            if (pair.Value == type)
            {
                guid = pair.Key;
                return true;
            }
        }
        guid = default;
        return false;
    }

    // ---- v18 Component の生存 / Collider 解決 ------------------------------

    // Component がまだ生きているか。
    // GameObject が生きていても Component だけ壊れていることがある。
    internal static bool IsComponentAlive(ComponentHandle handle)
    {
        if (handle.IsEmpty || api.ComponentAlive == null) return false;
        int alive = 0;
        var status = (RuntimeStatus)api.ComponentAlive(handle, &alive);
        return status == RuntimeStatus.Ok && alive != 0;
    }

    // 接触イベントが持つ ColliderID から、その Collider の Handle を引く。
    internal static RuntimeResult<ComponentHandle> FindColliderComponent(
        ObjectHandle owner, uint colliderId)
    {
        if (api.FindColliderComponent == null) return new(RuntimeStatus.ServiceUnavailable);
        ComponentHandle handle = default;
        var status = (RuntimeStatus)api.FindColliderComponent(owner, colliderId, &handle);
        return new RuntimeResult<ComponentHandle>(status, handle);
    }

    // ---- v17 Landscape -----------------------------------------------------

    internal static RuntimeStatus LandscapeInfo(ComponentHandle handle,
        out int width, out int height, out float cellSize)
    {
        width = 0;
        height = 0;
        cellSize = 0.0f;
        if (api.LandscapeInfo == null) return RuntimeStatus.ServiceUnavailable;
        int w = 0;
        int h = 0;
        float cell = 0.0f;
        var status = (RuntimeStatus)api.LandscapeInfo(handle, &w, &h, &cell);
        width = w;
        height = h;
        cellSize = cell;
        return status;
    }

    internal static RuntimeResult<float> LandscapeGetHeight(ComponentHandle handle, int x, int z)
    {
        if (api.LandscapeGetHeight == null) return new(RuntimeStatus.ServiceUnavailable);
        float value = 0.0f;
        var status = (RuntimeStatus)api.LandscapeGetHeight(handle, x, z, &value);
        return new RuntimeResult<float>(status, value);
    }

    internal static RuntimeStatus LandscapeSetHeight(ComponentHandle handle, int x, int z,
        float value)
    {
        if (api.LandscapeSetHeight == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.LandscapeSetHeight(handle, x, z, value);
    }

    internal static RuntimeResult<float> LandscapeSampleHeight(ComponentHandle handle,
        float localX, float localZ)
    {
        if (api.LandscapeSampleHeight == null) return new(RuntimeStatus.ServiceUnavailable);
        float value = 0.0f;
        var status = (RuntimeStatus)api.LandscapeSampleHeight(handle, localX, localZ, &value);
        return new RuntimeResult<float>(status, value);
    }

    internal static RuntimeStatus LandscapeSculpt(ComponentHandle handle, Vector3 localCenter,
        int mode, int direction, float radius, float strength, float falloff,
        float flattenHeight, float noiseScale, float deltaTime)
    {
        if (api.LandscapeSculpt == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.LandscapeSculpt(handle, localCenter, mode, direction,
            radius, strength, falloff, flattenHeight, noiseScale, deltaTime);
    }

    internal static RuntimeStatus LandscapeRaycast(ComponentHandle handle, Vector3 localOrigin,
        Vector3 localDirection, float maxDistance, out Vector3 position, out Vector3 normal,
        out float distance)
    {
        position = default;
        normal = new Vector3(0.0f, 1.0f, 0.0f);
        distance = 0.0f;
        if (api.LandscapeRaycast == null) return RuntimeStatus.ServiceUnavailable;
        Vector3 hitPosition = default;
        Vector3 hitNormal = default;
        float hitDistance = 0.0f;
        var status = (RuntimeStatus)api.LandscapeRaycast(handle, localOrigin, localDirection,
            maxDistance, &hitPosition, &hitNormal, &hitDistance);
        position = hitPosition;
        normal = hitNormal;
        distance = hitDistance;
        return status;
    }

    // ---- 実行文脈に依存しない生死・有効判定 --------------------------------
    //
    // 【なぜ ScriptExecutionContext を通さないか】
    //   Inspector / Serialization / Scene 保存は Managed callback の外から走る。
    //   「いま callback 中か」で答えが変わる判定を identity に混ぜると、
    //   生きている GameObject が保存時だけ死んで見えて参照が空になる。
    //   Handle の有効性は World と世代番号だけで決まる話なので、
    //   関数ポインタ表へ直接聞く。Current Context は見ない。

    internal static bool IsHandleAlive(ObjectHandle handle)
        => !handle.IsEmpty && IsGameObjectValid(handle) == RuntimeStatus.Ok;

    // 親を辿って 1 つでも無効なら false。Unity の activeInHierarchy と同じ意味。
    internal static bool ActiveInHierarchy(ObjectHandle handle)
    {
        var current = handle;
        for (var depth = 0; depth < 256 && !current.IsEmpty; ++depth)
        {
            var enabled = IsGameObjectEnabled(current);
            if (!enabled.Succeeded || !enabled.Value) return false;
            var parent = GetParent(current);
            if (!parent.Succeeded || parent.Value.IsEmpty) return true;
            current = parent.Value;
        }
        return true;
    }

    // ComponentHandle からその Managed instance を引く。
    //
    // 同じ GameObject に同じ型の MonoBehaviour が 2 つ付いていても、
    // ScriptComponent が別なので取り違えない。
    // 型で探す FindOn は 1 つ目を返してしまうので、参照の復元には使わない。
    internal static object? FindManagedTarget(ComponentHandle component)
    {
        if (component.IsEmpty) return null;
        if (!InstancesByComponent.TryGetValue(new ComponentInstanceKey(component),
            out var behaviour))
            return null;
        return behaviour is MonoBehaviourHost host ? host.Target : behaviour;
    }

    internal static RuntimeResult<T> GetBehaviour<T>(ComponentHandle component)
        where T : ScriptBehaviour
    {
        if (component.IsEmpty) return new(RuntimeStatus.InvalidHandle);
        if (!InstancesByComponent.TryGetValue(new ComponentInstanceKey(component),
            out var behaviour))
            return new(RuntimeStatus.ComponentNotFound);
        return behaviour is T typed
            ? new RuntimeResult<T>(RuntimeStatus.Ok, typed)
            : new RuntimeResult<T>(RuntimeStatus.TypeMismatch);
    }

    internal static RuntimeResult<T[]> GetBehaviours<T>(ObjectHandle owner)
        where T : ScriptBehaviour
    {
        if (owner.IsEmpty) return new(RuntimeStatus.InvalidHandle, Array.Empty<T>());
        var found = new List<T>();
        foreach (var pair in InstancesByComponent)
        {
            var behaviour = pair.Value;
            var candidate = behaviour.GameObject;
            if (candidate.World == owner.World && candidate.Object == owner.Object &&
                candidate.Generation == owner.Generation && behaviour is T typed)
                found.Add(typed);
        }
        return found.Count == 0
            ? new RuntimeResult<T[]>(RuntimeStatus.ComponentNotFound, Array.Empty<T>())
            : new RuntimeResult<T[]>(RuntimeStatus.Ok, found.ToArray());
    }

    internal static RuntimeResult<ulong> EventDroppedCount(EventSubscription subscription)
    {
        if (api.EventDroppedCount == null) return new(RuntimeStatus.ServiceUnavailable);
        ulong value = 0;
        return new RuntimeResult<ulong>(
            (RuntimeStatus)api.EventDroppedCount(subscription.Id, &value), value);
    }
}
