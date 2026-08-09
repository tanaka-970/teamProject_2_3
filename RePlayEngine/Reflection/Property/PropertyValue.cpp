#include "PropertyValue.h"

#include <cmath>
#include <limits>

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

        bool IsIntegerLike(PropertyType type) noexcept
        {
            switch (type)
            {
            case PropertyType::Bool:
            case PropertyType::Int:
            case PropertyType::Int64:
            case PropertyType::UInt64:
            case PropertyType::Enum:
            case PropertyType::CollisionLayer:
            case PropertyType::CollisionMask:
            case PropertyType::ColliderReference:
                return true;
            default:
                return false;
            }
        }

        bool IsNumeric(PropertyType type) noexcept
        {
            return IsIntegerLike(type) ||
                type == PropertyType::Float || type == PropertyType::Double;
        }

        bool IsGuidString(PropertyType type) noexcept
        {
            return type == PropertyType::AssetReference ||
                type == PropertyType::SceneReference;
        }

        // 値の等価判定に使う許容差。
        //
        // 名前空間スコープへ置く理由:
        //   関数ローカルの constexpr をラムダの中から参照すると、MSVC が
        //   「既定のキャプチャモードが無いのでキャプチャできない」(C3493) として弾く。
        //   名前空間スコープの定数ならキャプチャの対象にならないため、
        //   キャプチャ無しのラムダからそのまま使える。
        constexpr float value_float_tolerance = 0.00001f;
        constexpr double value_double_tolerance = 0.0000001;

        bool NearlyEqualFloat(float a, float b) noexcept
        {
            return std::fabs(a - b) <= value_float_tolerance;
        }
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

    bool PropertyValue::AsBool(bool fallback) const noexcept
    {
        if (const bool* value = std::get_if<bool>(&storage_)) return *value;
        if (const int* value = std::get_if<int>(&storage_)) return *value != 0;
        if (const auto* value = std::get_if<std::int64_t>(&storage_)) return *value != 0;
        if (const auto* value = std::get_if<std::uint64_t>(&storage_)) return *value != 0;
        return fallback;
    }

    int PropertyValue::AsInt(int fallback) const noexcept
    {
        if (const int* value = std::get_if<int>(&storage_)) return *value;
        if (const bool* value = std::get_if<bool>(&storage_)) return *value ? 1 : 0;
        if (const auto* value = std::get_if<std::int64_t>(&storage_))
            return static_cast<int>(*value);
        if (const auto* value = std::get_if<std::uint64_t>(&storage_))
            return static_cast<int>(*value);
        if (const float* value = std::get_if<float>(&storage_)) return static_cast<int>(*value);
        if (const double* value = std::get_if<double>(&storage_)) return static_cast<int>(*value);
        return fallback;
    }

    std::int64_t PropertyValue::AsInt64(std::int64_t fallback) const noexcept
    {
        if (const auto* value = std::get_if<std::int64_t>(&storage_)) return *value;
        if (const int* value = std::get_if<int>(&storage_))
            return static_cast<std::int64_t>(*value);
        if (const bool* value = std::get_if<bool>(&storage_)) return *value ? 1 : 0;
        if (const auto* value = std::get_if<std::uint64_t>(&storage_))
            return static_cast<std::int64_t>(*value);
        if (const float* value = std::get_if<float>(&storage_))
            return static_cast<std::int64_t>(*value);
        if (const double* value = std::get_if<double>(&storage_))
            return static_cast<std::int64_t>(*value);
        return fallback;
    }

    std::uint64_t PropertyValue::AsUInt64(std::uint64_t fallback) const noexcept
    {
        if (const auto* value = std::get_if<std::uint64_t>(&storage_)) return *value;
        if (const auto* value = std::get_if<std::int64_t>(&storage_))
            return *value < 0 ? 0u : static_cast<std::uint64_t>(*value);
        if (const int* value = std::get_if<int>(&storage_))
            return *value < 0 ? 0u : static_cast<std::uint64_t>(*value);
        if (const bool* value = std::get_if<bool>(&storage_)) return *value ? 1u : 0u;
        if (const float* value = std::get_if<float>(&storage_))
            return *value < 0.0f ? 0u : static_cast<std::uint64_t>(*value);
        if (const double* value = std::get_if<double>(&storage_))
            return *value < 0.0 ? 0u : static_cast<std::uint64_t>(*value);
        return fallback;
    }

    float PropertyValue::AsFloat(float fallback) const noexcept
    {
        if (const float* value = std::get_if<float>(&storage_)) return *value;
        if (const double* value = std::get_if<double>(&storage_)) return static_cast<float>(*value);
        if (const int* value = std::get_if<int>(&storage_)) return static_cast<float>(*value);
        if (const auto* value = std::get_if<std::int64_t>(&storage_))
            return static_cast<float>(*value);
        if (const auto* value = std::get_if<std::uint64_t>(&storage_))
            return static_cast<float>(*value);
        return fallback;
    }

    double PropertyValue::AsDouble(double fallback) const noexcept
    {
        if (const double* value = std::get_if<double>(&storage_)) return *value;
        if (const float* value = std::get_if<float>(&storage_)) return static_cast<double>(*value);
        if (const int* value = std::get_if<int>(&storage_)) return static_cast<double>(*value);
        if (const auto* value = std::get_if<std::int64_t>(&storage_))
            return static_cast<double>(*value);
        if (const auto* value = std::get_if<std::uint64_t>(&storage_))
            return static_cast<double>(*value);
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

    ComponentReference PropertyValue::AsComponentReference() const noexcept
    {
        if (const auto* value = std::get_if<ComponentReference>(&storage_)) return *value;
        return ComponentReference{};
    }

    AssetReference PropertyValue::AsAssetReference() const
    {
        // 型が AssetReference でなくても、文字列を持っていれば拾う。
        // 保存済みの値を「型が違う」という理由だけで捨てないため。
        return AssetReference{ AsString() };
    }

    SceneReference PropertyValue::AsSceneReference() const
    {
        return SceneReference{ AsString() };
    }

    bool PropertyValue::IsFinite() const noexcept
    {
        if (type_ == PropertyType::Array)
        {
            for (const PropertyValue& element : array_elements_)
            {
                if (!element.IsFinite()) return false;
            }
            return true;
        }

        if (const float* value = std::get_if<float>(&storage_)) return std::isfinite(*value);
        if (const double* value = std::get_if<double>(&storage_)) return std::isfinite(*value);

        if (const auto* value = std::get_if<DirectX::XMFLOAT2>(&storage_))
            return std::isfinite(value->x) && std::isfinite(value->y);
        if (const auto* value = std::get_if<DirectX::XMFLOAT3>(&storage_))
            return std::isfinite(value->x) && std::isfinite(value->y) && std::isfinite(value->z);
        if (const auto* value = std::get_if<DirectX::XMFLOAT4>(&storage_))
        {
            return std::isfinite(value->x) && std::isfinite(value->y) &&
                std::isfinite(value->z) && std::isfinite(value->w);
        }
        return true;
    }

    bool ValuesEqual(const PropertyValue& a, const PropertyValue& b) noexcept
    {
        if (a.Type() != b.Type()) return false;

        switch (a.Type())
        {
        case PropertyType::Bool:
            return a.AsBool() == b.AsBool();

        case PropertyType::Int:
        case PropertyType::Enum:
        case PropertyType::CollisionLayer:
        case PropertyType::CollisionMask:
        case PropertyType::ColliderReference:
            return a.AsInt() == b.AsInt();

        case PropertyType::Int64:
            return a.AsInt64() == b.AsInt64();

        case PropertyType::UInt64:
            return a.AsUInt64() == b.AsUInt64();

        case PropertyType::Float:
            return NearlyEqualFloat(a.AsFloat(), b.AsFloat());

        case PropertyType::Double:
            return std::fabs(a.AsDouble() - b.AsDouble()) <= value_double_tolerance;

        case PropertyType::String:
        case PropertyType::AssetPath:
        case PropertyType::AssetReference:
        case PropertyType::SceneReference:
            return a.AsString() == b.AsString();

        case PropertyType::ObjectReference:
            return a.AsObjectReference() == b.AsObjectReference();

        case PropertyType::ComponentReference:
            return a.AsComponentReference() == b.AsComponentReference();

        case PropertyType::Vector2:
        {
            const DirectX::XMFLOAT2 x = a.AsVector2();
            const DirectX::XMFLOAT2 y = b.AsVector2();
            return NearlyEqualFloat(x.x, y.x) && NearlyEqualFloat(x.y, y.y);
        }
        case PropertyType::Vector3:
        {
            const DirectX::XMFLOAT3 x = a.AsVector3();
            const DirectX::XMFLOAT3 y = b.AsVector3();
            return NearlyEqualFloat(x.x, y.x) && NearlyEqualFloat(x.y, y.y) && NearlyEqualFloat(x.z, y.z);
        }
        case PropertyType::Vector4:
        case PropertyType::Quaternion:
        case PropertyType::Color:
        {
            const DirectX::XMFLOAT4 x = a.AsVector4();
            const DirectX::XMFLOAT4 y = b.AsVector4();
            return NearlyEqualFloat(x.x, y.x) && NearlyEqualFloat(x.y, y.y) &&
                NearlyEqualFloat(x.z, y.z) && NearlyEqualFloat(x.w, y.w);
        }
        case PropertyType::Array:
        {
            if (a.ArrayElementType() != b.ArrayElementType()) return false;
            const std::vector<PropertyValue>& left = a.ArrayElements();
            const std::vector<PropertyValue>& right = b.ArrayElements();
            if (left.size() != right.size()) return false;
            for (std::size_t index = 0; index < left.size(); ++index)
            {
                if (!ValuesEqual(left[index], right[index])) return false;
            }
            return true;
        }
        }
        return false;
    }

    bool PropertyValue::ConvertTo(PropertyType target, PropertyValue& out) const
    {
        if (target == type_)
        {
            out = *this;
            return true;
        }

        // 配列は他の型と行き来させない。
        // 要素数が決まらない変換を許すと、黙って値が消える経路ができてしまう。
        if (target == PropertyType::Array || type_ == PropertyType::Array) return false;

        // Scene ファイル側でプロパティの型が変わっていた場合の救済。
        // 意味が保てる組み合わせだけ通し、それ以外は false を返して既定値を維持させる。
        switch (target)
        {
        case PropertyType::Bool:
            if (IsIntegerLike(type_))
            {
                out = MakeBool(AsInt64() != 0);
                return true;
            }
            return false;

        case PropertyType::Int:
        case PropertyType::Enum:
        case PropertyType::CollisionLayer:
        case PropertyType::CollisionMask:
        case PropertyType::ColliderReference:
            // 内部表現がどれも int なので、意味の付け替えとして相互に通す。
            // 古い Scene が生の int で保存していた値も、そのまま読める。
            if (IsNumeric(type_))
            {
                const int raw = AsInt();
                switch (target)
                {
                case PropertyType::Enum:              out = MakeEnum(raw); break;
                case PropertyType::CollisionLayer:    out = MakeCollisionLayer(raw); break;
                case PropertyType::CollisionMask:     out = MakeCollisionMask(raw); break;
                case PropertyType::ColliderReference: out = MakeColliderReference(raw); break;
                default:                              out = MakeInt(raw); break;
                }
                return true;
            }
            return false;

        case PropertyType::Int64:
            if (IsNumeric(type_))
            {
                out = MakeInt64(AsInt64());
                return true;
            }
            return false;

        case PropertyType::UInt64:
            if (IsNumeric(type_))
            {
                out = MakeUInt64(AsUInt64());
                return true;
            }
            return false;

        case PropertyType::Float:
            if (IsNumeric(type_))
            {
                out = MakeFloat(AsFloat());
                return true;
            }
            return false;

        case PropertyType::Double:
            if (IsNumeric(type_))
            {
                out = MakeDouble(AsDouble());
                return true;
            }
            return false;

        case PropertyType::String:
        case PropertyType::AssetPath:
            // AssetPath と AssetReference は行き来させない。
            // 片方は「プロジェクト相対パス」、もう片方は「AssetGUID」で、
            // 中身の意味がまったく違う。文字列として写すと壊れた参照になる。
            if (type_ == PropertyType::String || type_ == PropertyType::AssetPath)
            {
                out = target == PropertyType::String
                    ? MakeString(AsString()) : MakeAssetPath(AsString());
                return true;
            }
            return false;

        case PropertyType::AssetReference:
        case PropertyType::SceneReference:
            // GUID 文字列どうし、および素の文字列からは受け付ける。
            if (IsGuidString(type_) || type_ == PropertyType::String)
            {
                out = target == PropertyType::AssetReference
                    ? MakeAssetReference(AsString()) : MakeSceneReference(AsString());
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
        case PropertyType::ComponentReference:
            // 参照どうしは変換しない。指す先の種類が違う。
            return false;

        case PropertyType::Array:
            return false;
        }
        return false;
    }

    PropertyValue PropertyValue::Lerp(const PropertyValue& a, const PropertyValue& b, float t)
    {
        if (a.Type() != b.Type()) return a;

        const auto lerp_float = [t](float x, float y) noexcept
        {
            return x + (y - x) * t;
        };
        const auto lerp_double = [t](double x, double y) noexcept
        {
            return x + (y - x) * static_cast<double>(t);
        };

        switch (a.Type())
        {
        case PropertyType::Float:
            return MakeFloat(lerp_float(a.AsFloat(), b.AsFloat()));

        case PropertyType::Double:
            return MakeDouble(lerp_double(a.AsDouble(), b.AsDouble()));

        case PropertyType::Int:
            return MakeInt(static_cast<int>(std::lround(
                lerp_double(static_cast<double>(a.AsInt()), static_cast<double>(b.AsInt())))));

        case PropertyType::Int64:
            return MakeInt64(static_cast<std::int64_t>(std::llround(
                lerp_double(static_cast<double>(a.AsInt64()), static_cast<double>(b.AsInt64())))));

        case PropertyType::UInt64:
        {
            const double mixed = lerp_double(
                static_cast<double>(a.AsUInt64()), static_cast<double>(b.AsUInt64()));
            return MakeUInt64(mixed <= 0.0
                ? 0u : static_cast<std::uint64_t>(std::llround(mixed)));
        }

        case PropertyType::Vector2:
        {
            const DirectX::XMFLOAT2 x = a.AsVector2();
            const DirectX::XMFLOAT2 y = b.AsVector2();
            return MakeVector2({ lerp_float(x.x, y.x), lerp_float(x.y, y.y) });
        }

        case PropertyType::Vector3:
        {
            const DirectX::XMFLOAT3 x = a.AsVector3();
            const DirectX::XMFLOAT3 y = b.AsVector3();
            return MakeVector3({
                lerp_float(x.x, y.x),
                lerp_float(x.y, y.y),
                lerp_float(x.z, y.z) });
        }

        case PropertyType::Vector4:
        {
            const DirectX::XMFLOAT4 x = a.AsVector4();
            const DirectX::XMFLOAT4 y = b.AsVector4();
            return MakeVector4({
                lerp_float(x.x, y.x),
                lerp_float(x.y, y.y),
                lerp_float(x.z, y.z),
                lerp_float(x.w, y.w) });
        }

        case PropertyType::Color:
        {
            const DirectX::XMFLOAT4 x = a.AsVector4();
            const DirectX::XMFLOAT4 y = b.AsVector4();
            return MakeColor({
                lerp_float(x.x, y.x),
                lerp_float(x.y, y.y),
                lerp_float(x.z, y.z),
                lerp_float(x.w, y.w) });
        }

        case PropertyType::Quaternion:
        {
            const DirectX::XMFLOAT4 ax = a.AsVector4();
            const DirectX::XMFLOAT4 by = b.AsVector4();
            const DirectX::XMVECTOR x = DirectX::XMLoadFloat4(&ax);
            const DirectX::XMVECTOR y = DirectX::XMLoadFloat4(&by);
            DirectX::XMFLOAT4 mixed{};
            DirectX::XMStoreFloat4(&mixed, DirectX::XMQuaternionSlerp(x, y, t));
            return MakeQuaternion(mixed);
        }

        case PropertyType::Bool:
        case PropertyType::String:
        case PropertyType::Enum:
        case PropertyType::CollisionLayer:
        case PropertyType::CollisionMask:
        case PropertyType::ColliderReference:
        case PropertyType::AssetPath:
        case PropertyType::AssetReference:
        case PropertyType::SceneReference:
        case PropertyType::ObjectReference:
        case PropertyType::ComponentReference:
        case PropertyType::Array:
            return t < 1.0f ? a : b;
        }
        return a;
    }
}
