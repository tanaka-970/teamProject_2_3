using System;

namespace ReplayEngine;

// 角度の単位変換をここ 1 か所に集める。
//
// 【なぜ変換層を挟むか】
//   C++ の Transform とその上の TransformAccess はラジアンが正本で、
//   既存スクリプトはその意味で書かれている。単位を変えると黙って壊れる。
//   一方 Unity 風 API は degree の方が人間に自然なので、
//   Public Authoring 層の入口と出口だけで変換する。
//   Legacy の TransformAccess はラジアンのまま何も変えていない。
internal static class UnityAngles
{
    internal const float DegreeToRadian = MathF.PI / 180.0f;
    internal const float RadianToDegree = 180.0f / MathF.PI;

    internal static Vector3 ToDegrees(Vector3 radians) => new(
        radians.X * RadianToDegree, radians.Y * RadianToDegree, radians.Z * RadianToDegree);

    internal static Vector3 ToRadians(Vector3 degrees) => new(
        degrees.X * DegreeToRadian, degrees.Y * DegreeToRadian, degrees.Z * DegreeToRadian);

    // Quaternion からオイラー角（ラジアン）へ。
    // Quaternion.Euler が Y(yaw) -> X(pitch) -> Z(roll) の順で組むので、
    // 取り出しも同じ順で行う。順番を変えると往復で値が変わる。
    internal static Vector3 ToEulerRadians(Quaternion rotation)
    {
        var q = rotation.Normalized;
        var sinPitch = 2.0f * (q.W * q.X - q.Y * q.Z);
        sinPitch = Mathf.Clamp(sinPitch, -1.0f, 1.0f);
        var pitch = MathF.Asin(sinPitch);

        // 真上・真下を向くとヨーとロールが縮退する。分離できないので roll を 0 にする。
        if (MathF.Abs(sinPitch) > 0.9999f)
        {
            var degenerateYaw = MathF.Atan2(2.0f * (q.W * q.Y + q.X * q.Z),
                1.0f - 2.0f * (q.X * q.X + q.Y * q.Y));
            return new Vector3(pitch, degenerateYaw, 0.0f);
        }

        var yaw = MathF.Atan2(2.0f * (q.W * q.Y + q.Z * q.X),
            1.0f - 2.0f * (q.X * q.X + q.Y * q.Y));
        var roll = MathF.Atan2(2.0f * (q.W * q.Z + q.X * q.Y),
            1.0f - 2.0f * (q.Z * q.Z + q.X * q.X));
        return new Vector3(pitch, yaw, roll);
    }

    internal static Quaternion FromEulerDegrees(Vector3 degrees)
        => Quaternion.Euler(ToRadians(degrees));

    internal static Vector3 ToEulerDegrees(Quaternion rotation)
        => ToDegrees(ToEulerRadians(rotation));
}

// Unity 風の Transform。
//
// 値の読み書きは既存 TransformAccess をそのまま使う。
// ここで足しているのは degree 変換と Unity と同じ名前だけ。
public sealed class Transform : Component
{
    private readonly GameObject owner;

    internal Transform(GameObject owner) => this.owner = owner;

    internal ObjectHandle Handle => owner.Handle;
    internal TransformAccess Access => new(owner.Handle);

    public override GameObject gameObject => owner;

    internal override bool IsAlive => owner.IsAlive;
    internal override bool SameTarget(Object other)
        => other is Transform transform && owner.SameTarget(transform.owner);
    internal override int IdentityHash() => owner.IdentityHash() ^ 0x7A5F;
    internal override void DestroySelf() { }

    // ---- 位置・回転・拡大 -----------------------------------------------------

    public Vector3 position
    {
        get => Access.Position;
        set { var binding = Access; binding.Position = value; }
    }

    public Quaternion rotation
    {
        get => Access.Rotation;
        set { var binding = Access; binding.Rotation = value; }
    }

    // degree。Unity と同じ。
    public Vector3 eulerAngles
    {
        get => UnityAngles.ToEulerDegrees(Access.Rotation);
        set { var binding = Access; binding.Rotation = UnityAngles.FromEulerDegrees(value); }
    }

    public Vector3 localPosition
    {
        get => Access.LocalPosition;
        set { var binding = Access; binding.LocalPosition = value; }
    }

    public Quaternion localRotation
    {
        get => Quaternion.Euler(Access.LocalRotationEuler);
        set { var binding = Access; binding.LocalRotationEuler = UnityAngles.ToEulerRadians(value); }
    }

    // degree。内部の LocalRotationEuler はラジアンなのでここで変換する。
    public Vector3 localEulerAngles
    {
        get => UnityAngles.ToDegrees(Access.LocalRotationEuler);
        set { var binding = Access; binding.LocalRotationEuler = UnityAngles.ToRadians(value); }
    }

    public Vector3 localScale
    {
        get => Access.LocalScale;
        set { var binding = Access; binding.LocalScale = value; }
    }

    public Vector3 lossyScale => Access.Scale;

    // ---- 方向 -----------------------------------------------------------------

    public Vector3 forward => Access.Forward;
    public Vector3 right => Access.Right;
    public Vector3 up => Access.Up;

    // ---- 親子 -----------------------------------------------------------------

    public Transform? parent
    {
        get
        {
            var result = Access.Parent();
            if (!result.Succeeded || result.Value.IsEmpty) return null;
            return GameObject.Wrap(result.Value)?.transform;
        }
        set => SetParent(value);
    }

    public int childCount
    {
        get
        {
            var result = Access.Children();
            return result.Succeeded ? result.Value.Length : 0;
        }
    }

    public Transform? GetChild(int index)
    {
        var result = Access.Children();
        if (!result.Succeeded || index < 0 || index >= result.Value.Length) return null;
        return GameObject.Wrap(result.Value[index])?.transform;
    }

    public void SetParent(Transform? newParent, bool worldPositionStays = true)
        => Access.SetParent(newParent != null ? newParent.Handle : default, worldPositionStays);

    public Transform? Find(string childName)
    {
        if (string.IsNullOrEmpty(childName)) return null;
        var children = Access.Children();
        if (!children.Succeeded) return null;
        foreach (var child in children.Value)
        {
            var wrapped = GameObject.Wrap(child);
            if (wrapped != null && string.Equals(wrapped.name, childName, StringComparison.Ordinal))
                return wrapped.transform;
        }
        return null;
    }

    // ---- 操作 -----------------------------------------------------------------

    public void Translate(Vector3 translation) => Access.Translate(translation);

    public void Translate(Vector3 translation, Space relativeTo)
        => Access.Translate(translation, relativeTo == Space.Self);

    public void Translate(float x, float y, float z) => Translate(new Vector3(x, y, z));

    // degree。Unity と同じ。
    public void Rotate(Vector3 eulerDegrees) => Access.Rotate(UnityAngles.ToRadians(eulerDegrees));

    public void Rotate(float xDegrees, float yDegrees, float zDegrees)
        => Rotate(new Vector3(xDegrees, yDegrees, zDegrees));

    public void LookAt(Transform? target)
    {
        if (target == null) return;
        Access.LookAt(target.position);
    }

    public void LookAt(Vector3 worldPosition) => Access.LookAt(worldPosition);

    public void LookAt(Vector3 worldPosition, Vector3 worldUp) => Access.LookAt(worldPosition, worldUp);
}

public enum Space
{
    World = 0,
    Self = 1,
}
