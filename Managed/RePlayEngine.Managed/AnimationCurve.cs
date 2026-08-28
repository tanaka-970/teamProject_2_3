using System;
using System.Collections.Generic;

namespace ReplayEngine;

public enum WrapMode
{
    Clamp = 0,
    Loop = 1,
    PingPong = 2,
}

[Serializable]
public struct Keyframe
{
    public Keyframe(float time, float value, float inTangent = 0.0f, float outTangent = 0.0f)
    {
        Time = time;
        Value = value;
        InTangent = inTangent;
        OutTangent = outTangent;
    }

    public float Time;
    public float Value;
    public float InTangent;
    public float OutTangent;
}

[Serializable]
public sealed class AnimationCurve
{
    public List<Keyframe> Keys = new();
    public WrapMode PreWrapMode = WrapMode.Clamp;
    public WrapMode PostWrapMode = WrapMode.Clamp;

    public AnimationCurve() { }

    public AnimationCurve(params Keyframe[] keys)
    {
        if (keys != null) Keys.AddRange(keys);
        SortKeys();
    }

    public int Length => Keys.Count;

    public int AddKey(Keyframe key)
    {
        Keys.Add(key);
        SortKeys();
        return Keys.FindIndex(item => item.Time == key.Time && item.Value == key.Value);
    }

    public bool RemoveKey(int index)
    {
        if ((uint)index >= (uint)Keys.Count) return false;
        Keys.RemoveAt(index);
        return true;
    }

    public float Evaluate(float time)
    {
        if (Keys.Count == 0) return 0.0f;
        if (Keys.Count == 1) return Keys[0].Value;

        var first = Keys[0];
        var last = Keys[^1];
        time = WrapTime(time, first.Time, last.Time,
            time < first.Time ? PreWrapMode : PostWrapMode);
        if (time <= first.Time) return first.Value;
        if (time >= last.Time) return last.Value;

        var high = Keys.BinarySearch(new Keyframe(time, 0.0f), KeyframeTimeComparer.Instance);
        if (high >= 0) return Keys[high].Value;
        high = ~high;
        var low = high - 1;
        var a = Keys[low];
        var b = Keys[high];
        var duration = b.Time - a.Time;
        if (duration <= Mathf.Epsilon) return b.Value;
        var t = (time - a.Time) / duration;
        var t2 = t * t;
        var t3 = t2 * t;
        return (2.0f * t3 - 3.0f * t2 + 1.0f) * a.Value +
            (t3 - 2.0f * t2 + t) * duration * a.OutTangent +
            (-2.0f * t3 + 3.0f * t2) * b.Value +
            (t3 - t2) * duration * b.InTangent;
    }

    public static AnimationCurve Linear(float startTime, float startValue,
        float endTime, float endValue)
    {
        var duration = endTime - startTime;
        var tangent = MathF.Abs(duration) > Mathf.Epsilon
            ? (endValue - startValue) / duration : 0.0f;
        return new AnimationCurve(
            new Keyframe(startTime, startValue, tangent, tangent),
            new Keyframe(endTime, endValue, tangent, tangent));
    }

    private void SortKeys() => Keys.Sort(KeyframeTimeComparer.Instance);

    private static float WrapTime(float time, float start, float end, WrapMode mode)
    {
        var length = end - start;
        if (length <= Mathf.Epsilon || mode == WrapMode.Clamp)
            return Mathf.Clamp(time, start, end);
        var offset = time - start;
        if (mode == WrapMode.Loop)
            return start + PositiveModulo(offset, length);
        var period = length * 2.0f;
        var folded = PositiveModulo(offset, period);
        return start + (folded <= length ? folded : period - folded);
    }

    private static float PositiveModulo(float value, float divisor)
        => ((value % divisor) + divisor) % divisor;

    private sealed class KeyframeTimeComparer : IComparer<Keyframe>
    {
        internal static readonly KeyframeTimeComparer Instance = new();
        public int Compare(Keyframe left, Keyframe right) => left.Time.CompareTo(right.Time);
    }
}
