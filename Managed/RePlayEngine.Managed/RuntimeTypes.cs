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
