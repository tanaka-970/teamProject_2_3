using System;
using System.Runtime.InteropServices;

namespace ReplayEngine;

[StructLayout(LayoutKind.Sequential)]
public struct Vector2
{
    public float X;
    public float Y;

    public Vector2(float x, float y)
    {
        X = x;
        Y = y;
    }

    public static Vector2 Zero => default;
    public static Vector2 One => new(1.0f, 1.0f);
    public float SqrMagnitude => X * X + Y * Y;
    public float Magnitude => MathF.Sqrt(SqrMagnitude);
    public Vector2 Normalized => Magnitude > Mathf.Epsilon ? this / Magnitude : Zero;
    public static float Dot(Vector2 a, Vector2 b) => a.X * b.X + a.Y * b.Y;
    public static float Distance(Vector2 a, Vector2 b) => (a - b).Magnitude;
    public static Vector2 Lerp(Vector2 a, Vector2 b, float t) => a + (b - a) * Mathf.Clamp01(t);
    public static Vector2 operator +(Vector2 a, Vector2 b) => new(a.X + b.X, a.Y + b.Y);
    public static Vector2 operator -(Vector2 a, Vector2 b) => new(a.X - b.X, a.Y - b.Y);
    public static Vector2 operator -(Vector2 value) => new(-value.X, -value.Y);
    public static Vector2 operator *(Vector2 value, float scale) => new(value.X * scale, value.Y * scale);
    public static Vector2 operator *(float scale, Vector2 value) => value * scale;
    public static Vector2 operator /(Vector2 value, float scale) => new(value.X / scale, value.Y / scale);
}

[StructLayout(LayoutKind.Sequential)]
public struct Vector3
{
    public float X;
    public float Y;
    public float Z;

    public Vector3(float x, float y, float z)
    {
        X = x;
        Y = y;
        Z = z;
    }

    public static Vector3 Zero => default;
    public static Vector3 One => new(1.0f, 1.0f, 1.0f);
    public static Vector3 Right => new(1.0f, 0.0f, 0.0f);
    public static Vector3 Up => new(0.0f, 1.0f, 0.0f);
    public static Vector3 Forward => new(0.0f, 0.0f, 1.0f);
    public float SqrMagnitude => X * X + Y * Y + Z * Z;
    public float Magnitude => MathF.Sqrt(SqrMagnitude);
    public Vector3 Normalized => Magnitude > Mathf.Epsilon ? this / Magnitude : Zero;
    public static float Dot(Vector3 a, Vector3 b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;
    public static Vector3 Cross(Vector3 a, Vector3 b) => new(
        a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);
    public static float Distance(Vector3 a, Vector3 b) => (a - b).Magnitude;
    public static Vector3 Lerp(Vector3 a, Vector3 b, float t) => a + (b - a) * Mathf.Clamp01(t);
    public static Vector3 MoveTowards(Vector3 current, Vector3 target, float maxDistanceDelta)
    {
        var delta = target - current;
        var distance = delta.Magnitude;
        return distance <= maxDistanceDelta || distance <= Mathf.Epsilon
            ? target : current + delta / distance * maxDistanceDelta;
    }
    public static Vector3 operator +(Vector3 a, Vector3 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    public static Vector3 operator -(Vector3 a, Vector3 b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
    public static Vector3 operator -(Vector3 value) => new(-value.X, -value.Y, -value.Z);
    public static Vector3 operator *(Vector3 value, float scale) => new(value.X * scale, value.Y * scale, value.Z * scale);
    public static Vector3 operator *(float scale, Vector3 value) => value * scale;
    public static Vector3 operator /(Vector3 value, float scale) => new(value.X / scale, value.Y / scale, value.Z / scale);
}

[StructLayout(LayoutKind.Sequential)]
public struct Vector4
{
    public float X;
    public float Y;
    public float Z;
    public float W;

    public Vector4(float x, float y, float z, float w)
    {
        X = x;
        Y = y;
        Z = z;
        W = w;
    }

    public static Vector4 Zero => default;
    public float SqrMagnitude => X * X + Y * Y + Z * Z + W * W;
    public float Magnitude => MathF.Sqrt(SqrMagnitude);
    public Vector4 Normalized => Magnitude > Mathf.Epsilon ? this / Magnitude : Zero;
    public static float Dot(Vector4 a, Vector4 b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W;
    public static Vector4 Lerp(Vector4 a, Vector4 b, float t) => a + (b - a) * Mathf.Clamp01(t);
    public static Vector4 operator +(Vector4 a, Vector4 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z, a.W + b.W);
    public static Vector4 operator -(Vector4 a, Vector4 b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z, a.W - b.W);
    public static Vector4 operator *(Vector4 value, float scale) => new(value.X * scale, value.Y * scale, value.Z * scale, value.W * scale);
    public static Vector4 operator /(Vector4 value, float scale) => new(value.X / scale, value.Y / scale, value.Z / scale, value.W / scale);
}

[StructLayout(LayoutKind.Sequential)]
public struct Quaternion
{
    public float X;
    public float Y;
    public float Z;
    public float W;

    public Quaternion(float x, float y, float z, float w)
    {
        X = x;
        Y = y;
        Z = z;
        W = w;
    }

    public static Quaternion Identity => new(0.0f, 0.0f, 0.0f, 1.0f);
    public bool IsZero => X == 0.0f && Y == 0.0f && Z == 0.0f && W == 0.0f;
    public float SqrMagnitude => X * X + Y * Y + Z * Z + W * W;
    public Quaternion Normalized
    {
        get
        {
            var length = MathF.Sqrt(SqrMagnitude);
            return length > Mathf.Epsilon
                ? new Quaternion(X / length, Y / length, Z / length, W / length)
                : Identity;
        }
    }
    public static Quaternion Inverse(Quaternion value)
    {
        var sqr = value.SqrMagnitude;
        return sqr > Mathf.Epsilon
            ? new Quaternion(-value.X / sqr, -value.Y / sqr, -value.Z / sqr, value.W / sqr)
            : Identity;
    }
    public static Quaternion AngleAxis(float radians, Vector3 axis)
    {
        axis = axis.Normalized;
        var half = radians * 0.5f;
        var scale = MathF.Sin(half);
        return new Quaternion(axis.X * scale, axis.Y * scale, axis.Z * scale,
            MathF.Cos(half)).Normalized;
    }
    public static Quaternion Euler(float x, float y, float z)
        => AngleAxis(y, Vector3.Up) * AngleAxis(x, Vector3.Right) * AngleAxis(z, Vector3.Forward);
    public static Quaternion Euler(Vector3 radians) => Euler(radians.X, radians.Y, radians.Z);
    public static Quaternion LookRotation(Vector3 forward, Vector3 up)
    {
        forward = forward.Normalized;
        if (forward.SqrMagnitude <= Mathf.Epsilon) return Identity;
        var right = Vector3.Cross(up.Normalized, forward).Normalized;
        if (right.SqrMagnitude <= Mathf.Epsilon)
            right = Vector3.Cross(MathF.Abs(forward.Y) < 0.999f ? Vector3.Up : Vector3.Right,
                forward).Normalized;
        up = Vector3.Cross(forward, right);
        var m00 = right.X; var m01 = up.X; var m02 = forward.X;
        var m10 = right.Y; var m11 = up.Y; var m12 = forward.Y;
        var m20 = right.Z; var m21 = up.Z; var m22 = forward.Z;
        var trace = m00 + m11 + m22;
        Quaternion result;
        if (trace > 0.0f)
        {
            var s = MathF.Sqrt(trace + 1.0f) * 2.0f;
            result = new((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s);
        }
        else if (m00 > m11 && m00 > m22)
        {
            var s = MathF.Sqrt(1.0f + m00 - m11 - m22) * 2.0f;
            result = new(0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s);
        }
        else if (m11 > m22)
        {
            var s = MathF.Sqrt(1.0f + m11 - m00 - m22) * 2.0f;
            result = new((m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s);
        }
        else
        {
            var s = MathF.Sqrt(1.0f + m22 - m00 - m11) * 2.0f;
            result = new((m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s);
        }
        return result.Normalized;
    }
    public static Quaternion Slerp(Quaternion a, Quaternion b, float t)
    {
        t = Mathf.Clamp01(t);
        a = a.Normalized; b = b.Normalized;
        var dot = a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W;
        if (dot < 0.0f) { b = new(-b.X, -b.Y, -b.Z, -b.W); dot = -dot; }
        if (dot > 0.9995f)
            return new Quaternion(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t,
                a.Z + (b.Z - a.Z) * t, a.W + (b.W - a.W) * t).Normalized;
        var theta0 = MathF.Acos(Mathf.Clamp(dot, -1.0f, 1.0f));
        var theta = theta0 * t;
        var sinTheta0 = MathF.Sin(theta0);
        var s0 = MathF.Cos(theta) - dot * MathF.Sin(theta) / sinTheta0;
        var s1 = MathF.Sin(theta) / sinTheta0;
        return new Quaternion(a.X * s0 + b.X * s1, a.Y * s0 + b.Y * s1,
            a.Z * s0 + b.Z * s1, a.W * s0 + b.W * s1).Normalized;
    }
    public static Quaternion operator *(Quaternion a, Quaternion b) => new(
        a.W * b.X + a.X * b.W + a.Y * b.Z - a.Z * b.Y,
        a.W * b.Y - a.X * b.Z + a.Y * b.W + a.Z * b.X,
        a.W * b.Z + a.X * b.Y - a.Y * b.X + a.Z * b.W,
        a.W * b.W - a.X * b.X - a.Y * b.Y - a.Z * b.Z);
    public static Vector3 operator *(Quaternion rotation, Vector3 point)
    {
        var q = rotation.Normalized;
        var vector = new Vector3(q.X, q.Y, q.Z);
        var uv = Vector3.Cross(vector, point);
        var uuv = Vector3.Cross(vector, uv);
        return point + (uv * q.W + uuv) * 2.0f;
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct Color
{
    public float R;
    public float G;
    public float B;
    public float A;

    public Color(float r, float g, float b, float a = 1.0f)
    {
        R = r;
        G = g;
        B = b;
        A = a;
    }

    public static Color White => new(1.0f, 1.0f, 1.0f, 1.0f);
    public static Color Black => new(0.0f, 0.0f, 0.0f, 1.0f);
    public static Color Clear => default;
    public static Color Lerp(Color a, Color b, float t)
    {
        t = Mathf.Clamp01(t);
        return new(a.R + (b.R - a.R) * t, a.G + (b.G - a.G) * t,
            a.B + (b.B - a.B) * t, a.A + (b.A - a.A) * t);
    }
    public static Color operator *(Color color, float scale)
        => new(color.R * scale, color.G * scale, color.B * scale, color.A * scale);
}

public static class Mathf
{
    public const float Pi = MathF.PI;
    public const float Deg2Rad = MathF.PI / 180.0f;
    public const float Rad2Deg = 180.0f / MathF.PI;
    public const float Epsilon = 1.0e-6f;
    public static float Clamp(float value, float minimum, float maximum)
        => MathF.Max(minimum, MathF.Min(maximum, value));
    public static float Clamp01(float value) => Clamp(value, 0.0f, 1.0f);
    public static float Lerp(float a, float b, float t) => a + (b - a) * Clamp01(t);
    public static float MoveTowards(float current, float target, float maxDelta)
        => MathF.Abs(target - current) <= maxDelta ? target
            : current + MathF.CopySign(maxDelta, target - current);
}
