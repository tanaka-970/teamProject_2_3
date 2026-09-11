using System;

namespace ReplayEngine;

// 時間。値は C++ が毎フレーム 1 回だけ Managed へ渡したものをそのまま読む。
//
// World には依存しないので実行文脈の解決は要らない。
// Scene や Physics のように World 依存のものは ScriptExecutionContext を通す。
public static class Time
{
    // 既にスケール済みの 1 フレームの秒数。
    public static float deltaTime => NativeBridge.TimeDeltaTime;
    public static float fixedDeltaTime => NativeBridge.TimeFixedDeltaTime;
    public static int frameCount => (int)NativeBridge.TimeFrameIndex;

    // 読み取り専用。
    //
    // 既存の GetTimeScale をそのまま使う。ABI へ新しい関数は足していない。
    //
    // 【なぜ setter が無いか】
    //   時間の倍率は framework の object_time_scale が正本で、
    //   Managed から書き込む入口が Runtime API に無い。
    //   設定できる風の API を作らず、getter だけにしてある。
    public static float timeScale
    {
        get
        {
            var result = NativeBridge.TimeScale();
            return result.Succeeded ? result.Value : 1.0f;
        }
    }
}

// ログ。既存の LogInfo / LogWarning / LogError をそのまま使う。
// ゲーム制作者が ScriptRuntimeContext を触らずに書けるようにするだけの層。
public static class Debug
{
    public static void Log(object? message) => Write(0, message, null);
    public static void Log(object? message, Object? context) => Write(0, message, context);

    public static void LogWarning(object? message) => Write(1, message, null);
    public static void LogWarning(object? message, Object? context) => Write(1, message, context);

    public static void LogError(object? message) => Write(2, message, null);
    public static void LogError(object? message, Object? context) => Write(2, message, context);

    public static void LogException(Exception exception) => Write(2, exception, null);
    public static void LogException(Exception exception, Object? context)
        => Write(2, exception, context);

    // 条件が偽のときだけエラーを出す。開発中の前提の明示に使う。
    public static void Assert(bool condition, object? message = null)
    {
        if (condition) return;
        Write(2, message ?? "Assertion failed", null);
    }

    private static void Write(int severity, object? message, Object? context)
    {
        var runtime = ScriptExecutionContext.Runtime;
        var text = message?.ToString() ?? "null";
        var source = SourceOf(context);
        switch (severity)
        {
            case 1: runtime.LogWarning(text, source); break;
            case 2: runtime.LogError(text, source); break;
            default: runtime.LogInfo(text, source); break;
        }
    }

    // どの GameObject から出たログかを Console へ伝える。
    private static ObjectHandle SourceOf(Object? context) => context switch
    {
        GameObject gameObject => gameObject.Handle,
        Component component when component.gameObject != null => component.gameObject.Handle,
        _ => default,
    };
}
