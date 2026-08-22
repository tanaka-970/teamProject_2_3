using System;

namespace ReplayEngine;

// GameObject 1 つぶんの Transform 入口。
//
// Local / World の両方を同じ形で扱えるようにする。回転の正本は
// C++ の Transform（ローカルはラジアンのオイラー角）なので、ここでも同じ単位を使う。
public readonly struct TransformAccess
{
    public TransformAccess(ObjectHandle handle)
    {
        Handle = handle;
    }

    public ObjectHandle Handle { get; }
    public bool IsValid => !Handle.IsEmpty;

    // ---- Local ---------------------------------------------------------------

    public Vector3 LocalPosition
    {
        get => Unwrap(NativeBridge.GetLocalPosition(Handle));
        set => NativeBridge.SetLocalPosition(Handle, value);
    }

    // ラジアン。
    public Vector3 LocalRotationEuler
    {
        get => Unwrap(NativeBridge.GetLocalRotationEuler(Handle));
        set => NativeBridge.SetLocalRotationEuler(Handle, value);
    }

    public Vector3 LocalScale
    {
        get => Unwrap(NativeBridge.GetLocalScale(Handle), new Vector3(1.0f, 1.0f, 1.0f));
        set => NativeBridge.SetLocalScale(Handle, value);
    }

    // ---- World ---------------------------------------------------------------

    public Vector3 Position
    {
        get => Unwrap(NativeBridge.GetWorldPosition(Handle));
        set => NativeBridge.SetWorldPosition(Handle, value);
    }

    public Quaternion Rotation
    {
        get
        {
            var result = NativeBridge.GetWorldRotation(Handle);
            return result.Succeeded ? result.Value : new Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
        }
        set => NativeBridge.SetWorldRotation(Handle, value);
    }

    public Vector3 Scale
    {
        get => Unwrap(NativeBridge.GetWorldScale(Handle), new Vector3(1.0f, 1.0f, 1.0f));
        set => NativeBridge.SetWorldScale(Handle, value);
    }

    // ---- 方向 -----------------------------------------------------------------

    public Vector3 Forward => Axes().forward;
    public Vector3 Right => Axes().right;
    public Vector3 Up => Axes().up;

    public (Vector3 forward, Vector3 right, Vector3 up) Axes()
    {
        NativeBridge.GetWorldAxes(Handle, out var forward, out var right, out var up);
        return (forward, right, up);
    }

    // ---- 操作 -----------------------------------------------------------------

    // relativeToSelf = true なら自分の向きを基準に、false ならワールド軸で動かす。
    public RuntimeStatus Translate(Vector3 delta, bool relativeToSelf = false)
    {
        if (!relativeToSelf)
        {
            var current = Position;
            return NativeBridge.SetWorldPosition(Handle,
                new Vector3(current.X + delta.X, current.Y + delta.Y, current.Z + delta.Z));
        }

        var (forward, right, up) = Axes();
        var origin = Position;
        return NativeBridge.SetWorldPosition(Handle, new Vector3(
            origin.X + right.X * delta.X + up.X * delta.Y + forward.X * delta.Z,
            origin.Y + right.Y * delta.X + up.Y * delta.Y + forward.Y * delta.Z,
            origin.Z + right.Z * delta.X + up.Z * delta.Y + forward.Z * delta.Z));
    }

    // ローカルのオイラー角へ加算する。単位はラジアン。
    public RuntimeStatus Rotate(Vector3 deltaEuler)
    {
        var current = LocalRotationEuler;
        return NativeBridge.SetLocalRotationEuler(Handle, new Vector3(
            current.X + deltaEuler.X,
            current.Y + deltaEuler.Y,
            current.Z + deltaEuler.Z));
    }

    public RuntimeStatus LookAt(Vector3 target)
        => NativeBridge.LookAt(Handle, target, new Vector3(0.0f, 1.0f, 0.0f));

    public RuntimeStatus LookAt(Vector3 target, Vector3 worldUp)
        => NativeBridge.LookAt(Handle, target, worldUp);

    // ---- 親子 -----------------------------------------------------------------

    public RuntimeResult<ObjectHandle> Parent() => NativeBridge.GetParent(Handle);
    public RuntimeResult<ObjectHandle[]> Children() => NativeBridge.GetChildren(Handle);
    public RuntimeStatus SetParent(ObjectHandle parent, bool preserveWorldTransform = true)
        => NativeBridge.SetParent(Handle, parent, preserveWorldTransform);

    private static Vector3 Unwrap(RuntimeResult<Vector3> result, Vector3 fallback = default)
        => result.Succeeded ? result.Value : fallback;
}
