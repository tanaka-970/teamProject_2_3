#include "UIPuppetDeformComponent.h"

#include "../../Reflection/Property/PropertyValue.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace ReplayEngine::Components
{
    namespace
    {
        constexpr int maximum_pins = 64;

        std::string PinPropertyName(int index, const char* field)
        {
            char buffer[96]{};
            std::snprintf(buffer, sizeof(buffer), "pin[%d].%s", index, field);
            return std::string(buffer);
        }
    }

    void UIPuppetDeformComponent::NormalizePinArrays()
    {
        const std::size_t count = pin_positions.size();
        if (pin_bind_positions.size() < count)
        {
            const std::size_t old = pin_bind_positions.size();
            pin_bind_positions.resize(count);
            for (std::size_t index = old; index < count; ++index)
                pin_bind_positions[index] = pin_positions[index];
        }
        else if (pin_bind_positions.size() > count)
        {
            pin_bind_positions.resize(count);
        }

        if (pin_radii.size() < count) pin_radii.resize(count, 0.35f);
        else if (pin_radii.size() > count) pin_radii.resize(count);
    }

    void UIPuppetDeformComponent::SetPinCount(int count)
    {
        count = (std::max)(0, (std::min)(maximum_pins, count));
        const std::size_t old = pin_positions.size();
        pin_positions.resize(static_cast<std::size_t>(count), { 0.5f, 0.5f });
        if (pin_bind_positions.size() < pin_positions.size())
            pin_bind_positions.resize(pin_positions.size(), { 0.5f, 0.5f });
        if (pin_radii.size() < pin_positions.size())
            pin_radii.resize(pin_positions.size(), 0.35f);
        if (pin_bind_positions.size() > pin_positions.size())
            pin_bind_positions.resize(pin_positions.size());
        if (pin_radii.size() > pin_positions.size()) pin_radii.resize(pin_positions.size());
        for (std::size_t index = old; index < pin_positions.size(); ++index)
            pin_bind_positions[index] = pin_positions[index];
        RebuildDynamicProperties();
    }

    void UIPuppetDeformComponent::OnPropertyChanged(const char*)
    {
        grid_columns = (std::max)(1, (std::min)(32, grid_columns));
        grid_rows = (std::max)(1, (std::min)(32, grid_rows));
        NormalizePinArrays();
        RebuildDynamicProperties();
    }

    DirectX::XMFLOAT2 UIPuppetDeformComponent::DeformNormalizedPoint(
        const DirectX::XMFLOAT2& normalized) const noexcept
    {
        if (!enabled_deform || pin_positions.empty()) return normalized;

        DirectX::XMFLOAT2 displacement{ 0.0f, 0.0f };
        float total_weight = 0.0f;
        const std::size_t count = (std::min)(pin_positions.size(),
            (std::min)(pin_bind_positions.size(), pin_radii.size()));
        for (std::size_t index = 0; index < count; ++index)
        {
            const DirectX::XMFLOAT2 bind = pin_bind_positions[index];
            const DirectX::XMFLOAT2 current = pin_positions[index];
            const float radius = (std::max)(0.0001f, pin_radii[index]);
            const float dx = normalized.x - bind.x;
            const float dy = normalized.y - bind.y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance >= radius) continue;
            const float falloff = 1.0f - distance / radius;
            const float weight = falloff * falloff * (3.0f - 2.0f * falloff);
            displacement.x += (current.x - bind.x) * weight;
            displacement.y += (current.y - bind.y) * weight;
            total_weight += weight;
        }
        if (total_weight > 1.0f)
        {
            displacement.x /= total_weight;
            displacement.y /= total_weight;
        }
        return {
            normalized.x + displacement.x * global_strength,
            normalized.y + displacement.y * global_strength
        };
    }

    const std::vector<Reflection::PropertyDesc>*
        UIPuppetDeformComponent::DynamicProperties() const noexcept
    {
        const_cast<UIPuppetDeformComponent*>(this)->NormalizePinArrays();
        RebuildDynamicProperties();
        return dynamic_properties_.empty() ? nullptr : &dynamic_properties_;
    }

    void UIPuppetDeformComponent::RebuildDynamicProperties() const
    {
        dynamic_properties_.clear();
        dynamic_properties_.reserve(pin_positions.size() * 2);
        for (std::size_t i = 0; i < pin_positions.size(); ++i)
        {
            const int index = static_cast<int>(i);
            const std::string category = "Puppet Pin " + std::to_string(index + 1);

            Reflection::PropertyDesc position;
            position.name = PinPropertyName(index, "position");
            position.display_name = "位置";
            position.category = category;
            position.type = Reflection::PropertyType::Vector2;
            position.animatable = Reflection::Animatable::Interpolatable;
            position.serializable = false;
            position.step = 0.001;
            position.getter = [index](const Core::Component& component)
            {
                const auto& puppet = static_cast<const UIPuppetDeformComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= puppet.pin_positions.size())
                    return Reflection::PropertyValue{};
                return Reflection::PropertyValue::MakeVector2(
                    puppet.pin_positions[static_cast<std::size_t>(index)]);
            };
            position.setter = [index](Core::Component& component,
                const Reflection::PropertyValue& value)
            {
                auto& puppet = static_cast<UIPuppetDeformComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= puppet.pin_positions.size()) return;
                puppet.pin_positions[static_cast<std::size_t>(index)] = value.AsVector2();
            };
            dynamic_properties_.push_back(std::move(position));

            Reflection::PropertyDesc radius;
            radius.name = PinPropertyName(index, "radius");
            radius.display_name = "影響半径";
            radius.category = category;
            radius.type = Reflection::PropertyType::Float;
            radius.animatable = Reflection::Animatable::Interpolatable;
            radius.serializable = false;
            radius.has_range = true;
            radius.minimum = 0.001;
            radius.maximum = 4.0;
            radius.step = 0.001;
            radius.getter = [index](const Core::Component& component)
            {
                const auto& puppet = static_cast<const UIPuppetDeformComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= puppet.pin_radii.size())
                    return Reflection::PropertyValue{};
                return Reflection::PropertyValue::MakeFloat(
                    puppet.pin_radii[static_cast<std::size_t>(index)]);
            };
            radius.setter = [index](Core::Component& component,
                const Reflection::PropertyValue& value)
            {
                auto& puppet = static_cast<UIPuppetDeformComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= puppet.pin_radii.size()) return;
                puppet.pin_radii[static_cast<std::size_t>(index)] =
                    (std::max)(0.001f, value.AsFloat(0.35f));
            };
            dynamic_properties_.push_back(std::move(radius));
        }
    }
}
