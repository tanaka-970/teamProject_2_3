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
}
