#include "PropertyValue.h"

namespace ReplayEngine::Reflection
{
    namespace
    {
        const std::string& EmptyString() noexcept
        {
            static const std::string empty;
            return empty;
        }

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

    bool PropertyValue::AsBool(bool fallback) const noexcept
    {
        if (const bool* value = std::get_if<bool>(&storage_)) return *value;
        if (const int* value = std::get_if<int>(&storage_)) return *value != 0;
        return fallback;
    }

    int PropertyValue::AsInt(int fallback) const noexcept
    {
        if (const int* value = std::get_if<int>(&storage_)) return *value;
        if (const bool* value = std::get_if<bool>(&storage_)) return *value ? 1 : 0;
        if (const float* value = std::get_if<float>(&storage_)) return static_cast<int>(*value);
        if (const double* value = std::get_if<double>(&storage_)) return static_cast<int>(*value);
        return fallback;
    }

    float PropertyValue::AsFloat(float fallback) const noexcept
    {
        if (const float* value = std::get_if<float>(&storage_)) return *value;
        if (const double* value = std::get_if<double>(&storage_)) return static_cast<float>(*value);
        if (const int* value = std::get_if<int>(&storage_)) return static_cast<float>(*value);
        return fallback;
    }

    double PropertyValue::AsDouble(double fallback) const noexcept
    {
        if (const double* value = std::get_if<double>(&storage_)) return *value;
        if (const float* value = std::get_if<float>(&storage_)) return static_cast<double>(*value);
        if (const int* value = std::get_if<int>(&storage_)) return static_cast<double>(*value);
        return fallback;
    }

    const std::string& PropertyValue::AsString() const noexcept
    {
        if (const std::string* value = std::get_if<std::string>(&storage_)) return *value;
        return EmptyString();
    }

    DirectX::XMFLOAT2 PropertyValue::AsVector2() const noexcept
    {
        if (const auto* value = std::get_if<DirectX::XMFLOAT2>(&storage_)) return *value;
        if (const auto* value = std::get_if<DirectX::XMFLOAT3>(&storage_))
            return DirectX::XMFLOAT2{ value->x, value->y };
        if (const auto* value = std::get_if<DirectX::XMFLOAT4>(&storage_))
            return DirectX::XMFLOAT2{ value->x, value->y };
        return DirectX::XMFLOAT2{ 0.0f, 0.0f };
    }

    DirectX::XMFLOAT3 PropertyValue::AsVector3() const noexcept
    {
        if (const auto* value = std::get_if<DirectX::XMFLOAT3>(&storage_)) return *value;
        if (const auto* value = std::get_if<DirectX::XMFLOAT2>(&storage_))
            return DirectX::XMFLOAT3{ value->x, value->y, 0.0f };
        if (const auto* value = std::get_if<DirectX::XMFLOAT4>(&storage_))
            return DirectX::XMFLOAT3{ value->x, value->y, value->z };
        return DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
    }

    DirectX::XMFLOAT4 PropertyValue::AsVector4() const noexcept
    {
        if (const auto* value = std::get_if<DirectX::XMFLOAT4>(&storage_)) return *value;
        if (const auto* value = std::get_if<DirectX::XMFLOAT3>(&storage_))
            return DirectX::XMFLOAT4{ value->x, value->y, value->z, 1.0f };
        if (const auto* value = std::get_if<DirectX::XMFLOAT2>(&storage_))
            return DirectX::XMFLOAT4{ value->x, value->y, 0.0f, 1.0f };
        return DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 1.0f };
    }

    Core::ObjectID PropertyValue::AsObjectReference() const noexcept
    {
        if (const auto* value = std::get_if<Core::ObjectID>(&storage_)) return *value;
        return Core::ObjectID::Invalid();
    }

    bool PropertyValue::ConvertTo(PropertyType target, PropertyValue& out) const
    {
        if (target == type_)
        {
            out = *this;
            return true;
        }

        // Scene ファイル側でプロパティの型が変わっていた場合の救済。
        // 意味が保てる組み合わせだけ通し、それ以外は false を返して既定値を維持させる。
        switch (target)
        {
        case PropertyType::Bool:
            if (type_ == PropertyType::Int || type_ == PropertyType::Enum)
            {
                out = MakeBool(AsInt() != 0);
                return true;
            }
            return false;

        case PropertyType::Int:
        case PropertyType::Enum:
            if (type_ == PropertyType::Bool || type_ == PropertyType::Int ||
                type_ == PropertyType::Enum || type_ == PropertyType::Float ||
                type_ == PropertyType::Double)
            {
                out = target == PropertyType::Enum ? MakeEnum(AsInt()) : MakeInt(AsInt());
                return true;
            }
            return false;

        case PropertyType::Float:
            if (type_ == PropertyType::Int || type_ == PropertyType::Double ||
                type_ == PropertyType::Enum)
            {
                out = MakeFloat(AsFloat());
                return true;
            }
            return false;

        case PropertyType::Double:
            if (type_ == PropertyType::Int || type_ == PropertyType::Float ||
                type_ == PropertyType::Enum)
            {
                out = MakeDouble(AsDouble());
                return true;
            }
            return false;

        case PropertyType::String:
        case PropertyType::AssetPath:
            if (type_ == PropertyType::String || type_ == PropertyType::AssetPath)
            {
                out = target == PropertyType::String
                    ? MakeString(AsString()) : MakeAssetPath(AsString());
                return true;
            }
            return false;

        case PropertyType::Vector2:
            if (type_ == PropertyType::Vector3 || type_ == PropertyType::Vector4)
            {
                out = MakeVector2(AsVector2());
                return true;
            }
            return false;

        case PropertyType::Vector3:
            if (type_ == PropertyType::Vector2 || type_ == PropertyType::Vector4 ||
                type_ == PropertyType::Color || type_ == PropertyType::Quaternion)
            {
                out = MakeVector3(AsVector3());
                return true;
            }
            return false;

        case PropertyType::Vector4:
        case PropertyType::Quaternion:
        case PropertyType::Color:
            if (type_ == PropertyType::Vector2 || type_ == PropertyType::Vector3 ||
                type_ == PropertyType::Vector4 || type_ == PropertyType::Quaternion ||
                type_ == PropertyType::Color)
            {
                const DirectX::XMFLOAT4 value = AsVector4();
                if (target == PropertyType::Quaternion) out = MakeQuaternion(value);
                else if (target == PropertyType::Color) out = MakeColor(value);
                else out = MakeVector4(value);
                return true;
            }
            return false;

        case PropertyType::ObjectReference:
            return false;
        }
        return false;
    }
}
