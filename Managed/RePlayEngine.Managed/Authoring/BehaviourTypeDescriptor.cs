using System;
using System.Collections;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq.Expressions;
using System.Reflection;

namespace ReplayEngine;

// MonoBehaviour 派生型ごとの「どのメッセージを持っているか」の解析結果。
//
// 【なぜ型ごとに 1 回だけ解析するか】
//   Unity 風のメッセージは override ではなく名前で見つける。
//   素直に書くと毎フレーム GetMethod / MethodInfo.Invoke になり、
//   Script が増えるほど Update が重くなる。
//   Assembly をロードした後の最初のアクセスで 1 回だけ解析し、
//   delegate へ束ねてキャッシュする。以降は delegate 呼び出しだけになる。
//
// 解析できなかったシグネチャは診断メッセージへ積む。黙って無視すると
// 「Update と書いたのに呼ばれない」の原因が追えなくなる。
internal sealed class BehaviourTypeDescriptor
{
    private static readonly ConcurrentDictionary<Type, BehaviourTypeDescriptor> Cache = new();

    // Unity のメッセージ名。ここに無い名前は解析対象にしない。
    private static readonly string[] VoidMessageNames =
    {
        "Awake", "OnEnable", "FixedUpdate", "Update", "LateUpdate",
        "OnDisable", "OnDestroy", "Reset", "OnValidate",
        "OnDrawGizmos", "OnDrawGizmosSelected",
    };

    private readonly Dictionary<string, Action<MonoBehaviour>> voidMessages = new(StringComparer.Ordinal);

    private BehaviourTypeDescriptor(Type type)
    {
        Type = type;
        var diagnostics = new List<string>();

        foreach (var name in VoidMessageNames)
        {
            var invoker = BuildVoidInvoker(type, name, diagnostics);
            if (invoker != null) voidMessages[name] = invoker;
        }

        BuildStartInvoker(type, diagnostics, out var start, out var startCoroutine);
        Start = start;
        StartCoroutineMessage = startCoroutine;

        Collision = BuildArgumentInvoker<Collision>(type, "OnCollisionEnter", diagnostics);
        CollisionStay = BuildArgumentInvoker<Collision>(type, "OnCollisionStay", diagnostics);
        CollisionExit = BuildArgumentInvoker<Collision>(type, "OnCollisionExit", diagnostics);
        TriggerEnter = BuildArgumentInvoker<Collider>(type, "OnTriggerEnter", diagnostics);
        TriggerStay = BuildArgumentInvoker<Collider>(type, "OnTriggerStay", diagnostics);
        TriggerExit = BuildArgumentInvoker<Collider>(type, "OnTriggerExit", diagnostics);

        Diagnostics = diagnostics.Count == 0 ? Array.Empty<string>() : diagnostics.ToArray();

        voidMessages.TryGetValue("Awake", out var awake); Awake = awake;
        voidMessages.TryGetValue("OnEnable", out var onEnable); OnEnable = onEnable;
        voidMessages.TryGetValue("FixedUpdate", out var fixedUpdate); FixedUpdate = fixedUpdate;
        voidMessages.TryGetValue("Update", out var update); Update = update;
        voidMessages.TryGetValue("LateUpdate", out var lateUpdate); LateUpdate = lateUpdate;
        voidMessages.TryGetValue("OnDisable", out var onDisable); OnDisable = onDisable;
        voidMessages.TryGetValue("OnDestroy", out var onDestroy); OnDestroy = onDestroy;
    }

    internal Type Type { get; }

    internal Action<MonoBehaviour>? Awake { get; }
    internal Action<MonoBehaviour>? OnEnable { get; }
    internal Action<MonoBehaviour>? Start { get; }
    internal Func<MonoBehaviour, IEnumerator>? StartCoroutineMessage { get; }
    internal Action<MonoBehaviour>? FixedUpdate { get; }
    internal Action<MonoBehaviour>? Update { get; }
    internal Action<MonoBehaviour>? LateUpdate { get; }
    internal Action<MonoBehaviour>? OnDisable { get; }
    internal Action<MonoBehaviour>? OnDestroy { get; }

    internal Action<MonoBehaviour, Collision>? Collision { get; }
    internal Action<MonoBehaviour, Collision>? CollisionStay { get; }
    internal Action<MonoBehaviour, Collision>? CollisionExit { get; }
    internal Action<MonoBehaviour, Collider>? TriggerEnter { get; }
    internal Action<MonoBehaviour, Collider>? TriggerStay { get; }
    internal Action<MonoBehaviour, Collider>? TriggerExit { get; }

    // 名前は合っているが形が違うメソッドの一覧。空なら問題なし。
    internal string[] Diagnostics { get; }

    // 接触イベントをどれか 1 つでも受け取るか。
    // 使わない Behaviour に購読と Poll のコストを掛けないための判定。
    internal bool WantsCollision => Collision != null || CollisionStay != null || CollisionExit != null;
    internal bool WantsTrigger => TriggerEnter != null || TriggerStay != null || TriggerExit != null;

    internal static BehaviourTypeDescriptor For(Type type) => Cache.GetOrAdd(type, Build);

    private static BehaviourTypeDescriptor Build(Type type) => new(type);

    // Assembly を捨てたら解析結果も捨てる。
    // 古い Type を握り続けると AssemblyLoadContext が解放されない。
    internal static void ClearCache() => Cache.Clear();

    private const BindingFlags SearchFlags =
        BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;

    // 引数なし void のメッセージを探して delegate にする。
    // private でも protected でも拾う。Unity と同じく可視性は問わない。
    private static Action<MonoBehaviour>? BuildVoidInvoker(Type type, string name,
        List<string> diagnostics)
    {
        var candidates = FindCandidates(type, name);
        if (candidates.Count == 0) return null;

        MethodInfo? match = null;
        foreach (var method in candidates)
        {
            if (method.GetParameters().Length == 0 && method.ReturnType == typeof(void))
            {
                if (match == null) match = method;
            }
        }

        if (match == null)
        {
            diagnostics.Add(Describe(type, name, candidates,
                "引数なしの void として宣言してください"));
            return null;
        }
        if (candidates.Count > 1)
        {
            diagnostics.Add(Describe(type, name, candidates,
                "同じ名前のメソッドが複数あります。呼ばれるのは引数なしの void 1 つだけです"));
        }

        return CompileVoid(match);
    }


    // Start だけは Unity と同じく `void Start()` と `IEnumerator Start()` の両方を許す。
    // 派生型から基底型の順に見て、最初に見つかった有効な形を使う。
    private static void BuildStartInvoker(Type type, List<string> diagnostics,
        out Action<MonoBehaviour>? start, out Func<MonoBehaviour, IEnumerator>? coroutine)
    {
        start = null;
        coroutine = null;
        var candidates = FindCandidates(type, "Start");
        if (candidates.Count == 0) return;

        MethodInfo? match = null;
        foreach (var method in candidates)
        {
            if (method.GetParameters().Length != 0) continue;
            if (method.ReturnType == typeof(void) ||
                typeof(IEnumerator).IsAssignableFrom(method.ReturnType))
            {
                match = method;
                break;
            }
        }

        if (match == null)
        {
            diagnostics.Add(Describe(type, "Start", candidates,
                "引数なしの void または IEnumerator として宣言してください"));
            return;
        }
        if (candidates.Count > 1)
        {
            diagnostics.Add(Describe(type, "Start", candidates,
                "同じ名前が複数あります。派生型側で最初に見つかった有効な Start だけを呼びます"));
        }

        if (match.ReturnType == typeof(void)) start = CompileVoid(match);
        else coroutine = CompileCoroutine(match);
    }

    private static Func<MonoBehaviour, IEnumerator> CompileCoroutine(MethodInfo method)
    {
        var self = Expression.Parameter(typeof(MonoBehaviour), "self");
        var typed = Expression.Convert(self, method.DeclaringType!);
        var call = Expression.Call(typed, method);
        var result = Expression.Convert(call, typeof(IEnumerator));
        return Expression.Lambda<Func<MonoBehaviour, IEnumerator>>(result, self).Compile();
    }

    // (MonoBehaviour self) => ((Derived)self).Method() をコンパイルする。
    //
    // 【なぜ Delegate.CreateDelegate ではないか】
    //   open instance delegate は第 1 引数がメソッドの宣言型と一致していないと
    //   作れない。Action<MonoBehaviour> では派生型のメソッドへ束ねられず、
    //   throwOnBindFailure:false のときは黙って null が返る。
    //   その null をそのまま持つと「Update と書いたのに呼ばれない」になる。
    //   式木なら派生型へのキャストを含めて 1 回だけコンパイルできる。
    private static Action<MonoBehaviour> CompileVoid(MethodInfo method)
    {
        var self = Expression.Parameter(typeof(MonoBehaviour), "self");
        var typed = Expression.Convert(self, method.DeclaringType!);
        return Expression.Lambda<Action<MonoBehaviour>>(
            Expression.Call(typed, method), self).Compile();
    }

    private static Action<MonoBehaviour, TArgument> CompileArgument<TArgument>(MethodInfo method)
    {
        var self = Expression.Parameter(typeof(MonoBehaviour), "self");
        var argument = Expression.Parameter(typeof(TArgument), "argument");
        var typed = Expression.Convert(self, method.DeclaringType!);
        return Expression.Lambda<Action<MonoBehaviour, TArgument>>(
            Expression.Call(typed, method, argument), self, argument).Compile();
    }

    // 引数 1 つのメッセージ（衝突・トリガー）を探して delegate にする。
    private static Action<MonoBehaviour, TArgument>? BuildArgumentInvoker<TArgument>(
        Type type, string name, List<string> diagnostics)
    {
        var candidates = FindCandidates(type, name);
        if (candidates.Count == 0) return null;

        MethodInfo? match = null;
        foreach (var method in candidates)
        {
            var parameters = method.GetParameters();
            if (parameters.Length == 1 && parameters[0].ParameterType == typeof(TArgument) &&
                method.ReturnType == typeof(void))
            {
                if (match == null) match = method;
            }
        }

        if (match == null)
        {
            diagnostics.Add(Describe(type, name, candidates,
                $"引数を {typeof(TArgument).Name} 1 つだけ取る void として宣言してください"));
            return null;
        }

        return CompileArgument<TArgument>(match);
    }

    // 派生から基底へ、その名前のインスタンスメソッドを全部集める。
    // GetMethod は同名が複数あると AmbiguousMatchException を投げるため使わない。
    private static List<MethodInfo> FindCandidates(Type type, string name)
    {
        var found = new List<MethodInfo>();
        for (var current = type; current != null && current != typeof(MonoBehaviour) &&
            current != typeof(object); current = current.BaseType)
        {
            foreach (var method in current.GetMethods(SearchFlags | BindingFlags.DeclaredOnly))
            {
                if (!string.Equals(method.Name, name, StringComparison.Ordinal)) continue;
                if (method.IsAbstract) continue;
                found.Add(method);
            }
        }
        return found;
    }


    private static string Describe(Type type, string name, List<MethodInfo> candidates,
        string requirement)
    {
        var shapes = new List<string>();
        foreach (var method in candidates)
        {
            var parameters = method.GetParameters();
            var names = new string[parameters.Length];
            for (var index = 0; index < parameters.Length; ++index)
                names[index] = parameters[index].ParameterType.Name;
            shapes.Add($"{method.ReturnType.Name} {name}({string.Join(", ", names)})");
        }
        return $"{type.FullName}.{name} は呼び出されません。" +
            $"見つかった形: {string.Join(" / ", shapes)}。{requirement}。";
    }
}
