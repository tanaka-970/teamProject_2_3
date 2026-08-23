using System;

namespace ReplayEngine;

// Engine が発行する組み込みイベントの型 GUID。
//
// C++ の ReplayEngine::Runtime::EngineEvents と同じ値。
// 文字列を手で書かないための唯一の置き場所。
public static class EngineEventIds
{
    public const string BeforeSceneUnload = "a1000000000000000000000000000001";
    public const string SceneLoadRequested = "a1000000000000000000000000000002";
    public const string SceneLoadStarted = "a1000000000000000000000000000003";
    public const string SceneLoaded = "a1000000000000000000000000000004";
    public const string SceneLoadFailed = "a1000000000000000000000000000005";
    public const string WorldChanged = "a1000000000000000000000000000006";
    public const string ApplicationQuitRequested = "a1000000000000000000000000000007";
    public const string MotionEvent = "a1000000000000000000000000000008";
    public const string ButtonStateChanged = "a1000000000000000000000000000009";
    public const string StateChanged = "a1000000000000000000000000000010";
    public const string InputFieldSubmitted = "a1000000000000000000000000000011";
    public const string InputFieldCanceled = "a1000000000000000000000000000012";
    public const string CompositionMarker = "a1000000000000000000000000000013";
    public const string CollisionEnter = "a1000000000000000000000000000014";
    public const string CollisionStay = "a1000000000000000000000000000015";
    public const string CollisionExit = "a1000000000000000000000000000016";
    public const string TriggerEnter = "a1000000000000000000000000000017";
    public const string TriggerStay = "a1000000000000000000000000000018";
    public const string TriggerExit = "a1000000000000000000000000000019";
}

// 接触 1 件分。OnCollisionEnter などが受け取る。
public readonly struct CollisionInfo
{
    internal CollisionInfo(RuntimeEvent source)
    {
        Other = source.Target;
        source.TryGetDouble("point_x", out var px);
        source.TryGetDouble("point_y", out var py);
        source.TryGetDouble("point_z", out var pz);
        source.TryGetDouble("normal_x", out var nx);
        source.TryGetDouble("normal_y", out var ny);
        source.TryGetDouble("normal_z", out var nz);
        source.TryGetInt("hit_kind", out var kind);
        source.TryGetInt("self_collider", out var selfCollider);
        source.TryGetInt("other_collider", out var collider);
        source.TryGetDouble("relative_velocity_x", out var rvx);
        source.TryGetDouble("relative_velocity_y", out var rvy);
        source.TryGetDouble("relative_velocity_z", out var rvz);
        source.TryGetDouble("penetration_depth", out var penetration);
        source.TryGetBool("other_valid", out var valid);
        ContactPoint = new Vector3((float)px, (float)py, (float)pz);
        ContactNormal = new Vector3((float)nx, (float)ny, (float)nz);
        HitKind = kind;
        SelfColliderId = selfCollider;
        OtherColliderId = collider;
        RelativeVelocity = new Vector3((float)rvx, (float)rvy, (float)rvz);
        PenetrationDepth = (float)penetration;
        OtherValid = valid;
    }

    // 相手の GameObject。Exit では既に破棄されていることがある（OtherValid を見る）。
    public ObjectHandle Other { get; }
    public bool OtherValid { get; }
    public Vector3 ContactPoint { get; }
    public Vector3 ContactNormal { get; }
    public int HitKind { get; }
    public int SelfColliderId { get; }
    public int OtherColliderId { get; }
    public Vector3 RelativeVelocity { get; }
    public float PenetrationDepth { get; }
}

// Trigger の接触 1 件分。
public readonly struct TriggerInfo
{
    internal TriggerInfo(RuntimeEvent source)
    {
        Other = source.Target;
        source.TryGetInt("self_collider", out var self);
        source.TryGetInt("other_collider", out var other);
        source.TryGetBool("self_is_trigger", out var isTrigger);
        source.TryGetBool("other_valid", out var valid);
        SelfColliderId = self;
        OtherColliderId = other;
        SelfIsTrigger = isTrigger;
        OtherValid = valid;
    }

    public ObjectHandle Other { get; }
    public bool OtherValid { get; }
    public int SelfColliderId { get; }
    public int OtherColliderId { get; }

    // 自分側が Trigger Collider なら true。相手が入ってきた側なら false。
    public bool SelfIsTrigger { get; }
}

// Scene 遷移イベント 1 件分。
public readonly struct SceneEventInfo
{
    internal SceneEventInfo(RuntimeEvent source)
    {
        TypeGuid = source.TypeGuid;
        source.TryGetString("scene", out var scene);
        SceneAssetGuid = scene;
        FrameIndex = source.FrameIndex;
    }

    public string TypeGuid { get; }
    public string SceneAssetGuid { get; }
    public ulong FrameIndex { get; }
}
