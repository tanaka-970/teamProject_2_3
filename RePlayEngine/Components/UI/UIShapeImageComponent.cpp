#include "UIShapeImageComponent.h"

#include "../../Reflection/Property/PropertyValue.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

namespace ReplayEngine::Components
{
    namespace
    {
        constexpr int maximum_path_points = 64;

        std::string PathPropertyName(int index, const char* field)
        {
            char buffer[96]{};
            std::snprintf(buffer, sizeof(buffer), "point[%d].%s", index, field);
            return std::string(buffer);
        }
    }

    void UIShapeImageComponent::NormalizePathArrays()
    {
        const std::size_t count = path_points.size();
        path_in_handles.resize(count, { 0.0f, 0.0f });
        path_out_handles.resize(count, { 0.0f, 0.0f });
    }

    void UIShapeImageComponent::SetPathPointCount(int count)
    {
        count = (std::max)(0, (std::min)(maximum_path_points, count));
        const std::size_t old = path_points.size();
        path_points.resize(static_cast<std::size_t>(count), { 0.5f, 0.5f });
        path_in_handles.resize(path_points.size(), { 0.0f, 0.0f });
        path_out_handles.resize(path_points.size(), { 0.0f, 0.0f });
        if (old == 0 && path_points.size() >= 2)
        {
            for (std::size_t i = 0; i < path_points.size(); ++i)
            {
                const float t = static_cast<float>(i) /
                    static_cast<float>((std::max)(std::size_t{ 1 }, path_points.size() - 1));
                path_points[i] = { t, 0.5f };
            }
        }
        RebuildDynamicProperties();
    }

    void UIShapeImageComponent::OnPropertyChanged(const char*)
    {
        NormalizePathArrays();
        RebuildDynamicProperties();
    }

    const std::vector<Reflection::PropertyDesc>*
        UIShapeImageComponent::DynamicProperties() const noexcept
    {
        const_cast<UIShapeImageComponent*>(this)->NormalizePathArrays();
        RebuildDynamicProperties();
        return dynamic_properties_.empty() ? nullptr : &dynamic_properties_;
    }

    void UIShapeImageComponent::RebuildDynamicProperties() const
    {
        dynamic_properties_.clear();
        dynamic_properties_.reserve(path_points.size() * 3);
        for (std::size_t i = 0; i < path_points.size(); ++i)
        {
            const int index = static_cast<int>(i);
            const std::string category = "Path Point " + std::to_string(index + 1);
            const auto add_vector = [&](const char* field, const char* display,
                std::vector<DirectX::XMFLOAT2> UIShapeImageComponent::* member)
            {
                Reflection::PropertyDesc desc;
                desc.name = PathPropertyName(index, field);
                desc.display_name = display;
                desc.category = category;
                desc.type = Reflection::PropertyType::Vector2;
                desc.animatable = Reflection::Animatable::Interpolatable;
                desc.serializable = false;
                desc.step = 0.001;
                desc.getter = [index, member](const Core::Component& component)
                {
                    const auto& shape = static_cast<const UIShapeImageComponent&>(component);
                    const auto& values = shape.*member;
                    if (index < 0 || static_cast<std::size_t>(index) >= values.size())
                        return Reflection::PropertyValue{};
                    return Reflection::PropertyValue::MakeVector2(
                        values[static_cast<std::size_t>(index)]);
                };
                desc.setter = [index, member](Core::Component& component,
                    const Reflection::PropertyValue& value)
                {
                    auto& shape = static_cast<UIShapeImageComponent&>(component);
                    auto& values = shape.*member;
                    if (index < 0 || static_cast<std::size_t>(index) >= values.size()) return;
                    values[static_cast<std::size_t>(index)] = value.AsVector2();
                };
                dynamic_properties_.push_back(std::move(desc));
            };
            add_vector("position", "位置", &UIShapeImageComponent::path_points);
            add_vector("in_handle", "入力ハンドル", &UIShapeImageComponent::path_in_handles);
            add_vector("out_handle", "出力ハンドル", &UIShapeImageComponent::path_out_handles);
        }
    }
}
