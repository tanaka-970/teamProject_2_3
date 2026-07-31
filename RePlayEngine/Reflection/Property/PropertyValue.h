#pragma once

#include "../../Core/ObjectID/ObjectID.h"

#include <DirectXMath.h>

#include <string>
#include <variant>

namespace ReplayEngine::Reflection
{
    // Property が取り得る型。
    //
    // 保存形式とも Editor の描画方法とも 1 対 1 で対応させる。
    // Vector4 / Quaternion / Color は内部表現がどれも XMFLOAT4 だが、
    // Editor での描き方（カラーピッカーか数値 4 つか）と保存時の意味が違うので型を分ける。
    enum class PropertyType
    {
        Bool,
        Int,
        Float,
        Double,
        String,
        Vector2,
        Vector3,
        Vector4,
        Quaternion,
        Color,
        Enum,            // 内部は int。表示は PropertyDesc の enum_labels を使う
        AssetPath,       // 内部は string。Asset の GUID かプロジェクト相対パス
        ObjectReference, // 内部は ObjectID。GameObject 間参照
    };

    const char* ToString(PropertyType type) noexcept;
    bool TryParsePropertyType(const std::string& text, PropertyType& out) noexcept;

    // Property 1 つ分の値。
    //
    // Component の実データそのものではなく、
    // 「Component <-> Editor」「Component <-> Scene ファイル」を行き来する際の
    // 受け渡し用の箱として使う。
    class PropertyValue final
    {
    public:
        using Storage = std::variant<
            bool,
            int,
            float,
            double,
            std::string,
            DirectX::XMFLOAT2,
            DirectX::XMFLOAT3,
            DirectX::XMFLOAT4,
            Core::ObjectID>;

        PropertyValue() = default;

        static PropertyValue MakeBool(bool value);
        static PropertyValue MakeInt(int value);
        static PropertyValue MakeFloat(float value);
        static PropertyValue MakeDouble(double value);
        static PropertyValue MakeString(std::string value);
        static PropertyValue MakeVector2(const DirectX::XMFLOAT2& value);
        static PropertyValue MakeVector3(const DirectX::XMFLOAT3& value);
        static PropertyValue MakeVector4(const DirectX::XMFLOAT4& value);
        static PropertyValue MakeQuaternion(const DirectX::XMFLOAT4& value);
        static PropertyValue MakeColor(const DirectX::XMFLOAT4& value);
        static PropertyValue MakeEnum(int value);
        static PropertyValue MakeAssetPath(std::string value);
        static PropertyValue MakeObjectReference(Core::ObjectID value);

        PropertyType Type() const noexcept { return type_; }

        // 型が違う場合は既定値を返す。壊れた Scene ファイルを読んでも例外を投げない。
        bool AsBool(bool fallback = false) const noexcept;
        int AsInt(int fallback = 0) const noexcept;
        float AsFloat(float fallback = 0.0f) const noexcept;
        double AsDouble(double fallback = 0.0) const noexcept;
        const std::string& AsString() const noexcept;
        DirectX::XMFLOAT2 AsVector2() const noexcept;
        DirectX::XMFLOAT3 AsVector3() const noexcept;
        DirectX::XMFLOAT4 AsVector4() const noexcept;
        Core::ObjectID AsObjectReference() const noexcept;

        // 型が一致しないときに、意味を保ったまま寄せられる範囲で変換する。
        // Scene ファイル側でプロパティの型が変わった場合の救済に使う。
        // 変換できない組み合わせでは false を返し、呼び出し側が既定値を維持できるようにする。
        bool ConvertTo(PropertyType target, PropertyValue& out) const;

    private:
        PropertyType type_ = PropertyType::Bool;
        Storage storage_{ false };
    };
}
