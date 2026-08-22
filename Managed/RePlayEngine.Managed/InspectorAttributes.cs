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
[System.Runtime.InteropServices.StructLayout(
    System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct AssetReference
{
    public AssetReference(string guid) => Guid = guid ?? string.Empty;

    public string Guid;

    public bool IsValid => !string.IsNullOrEmpty(Guid);
    public override string ToString() => Guid ?? string.Empty;
}
