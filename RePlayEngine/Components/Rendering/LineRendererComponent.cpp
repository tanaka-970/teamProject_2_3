#include "LineRendererComponent.h"

#include "../../Reflection/Property/PropertyBag.h"
#include "../../Reflection/Property/PropertyValue.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
    using DirectX::XMFLOAT3;
    using DirectX::XMFLOAT4;
    using DirectX::XMVECTOR;

    XMFLOAT3 CatmullRom(const XMFLOAT3& p0, const XMFLOAT3& p1,
        const XMFLOAT3& p2, const XMFLOAT3& p3, float t) noexcept
    {
        const float t2 = t * t;
        const float t3 = t2 * t;
        return {
            0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t +
                (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
                (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3),
            0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t +
                (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
                (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3),
            0.5f * ((2.0f * p1.z) + (-p0.z + p2.z) * t +
                (2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2 +
                (-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * t3)
        };
    }

    std::string PointPropertyName(int index)
    {
        char buffer[48]{};
        std::snprintf(buffer, sizeof(buffer), "points[%d]", index);
        return std::string(buffer);
    }

    bool ParsePointPropertyName(const std::string& name, int& index)
    {
        constexpr const char* prefix = "points[";
        if (name.compare(0, 7, prefix) != 0 || name.empty() || name.back() != ']')
            return false;
        if (name.size() == 8) return false;
        int parsed = 0;
        for (std::size_t character = 7; character + 1 < name.size(); ++character)
        {
            if (name[character] < '0' || name[character] > '9') return false;
            const int digit = name[character] - '0';
            if (parsed > ((std::numeric_limits<int>::max)() - 1 - digit) / 10)
                return false;
            parsed = parsed * 10 + digit;
        }
        index = parsed;
        return true;
    }

    ReplayEngine::Reflection::PropertyDesc MakePointProperty(int index)
    {
        using namespace ReplayEngine;
        Reflection::PropertyDesc desc;
        desc.name = PointPropertyName(index);
        desc.display_name = "点 " + std::to_string(index);
        desc.tooltip = "ラインを通すローカル座標。Motion から 1 点ずつ動かせる。";
        desc.type = Reflection::PropertyType::Vector3;
        desc.animatable = Reflection::Animatable::Interpolatable;
        desc.serializable = true;
        desc.getter = [index](const Core::Component& component)
        {
            if (component.TypeID() != Components::LineRendererComponent::StaticTypeID())
                return Reflection::PropertyValue{};
            const auto& line = static_cast<const Components::LineRendererComponent&>(component);
            if (index < 0 || static_cast<std::size_t>(index) >= line.points.size())
                return Reflection::PropertyValue{};
            return Reflection::PropertyValue::MakeVector3(
                line.points[static_cast<std::size_t>(index)]);
        };
        desc.setter = [index](Core::Component& component,
            const Reflection::PropertyValue& value)
        {
            if (component.TypeID() != Components::LineRendererComponent::StaticTypeID())
                return;
            auto& line = static_cast<Components::LineRendererComponent&>(component);
            if (index < 0 || static_cast<std::size_t>(index) >= line.points.size())
                return;
            line.points[static_cast<std::size_t>(index)] = value.AsVector3();
        };
        return desc;
    }
}

namespace ReplayEngine::Rendering
{
    std::vector<DirectX::XMFLOAT3> BuildCatmullRomLinePath(
        const std::vector<DirectX::XMFLOAT3>& control_points,
        int smoothing, bool closed)
    {
        if (control_points.size() < 2 || smoothing <= 0) return control_points;

        const int subdivisions = smoothing >= 255 ? 256 : smoothing + 1;
        const std::size_t segment_count = closed
            ? control_points.size() : control_points.size() - 1;
        if (segment_count > ((std::numeric_limits<std::size_t>::max)() - 1) /
            static_cast<std::size_t>(subdivisions))
        {
            return {};
        }

        std::vector<DirectX::XMFLOAT3> result;
        try
        {
            result.reserve(segment_count * static_cast<std::size_t>(subdivisions) + 1);
            for (std::size_t segment = 0; segment < segment_count; ++segment)
            {
                const std::size_t p1_index = segment;
                const std::size_t p2_index = (segment + 1) % control_points.size();
                const std::size_t p0_index = segment > 0 ? segment - 1
                    : (closed ? control_points.size() - 1 : p1_index);
                const std::size_t p3_index = segment + 2 < control_points.size()
                    ? segment + 2 : (closed ? (segment + 2) % control_points.size()
                        : p2_index);

                // Catmull-Rom は制御点を必ず通るため、手置きパスの位置と
                // 描画線がずれない。開いた端では端点を複製して全区間を残す。
                for (int division = 0; division < subdivisions; ++division)
                {
                    const float t = static_cast<float>(division) /
                        static_cast<float>(subdivisions);
                    result.push_back(CatmullRom(control_points[p0_index],
                        control_points[p1_index], control_points[p2_index],
                        control_points[p3_index], t));
                }
            }
            if (!closed) result.push_back(control_points.back());
        }
        catch (...)
        {
            result.clear();
        }
        return result;
    }

}
namespace ReplayEngine::Components
{
    LineRendererComponent::LineRendererComponent()
    {
        ResizePoints();
        RebuildDynamicProperties();
    }

    const std::vector<Reflection::PropertyDesc>*
        LineRendererComponent::DynamicProperties() const noexcept
    {
        return dynamic_properties_.empty() ? nullptr : &dynamic_properties_;
    }

    void LineRendererComponent::OnSerialize(Reflection::PropertyBag& output) const
    {
        output.Set("point_count", Reflection::PropertyValue::MakeInt(
            static_cast<int>(points.size())));
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            output.Set(PointPropertyName(static_cast<int>(index)),
                Reflection::PropertyValue::MakeVector3(points[index]));
        }
    }

    void LineRendererComponent::OnDeserialize(const Reflection::PropertyBag& input)
    {
        int inferred_count = point_count;
        if (const Reflection::PropertyValue* stored = input.Find("point_count"))
            inferred_count = stored->AsInt(inferred_count);
        for (const Reflection::PropertyBag::Entry& entry : input.Entries())
        {
            int index = 0;
            if (ParsePointPropertyName(entry.name, index))
                inferred_count = (std::max)(inferred_count, index + 1);
        }
        point_count = inferred_count;
        ResizePoints();
        RebuildDynamicProperties();
        for (const Reflection::PropertyBag::Entry& entry : input.Entries())
        {
            int index = 0;
            if (!ParsePointPropertyName(entry.name, index) || index < 0 ||
                static_cast<std::size_t>(index) >= points.size())
            {
                continue;
            }
            points[static_cast<std::size_t>(index)] = entry.value.AsVector3();
        }
    }

    void LineRendererComponent::OnPropertyChanged(const char* property_name)
    {
        if (property_name == nullptr || std::string(property_name) == "point_count")
        {
            ResizePoints();
            RebuildDynamicProperties();
        }
    }

    Rendering::LineStrokeStyle LineRendererComponent::StrokeStyle() const
    {
        Rendering::LineStrokeStyle style;
        style.width_start = width_start;
        style.width_end = width_end;
        style.billboard = billboard;
        style.uv_mode = uv_mode;
        style.uv_tiling = uv_tiling;
        style.uv_scroll = uv_scroll;
        style.texture_guid = texture.guid;
        style.fill_color = fill_color;
        style.fill_color_2 = fill_color_2;
        style.fill_mode = fill_mode;
        style.trim_start = trim_start;
        style.trim_end = trim_end;
        style.trim_offset = trim_offset;
        style.closed = closed;
        return style;
    }

    void LineRendererComponent::ResizePoints()
    {
        if (point_count < 0) point_count = 0;
        const std::size_t previous_size = points.size();
        try
        {
            points.resize(static_cast<std::size_t>(point_count));
            for (std::size_t index = previous_size; index < points.size(); ++index)
                points[index] = { static_cast<float>(index), 0.0f, 0.0f };
        }
        catch (...)
        {
            point_count = static_cast<int>((std::min)(points.size(),
                static_cast<std::size_t>((std::numeric_limits<int>::max)())));
        }
    }

    void LineRendererComponent::RebuildDynamicProperties()
    {
        dynamic_properties_.clear();
        try
        {
            dynamic_properties_.reserve(points.size());
            for (std::size_t index = 0; index < points.size(); ++index)
                dynamic_properties_.push_back(MakePointProperty(static_cast<int>(index)));
        }
        catch (...)
        {
            dynamic_properties_.clear();
        }
    }
}
