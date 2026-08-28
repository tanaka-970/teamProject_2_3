#pragma once

#include "framework.h"

#include "../../RePlayEngine/Components/UI/UIImageComponent.h"
#include "../../RePlayEngine/Motion/MotionBindingResolver.h"
#include "../../RePlayEngine/Motion/MotionEvaluator.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Object/Registry/ComponentRegistry.h"
#include "../../RePlayEngine/Reflection/Registry/PropertyRegistry.h"
#include "../../RePlayEngine/Scene/Runtime/Scene.h"

#include "imgui/imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>


namespace framework_motion_workspace::Detail
{
    using ReplayEngine::Core::Component;
    using ReplayEngine::Core::ComponentTypeID;
    using ReplayEngine::Motion::MotionAsset;
    using ReplayEngine::Motion::MotionBinding;
    using ReplayEngine::Motion::MotionBindingOrigin;
    using ReplayEngine::Motion::MotionBlendMode;
    using ReplayEngine::Motion::MotionEasing;
    using ReplayEngine::Motion::MotionEvaluator;
    using ReplayEngine::Motion::MotionEvent;
    using ReplayEngine::Motion::MotionEventTrack;
    using ReplayEngine::Motion::MotionKeyframe;
    using ReplayEngine::Motion::MotionTrack;
    using ReplayEngine::Reflection::Animatable;
    using ReplayEngine::Reflection::PropertyDesc;
    using ReplayEngine::Reflection::PropertyRegistry;
    using ReplayEngine::Reflection::PropertyType;
    using ReplayEngine::Reflection::PropertyValue;

    inline std::string Lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    inline float FrameStep(int fps) noexcept
    {
        return 1.0f / static_cast<float>((std::max)(1, fps));
    }

    inline float SnapMotionTime(float time, int fps, bool snap) noexcept
    {
        const float safe = (std::max)(0.0f, time);
        if (!snap) return safe;
        const float frame = FrameStep(fps);
        return std::round(safe / frame) * frame;
    }

    inline const char* PropertyTypeLabel(PropertyType type) noexcept
    {
        switch (type)
        {
        case PropertyType::Bool: return "Bool";
        case PropertyType::Int: return "Int";
        case PropertyType::Float: return "Float";
        case PropertyType::Double: return "Double";
        case PropertyType::String: return "String";
        case PropertyType::Vector2: return "Vector2";
        case PropertyType::Vector3: return "Vector3";
        case PropertyType::Vector4: return "Vector4";
        case PropertyType::Quaternion: return "Quaternion";
        case PropertyType::Color: return "Color";
        case PropertyType::Enum: return "Enum";
        case PropertyType::AssetReference: return "Asset";
        default: return "Other";
        }
    }

    inline PropertyValue DefaultValueFor(PropertyType type)
    {
        switch (type)
        {
        case PropertyType::Bool: return PropertyValue::MakeBool(false);
        case PropertyType::Int:
        case PropertyType::Enum: return PropertyValue::MakeInt(0);
        case PropertyType::Float: return PropertyValue::MakeFloat(0.0f);
        case PropertyType::Double: return PropertyValue::MakeDouble(0.0);
        case PropertyType::String: return PropertyValue::MakeString(std::string());
        case PropertyType::Vector2:
            return PropertyValue::MakeVector2(DirectX::XMFLOAT2{ 0.0f, 0.0f });
        case PropertyType::Vector3:
            return PropertyValue::MakeVector3(DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f });
        case PropertyType::Vector4:
            return PropertyValue::MakeVector4(
                DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 0.0f });
        case PropertyType::Quaternion:
            return PropertyValue::MakeQuaternion(
                DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 1.0f });
        case PropertyType::Color:
            return PropertyValue::MakeColor(
                DirectX::XMFLOAT4{ 1.0f, 1.0f, 1.0f, 1.0f });
        case PropertyType::AssetReference:
            return PropertyValue::MakeAssetReference(std::string());
        default:
            return PropertyValue{};
        }
    }

    inline PropertyValue TerminalKeyValueForNewTrack(const PropertyDesc& desc,
        const PropertyValue& current)
    {
        switch (desc.type)
        {
        case PropertyType::Float:
            if (desc.has_range && desc.maximum > desc.minimum)
            {
                const double center = (desc.minimum + desc.maximum) * 0.5;
                return PropertyValue::MakeFloat(static_cast<float>(
                    current.AsFloat() < center ? desc.maximum : desc.minimum));
            }
            return PropertyValue::MakeFloat(current.AsFloat() + 1.0f);

        case PropertyType::Int:
            if (desc.has_range && desc.maximum > desc.minimum)
            {
                const double center = (desc.minimum + desc.maximum) * 0.5;
                return PropertyValue::MakeInt(current.AsInt() < center
                    ? static_cast<int>(desc.maximum)
                    : static_cast<int>(desc.minimum));
            }
            return PropertyValue::MakeInt(current.AsInt() + 1);

        case PropertyType::Bool:
            return PropertyValue::MakeBool(!current.AsBool());

        default:
            // Color / Vector / Enum / AssetReference などは、機械的に意味のある
            // 別値を決められない。Track追加だけで勝手な色や参照へ変える方が
            // 危険なので、開始キーと同じ値のままにする。
            return current;
        }
    }

    inline bool DrawValueEditor(const char* label, PropertyValue& value, PropertyType type)
    {
        bool changed = false;
        switch (type)
        {
        case PropertyType::Bool:
        {
            bool v = value.AsBool();
            if (ImGui::Checkbox(label, &v))
            {
                value = PropertyValue::MakeBool(v);
                changed = true;
            }
            break;
        }
        case PropertyType::Int:
        case PropertyType::Enum:
        {
            int v = value.AsInt();
            if (ImGui::DragInt(label, &v, 1.0f))
            {
                value = type == PropertyType::Enum
                    ? PropertyValue::MakeEnum(v) : PropertyValue::MakeInt(v);
                changed = true;
            }
            break;
        }
        case PropertyType::Float:
        {
            float v = value.AsFloat();
            if (ImGui::DragFloat(label, &v, 0.01f))
            {
                value = PropertyValue::MakeFloat(v);
                changed = true;
            }
            break;
        }
        case PropertyType::Double:
        {
            float v = static_cast<float>(value.AsDouble());
            if (ImGui::DragFloat(label, &v, 0.01f))
            {
                value = PropertyValue::MakeDouble(static_cast<double>(v));
                changed = true;
            }
            break;
        }
        case PropertyType::Vector2:
        {
            DirectX::XMFLOAT2 v = value.AsVector2();
            float raw[2]{ v.x, v.y };
            if (ImGui::DragFloat2(label, raw, 0.01f))
            {
                value = PropertyValue::MakeVector2({ raw[0], raw[1] });
                changed = true;
            }
            break;
        }
        case PropertyType::Vector3:
        {
            DirectX::XMFLOAT3 v = value.AsVector3();
            float raw[3]{ v.x, v.y, v.z };
            if (ImGui::DragFloat3(label, raw, 0.01f))
            {
                value = PropertyValue::MakeVector3({ raw[0], raw[1], raw[2] });
                changed = true;
            }
            break;
        }
        case PropertyType::Vector4:
        case PropertyType::Quaternion:
        {
            DirectX::XMFLOAT4 v = value.AsVector4();
            float raw[4]{ v.x, v.y, v.z, v.w };
            if (ImGui::DragFloat4(label, raw, 0.01f))
            {
                value = type == PropertyType::Quaternion
                    ? PropertyValue::MakeQuaternion({ raw[0], raw[1], raw[2], raw[3] })
                    : PropertyValue::MakeVector4({ raw[0], raw[1], raw[2], raw[3] });
                changed = true;
            }
            break;
        }
        case PropertyType::Color:
        {
            DirectX::XMFLOAT4 v = value.AsVector4();
            float raw[4]{ v.x, v.y, v.z, v.w };
            if (ImGui::ColorEdit4(label, raw))
            {
                value = PropertyValue::MakeColor({ raw[0], raw[1], raw[2], raw[3] });
                changed = true;
            }
            break;
        }
        case PropertyType::String:
        case PropertyType::AssetReference:
        {
            char buffer[512]{};
            const std::string& source = value.AsString();
            strncpy_s(buffer, source.c_str(), _TRUNCATE);
            if (ImGui::InputText(label, buffer, IM_ARRAYSIZE(buffer)))
            {
                value = type == PropertyType::AssetReference
                    ? PropertyValue::MakeAssetReference(buffer)
                    : PropertyValue::MakeString(buffer);
                changed = true;
            }
            break;
        }
        default:
            ImGui::TextDisabled("%s: 未対応型", label);
            break;
        }
        return changed;
    }

    inline bool DrawEasingCombo(const char* label, MotionEasing& easing)
    {
        constexpr MotionEasing easings[] = {
            MotionEasing::Linear,
            MotionEasing::Step,
            MotionEasing::EaseInQuad,
            MotionEasing::EaseOutQuad,
            MotionEasing::EaseInOutQuad,
            MotionEasing::EaseInCubic,
            MotionEasing::EaseOutCubic,
            MotionEasing::EaseInOutCubic,
            MotionEasing::EaseInBack,
            MotionEasing::EaseOutBack,
            MotionEasing::EaseInOutBack,
            MotionEasing::EaseInElastic,
            MotionEasing::EaseOutElastic,
            MotionEasing::EaseInOutElastic,
            MotionEasing::CustomBezier,
        };

        bool changed = false;
        const char* preview = ReplayEngine::Motion::ToString(easing);
        if (ImGui::BeginCombo(label, preview))
        {
            for (MotionEasing candidate : easings)
            {
                const bool selected = candidate == easing;
                if (ImGui::Selectable(ReplayEngine::Motion::ToString(candidate),
                    selected))
                {
                    easing = candidate;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    inline const char* MotionBlendModeLabel(MotionBlendMode mode) noexcept
    {
        switch (mode)
        {
        case MotionBlendMode::Override: return "Override";
        case MotionBlendMode::Additive: return "Additive";
        case MotionBlendMode::Multiply: return "Multiply";
        case MotionBlendMode::Blend: return "Blend";
        }
        return "Override";
    }

    inline bool DrawBlendModeCombo(const char* label, MotionBlendMode& mode)
    {
        constexpr MotionBlendMode modes[] = {
            MotionBlendMode::Override,
            MotionBlendMode::Additive,
            MotionBlendMode::Multiply,
            MotionBlendMode::Blend,
        };

        bool changed = false;
        if (ImGui::BeginCombo(label, MotionBlendModeLabel(mode)))
        {
            for (MotionBlendMode candidate : modes)
            {
                const bool selected = candidate == mode;
                if (ImGui::Selectable(MotionBlendModeLabel(candidate), selected))
                {
                    mode = candidate;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    inline const char* MotionBindingOriginLabel(int origin) noexcept
    {
        switch (static_cast<MotionBindingOrigin>(origin))
        {
        case MotionBindingOrigin::Absolute: return "絶対";
        case MotionBindingOrigin::Self: return "自分";
        case MotionBindingOrigin::Parent: return "親";
        case MotionBindingOrigin::ChildPath: return "名前で子を探す";
        }
        return "絶対";
    }

    inline bool DrawMotionBindingOriginCombo(const char* label, int& origin)
    {
        constexpr int origins[] = {
            static_cast<int>(MotionBindingOrigin::Absolute),
            static_cast<int>(MotionBindingOrigin::Self),
            static_cast<int>(MotionBindingOrigin::Parent),
            static_cast<int>(MotionBindingOrigin::ChildPath),
        };

        bool changed = false;
        if (ImGui::BeginCombo(label, MotionBindingOriginLabel(origin)))
        {
            for (const int candidate : origins)
            {
                const bool selected = candidate == origin;
                if (ImGui::Selectable(MotionBindingOriginLabel(candidate), selected))
                {
                    origin = candidate;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    inline Component* ResolveBindingComponent(ReplayEngine::Scene::Scene& scene,
        const MotionBinding& binding)
    {
        return ReplayEngine::Motion::MotionBindingResolver::Resolve(scene, binding).component;
    }

    inline const PropertyDesc* FindPropertyForComponent(Component& component,
        const std::string& property)
    {
        if (const PropertyDesc* desc = PropertyRegistry::Find(component.TypeID(), property))
        {
            return desc;
        }
        if (const std::vector<PropertyDesc>* dynamic = component.DynamicProperties())
        {
            for (const PropertyDesc& desc : *dynamic)
            {
                if (desc.name == property) return &desc;
            }
        }
        return nullptr;
    }

    inline int ComponentTypeIndex(const ReplayEngine::Core::GameObject& object,
        Component* target)
    {
        if (target == nullptr) return 0;
        int index = 0;
        for (std::size_t i = 0; i < object.ComponentCount(); ++i)
        {
            Component* component = object.ComponentAt(i);
            if (component == nullptr || component->PendingDestroy()) continue;
            if (component->TypeID() != target->TypeID()) continue;
            if (component == target) return index;
            ++index;
        }
        return 0;
    }

    inline bool FindFirstAnimatable(ReplayEngine::Core::GameObject& object,
        Component*& component, const PropertyDesc*& desc)
    {
        for (std::size_t i = 0; i < object.ComponentCount(); ++i)
        {
            Component* candidate = object.ComponentAt(i);
            if (candidate == nullptr || candidate->PendingDestroy()) continue;

            const std::vector<PropertyDesc>& properties =
                PropertyRegistry::PropertiesOf(candidate->TypeID());
            for (const PropertyDesc& property : properties)
            {
                if (property.animatable == Animatable::None) continue;
                component = candidate;
                desc = &property;
                return true;
            }
            if (const std::vector<PropertyDesc>* dynamic =
                candidate->DynamicProperties())
            {
                for (const PropertyDesc& property : *dynamic)
                {
                    if (property.animatable == Animatable::None) continue;
                    component = candidate;
                    desc = &property;
                    return true;
                }
            }
        }
        return false;
    }

    inline bool FindUIImageOpacity(ReplayEngine::Core::GameObject& object,
        Component*& component, const PropertyDesc*& desc)
    {
        component = object.GetComponent<ReplayEngine::Components::UIImageComponent>();
        if (component == nullptr) return false;
        desc = PropertyRegistry::Find(component->TypeID(), "opacity");
        return desc != nullptr && desc->animatable != Animatable::None;
    }
}
