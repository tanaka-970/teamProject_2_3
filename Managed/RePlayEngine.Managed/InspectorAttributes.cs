using System;

namespace ReplayEngine;

// Inspector の見た目と編集範囲を宣言する属性。
//
// C++ の PropertyDesc / ScriptFieldDefinition が持つ設定と 1 対 1 で対応する。
// ここへ書いた内容は Schema 経由で Inspector と Scene 保存の両方へ効く。

// 数値の編集範囲。スライダーになる。
[AttributeUsage(AttributeTargets.Field, AllowMultiple = false)]
public sealed class RangeAttribute : Attribute
{
    public RangeAttribute(double minimum, double maximum)
    {
        Minimum = minimum;
        Maximum = maximum;
    }

    public double Minimum { get; }
    public double Maximum { get; }
}

// Inspector のホバーで出る説明。
[AttributeUsage(AttributeTargets.Field, AllowMultiple = false)]
public sealed class TooltipAttribute : Attribute
{
    public TooltipAttribute(string text) => Text = text ?? string.Empty;
    public string Text { get; }
}

// Inspector の折り畳み見出し。同じ見出しの Field がまとまる。
[AttributeUsage(AttributeTargets.Field, AllowMultiple = false)]
public sealed class HeaderAttribute : Attribute
{
    public HeaderAttribute(string text) => Text = text ?? string.Empty;
    public string Text { get; }
}

// 保存はするが Inspector には出さない。
[AttributeUsage(AttributeTargets.Field, AllowMultiple = false)]
public sealed class HideInInspectorAttribute : Attribute
{
}

// Inspector では変えられない。スクリプトからは書ける。
[AttributeUsage(AttributeTargets.Field, AllowMultiple = false)]
public sealed class ReadOnlyAttribute : Attribute
{
}

// Inspector の表示名を差し替える。既定はフィールド名を人が読める形にしたもの。
[AttributeUsage(AttributeTargets.Field, AllowMultiple = false)]
public sealed class DisplayNameAttribute : Attribute
{
    public DisplayNameAttribute(string text) => Text = text ?? string.Empty;
    public string Text { get; }
}

// AssetReference の Picker を種別で絞る。"Image" / "Model" / "Scene" など。
[AttributeUsage(AttributeTargets.Field, AllowMultiple = false)]
public sealed class AssetTypeAttribute : Attribute
{
    public AssetTypeAttribute(string kind) => Kind = kind ?? string.Empty;
    public string Kind { get; }
}

// Asset を GUID で持つフィールド。Inspector では Picker になる。
public interface IAssetReference
{
    string AssetGuid { get; }
}

public interface IAssetKind
{
}

public readonly struct SceneAsset : IAssetKind { public static string Kind => "Scene"; }
public readonly struct PrefabAsset : IAssetKind { public static string Kind => "Prefab"; }
public readonly struct ImageAsset : IAssetKind { public static string Kind => "Image"; }
public readonly struct SpriteAtlasAsset : IAssetKind { public static string Kind => "SpriteAtlas"; }
public readonly struct ModelAsset : IAssetKind { public static string Kind => "Model"; }
public readonly struct MaterialAsset : IAssetKind { public static string Kind => "Material"; }
public readonly struct AudioAsset : IAssetKind { public static string Kind => "Audio"; }
public readonly struct MotionAsset : IAssetKind { public static string Kind => "Motion"; }
public readonly struct CompositionAsset : IAssetKind { public static string Kind => "Composition"; }

public struct AssetReference : IAssetReference
{
    public AssetReference(string guid) => Guid = guid ?? string.Empty;

    public string Guid;

    public string AssetGuid => Guid ?? string.Empty;
    public bool IsValid => !string.IsNullOrEmpty(Guid);
    public override string ToString() => Guid ?? string.Empty;
}

// Asset種別を型で固定する参照。InspectorのPickerもTKindに対応する種別へ絞られる。
[System.Runtime.InteropServices.StructLayout(
    System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct AssetReference<TKind> : IAssetReference where TKind : struct, IAssetKind
{
    public AssetReference(string guid) => Guid = guid ?? string.Empty;

    public string Guid;

    public string AssetGuid => Guid ?? string.Empty;
    public bool IsValid => !string.IsNullOrEmpty(Guid);
    public AssetReference Untyped => new(Guid);
    public override string ToString() => Guid ?? string.Empty;
}
