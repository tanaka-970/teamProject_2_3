using System;
using System.Collections.Generic;

namespace ReplayEngine;

// Unity 風の GameObject。
//
// 中身は ObjectHandle 1 つ。生の C++ ポインタは持たない。
// Scene 切り替え・削除・ID 再利用のあとに古い参照を触っても、
// Handle の世代チェックで弾かれる。この安全機構は隠しただけで外していない。
public sealed class GameObject : Object
{
    private readonly ObjectHandle handle;
    private Transform? cachedTransform;

    private GameObject(ObjectHandle handle) => this.handle = handle;

    internal ObjectHandle Handle => handle;

    // 同じ Native Object へは同じ wrapper を返す。
    //
    // 【なぜ弱参照で覚えるか】
    //   毎回 new すると、同じ GameObject を 2 回引いただけで別物になり、
    //   == 比較も Dictionary のキーも成立しない。
    //   一方、強参照で持つと Scene を捨てても wrapper が残り続けて漏れる。
    //   弱参照なら、ユーザーが持っている間だけ同一性が保たれる。
    private static readonly Dictionary<ObjectKey, WeakReference<GameObject>> Cache = new();

    private readonly struct ObjectKey : IEquatable<ObjectKey>
    {
        internal ObjectKey(ObjectHandle handle)
        {
            World = handle.World;
            Object = handle.Object;
            Generation = handle.Generation;
        }

        private ulong World { get; }
        private ulong Object { get; }
        private uint Generation { get; }

        public bool Equals(ObjectKey other) => World == other.World &&
            Object == other.Object && Generation == other.Generation;
        public override bool Equals(object? obj) => obj is ObjectKey other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(World, Object, Generation);
    }

    internal static GameObject? Wrap(ObjectHandle handle)
    {
        if (handle.IsEmpty) return null;
        var key = new ObjectKey(handle);
        lock (Cache)
        {
            if (Cache.TryGetValue(key, out var weak) && weak.TryGetTarget(out var cached))
                return cached;

            var created = new GameObject(handle);
            Cache[key] = new WeakReference<GameObject>(created);
            // 掃除は溜まったときだけ。毎回全走査すると生成が重くなる。
            if (Cache.Count > 512) PruneLocked();
            return created;
        }
    }

    private static void PruneLocked()
    {
        var dead = new List<ObjectKey>();
        foreach (var pair in Cache)
        {
            if (!pair.Value.TryGetTarget(out _)) dead.Add(pair.Key);
        }
        foreach (var key in dead) Cache.Remove(key);
    }

    // Assembly を捨てるときに wrapper も捨てる。
    internal static void ClearCache()
    {
        lock (Cache) Cache.Clear();
    }

    // ---- Object ---------------------------------------------------------------

    // 生死は Handle と World だけで決まる。いま callback 中かは関係ない。
    //
    // ここを ScriptExecutionContext 経由にすると、Inspector や Scene 保存のように
    // callback の外から動く経路で「生きている GameObject が死んで見える」形の
    // 壊れ方が起きうる。identity と実行文脈は分けておく。
    internal override bool IsAlive => NativeBridge.IsHandleAlive(handle);

    internal override bool SameTarget(Object other)
        => other is GameObject gameObject &&
            handle.World == gameObject.handle.World &&
            handle.Object == gameObject.handle.Object &&
            handle.Generation == gameObject.handle.Generation;

    internal override int IdentityHash()
        => HashCode.Combine(handle.World, handle.Object, handle.Generation);

    internal override void DestroySelf() => ScriptExecutionContext.Runtime.Destroy(handle);

    public override string name
    {
        get
        {
            var result = ScriptExecutionContext.Runtime.GetName(handle);
            return result.Succeeded ? result.Value : string.Empty;
        }
        set => ScriptExecutionContext.Runtime.SetName(handle, value ?? string.Empty);
    }

    public Transform transform => cachedTransform ??= new Transform(this);

    // ---- 有効・無効 -----------------------------------------------------------

    public bool activeSelf
    {
        get
        {
            var result = ScriptExecutionContext.Runtime.IsEnabled(handle);
            return result.Succeeded && result.Value;
        }
    }

    // 親を辿って 1 つでも無効なら false。Unity と同じ意味。
    // Coroutine を止めるかの判定もここと同じ関数を使う。答えが 2 つに割れない。
    public bool activeInHierarchy => NativeBridge.ActiveInHierarchy(handle);

    public void SetActive(bool value)
    {
        ScriptExecutionContext.Runtime.SetEnabled(handle, value);
        // 無効にした GameObject の Coroutine はその場で止める。Unity と同じ意味。
        //
        // 子や孫は辿らない。親を無効にした場合は activeInHierarchy が false に
        // なるので、フレーム末尾の Pump が同じフレームのうちに止める。
        // ここで階層を辿ると、Pump 側の判定と二重管理になる。
        if (!value) MonoBehaviourHost.StopCoroutinesOn(handle);
    }

    // ---- Component ------------------------------------------------------------

    public T? GetComponent<T>() where T : class
    {
        // Transform だけは Component 表に登録されていない特別扱い。
        if (typeof(T) == typeof(Transform)) return transform as T;

        if (typeof(MonoBehaviour).IsAssignableFrom(typeof(T)))
            return MonoBehaviourHost.FindOn(handle, typeof(T)) as T;

        var entry = NativeComponentRegistry.Find(typeof(T));
        if (entry == null) return null;

        foreach (var nativeTypeName in entry.NativeTypeNames)
        {
            var typeId = ComponentTypes.IdOf(nativeTypeName);
            if (typeId == 0) continue;
            var found = ScriptExecutionContext.Runtime.GetComponent(handle, typeId);
            if (!found.Succeeded || found.Value.IsEmpty) continue;
            return entry.Create(this, found.Value) as T;
        }
        return null;
    }

    public bool TryGetComponent<T>(out T? value) where T : class
    {
        value = GetComponent<T>();
        return value != null;
    }

    public T? AddComponent<T>() where T : class
    {
        if (typeof(T) == typeof(Transform)) return transform as T;

        // Managed Behaviour も同じ入口から足せる。
        //
        // 構造は今までどおり GameObject -> ScriptComponent -> Managed instance。
        // 別の Component システムは作らない。型 GUID は Assembly ロード時に
        // 作った表（ReplayGuid の逆引き）から引く。
        if (typeof(MonoBehaviour).IsAssignableFrom(typeof(T)))
        {
            var script = NativeBridge.AddScriptComponentFor(handle, typeof(T));
            if (!script.Succeeded || script.Value.IsEmpty) return null;

            // instance は Native 側の AddScriptComponent がこの場で作る。
            // Awake / OnEnable / Start は Scene の同期点のまま。
            //
            // 型ではなく戻ってきた ComponentHandle から引く。
            // 同じ型がすでに付いている GameObject でも、いま足した方が返る。
            return NativeBridge.FindManagedTarget(script.Value) as T;
        }

        var entry = NativeComponentRegistry.Find(typeof(T));
        if (entry == null) return null;

        // 候補が複数ある型（Collider）は、どれを作るか決められないので追加しない。
        // BoxCollider のように具体的な型で呼べば作れる。
        if (entry.NativeTypeNames.Length != 1) return null;
        var typeId = ComponentTypes.IdOf(entry.NativeTypeNames[0]);
        if (typeId == 0) return null;
        var added = ScriptExecutionContext.Runtime.AddComponent(handle, typeId);
        if (!added.Succeeded || added.Value.IsEmpty) return null;
        return entry.Create(this, added.Value) as T;
    }

    public T[] GetComponents<T>() where T : class
    {
        if (typeof(T) == typeof(Transform))
        {
            var only = transform as T;
            return only != null ? new[] { only } : Array.Empty<T>();
        }

        if (typeof(MonoBehaviour).IsAssignableFrom(typeof(T)))
            return MonoBehaviourHost.FindAllOn<T>(handle);

        var entry = NativeComponentRegistry.Find(typeof(T));
        if (entry == null) return Array.Empty<T>();

        var results = new List<T>();
        foreach (var nativeTypeName in entry.NativeTypeNames)
        {
            var typeId = ComponentTypes.IdOf(nativeTypeName);
            if (typeId == 0) continue;
            var found = ScriptExecutionContext.Runtime.GetComponents(handle, typeId);
            if (!found.Succeeded) continue;
            foreach (var component in found.Value)
            {
                if (entry.Create(this, component) is T typed) results.Add(typed);
            }
        }
        return results.Count == 0 ? Array.Empty<T>() : results.ToArray();
    }

    public T? GetComponentInParent<T>() where T : class
        => GetComponentInParent<T>(false);

    public T? GetComponentInParent<T>(bool includeInactive) where T : class
    {
        var runtime = ScriptExecutionContext.Runtime;
        var current = this;
        for (var depth = 0; depth < 256 && current != null; ++depth)
        {
            // Unity と同じく呼び出し元自身は inactive でも検索する。
            if (depth == 0 || includeInactive || current.activeInHierarchy)
            {
                var found = current.GetComponent<T>();
                if (found != null) return found;
            }
            var parent = runtime.GetParent(current.handle);
            if (!parent.Succeeded || parent.Value.IsEmpty) return null;
            current = Wrap(parent.Value);
        }
        return null;
    }

    public T? GetComponentInChildren<T>() where T : class
        => GetComponentInChildren<T>(false);

    public T? GetComponentInChildren<T>(bool includeInactive) where T : class
    {
        // Unity と同じく呼び出し元自身は inactive でも検索する。
        var found = GetComponent<T>();
        if (found != null) return found;
        foreach (var child in Children())
        {
            if (!includeInactive && !child.activeInHierarchy) continue;
            var inChild = child.GetComponentInChildren<T>(includeInactive);
            if (inChild != null) return inChild;
        }
        return null;
    }

    public T[] GetComponentsInParent<T>() where T : class
        => GetComponentsInParent<T>(false);

    public T[] GetComponentsInParent<T>(bool includeInactive) where T : class
    {
        var runtime = ScriptExecutionContext.Runtime;
        var results = new List<T>();
        var current = this;
        for (var depth = 0; depth < 256 && current != null; ++depth)
        {
            if (depth == 0 || includeInactive || current.activeInHierarchy)
                results.AddRange(current.GetComponents<T>());
            var parent = runtime.GetParent(current.handle);
            if (!parent.Succeeded || parent.Value.IsEmpty) break;
            current = Wrap(parent.Value);
        }
        return results.ToArray();
    }

    public T[] GetComponentsInChildren<T>() where T : class
        => GetComponentsInChildren<T>(false);

    public T[] GetComponentsInChildren<T>(bool includeInactive) where T : class
    {
        var results = new List<T>();
        Collect(this, results, includeInactive, true);
        return results.ToArray();

        static void Collect(GameObject node, List<T> into, bool includeInactive, bool root)
        {
            if (!root && !includeInactive && !node.activeInHierarchy) return;
            into.AddRange(node.GetComponents<T>());
            foreach (var child in node.Children())
                Collect(child, into, includeInactive, false);
        }
    }

    private IEnumerable<GameObject> Children()
    {
        var result = ScriptExecutionContext.Runtime.GetChildren(handle);
        if (!result.Succeeded) yield break;
        foreach (var child in result.Value)
        {
            var wrapped = Wrap(child);
            if (wrapped != null) yield return wrapped;
        }
    }

    // ---- static ---------------------------------------------------------------

    public static GameObject? Find(string name)
    {
        if (string.IsNullOrEmpty(name)) return null;
        // Low-Level FindGameObject(name) は inactive も返す既存仕様なので変えない。
        // Public Authoring API だけ activeInHierarchy 限定の専用入口を使う。
        var result = NativeBridge.FindActiveGameObjectByName(name);
        return result.Succeeded ? Wrap(result.Value) : null;
    }

    public static GameObject? Create(string name = "")
    {
        var result = ScriptExecutionContext.Runtime.CreateGameObject(name ?? string.Empty);
        return result.Succeeded ? Wrap(result.Value) : null;
    }
}

// Public wrapper 型と C++ Component 型名の対応表。
//
// GetComponent<T>() のたびに全 Component の型名を文字列比較しないための索引。
// 登録は静的コンストラクタで 1 回だけ行う。
internal static class NativeComponentRegistry
{
    internal sealed class Entry
    {
        internal Entry(string[] nativeTypeNames,
            Func<GameObject, ComponentHandle, Component> create)
        {
            NativeTypeNames = nativeTypeNames;
            Create = create;
        }

        // 候補が複数あるのは Collider のように、C++ 側で
        // Box / Sphere / Capsule / Mesh が別の型になっているものだけ。
        internal string[] NativeTypeNames { get; }
        internal Func<GameObject, ComponentHandle, Component> Create { get; }
    }

    private static readonly Dictionary<Type, Entry> Map = new();

    internal static void Register<T>(string nativeTypeName,
        Func<GameObject, ComponentHandle, Component> create) where T : Component
        => Map[typeof(T)] = new Entry(new[] { nativeTypeName }, create);

    internal static void Register<T>(string[] nativeTypeNames,
        Func<GameObject, ComponentHandle, Component> create) where T : Component
        => Map[typeof(T)] = new Entry(nativeTypeNames, create);

    internal static Entry? Find(Type type)
        => Map.TryGetValue(type, out var entry) ? entry : null;

    // ComponentHandle が指す実際の C++ 型名から、対応する wrapper を作る。
    //
    // 接触イベントで引いた Collider が Box なのか Sphere なのかは
    // Handle からしか分からない。型名で 1 つに定まるものだけを対象にする。
    internal static Func<GameObject, ComponentHandle, Component>? FindByNativeTypeName(
        ComponentHandle handle)
    {
        var name = NativeBridge.GetComponentTypeName(handle);
        if (!name.Succeeded || string.IsNullOrEmpty(name.Value)) return null;
        foreach (var pair in Map)
        {
            // 候補が複数ある登録（Collider 基底）は実型を決められないので飛ばす。
            if (pair.Value.NativeTypeNames.Length != 1) continue;
            if (pair.Value.NativeTypeNames[0] == name.Value) return pair.Value.Create;
        }
        return null;
    }

    internal static IEnumerable<KeyValuePair<Type, Entry>> All() => Map;
}
