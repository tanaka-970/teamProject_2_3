using System;
using System.Collections.Concurrent;

namespace ReplayEngine;

// C++ Component を型で扱うための土台。
//
// ここに Component 型ごとの分岐は書かない。すべての値は Inspector と同じ
// PropertyRegistry を名前で読み書きするので、C++ 側でプロパティを 1 つ登録すれば
// そのまま C# からも触れる。
public interface IComponentBinding<TSelf> where TSelf : IComponentBinding<TSelf>
{
    // C++ の ComponentTypeInfo::type_name。"CameraComponent" のようなクラス名。
    static abstract string NativeTypeName { get; }

    static abstract TSelf FromHandle(ComponentHandle handle);
}

// 型名から ComponentTypeID を引いて覚えておく。
//
// ID は型名のハッシュなのでプロセス内で不変。毎フレーム引き直さない。
public static class ComponentTypes
{
    private static readonly ConcurrentDictionary<string, uint> Cache = new();

    // 解決できなければ 0。Runtime が未接続のときは覚えないので、後で引き直せる。
    public static uint IdOf(string nativeTypeName)
    {
        if (string.IsNullOrEmpty(nativeTypeName)) return 0;
        if (Cache.TryGetValue(nativeTypeName, out var cached)) return cached;

        var result = NativeBridge.ComponentTypeId(nativeTypeName);
        if (!result.Succeeded || result.Value == 0) return 0;
        Cache[nativeTypeName] = result.Value;
        return result.Value;
    }

    public static uint IdOf<T>() where T : IComponentBinding<T>
        => IdOf(T.NativeTypeName);

    // Scene を跨いでも型 ID は変わらないが、Assembly 再読み込みの後に
    // 明示的に捨てたい場合のための入口。
    public static void ClearCache() => Cache.Clear();
}

// Component 1 つ分のプロパティ入口。値はすべて名前で読み書きする。
public readonly struct ComponentAccessor
{
    public ComponentAccessor(ComponentHandle handle)
    {
        Handle = handle;
    }

    public ComponentHandle Handle { get; }
    public bool IsValid => !Handle.IsEmpty;

    public RuntimeResult<string> TypeName() => NativeBridge.GetComponentTypeName(Handle);

    public RuntimeStatus SetEnabled(bool enabled)
        => NativeBridge.SetComponentEnabled(Handle, enabled);
    public RuntimeResult<bool> IsEnabled() => NativeBridge.IsComponentEnabled(Handle);

    // ---- 明示的に状態を見たいとき ------------------------------------------

    public RuntimeResult<bool> TryGetBool(string name) => NativeBridge.GetPropertyBool(Handle, name);
    public RuntimeResult<long> TryGetInt(string name) => NativeBridge.GetPropertyInt(Handle, name);
    public RuntimeResult<double> TryGetDouble(string name) => NativeBridge.GetPropertyDouble(Handle, name);
    public RuntimeResult<string> TryGetString(string name) => NativeBridge.GetPropertyString(Handle, name);
    public RuntimeResult<Vector2> TryGetVector2(string name) => NativeBridge.GetPropertyVector2(Handle, name);
    public RuntimeResult<Vector3> TryGetVector3(string name) => NativeBridge.GetPropertyVector3(Handle, name);
    public RuntimeResult<Vector4> TryGetVector4(string name) => NativeBridge.GetPropertyVector4(Handle, name);

    public RuntimeStatus SetBool(string name, bool value) => NativeBridge.SetPropertyBool(Handle, name, value);
    public RuntimeStatus SetInt(string name, long value) => NativeBridge.SetPropertyInt(Handle, name, value);
    public RuntimeStatus SetDouble(string name, double value) => NativeBridge.SetPropertyDouble(Handle, name, value);
    public RuntimeStatus SetFloat(string name, float value) => NativeBridge.SetPropertyDouble(Handle, name, value);
    public RuntimeStatus SetString(string name, string value) => NativeBridge.SetPropertyString(Handle, name, value);
    public RuntimeStatus SetVector2(string name, Vector2 value) => NativeBridge.SetPropertyVector2(Handle, name, value);
    public RuntimeStatus SetVector3(string name, Vector3 value) => NativeBridge.SetPropertyVector3(Handle, name, value);
    public RuntimeStatus SetVector4(string name, Vector4 value) => NativeBridge.SetPropertyVector4(Handle, name, value);

    public RuntimeStatus SetColor(string name, Color value)
        => NativeBridge.SetPropertyVector4(Handle, name, new Vector4(value.R, value.G, value.B, value.A));

    public RuntimeResult<Color> TryGetColor(string name)
    {
        var result = NativeBridge.GetPropertyVector4(Handle, name);
        return new RuntimeResult<Color>(result.Status,
            new Color(result.Value.X, result.Value.Y, result.Value.Z, result.Value.W));
    }

    // ---- 失敗時に既定値でよいとき ------------------------------------------

    public bool GetBool(string name, bool fallback = false)
    {
        var result = TryGetBool(name);
        return result.Succeeded ? result.Value : fallback;
    }

    public int GetInt(string name, int fallback = 0)
    {
        var result = TryGetInt(name);
        return result.Succeeded ? (int)result.Value : fallback;
    }

    public float GetFloat(string name, float fallback = 0.0f)
    {
        var result = TryGetDouble(name);
        return result.Succeeded ? (float)result.Value : fallback;
    }

    public string GetString(string name, string fallback = "")
    {
        var result = TryGetString(name);
        return result.Succeeded ? result.Value : fallback;
    }

    public Vector2 GetVector2(string name)
    {
        var result = TryGetVector2(name);
        return result.Succeeded ? result.Value : default;
    }

    public Vector3 GetVector3(string name)
    {
        var result = TryGetVector3(name);
        return result.Succeeded ? result.Value : default;
    }

    public Vector4 GetVector4(string name)
    {
        var result = TryGetVector4(name);
        return result.Succeeded ? result.Value : default;
    }

    public Color GetColor(string name)
    {
        var result = TryGetColor(name);
        return result.Succeeded ? result.Value : new Color(1.0f, 1.0f, 1.0f);
    }
}

// 型付きの入口が無い Component をそのまま触るための箱。
// C++ で新しい Component を足した直後でも、C# を書き足さずに扱える。
public readonly struct GenericComponent
{
    public GenericComponent(ComponentHandle handle)
    {
        Accessor = new ComponentAccessor(handle);
    }

    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;
}
