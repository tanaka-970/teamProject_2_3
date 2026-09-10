using System;

namespace ReplayEngine;

// Managed callback を実行している間だけ有効な「いま動かしている文脈」。
//
// 【なぜ Singleton へ CurrentScene / CurrentWorld を固定しないか】
//   Editor と Play、複数 World、将来の並列実行が同じプロセスに同居する。
//   どこか 1 か所へ「現在の World」を書き込む方式だと、Play を止めた瞬間や
//   World を入れ替えた瞬間に、誰かが古い World を指したまま残る。
//   ここでは callback の入口で積み、出口で必ず戻す。
//   callback の外では Current は null になり、Time / Physics / Debug は
//   「実行文脈が無い」ことを判定できる。
//
// [ThreadStatic] にしてあるのは、将来 Managed callback を別スレッドから
// 呼ぶようになったときに、他スレッドの文脈を踏まないため。
internal static class ScriptExecutionContext
{
    [ThreadStatic]
    private static Frame current;

    internal readonly struct Frame
    {
        internal Frame(ScriptRuntimeContext runtime, MonoBehaviourHost? host)
        {
            Runtime = runtime;
            Host = host;
        }

        internal ScriptRuntimeContext Runtime { get; }
        internal MonoBehaviourHost? Host { get; }
        internal bool IsValid => Runtime != null;
    }

    // callback 実行中かどうか。
    internal static bool Active => current.IsValid;

    // 実行中の Runtime。callback の外では Unavailable を返す。
    // null を返さないのは、呼び出し側に毎回 null 判定を書かせないため。
    // Unavailable はすべての呼び出しが ServiceUnavailable を返す安全な実体。
    internal static ScriptRuntimeContext Runtime =>
        current.IsValid ? current.Runtime : ScriptRuntimeContext.Unavailable;

    // 実行中の MonoBehaviour を持つ Host。Legacy ScriptBehaviour の実行中は null。
    internal static MonoBehaviourHost? Host => current.Host;

    // callback を包む。例外が出ても finally で必ず元へ戻す。
    internal readonly struct Scope : IDisposable
    {
        private readonly Frame previous;

        internal Scope(ScriptRuntimeContext runtime, MonoBehaviourHost? host)
        {
            previous = current;
            current = new Frame(runtime, host);
        }

        public void Dispose() => current = previous;
    }

    internal static Scope Enter(ScriptRuntimeContext runtime, MonoBehaviourHost? host)
        => new(runtime, host);
}
