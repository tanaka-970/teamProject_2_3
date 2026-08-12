#include "PropertyValue.h"

#include <cmath>
#include <limits>


namespace ReplayEngine::Reflection
{
    namespace
    {
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
}
