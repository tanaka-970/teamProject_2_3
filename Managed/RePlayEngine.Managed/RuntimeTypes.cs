using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

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
    SaveSlotNotFound = 16,
    SaveKeyNotFound = 17,
    SaveTypeMismatch = 18,
    SaveCorrupt = 19,
    SaveIOFailure = 20,
    ComponentDependencyMissing = 21,
    ComponentHasDependents = 22,
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

public readonly struct AudioVoice
{
    internal AudioVoice(ulong id)
    {
        Id = id;
    }

    internal ulong Id { get; }
    public bool IsValid => Id != 0;

    public RuntimeStatus Stop() => NativeBridge.StopAudio(this);
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

public enum UIFocusDirection : int
{
    Up = 0,
    Down = 1,
    Left = 2,
    Right = 3,
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

        Payload = NativeBridge.ConsumeParsedEventPayload();
    }

    public string TypeGuid { get; }
    public string TypeName { get; }
    public ObjectHandle Source { get; }
    public ObjectHandle Target { get; }
    public ulong FrameIndex { get; }

    public RuntimeEventPayload Payload { get; }

    public bool TryGetBool(string key, out bool value)
    {
        if (Payload != null) return Payload.TryGetBool(key, out value);
        value = false;
        return false;
    }

    public bool TryGetInt(string key, out int value)
    {
        if (Payload != null) return Payload.TryGetInt(key, out value);
        value = 0;
        return false;
    }

    public bool TryGetDouble(string key, out double value)
    {
        if (Payload != null) return Payload.TryGetDouble(key, out value);
        value = 0.0;
        return false;
    }
    public bool TryGetString(string key, out string value)
    {
        if (Payload != null) return Payload.TryGetString(key, out value);
        value = string.Empty;
        return false;
    }
}


[StructLayout(LayoutKind.Sequential)]
public struct GroundHit
{
    public Vector3 Position;
    public Vector3 Normal;
    public ObjectHandle Object;
    public uint ColliderId;
    private int valid;

    public bool Valid => valid != 0;
}

[StructLayout(LayoutKind.Sequential)]
public struct SphereSweepHit
{
    public Vector3 Center;
    public Vector3 Normal;
    public float Fraction;
    public ObjectHandle Object;
    public uint ColliderId;
    private int valid;

    public bool Valid => valid != 0;
}


public sealed class RuntimeEventPayload
{
    private enum ValueKind
    {
        Bool,
        Int,
        Double,
        String,
    }

    private readonly struct ValueEntry
    {
        internal ValueEntry(ValueKind kind, object value)
        {
            Kind = kind;
            Value = value;
        }

        internal ValueKind Kind { get; }
        internal object Value { get; }
    }

    private readonly Dictionary<string, ValueEntry> values =
        new(StringComparer.Ordinal);

    public RuntimeEventPayload SetBool(string key, bool value)
    {
        values[key ?? string.Empty] = new ValueEntry(ValueKind.Bool, value);
        return this;
    }

    public RuntimeEventPayload SetInt(string key, int value)
    {
        values[key ?? string.Empty] = new ValueEntry(ValueKind.Int, value);
        return this;
    }

    public RuntimeEventPayload SetDouble(string key, double value)
    {
        values[key ?? string.Empty] = new ValueEntry(ValueKind.Double, value);
        return this;
    }

    public RuntimeEventPayload SetString(string key, string value)
    {
        values[key ?? string.Empty] = new ValueEntry(ValueKind.String, value ?? string.Empty);
        return this;
    }

    public bool TryGetBool(string key, out bool value)
    {
        if (values.TryGetValue(key ?? string.Empty, out var entry) &&
            entry.Kind == ValueKind.Bool)
        {
            value = (bool)entry.Value;
            return true;
        }
        value = false;
        return false;
    }

    public bool TryGetInt(string key, out int value)
    {
        if (values.TryGetValue(key ?? string.Empty, out var entry) &&
            entry.Kind == ValueKind.Int)
        {
            value = (int)entry.Value;
            return true;
        }
        value = 0;
        return false;
    }

    public bool TryGetDouble(string key, out double value)
    {
        if (values.TryGetValue(key ?? string.Empty, out var entry) &&
            entry.Kind == ValueKind.Double)
        {
            value = (double)entry.Value;
            return true;
        }
        value = 0.0;
        return false;
    }

    public bool TryGetString(string key, out string value)
    {
        if (values.TryGetValue(key ?? string.Empty, out var entry) &&
            entry.Kind == ValueKind.String)
        {
            value = (string)entry.Value;
            return true;
        }
        value = string.Empty;
        return false;
    }

    internal string Serialize()
    {
        var builder = new StringBuilder();
        foreach (var pair in values)
        {
            var key = Hex(pair.Key);
            switch (pair.Value.Kind)
            {
                case ValueKind.Bool:
                    builder.Append("b:").Append(key).Append(':')
                        .Append((bool)pair.Value.Value ? '1' : '0').Append('\n');
                    break;
                case ValueKind.Int:
                    builder.Append("i:").Append(key).Append(':')
                        .Append(((int)pair.Value.Value).ToString(CultureInfo.InvariantCulture))
                        .Append('\n');
                    break;
                case ValueKind.Double:
                    builder.Append("d:").Append(key).Append(':')
                        .Append(((double)pair.Value.Value).ToString("R", CultureInfo.InvariantCulture))
                        .Append('\n');
                    break;
                case ValueKind.String:
                    builder.Append("s:").Append(key).Append(':')
                        .Append(Hex((string)pair.Value.Value)).Append('\n');
                    break;
            }
        }
        return builder.ToString();
    }

    internal bool TryAddEncoded(string text)
    {
        var parts = text.Split(':', 3, StringSplitOptions.None);
        if (parts.Length != 3 || parts[0].Length != 1) return false;
        if (!TryUnhex(parts[1], out var key)) return false;

        switch (parts[0][0])
        {
            case 'b':
                if (parts[2] == "1") { SetBool(key, true); return true; }
                if (parts[2] == "0") { SetBool(key, false); return true; }
                return false;
            case 'i':
                if (!int.TryParse(parts[2], NumberStyles.Integer,
                    CultureInfo.InvariantCulture, out var intValue)) return false;
                SetInt(key, intValue);
                return true;
            case 'd':
                if (!double.TryParse(parts[2], NumberStyles.Float,
                    CultureInfo.InvariantCulture, out var doubleValue) ||
                    double.IsNaN(doubleValue) || double.IsInfinity(doubleValue)) return false;
                SetDouble(key, doubleValue);
                return true;
            case 's':
                if (!TryUnhex(parts[2], out var stringValue)) return false;
                SetString(key, stringValue);
                return true;
            default:
                return false;
        }
    }

    private static string Hex(string text)
        => Convert.ToHexString(Encoding.UTF8.GetBytes(text));

    private static bool TryUnhex(string text, out string value)
    {
        try
        {
            value = Encoding.UTF8.GetString(Convert.FromHexString(text));
            return true;
        }
        catch (FormatException)
        {
            value = string.Empty;
            return false;
        }
    }
}
