// PropertyValue の責務のうち、保存形式の型名と値の生成を持つ。
//
//   PropertyValue.cpp                  … 型名テーブルと値の生成（このファイル）
//   PropertyValueAccessors.cpp         … 型ごとの値の取り出し
//   PropertyValueComparison.cpp        … 有限値判定と等価判定
//   PropertyValueConversion.cpp        … 型変換規則
//   PropertyValueInterpolation.cpp     … 補間規則

#include "PropertyValue.h"

#include <cmath>
#include <limits>


namespace ReplayEngine::Reflection
{
    namespace
    {
        struct TypeNameEntry
        {
            PropertyType type;
            const char* name;
        };

        // Scene ファイルへ書き出す型名。値そのものではなく識別子なので変更しないこと。
        constexpr TypeNameEntry type_names[] = {
            { PropertyType::Bool,            "bool" },
            { PropertyType::Int,             "int" },
            { PropertyType::Float,           "float" },
            { PropertyType::Double,          "double" },
            { PropertyType::String,          "string" },
            { PropertyType::Vector2,         "vec2" },
            { PropertyType::Vector3,         "vec3" },
            { PropertyType::Vector4,         "vec4" },
            { PropertyType::Quaternion,      "quat" },
            { PropertyType::Color,           "color" },
            { PropertyType::Enum,            "enum" },
            { PropertyType::AssetPath,       "asset" },
            { PropertyType::ObjectReference, "objref" },
            { PropertyType::CollisionLayer,    "layer" },
            { PropertyType::CollisionMask,     "layermask" },
            { PropertyType::ColliderReference, "colliderref" },
            // v11 で追加。
            { PropertyType::Int64,             "int64" },
            { PropertyType::UInt64,            "uint64" },
            { PropertyType::AssetReference,    "assetref" },
            { PropertyType::SceneReference,    "sceneref" },
            { PropertyType::ComponentReference, "compref" },
            { PropertyType::Array,             "array" },
        };

    }

    const char* ToString(PropertyType type) noexcept
    {
        for (const TypeNameEntry& entry : type_names)
        {
            if (entry.type == type) return entry.name;
        }
        return "bool";
    }

    bool TryParsePropertyType(const std::string& text, PropertyType& out) noexcept
    {
        for (const TypeNameEntry& entry : type_names)
        {
            if (text == entry.name)
            {
                out = entry.type;
                return true;
            }
        }
        return false;
    }

    bool IsContainerType(PropertyType type) noexcept
    {
        return type == PropertyType::Array;
    }

    // ---- 特殊メンバ -------------------------------------------------------
    //
    // ここで定義する。std::vector<PropertyValue> をメンバに持つため、
    // PropertyValue が完全型になった位置で実体化させる必要がある。

    PropertyValue::PropertyValue() = default;
    PropertyValue::~PropertyValue() = default;
    PropertyValue::PropertyValue(const PropertyValue& other) = default;
    PropertyValue::PropertyValue(PropertyValue&& other) noexcept = default;
    PropertyValue& PropertyValue::operator=(const PropertyValue& other) = default;
    PropertyValue& PropertyValue::operator=(PropertyValue&& other) noexcept = default;

    PropertyValue PropertyValue::MakeBool(bool value)
    {
        PropertyValue result;
        result.type_ = PropertyType::Bool;
        result.storage_ = value;
        return result;
    }

    PropertyValue PropertyValue::MakeInt(int value)
    {
        PropertyValue result;
        result.type_ = PropertyType::Int;
        result.storage_ = value;
        return result;
    }

    PropertyValue PropertyValue::MakeInt64(std::int64_t value)
    {
        PropertyValue result;
        result.type_ = PropertyType::Int64;
        result.storage_ = value;
        return result;
    }

    PropertyValue PropertyValue::MakeUInt64(std::uint64_t value)
    {
        PropertyValue result;
        result.type_ = PropertyType::UInt64;
        result.storage_ = value;
        return result;
    }

    PropertyValue PropertyValue::MakeFloat(float value)
    {
        PropertyValue result;
        result.type_ = PropertyType::Float;
        result.storage_ = value;
        return result;
    }

    PropertyValue PropertyValue::MakeDouble(double value)
    {
        PropertyValue result;
        result.type_ = PropertyType::Double;
        result.storage_ = value;
        return result;
    }

    PropertyValue PropertyValue::MakeString(std::string value)
    {
        PropertyValue result;
        result.type_ = PropertyType::String;
        result.storage_ = std::move(value);
        return result;
    }

    PropertyValue PropertyValue::MakeVector2(const DirectX::XMFLOAT2& value)
    {
        PropertyValue result;
        result.type_ = PropertyType::Vector2;
        result.storage_ = value;
        return result;
    }

    PropertyValue PropertyValue::MakeVector3(const DirectX::XMFLOAT3& value)
    {
        PropertyValue result;
        result.type_ = PropertyType::Vector3;
        result.storage_ = value;
        return result;
    }

    PropertyValue PropertyValue::MakeVector4(const DirectX::XMFLOAT4& value)
    {
        PropertyValue result;
        result.type_ = PropertyType::Vector4;
        result.storage_ = value;
        return result;
    }

    PropertyValue PropertyValue::MakeQuaternion(const DirectX::XMFLOAT4& value)
    {
        PropertyValue result;
        result.type_ = PropertyType::Quaternion;
        result.storage_ = value;
        return result;
    }

    PropertyValue PropertyValue::MakeColor(const DirectX::XMFLOAT4& value)
    {
        PropertyValue result;
        result.type_ = PropertyType::Color;
        result.storage_ = value;
        return result;
    }

    PropertyValue PropertyValue::MakeEnum(int value)
    {
        PropertyValue result;
        result.type_ = PropertyType::Enum;
        result.storage_ = value;
        return result;
    }

    PropertyValue PropertyValue::MakeCollisionLayer(int value)
    {
        PropertyValue result;
        result.type_ = PropertyType::CollisionLayer;
        result.storage_ = value;
        return result;
    }

    PropertyValue PropertyValue::MakeCollisionMask(int value)
    {
        PropertyValue result;
        result.type_ = PropertyType::CollisionMask;
        result.storage_ = value;
        return result;
    }

    PropertyValue PropertyValue::MakeColliderReference(int value)
    {
        PropertyValue result;
        result.type_ = PropertyType::ColliderReference;
        result.storage_ = value;
        return result;
    }

    PropertyValue PropertyValue::MakeAssetPath(std::string value)
    {
        PropertyValue result;
        result.type_ = PropertyType::AssetPath;
        result.storage_ = std::move(value);
        return result;
    }

    PropertyValue PropertyValue::MakeObjectReference(Core::ObjectID value)
    {
        PropertyValue result;
        result.type_ = PropertyType::ObjectReference;
        result.storage_ = value;
        return result;
    }

    PropertyValue PropertyValue::MakeAssetReference(std::string guid)
    {
        PropertyValue result;
        result.type_ = PropertyType::AssetReference;
        result.storage_ = std::move(guid);
        return result;
    }

    PropertyValue PropertyValue::MakeSceneReference(std::string guid)
    {
        PropertyValue result;
        result.type_ = PropertyType::SceneReference;
        result.storage_ = std::move(guid);
        return result;
    }

    PropertyValue PropertyValue::MakeComponentReference(const ComponentReference& value)
    {
        PropertyValue result;
        result.type_ = PropertyType::ComponentReference;
        result.storage_ = value;
        return result;
    }

    PropertyValue PropertyValue::MakeArray(PropertyType element_type,
        std::vector<PropertyValue> elements)
    {
        PropertyValue result;
        result.type_ = PropertyType::Array;

        // 入れ子の配列は今回未対応。要素型が Array の場合は空配列にする。
        // 中途半端に一段だけ書き出して読み戻せない形にしない。
        result.array_element_type_ =
            IsContainerType(element_type) ? PropertyType::Bool : element_type;
        if (!IsContainerType(element_type)) result.array_elements_ = std::move(elements);
        return result;
    }
}
