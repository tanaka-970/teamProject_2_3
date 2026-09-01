using System;
using System.Collections;
using System.Collections.Generic;

namespace ReplayEngine;

// Coroutine が yield return できる待機条件。
public interface IYieldInstruction
{
    // まだ待つなら true。false になったフレームで再開する。
    bool KeepWaiting(float deltaTime);
}

public sealed class WaitForSeconds : IYieldInstruction
{
    private float remaining;

    public WaitForSeconds(float seconds) => remaining = seconds;

    public bool KeepWaiting(float deltaTime)
    {
        remaining -= deltaTime;
        return remaining > 0.0f;
    }
}

// 条件が true になるまで待つ。
public sealed class WaitUntil : IYieldInstruction
{
    private readonly Func<bool> predicate;

    public WaitUntil(Func<bool> predicate) => this.predicate = predicate;

    public bool KeepWaiting(float deltaTime) => predicate != null && !predicate();
}

// 条件が false になるまで待つ。
public sealed class WaitWhile : IYieldInstruction
{
    private readonly Func<bool> predicate;

    public WaitWhile(Func<bool> predicate) => this.predicate = predicate;

    public bool KeepWaiting(float deltaTime) => predicate != null && predicate();
}

// 走っている Coroutine 1 本の取っ手。
public sealed class Coroutine
{
    internal Coroutine(IEnumerator body) => Body = body;

    internal IEnumerator Body { get; }
    internal IYieldInstruction? Waiting { get; set; }
    public bool Finished { get; internal set; }
    public bool Canceled { get; internal set; }

    public void Cancel()
    {
        Canceled = true;
        Finished = true;
    }
}

// 一定時間後に 1 回だけ呼ぶ / 繰り返し呼ぶ。
public sealed class Timer
{
    internal Timer(float interval, Action callback, bool repeat)
    {
        Interval = interval > 0.0f ? interval : 0.0f;
        Callback = callback;
        Repeat = repeat;
        remaining = Interval;
    }

    private float remaining;

    public float Interval { get; }
    public bool Repeat { get; }
    internal Action Callback { get; }
    public bool Finished { get; private set; }

    public void Cancel() => Finished = true;

    internal void Advance(float deltaTime)
    {
        if (Finished) return;
        remaining -= deltaTime;
        // 1 フレームで複数回ぶんの時間が経っても、繰り返しは 1 回にまとめる。
        // 重い処理を積んだタイマーが遅延でさらに詰まるのを避けるため。
        if (remaining > 0.0f) return;

        Callback?.Invoke();
        if (Repeat) remaining = Interval;
        else Finished = true;
    }
}

// 値を時間で動かす。Update するのは所有している ScriptBehaviour。
public sealed class Tween
{
    internal Tween(float from, float to, float duration, Action<float> apply,
        Func<float, float>? easing)
    {
        this.from = from;
        this.to = to;
        this.duration = duration > 0.0f ? duration : 0.0f;
        this.apply = apply;
        this.easing = easing;
    }

    private readonly float from;
    private readonly float to;
    private readonly float duration;
    private readonly Action<float> apply;
    private readonly Func<float, float>? easing;
    private float elapsed;

    public bool Finished { get; private set; }

    public void Cancel() => Finished = true;

    internal void Advance(float deltaTime)
    {
        if (Finished) return;

        // duration が 0 なら 1 フレームで終端まで飛ぶ。0 除算を作らない。
        float t = 1.0f;
        if (duration > 0.0f)
        {
            elapsed += deltaTime;
            t = elapsed / duration;
            if (t > 1.0f) t = 1.0f;
        }

        var shaped = easing != null ? easing(t) : t;
        apply?.Invoke(from + (to - from) * shaped);
        if (t >= 1.0f) Finished = true;
    }
}

// よく使う補間曲線。
public static class Easing
{
    public static float Linear(float t) => t;
    public static float InQuad(float t) => t * t;
    public static float OutQuad(float t) => 1.0f - (1.0f - t) * (1.0f - t);
    public static float InOutQuad(float t)
        => t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
    public static float InCubic(float t) => t * t * t;
    public static float OutCubic(float t)
    {
        var inverted = 1.0f - t;
        return 1.0f - inverted * inverted * inverted;
    }
}

// ScriptBehaviour 1 つぶんの Coroutine / Timer / Tween をまとめて進める。
internal sealed class CoroutineRunner
{
    private readonly List<Coroutine> coroutines = new();
    private readonly List<Timer> timers = new();
    private readonly List<Tween> tweens = new();

    public Coroutine Start(IEnumerator body)
    {
        var coroutine = new Coroutine(body);
        coroutines.Add(coroutine);
        return coroutine;
    }

    public Timer AddTimer(Timer timer)
    {
        timers.Add(timer);
        return timer;
    }

    public Tween AddTween(Tween tween)
    {
        tweens.Add(tween);
        return tween;
    }

    public void CancelAll()
    {
        foreach (var coroutine in coroutines) coroutine.Cancel();
        foreach (var timer in timers) timer.Cancel();
        foreach (var tween in tweens) tween.Cancel();
        coroutines.Clear();
        timers.Clear();
        tweens.Clear();
    }

    public void Advance(float deltaTime)
    {
        // 添字で回す。進行中に新しい Coroutine が積まれても、
        // この回では動かさず次のフレームから動かす。
        var coroutineCount = coroutines.Count;
        for (var index = 0; index < coroutineCount && index < coroutines.Count; ++index)
        {
            Step(coroutines[index], deltaTime);
        }
        var timerCount = timers.Count;
        for (var index = 0; index < timerCount && index < timers.Count; ++index)
        {
            timers[index].Advance(deltaTime);
        }
        var tweenCount = tweens.Count;
        for (var index = 0; index < tweenCount && index < tweens.Count; ++index)
        {
            tweens[index].Advance(deltaTime);
        }

        coroutines.RemoveAll(entry => entry.Finished);
        timers.RemoveAll(entry => entry.Finished);
        tweens.RemoveAll(entry => entry.Finished);
    }

    private static void Step(Coroutine coroutine, float deltaTime)
    {
        if (coroutine.Finished) return;

        if (coroutine.Waiting != null)
        {
            if (coroutine.Waiting.KeepWaiting(deltaTime)) return;
            coroutine.Waiting = null;
        }

        if (!coroutine.Body.MoveNext())
        {
            coroutine.Finished = true;
            return;
        }

        // yield return null は「次のフレームまで」。待機条件なら覚えておく。
        coroutine.Waiting = coroutine.Body.Current as IYieldInstruction;
    }
}
