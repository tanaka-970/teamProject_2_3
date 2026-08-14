#include "PropertyValue.h"

#include <cmath>
#include <limits>


namespace ReplayEngine::Reflection
{
    namespace
    {
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
}
