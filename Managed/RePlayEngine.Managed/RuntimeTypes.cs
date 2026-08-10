using System.Runtime.InteropServices;

namespace ReplayEngine;

public enum RuntimeStatus : int
{
    Ok = 0,
    InvalidHandle = 1,
    WrongWorld = 2,
    ObjectDestroyed = 3,
    ComponentDestroyed = 4,
    ComponentNotFound = 5,
    TypeMismatch = 6,
    AssetMissing = 7,
    InvalidAssetType = 8,
    SceneMissing = 9,
    SceneLoadFailed = 10,
    TransitionInProgress = 11,
    UnsupportedOperation = 12,
    ServiceUnavailable = 13,
    InvalidArgument = 14,
    DeferredOperationRejected = 15,
}

[StructLayout(LayoutKind.Sequential)]
public struct ObjectHandle
{
    public ulong World;
    public ulong Object;
    public uint Generation;

    public bool IsEmpty => World == 0 || Object == 0 || Generation == 0;
}

[StructLayout(LayoutKind.Sequential)]
public struct ComponentHandle
{
    public ObjectHandle Owner;
    public ulong Instance;
    public uint TypeId;

    public bool IsEmpty => Owner.IsEmpty || Instance == 0;
}

public readonly struct MotionPlayer
{
    internal MotionPlayer(ComponentHandle handle)
    {
        Handle = handle;
    }

    internal ComponentHandle Handle { get; }
    public bool IsValid => !Handle.IsEmpty;

    public RuntimeStatus Play() => NativeBridge.MotionPlay(Handle);
    public RuntimeStatus PlayFrom(float seconds) => NativeBridge.MotionPlayFrom(Handle, seconds);
    public RuntimeStatus Pause() => NativeBridge.MotionPause(Handle);
    public RuntimeStatus Resume() => NativeBridge.MotionResume(Handle);
    public RuntimeStatus Stop() => NativeBridge.MotionStop(Handle);
    public RuntimeStatus Reverse() => NativeBridge.MotionReverse(Handle);
    public RuntimeStatus SetTime(float seconds) => NativeBridge.MotionSetTime(Handle, seconds);
    public RuntimeStatus SetSpeed(float speed) => NativeBridge.MotionSetSpeed(Handle, speed);
    public RuntimeStatus SetWeight(float weight) => NativeBridge.MotionSetWeight(Handle, weight);
    public RuntimeResult<bool> IsPlaying() => NativeBridge.MotionIsPlaying(Handle);
    public RuntimeResult<float> GetTime() => NativeBridge.MotionGetTime(Handle);
    public RuntimeResult<float> GetDuration() => NativeBridge.MotionGetDuration(Handle);
}

[StructLayout(LayoutKind.Sequential)]
public struct ObjectReference
{
    public ulong ObjectId;

    public bool IsAssigned => ObjectId != 0;
}

[StructLayout(LayoutKind.Sequential)]
public struct ComponentReference
{
    public ulong OwnerObjectId;
    public uint ComponentStableId;

    public bool IsAssigned => OwnerObjectId != 0 && ComponentStableId != 0;
}


[StructLayout(LayoutKind.Sequential)]
public struct RaycastHit
{
    public Vector3 Point;
    public Vector3 Normal;
    public float Distance;
    public ObjectHandle Object;
    public uint ColliderId;
    private int valid;

    public bool Valid => valid != 0;
}

public readonly struct RuntimeResult<T>
{
    public RuntimeResult(RuntimeStatus status, T value = default!)
    {
        Status = status;
        Value = value;
    }

    public RuntimeStatus Status { get; }
    public T Value { get; }
    public bool Succeeded => Status == RuntimeStatus.Ok;
}

public readonly struct EventSubscription
{
    internal EventSubscription(ulong id)
    {
        Id = id;
    }

    internal ulong Id { get; }
    public bool IsValid => Id != 0;
}

public readonly struct RuntimeEvent
{
    internal RuntimeEvent(string typeGuid, string typeName, ObjectHandle source, ObjectHandle target, ulong frameIndex)
    {
        TypeGuid = typeGuid;
        TypeName = typeName;
        Source = source;
        Target = target;
        FrameIndex = frameIndex;
    }

    public string TypeGuid { get; }
    public string TypeName { get; }
    public ObjectHandle Source { get; }
    public ObjectHandle Target { get; }
    public ulong FrameIndex { get; }
}
