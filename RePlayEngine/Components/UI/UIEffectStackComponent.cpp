#include "UIEffectStackComponent.h"

#include "../../Reflection/Property/PropertyBag.h"
#include "../../Reflection/Property/PropertyValue.h"
#include "../../Rendering/Materials/MaterialSchema.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <utility>

namespace ReplayEngine::Components
{
    namespace
    {
        constexpr int max_effect_count = 16;

        std::string EffectPropertyName(int index, const char* name)
        {
            char buffer[64]{};
            std::snprintf(buffer, sizeof(buffer), "effects[%d].%s", index, name);
            return std::string(buffer);
        }

        bool ParseEffectPropertyName(const std::string& name, int& index,
            std::string& property)
        {
            const std::string prefix = "effects[";
            if (name.compare(0, prefix.size(), prefix) != 0) return false;
            const std::size_t close = name.find(']', prefix.size());
            if (close == std::string::npos || close + 2 > name.size() ||
                name[close + 1] != '.')
            {
                return false;
            }

            int parsed = 0;
            for (std::size_t i = prefix.size(); i < close; ++i)
            {
                if (!std::isdigit(static_cast<unsigned char>(name[i]))) return false;
                parsed = parsed * 10 + (name[i] - '0');
            }
            index = parsed;
            property = name.substr(close + 2);
            return !property.empty();
        }

        Reflection::PropertyValue ReadEffectValue(const UI::UIEffect& effect,
            const std::string& property)
        {
            if (property == "enabled") return Reflection::PropertyValue::MakeBool(effect.enabled);
            if (property == "type") return Reflection::PropertyValue::MakeEnum(effect.kind);
            if (property == "radius") return Reflection::PropertyValue::MakeFloat(effect.radius);
            if (property == "intensity") return Reflection::PropertyValue::MakeFloat(effect.intensity);
            if (property == "threshold") return Reflection::PropertyValue::MakeFloat(effect.threshold);
            if (property == "amount") return Reflection::PropertyValue::MakeFloat(effect.amount);
            if (property == "angle") return Reflection::PropertyValue::MakeFloat(effect.angle);
            if (property == "progress") return Reflection::PropertyValue::MakeFloat(effect.progress);
            if (property == "softness") return Reflection::PropertyValue::MakeFloat(effect.softness);
            if (property == "speed") return Reflection::PropertyValue::MakeFloat(effect.speed);
            if (property == "seed") return Reflection::PropertyValue::MakeFloat(effect.seed);
            if (property == "direction") return Reflection::PropertyValue::MakeVector2(effect.direction);
            if (property == "color") return Reflection::PropertyValue::MakeColor(effect.color);
            if (property == "mask") return Reflection::PropertyValue::MakeAssetReference(effect.mask);
            if (property == "custom_shader") return Reflection::PropertyValue::MakeAssetReference(effect.custom_shader);
            if (property.rfind("custom.", 0) == 0)
            {
                const std::string saved_name = "prop." + property.substr(7);
                const Reflection::PropertyValue* value = effect.custom_parameters.Find(saved_name);
                return value != nullptr ? *value : Reflection::PropertyValue{};
            }
            return Reflection::PropertyValue{};
        }

        void WriteEffectValue(UI::UIEffect& effect, const std::string& property,
            const Reflection::PropertyValue& value)
        {
            if (property == "enabled") effect.enabled = value.AsBool(effect.enabled);
            else if (property == "type") effect.kind = value.AsInt(effect.kind);
            else if (property == "radius") effect.radius = value.AsFloat(effect.radius);
            else if (property == "intensity") effect.intensity = value.AsFloat(effect.intensity);
            else if (property == "threshold") effect.threshold = value.AsFloat(effect.threshold);
            else if (property == "amount") effect.amount = value.AsFloat(effect.amount);
            else if (property == "angle") effect.angle = value.AsFloat(effect.angle);
            else if (property == "progress") effect.progress = value.AsFloat(effect.progress);
            else if (property == "softness") effect.softness = value.AsFloat(effect.softness);
            else if (property == "speed") effect.speed = value.AsFloat(effect.speed);
            else if (property == "seed") effect.seed = value.AsFloat(effect.seed);
            else if (property == "direction") effect.direction = value.AsVector2();
            else if (property == "color") effect.color = value.AsVector4();
            else if (property == "mask") effect.mask = value.AsAssetReference().guid;
            else if (property == "custom_shader") effect.custom_shader = value.AsAssetReference().guid;
            else if (property.rfind("custom.", 0) == 0)
            {
                effect.custom_parameters.Set("prop." + property.substr(7), value);
            }
        }

        Reflection::PropertyDesc MakeEffectProperty(int index, std::string property,
            Reflection::PropertyType type, Reflection::Animatable animatable)
        {
            Reflection::PropertyDesc desc;
            desc.name = EffectPropertyName(index, property.c_str());
            desc.type = type;
            desc.animatable = animatable;
            desc.serializable = true;
            desc.getter = [index, property](const Core::Component& component)
            {
                if (component.TypeID() != UIEffectStackComponent::StaticTypeID())
                    return Reflection::PropertyValue{};
                const UIEffectStackComponent& stack =
                    static_cast<const UIEffectStackComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= stack.effects.size())
                    return Reflection::PropertyValue{};
                return ReadEffectValue(stack.effects[static_cast<std::size_t>(index)],
                    property);
            };
            desc.setter = [index, property](Core::Component& component,
                const Reflection::PropertyValue& value)
            {
                if (component.TypeID() != UIEffectStackComponent::StaticTypeID())
                    return;
                UIEffectStackComponent& stack =
                    static_cast<UIEffectStackComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= stack.effects.size())
                    return;
                WriteEffectValue(stack.effects[static_cast<std::size_t>(index)],
                    property, value);
            };
            return desc;
        }

        Reflection::PropertyDesc MakeCustomEffectProperty(int index,
            const Rendering::ShaderProperty& property)
        {
            Reflection::PropertyDesc desc = MakeEffectProperty(index,
                "custom." + property.name,
                Rendering::MaterialSchema::PropertyTypeFor(property.kind),
                property.kind == Rendering::ShaderPropertyKind::Texture
                    ? Reflection::Animatable::None
                    : (property.kind == Rendering::ShaderPropertyKind::Toggle ||
                        property.kind == Rendering::ShaderPropertyKind::Enum
                        ? Reflection::Animatable::Step
                        : Reflection::Animatable::Interpolatable));
            desc.display_name = property.DisplayName();
            desc.category = property.category.empty() ? "Custom Shader" : property.category;
            desc.tooltip = property.tooltip;
            if (property.kind == Rendering::ShaderPropertyKind::Range)
            {
                desc.has_range = true;
                desc.minimum = property.minimum;
                desc.maximum = property.maximum;
            }
            if (property.kind == Rendering::ShaderPropertyKind::Enum)
                desc.enum_labels = property.enum_names;
            if (property.kind == Rendering::ShaderPropertyKind::Texture)
                desc.asset_type = "Image";
            return desc;
        }
    }

    UIEffectStackComponent::UIEffectStackComponent()
    {
        ResizeEffects();
        RebuildDynamicProperties();
    }

    const std::vector<Reflection::PropertyDesc>*
        UIEffectStackComponent::DynamicProperties() const noexcept
    {
        return dynamic_properties_.empty() ? nullptr : &dynamic_properties_;
    }

    void UIEffectStackComponent::OnSerialize(Reflection::PropertyBag& output) const
    {
        output.Set("effect_count",
            Reflection::PropertyValue::MakeInt(static_cast<int>(effects.size())));
        for (std::size_t index = 0; index < effects.size(); ++index)
        {
            const int i = static_cast<int>(index);
            const UI::UIEffect& effect = effects[index];
            output.Set(EffectPropertyName(i, "enabled"),
                Reflection::PropertyValue::MakeBool(effect.enabled));
            output.Set(EffectPropertyName(i, "type"),
                Reflection::PropertyValue::MakeEnum(effect.kind));
            output.Set(EffectPropertyName(i, "radius"),
                Reflection::PropertyValue::MakeFloat(effect.radius));
            output.Set(EffectPropertyName(i, "intensity"),
                Reflection::PropertyValue::MakeFloat(effect.intensity));
            output.Set(EffectPropertyName(i, "threshold"),
                Reflection::PropertyValue::MakeFloat(effect.threshold));
            output.Set(EffectPropertyName(i, "amount"),
                Reflection::PropertyValue::MakeFloat(effect.amount));
            output.Set(EffectPropertyName(i, "angle"),
                Reflection::PropertyValue::MakeFloat(effect.angle));
            output.Set(EffectPropertyName(i, "progress"),
                Reflection::PropertyValue::MakeFloat(effect.progress));
            output.Set(EffectPropertyName(i, "softness"),
                Reflection::PropertyValue::MakeFloat(effect.softness));
            output.Set(EffectPropertyName(i, "speed"),
                Reflection::PropertyValue::MakeFloat(effect.speed));
            output.Set(EffectPropertyName(i, "seed"),
                Reflection::PropertyValue::MakeFloat(effect.seed));
            output.Set(EffectPropertyName(i, "direction"),
                Reflection::PropertyValue::MakeVector2(effect.direction));
            output.Set(EffectPropertyName(i, "color"),
                Reflection::PropertyValue::MakeColor(effect.color));
            output.Set(EffectPropertyName(i, "mask"),
                Reflection::PropertyValue::MakeAssetReference(effect.mask));
            output.Set(EffectPropertyName(i, "custom_shader"),
                Reflection::PropertyValue::MakeAssetReference(effect.custom_shader));
            for (const Reflection::PropertyBag::Entry& parameter :
                effect.custom_parameters.Entries())
            {
                const std::string name = parameter.name.rfind("prop.", 0) == 0
                    ? parameter.name.substr(5) : parameter.name;
                output.Set(EffectPropertyName(i, ("custom." + name).c_str()),
                    parameter.value);
            }
        }
    }

    void UIEffectStackComponent::OnDeserialize(const Reflection::PropertyBag& input)
    {
        int inferred_count = effect_count;
        if (const Reflection::PropertyValue* stored_count = input.Find("effect_count"))
        {
            inferred_count = stored_count->AsInt(inferred_count);
        }
        for (const Reflection::PropertyBag::Entry& entry : input.Entries())
        {
            int index = 0;
            std::string property;
            if (ParseEffectPropertyName(entry.name, index, property))
            {
                inferred_count = (std::max)(inferred_count, index + 1);
            }
        }

        effect_count = inferred_count;
        ResizeEffects();
        RebuildDynamicProperties();

        for (const Reflection::PropertyBag::Entry& entry : input.Entries())
        {
            int index = 0;
            std::string property;
            if (!ParseEffectPropertyName(entry.name, index, property)) continue;
            if (index < 0 || static_cast<std::size_t>(index) >= effects.size()) continue;
            WriteEffectValue(effects[static_cast<std::size_t>(index)],
                property, entry.value);
        }
    }

    void UIEffectStackComponent::OnPropertyChanged(const char* property_name)
    {
        if (property_name == nullptr || std::string(property_name) == "effect_count")
        {
            ResizeEffects();
            RebuildDynamicProperties();
        }
    }

    void UIEffectStackComponent::SetCustomShaderSchema(std::size_t effect_index,
        Rendering::ShaderPropertySchemaRef schema)
    {
        if (effect_index >= effects.size()) return;
        if (custom_schemas_.size() != effects.size()) custom_schemas_.resize(effects.size());
        if (custom_schemas_[effect_index] == schema) return;

        custom_schemas_[effect_index] = std::move(schema);
        if (custom_schemas_[effect_index])
        {
            Rendering::MaterialSchema::EnsurePropertyBag(
                effects[effect_index].custom_parameters, *custom_schemas_[effect_index]);
        }
        RebuildDynamicProperties();
    }

    bool UIEffectStackComponent::HasActiveEffects() const noexcept
    {
        if (!enabled) return false;
        for (const UI::UIEffect& effect : effects)
        {
            if (effect.enabled) return true;
        }
        return false;
    }

    DirectX::XMFLOAT4 UIEffectStackComponent::ExpandBounds() const noexcept
    {
        DirectX::XMFLOAT4 expansion{ 0.0f, 0.0f, 0.0f, 0.0f };
        if (!enabled) return expansion;

        for (const UI::UIEffect& effect : effects)
        {
            if (!effect.enabled) continue;
            const DirectX::XMFLOAT4 current = effect.ExpandBounds();
            expansion.x += current.x;
            expansion.y += current.y;
            expansion.z += current.z;
            expansion.w += current.w;
        }
        return expansion;
    }

    void UIEffectStackComponent::ResizeEffects()
    {
        effect_count = (std::max)(0, (std::min)(max_effect_count, effect_count));
        effects.resize(static_cast<std::size_t>(effect_count));
        custom_schemas_.resize(static_cast<std::size_t>(effect_count));
    }

    void UIEffectStackComponent::RebuildDynamicProperties()
    {
        dynamic_properties_.clear();
        dynamic_properties_.reserve(effects.size() * 24);
        for (std::size_t index = 0; index < effects.size(); ++index)
        {
            const int i = static_cast<int>(index);
            auto push = [&](Reflection::PropertyDesc desc)
            {
                dynamic_properties_.push_back(std::move(desc));
            };

            push(MakeEffectProperty(i, "enabled", Reflection::PropertyType::Bool,
                Reflection::Animatable::Step).Display("有効"));
            push(MakeEffectProperty(i, "type", Reflection::PropertyType::Enum,
                Reflection::Animatable::Step)
                .Display("種類")
                .AsEnum({ "Blur", "Glow", "Color Adjust", "Noise",
                    "Shake", "Mask", "Wipe", "Dissolve", "Distortion",
                    "Chromatic Aberration" }));
            push(MakeEffectProperty(i, "radius", Reflection::PropertyType::Float,
                Reflection::Animatable::Interpolatable).Display("半径").Range(0.0, 512.0).Step(0.1));
            push(MakeEffectProperty(i, "intensity", Reflection::PropertyType::Float,
                Reflection::Animatable::Interpolatable).Display("強度").Range(0.0, 32.0).Step(0.01));
            push(MakeEffectProperty(i, "threshold", Reflection::PropertyType::Float,
                Reflection::Animatable::Interpolatable).Display("しきい値").Range(0.0, 1.0).Step(0.01));
            push(MakeEffectProperty(i, "amount", Reflection::PropertyType::Float,
                Reflection::Animatable::Interpolatable).Display("量").Range(-512.0, 512.0).Step(0.1));
            push(MakeEffectProperty(i, "angle", Reflection::PropertyType::Float,
                Reflection::Animatable::Interpolatable).Display("角度").Range(-360.0, 360.0).Step(0.1));
            push(MakeEffectProperty(i, "progress", Reflection::PropertyType::Float,
                Reflection::Animatable::Interpolatable).Display("進行").Range(0.0, 1.0).Step(0.001));
            push(MakeEffectProperty(i, "softness", Reflection::PropertyType::Float,
                Reflection::Animatable::Interpolatable).Display("柔らかさ").Range(0.0, 1.0).Step(0.001));
            push(MakeEffectProperty(i, "speed", Reflection::PropertyType::Float,
                Reflection::Animatable::Interpolatable).Display("速度").Step(0.01));
            push(MakeEffectProperty(i, "seed", Reflection::PropertyType::Float,
                Reflection::Animatable::Interpolatable).Display("Seed").Step(1.0));
            push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                Reflection::Animatable::Interpolatable).Display("方向").Step(0.01));
            push(MakeEffectProperty(i, "color", Reflection::PropertyType::Color,
                Reflection::Animatable::Interpolatable).Display("色").AsColor());
            push(MakeEffectProperty(i, "mask", Reflection::PropertyType::AssetReference,
                Reflection::Animatable::Step).Display("マスク").OfAssetType("Image"));
            push(MakeEffectProperty(i, "custom_shader",
                Reflection::PropertyType::AssetReference,
                Reflection::Animatable::Step)
                .Display("カスタムシェーダー")
                .Tooltip("Shader Composer の PostProcess 出力を UI Effect として使う。")
                .OfAssetType("Shader"));

            if (index < custom_schemas_.size() && custom_schemas_[index])
            {
                for (const Rendering::ShaderProperty& property :
                    custom_schemas_[index]->Properties())
                {
                    push(MakeCustomEffectProperty(i, property));
                }
            }
        }
    }
}
