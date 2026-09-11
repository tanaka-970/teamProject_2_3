using System;

namespace ReplayEngine;

// ゲーム制作者向けの Public オブジェクトモデル。
//
//   Object
//     ├ GameObject
//     └ Component
//         └ Behaviour
//             └ MonoBehaviour
//
// 内部では今までどおり ObjectHandle / ComponentHandle / RuntimeResult を使う。
// ここで隠しているのは「ゲーム制作者のコードに出さない」ことだけで、
// 安全機構そのものは 1 つも外していない。
public abstract class Object
{
    // Native 側の実体がまだ生きているか。
    // Destroy 済みや Scene 切り替え後は false になる。
    internal abstract bool IsAlive { get; }

    // Unity と同じ小文字。Unity 経験者が説明なしで書けることを優先する。
    public abstract string name { get; set; }

    // Unity と同じ「破棄済みなら == null が true」。
    //
    // 【なぜ演算子を書くか】
    //   C# の参照としては生きている wrapper でも、Native 側の GameObject が
    //   すでに壊されていることがある。そのまま使うと解放済みを触りに行く。
    //   Unity 利用者は `if (target == null)` で書くので、同じ意味にしておく。
    //   本当の null と破棄済みを両方 true にするのはここだけの約束で、
    //   内部の判定には IsAlive をそのまま使う。
    public static bool operator ==(Object? left, Object? right)
    {
        var leftDead = IsNullOrDead(left);
        var rightDead = IsNullOrDead(right);
        if (leftDead || rightDead) return leftDead && rightDead;
        return ReferenceEquals(left, right) || left!.SameTarget(right!);
    }

    public static bool operator !=(Object? left, Object? right) => !(left == right);

    // if (target) と書けるようにする。Unity と同じ意味。
    public static implicit operator bool(Object? value) => !IsNullOrDead(value);

    private static bool IsNullOrDead(Object? value)
        => value is null || !value.IsAlive;

    // 同じ Native 実体を指しているか。派生が identity の中身を決める。
    internal abstract bool SameTarget(Object other);

    public override bool Equals(object? obj)
        => obj is Object other && this == other;

    public override int GetHashCode() => IdentityHash();

    internal abstract int IdentityHash();

    public override string ToString() => IsAlive ? name : "null";

    // ---- static ヘルパ --------------------------------------------------------

    // 破棄は Native 側の遅延破棄へ積む。その場で消さない。
    // Scene の走査中に実体が消えると、走査している配列が壊れるため。
    public static void Destroy(Object? target) => target?.DestroySelf();

    internal abstract void DestroySelf();

    public static GameObject? Instantiate(PrefabReference prefab)
        => Instantiate(prefab, Vector3.Zero, Quaternion.Identity, null);

    public static GameObject? Instantiate(PrefabReference prefab, Vector3 position)
        => Instantiate(prefab, position, Quaternion.Identity, null);

    public static GameObject? Instantiate(PrefabReference prefab, Vector3 position,
        Quaternion rotation)
        => Instantiate(prefab, position, rotation, null);

    public static GameObject? Instantiate(PrefabReference prefab, Vector3 position,
        Quaternion rotation, Transform? parent)
    {
        if (!prefab.IsValid) return null;
        var runtime = ScriptExecutionContext.Runtime;
        // Native の Instantiate はラジアンのオイラー角を受け取る。
        var euler = UnityAngles.ToEulerRadians(rotation);
        var result = runtime.Instantiate(prefab.AssetGuid, position, euler,
            new Vector3(1.0f, 1.0f, 1.0f), parent != null ? parent.Handle : default);
        return result.Succeeded ? GameObject.Wrap(result.Value) : null;
    }
}

// Prefab を型で受け取るための識別子。
// Inspector からは AssetReference<PrefabAsset> として割り当てられる。
public readonly struct PrefabReference
{
    public PrefabReference(string assetGuid) => AssetGuid = assetGuid ?? string.Empty;

    public string AssetGuid { get; }
    public bool IsValid => !string.IsNullOrEmpty(AssetGuid);

    public static implicit operator PrefabReference(AssetReference<PrefabAsset> asset)
        => new(asset.Guid);
}

// GameObject に付く機能の共通の親。
public abstract class Component : Object
{
    public abstract GameObject gameObject { get; }

    public Transform transform => gameObject.transform;

    public override string name
    {
        get => gameObject.name;
        set => gameObject.name = value;
    }

    public T? GetComponent<T>() where T : class => gameObject.GetComponent<T>();
    public bool TryGetComponent<T>(out T? value) where T : class
        => gameObject.TryGetComponent(out value);
    public T? AddComponent<T>() where T : class => gameObject.AddComponent<T>();
    public T[] GetComponents<T>() where T : class => gameObject.GetComponents<T>();
    public T? GetComponentInParent<T>() where T : class => gameObject.GetComponentInParent<T>();
    public T? GetComponentInParent<T>(bool includeInactive) where T : class
        => gameObject.GetComponentInParent<T>(includeInactive);
    public T? GetComponentInChildren<T>() where T : class => gameObject.GetComponentInChildren<T>();
    public T? GetComponentInChildren<T>(bool includeInactive) where T : class
        => gameObject.GetComponentInChildren<T>(includeInactive);
    public T[] GetComponentsInParent<T>() where T : class => gameObject.GetComponentsInParent<T>();
    public T[] GetComponentsInParent<T>(bool includeInactive) where T : class
        => gameObject.GetComponentsInParent<T>(includeInactive);
    public T[] GetComponentsInChildren<T>() where T : class => gameObject.GetComponentsInChildren<T>();
    public T[] GetComponentsInChildren<T>(bool includeInactive) where T : class
        => gameObject.GetComponentsInChildren<T>(includeInactive);
}

// 有効・無効を持つ Component。
public abstract class Behaviour : Component
{
    public abstract bool enabled { get; set; }

    // GameObject も自分も有効なときだけ true。
    public bool isActiveAndEnabled => IsAlive && enabled && gameObject.activeInHierarchy;
}
