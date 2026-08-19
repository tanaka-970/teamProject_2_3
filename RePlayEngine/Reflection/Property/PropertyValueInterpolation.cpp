#include "PropertyValue.h"

#include <cmath>
#include <limits>


namespace ReplayEngine::Reflection
{
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
