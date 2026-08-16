#include "ScreenEffectStackComponent.h"

#include "../../Reflection/Property/PropertyBag.h"
#include "../../Reflection/Property/PropertyValue.h"
#include "../../Rendering/Materials/MaterialSchema.h"
#include "../../Rendering/Effects/EffectChain.h"
#include "../../Rendering/Effects/EffectPresetAsset.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>
#include <utility>

namespace ReplayEngine::Components
{
    namespace
    {
        constexpr int max_effect_count = 16;
        constexpr int brush_pattern_count = 16;
        constexpr std::array<const char*, brush_pattern_count> brush_pattern_labels{
            "ドライ平刷毛（右細り）", "細い横筋", "熊手ブラシ（右細り）",
            "乾いた角平刷毛", "曲線スウィッシュ", "絵の具多めの平刷毛",
            "粒状スカンブル", "滑らかな楕円筆", "乾いた散点ブラシ",
            "細いドライドラッグ", "絵の具多めの熊手筆", "短い乾いた払い",
            "粗い散布ブラシ", "平行ストリーク", "太い扇形ブラシ",
            "広い単独テーパーブラシ"
        };

        bool TryBrushPatternWeightProperty(const std::string& property, int& index)
        {
            constexpr const char* prefix = "brush_pattern_weight_";
            const std::size_t prefix_length = std::char_traits<char>::length(prefix);
            if (property.compare(0, prefix_length, prefix) != 0) return false;
            const std::string suffix = property.substr(prefix_length);
            if (suffix.empty()) return false;
            int parsed = 0;
            for (const char character : suffix)
            {
                if (!std::isdigit(static_cast<unsigned char>(character))) return false;
                parsed = parsed * 10 + (character - '0');
                if (parsed >= brush_pattern_count) return false;
            }
            index = parsed;
            return true;
        }

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
            if (close == std::string::npos || close == prefix.size() ||
                close + 2 > name.size() ||
                name[close + 1] != '.')
            {
                return false;
            }

            int parsed = 0;
            for (std::size_t i = prefix.size(); i < close; ++i)
            {
                if (!std::isdigit(static_cast<unsigned char>(name[i]))) return false;
                const int digit = name[i] - '0';
                if (parsed > (max_effect_count - 1 - digit) / 10) return false;
                parsed = parsed * 10 + digit;
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
            if (property == "color_2") return Reflection::PropertyValue::MakeColor(effect.color_2);
            if (property == "color_3") return Reflection::PropertyValue::MakeColor(effect.color_3);
            if (property == "color_4") return Reflection::PropertyValue::MakeColor(effect.color_4);
            if (property == "color_stop_2") return Reflection::PropertyValue::MakeFloat(effect.color_stop_2);
            if (property == "color_stop_3") return Reflection::PropertyValue::MakeFloat(effect.color_stop_3);
            if (property == "color_stop_4") return Reflection::PropertyValue::MakeFloat(effect.color_stop_4);
            if (property == "mask") return Reflection::PropertyValue::MakeAssetReference(effect.mask);
            if (property == "custom_shader") return Reflection::PropertyValue::MakeAssetReference(effect.custom_shader);
            if (property == "brush_atlas_enabled")
                return Reflection::PropertyValue::MakeBool(effect.brush_atlas_enabled);
            if (property == "brush_instanced_renderer_enabled")
                return Reflection::PropertyValue::MakeBool(effect.brush_instanced_renderer_enabled);
            if (property == "brush_pattern_mode")
                return Reflection::PropertyValue::MakeEnum(effect.brush_pattern_mode);
            if (property == "brush_pattern_index")
                return Reflection::PropertyValue::MakeEnum(effect.brush_pattern_index);
            int brush_weight_index = 0;
            if (TryBrushPatternWeightProperty(property, brush_weight_index))
                return Reflection::PropertyValue::MakeFloat(
                    effect.brush_pattern_weights[static_cast<std::size_t>(brush_weight_index)]);
            if (property == "waveform") return Reflection::PropertyValue::MakeEnum(effect.waveform);
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
            else if (property == "color_2") effect.color_2 = value.AsVector4();
            else if (property == "color_3") effect.color_3 = value.AsVector4();
            else if (property == "color_4") effect.color_4 = value.AsVector4();
            else if (property == "color_stop_2") effect.color_stop_2 = value.AsFloat(effect.color_stop_2);
            else if (property == "color_stop_3") effect.color_stop_3 = value.AsFloat(effect.color_stop_3);
            else if (property == "color_stop_4") effect.color_stop_4 = value.AsFloat(effect.color_stop_4);
            else if (property == "mask") effect.mask = value.AsAssetReference().guid;
            else if (property == "custom_shader") effect.custom_shader = value.AsAssetReference().guid;
            else if (property == "brush_atlas_enabled")
                effect.brush_atlas_enabled = value.AsBool(effect.brush_atlas_enabled);
            else if (property == "brush_instanced_renderer_enabled")
                effect.brush_instanced_renderer_enabled =
                    value.AsBool(effect.brush_instanced_renderer_enabled);
            else if (property == "brush_pattern_mode")
                effect.brush_pattern_mode = value.AsInt(effect.brush_pattern_mode);
            else if (property == "brush_pattern_index")
                effect.brush_pattern_index = value.AsInt(effect.brush_pattern_index);
            else
            {
                int brush_weight_index = 0;
                if (TryBrushPatternWeightProperty(property, brush_weight_index))
                    effect.brush_pattern_weights[static_cast<std::size_t>(brush_weight_index)] =
                        value.AsFloat(effect.brush_pattern_weights[
                            static_cast<std::size_t>(brush_weight_index)]);
                else if (property == "waveform") effect.waveform = value.AsInt(effect.waveform);
                else if (property.rfind("custom.", 0) == 0)
                {
                    effect.custom_parameters.Set("prop." + property.substr(7), value);
                }
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
                if (component.TypeID() != ScreenEffectStackComponent::StaticTypeID())
                    return Reflection::PropertyValue{};
                const ScreenEffectStackComponent& stack =
                    static_cast<const ScreenEffectStackComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= stack.effects.size())
                    return Reflection::PropertyValue{};
                return ReadEffectValue(stack.effects[static_cast<std::size_t>(index)],
                    property);
            };
            desc.setter = [index, property](Core::Component& component,
                const Reflection::PropertyValue& value)
            {
                if (component.TypeID() != ScreenEffectStackComponent::StaticTypeID())
                    return;
                ScreenEffectStackComponent& stack =
                    static_cast<ScreenEffectStackComponent&>(component);
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

        void ResetDefaultsForKind(UI::UIEffect& effect, UI::UIEffectKind kind)
        {
            // seed は種類をまたいで利用者が管理する値なので、ここでは変更しない。
            switch (kind)
            {
            case UI::UIEffectKind::Blur:
                effect.radius = 8.0f;
                effect.intensity = 1.0f;
                break;
            case UI::UIEffectKind::Glow:
                effect.radius = 12.0f;
                effect.intensity = 1.5f;
                effect.threshold = 0.0f;
                effect.color = { 1.0f, 1.0f, 1.0f, 1.0f };
                break;
            case UI::UIEffectKind::ColorAdjust:
                effect.radius = 1.0f;
                effect.intensity = 1.0f;
                effect.amount = 0.0f;
                effect.angle = 0.0f;
                effect.color = { 1.0f, 1.0f, 1.0f, 1.0f };
                break;
            case UI::UIEffectKind::Noise:
                effect.radius = 32.0f;
                effect.angle = 0.0f;
                effect.intensity = 0.08f;
                effect.amount = 2.0f;
                effect.speed = 1.0f;
                effect.color = { 1.0f, 1.0f, 1.0f, 1.0f };
                break;
            case UI::UIEffectKind::Shake:
                effect.amount = 6.0f;
                effect.intensity = 1.0f;
                effect.speed = 8.0f;
                break;
            case UI::UIEffectKind::Mask:
                effect.amount = 1.0f;
                effect.angle = 0.0f;
                effect.softness = 0.02f;
                effect.direction = { 0.5f, 0.5f };
                effect.speed = 0.5f;
                break;
            case UI::UIEffectKind::Wipe:
                effect.progress = 0.5f;
                effect.angle = 0.0f;
                effect.softness = 0.05f;
                break;
            case UI::UIEffectKind::Dissolve:
                effect.radius = 64.0f;
                effect.angle = 0.0f;
                effect.progress = 0.35f;
                effect.threshold = 0.08f;
                effect.color = { 1.0f, 0.35f, 0.05f, 1.0f };
                break;
            case UI::UIEffectKind::Distortion:
                effect.threshold = 12.0f;
                effect.amount = 6.0f;
                effect.intensity = 1.0f;
                effect.speed = 1.0f;
                break;
            case UI::UIEffectKind::ChromaticAberration:
                effect.amount = 4.0f;
                effect.intensity = 1.0f;
                break;
            case UI::UIEffectKind::Kuwahara:
                effect.radius = 5.0f;
                effect.intensity = 1.0f;
                effect.softness = 0.6f;
                break;
            case UI::UIEffectKind::Halftone:
                effect.radius = 8.0f;
                effect.intensity = 1.0f;
                effect.angle = 15.0f;
                effect.softness = 0.15f;
                effect.color = { 1.0f, 1.0f, 1.0f, 1.0f };
                break;
            case UI::UIEffectKind::DirectionalBlur:
                effect.angle = 0.0f;
                effect.amount = 12.0f;
                effect.intensity = 1.0f;
                break;
            case UI::UIEffectKind::RadialBlur:
                effect.direction = { 0.5f, 0.5f };
                effect.amount = 16.0f;
                effect.intensity = 1.0f;
                break;
            case UI::UIEffectKind::RotationalBlur:
                effect.direction = { 0.5f, 0.5f };
                effect.angle = 12.0f;
                effect.intensity = 1.0f;
                break;
            case UI::UIEffectKind::Vignette:
                effect.radius = 0.35f;
                effect.softness = 0.35f;
                effect.intensity = 0.65f;
                effect.color = { 0.0f, 0.0f, 0.0f, 1.0f };
                break;
            case UI::UIEffectKind::LightStreaks:
                effect.amount = 4.0f;
                effect.radius = 24.0f;
                effect.angle = 0.0f;
                effect.threshold = 0.6f;
                effect.intensity = 1.2f;
                effect.color = { 1.0f, 1.0f, 1.0f, 1.0f };
                break;
            case UI::UIEffectKind::LensDistortion:
                effect.direction = { 0.5f, 0.5f };
                effect.amount = 0.2f;
                effect.intensity = 1.0f;
                break;
            case UI::UIEffectKind::Posterize:
                effect.amount = 6.0f;
                effect.intensity = 1.0f;
                break;
            case UI::UIEffectKind::Threshold:
                effect.threshold = 0.5f;
                effect.softness = 0.04f;
                effect.intensity = 1.0f;
                effect.color = { 1.0f, 1.0f, 1.0f, 1.0f };
                break;
            case UI::UIEffectKind::ColorRamp:
                effect.color = { 0.02f, 0.02f, 0.08f, 1.0f };
                effect.color_2 = { 0.12f, 0.18f, 0.55f, 1.0f };
                effect.color_3 = { 0.95f, 0.32f, 0.12f, 1.0f };
                effect.color_4 = { 1.0f, 0.95f, 0.55f, 1.0f };
                effect.color_stop_2 = 0.333333f;
                effect.color_stop_3 = 0.666667f;
                effect.color_stop_4 = 1.0f;
                effect.intensity = 1.0f;
                break;
            case UI::UIEffectKind::Levels:
                effect.threshold = 0.05f;
                effect.amount = 0.95f;
                effect.angle = 0.15f;
                effect.direction = { 0.0f, 1.0f };
                effect.intensity = 1.0f;
                break;
            case UI::UIEffectKind::Temperature:
                effect.angle = 0.35f;
                effect.progress = 0.05f;
                effect.intensity = 1.0f;
                break;
            case UI::UIEffectKind::EdgeDetect:
                effect.radius = 1.0f;
                effect.intensity = 2.0f;
                effect.color = { 1.0f, 1.0f, 1.0f, 1.0f };
                break;
            case UI::UIEffectKind::Outline:
                effect.radius = 3.0f;
                effect.intensity = 1.0f;
                effect.color = { 0.0f, 0.0f, 0.0f, 1.0f };
                break;
            case UI::UIEffectKind::LongShadow:
                effect.angle = 45.0f;
                effect.amount = 24.0f;
                effect.intensity = 1.0f;
                effect.color = { 0.0f, 0.0f, 0.0f, 0.75f };
                break;
            case UI::UIEffectKind::CrossHatch:
                effect.radius = 8.0f;
                effect.angle = 45.0f;
                effect.amount = 3.0f;
                effect.softness = 0.1f;
                effect.intensity = 0.85f;
                effect.color = { 0.05f, 0.05f, 0.05f, 1.0f };
                break;
            case UI::UIEffectKind::BrushStroke:
                effect.radius = 8.0f;
                effect.amount = 3.0f;
                effect.threshold = 0.1f;
                effect.intensity = 0.8f;
                effect.progress = 32.0f;
                effect.softness = 0.35f;
                effect.angle = 0.75f;
                effect.brush_atlas_enabled = false;
                effect.brush_instanced_renderer_enabled = false;
                effect.brush_pattern_mode = 0;
                effect.brush_pattern_index = 0;
                effect.brush_pattern_weights.fill(1.0f);
                break;
            case UI::UIEffectKind::Mosaic:
                effect.radius = 12.0f;
                effect.intensity = 1.0f;
                break;
            case UI::UIEffectKind::Crystallize:
                effect.radius = 24.0f;
                effect.threshold = 0.45f;
                effect.intensity = 1.0f;
                effect.progress = 0.25f;
                effect.angle = 35.0f;
                effect.softness = 0.12f;
                effect.color = { 1.0f, 1.0f, 1.0f, 1.0f };
                break;
            case UI::UIEffectKind::StainedGlass:
                effect.radius = 24.0f;
                effect.threshold = 0.12f;
                effect.softness = 0.2f;
                effect.intensity = 1.0f;
                effect.color = { 0.0f, 0.0f, 0.0f, 1.0f };
                break;
            case UI::UIEffectKind::Twirl:
                effect.direction = { 0.5f, 0.5f };
                effect.angle = 180.0f;
                effect.radius = 0.45f;
                effect.intensity = 1.0f;
                break;
            case UI::UIEffectKind::Spherize:
                effect.direction = { 0.5f, 0.5f };
                effect.angle = 0.55f;
                effect.radius = 0.45f;
                effect.intensity = 1.0f;
                break;
            case UI::UIEffectKind::Ripple:
                effect.direction = { 0.5f, 0.5f };
                effect.radius = 48.0f;
                effect.amount = 6.0f;
                effect.speed = 2.0f;
                effect.intensity = 1.0f;
                break;
            case UI::UIEffectKind::PolarCoordinates:
                effect.direction = { 0.5f, 0.5f };
                effect.progress = 0.65f;
                effect.angle = 0.0f;
                break;
            case UI::UIEffectKind::Scanlines:
                effect.radius = 4.0f;
                effect.intensity = 0.25f;
                effect.speed = 30.0f;
                effect.color = { 0.0f, 0.0f, 0.0f, 1.0f };
                break;
            case UI::UIEffectKind::CRT:
                effect.progress = 0.12f;
                effect.radius = 4.0f;
                effect.intensity = 0.2f;
                effect.threshold = 0.25f;
                effect.softness = 0.35f;
                break;
            case UI::UIEffectKind::Glitch:
                effect.radius = 12.0f;
                effect.amount = 8.0f;
                effect.threshold = 0.18f;
                effect.intensity = 2.0f;
                effect.speed = 8.0f;
                break;
            case UI::UIEffectKind::Dither:
                effect.amount = 6.0f;
                effect.radius = 4.0f;
                effect.intensity = 1.0f;
                break;
            case UI::UIEffectKind::VHS:
                effect.radius = 3.0f;
                effect.amount = 4.0f;
                effect.threshold = 1.0f;
                effect.softness = 0.04f;
                effect.speed = 1.5f;
                break;
            case UI::UIEffectKind::Letterbox:
                effect.radius = 2.35f;
                effect.softness = 0.01f;
                effect.intensity = 1.0f;
                effect.color = { 0.0f, 0.0f, 0.0f, 1.0f };
                break;
            case UI::UIEffectKind::Waveform:
                effect.amount = 9.0f;
                effect.radius = 420.0f;
                effect.speed = 2.0f;
                effect.softness = 0.0f;
                effect.progress = 0.25f;
                effect.intensity = 0.6f;
                effect.angle = 0.0f;
                effect.direction = { 0.0f, 1.0f };
                effect.threshold = 0.15f;
                effect.waveform = 0;
                break;
            default:
                break;
            }
        }
    }

    ScreenEffectStackComponent::ScreenEffectStackComponent()
    {
        ResizeEffects();
        RebuildDynamicProperties();
    }

    const std::vector<Reflection::PropertyDesc>*
        ScreenEffectStackComponent::DynamicProperties() const noexcept
    {
        return dynamic_properties_.empty() ? nullptr : &dynamic_properties_;
    }

    void ScreenEffectStackComponent::OnSerialize(Reflection::PropertyBag& output) const
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
            output.Set(EffectPropertyName(i, "color_2"),
                Reflection::PropertyValue::MakeColor(effect.color_2));
            output.Set(EffectPropertyName(i, "color_3"),
                Reflection::PropertyValue::MakeColor(effect.color_3));
            output.Set(EffectPropertyName(i, "color_4"),
                Reflection::PropertyValue::MakeColor(effect.color_4));
            output.Set(EffectPropertyName(i, "color_stop_2"),
                Reflection::PropertyValue::MakeFloat(effect.color_stop_2));
            output.Set(EffectPropertyName(i, "color_stop_3"),
                Reflection::PropertyValue::MakeFloat(effect.color_stop_3));
            output.Set(EffectPropertyName(i, "color_stop_4"),
                Reflection::PropertyValue::MakeFloat(effect.color_stop_4));
            output.Set(EffectPropertyName(i, "mask"),
                Reflection::PropertyValue::MakeAssetReference(effect.mask));
            output.Set(EffectPropertyName(i, "custom_shader"),
                Reflection::PropertyValue::MakeAssetReference(effect.custom_shader));
            output.Set(EffectPropertyName(i, "brush_atlas_enabled"),
                Reflection::PropertyValue::MakeBool(effect.brush_atlas_enabled));
            output.Set(EffectPropertyName(i, "brush_instanced_renderer_enabled"),
                Reflection::PropertyValue::MakeBool(effect.brush_instanced_renderer_enabled));
            output.Set(EffectPropertyName(i, "brush_pattern_mode"),
                Reflection::PropertyValue::MakeEnum(effect.brush_pattern_mode));
            output.Set(EffectPropertyName(i, "brush_pattern_index"),
                Reflection::PropertyValue::MakeEnum(effect.brush_pattern_index));
            for (int brush_index = 0; brush_index < brush_pattern_count; ++brush_index)
            {
                output.Set(EffectPropertyName(i,
                    ("brush_pattern_weight_" + std::to_string(brush_index)).c_str()),
                    Reflection::PropertyValue::MakeFloat(effect.brush_pattern_weights[
                        static_cast<std::size_t>(brush_index)]));
            }
            output.Set(EffectPropertyName(i, "waveform"),
                Reflection::PropertyValue::MakeEnum(effect.waveform));
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

    void ScreenEffectStackComponent::OnDeserialize(const Reflection::PropertyBag& input)
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

        // 旧 BrushStroke の angle は Shader 側で未使用だった。アトラスを有効にして
        // 保存された既存シーンだけは、その旧初期値を今回の輪郭初期値へ移行する。
        // 通常マスクと、ユーザーがすでに調整した値は一切変更しない。
        for (UI::UIEffect& effect : effects)
        {
            if (static_cast<UI::UIEffectKind>(effect.kind) == UI::UIEffectKind::BrushStroke &&
                effect.brush_atlas_enabled && effect.angle == 0.15f)
            {
                effect.angle = 0.75f;
            }
        }

        // type は上のループで復元される。項目一覧は kind に依存するため、
        // 復元し終えた値でもう一度組み直す。
        RebuildDynamicProperties();
    }

    void ScreenEffectStackComponent::OnPropertyChanged(const char* property_name)
    {
        if (property_name == nullptr)
        {
            ResizeEffects();
            RebuildDynamicProperties();
            return;
        }

        const std::string changed_property(property_name);
        if (changed_property == "effect_count")
        {
            ResizeEffects();
            RebuildDynamicProperties();
            return;
        }

        int effect_index = 0;
        std::string property;
        if (ParseEffectPropertyName(changed_property, effect_index, property) &&
            property == "type")
        {
            if (effect_index >= 0 &&
                static_cast<std::size_t>(effect_index) < effects.size())
            {
                UI::UIEffect& effect = effects[static_cast<std::size_t>(effect_index)];
                ResetDefaultsForKind(effect,
                    static_cast<UI::UIEffectKind>(effect.kind));
            }

            // 種類を変えた直後に、その Effect が実際に使う項目だけへ差し替える。
            // 保存名は effects[i].radius などのままなので、Scene と Motion Binding は変わらない。
            RebuildDynamicProperties();
        }
    }

    void ScreenEffectStackComponent::SetCustomShaderSchema(std::size_t effect_index,
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

    const std::vector<UI::UIEffect>& ScreenEffectStackComponent::EffectiveEffects(
        const Assets::AssetDatabase* database) const noexcept
    {
        if (use_preset)
        {
            const std::vector<UI::UIEffect>* preset =
                Rendering::Effects::EffectPresetAsset::Resolve(database, effect_preset);
            if (preset != nullptr) return *preset;
        }
        return effects;
    }

    bool ScreenEffectStackComponent::HasActiveEffects() const noexcept
    {
        if (!enabled) return false;
        for (const UI::UIEffect& effect : effects)
        {
            if (effect.enabled) return true;
        }
        return false;
    }

    bool ScreenEffectStackComponent::HasActiveEffects(const Assets::AssetDatabase* database) const noexcept
    {
        if (!enabled) return false;
        for (const UI::UIEffect& effect : EffectiveEffects(database))
        {
            if (effect.enabled) return true;
        }
        return false;
    }

    DirectX::XMFLOAT4 ScreenEffectStackComponent::ExpandBounds(float target_width,
        float target_height) const noexcept
    {
        if (!enabled) return { 0.0f, 0.0f, 0.0f, 0.0f };
        return Rendering::Effects::EffectChain::ExpandBounds(
            effects, target_width, target_height);
    }

    DirectX::XMFLOAT4 ScreenEffectStackComponent::ExpandBounds(float target_width, float target_height,
        const Assets::AssetDatabase* database) const noexcept
    {
        if (!enabled) return { 0.0f, 0.0f, 0.0f, 0.0f };
        return Rendering::Effects::EffectChain::ExpandBounds(
            EffectiveEffects(database), target_width, target_height);
    }

    void ScreenEffectStackComponent::ResizeEffects()
    {
        effect_count = (std::max)(0, (std::min)(max_effect_count, effect_count));
        effects.resize(static_cast<std::size_t>(effect_count));
        custom_schemas_.resize(static_cast<std::size_t>(effect_count));
    }

    void ScreenEffectStackComponent::RebuildDynamicProperties()
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
                .Tooltip("適用する Effect の種類。変更すると、その種類が使う項目だけを表示する。")
                .AsEnum({ "ぼかし", "発光", "色調補正", "ノイズ",
                    "揺れ", "マスク", "ワイプ", "ディゾルブ", "歪み",
                    "色収差", "クワハラ", "網点",
                    "方向ブラー", "放射ブラー", "回転ブラー",
                    "ビネット", "光条", "レンズ歪み",
                    "ポスタライズ", "二値化", "カラーランプ", "レベル補正",
                    "色温度", "エッジ検出", "輪郭線", "ロングシャドウ",
                    "クロスハッチング", "ブラシストローク", "モザイク", "結晶化",
                    "ステンドグラス", "渦巻き", "球面化", "波紋",
                    "極座標", "走査線", "CRT", "グリッチ",
                    "ディザ", "VHS", "レターボックス", "波形" }));

            const auto add_float = [&](const char* name, const char* display,
                const char* tooltip, double minimum, double maximum, double step)
            {
                push(MakeEffectProperty(i, name, Reflection::PropertyType::Float,
                    Reflection::Animatable::Interpolatable)
                    .Display(display).Tooltip(tooltip)
                    .Range(minimum, maximum).Step(step));
            };
            const auto add_named_color = [&](const char* name, const char* display,
                const char* tooltip)
            {
                push(MakeEffectProperty(i, name, Reflection::PropertyType::Color,
                    Reflection::Animatable::Interpolatable)
                    .Display(display).Tooltip(tooltip).AsColor());
            };
            const auto add_color = [&](const char* display, const char* tooltip)
            {
                add_named_color("color", display, tooltip);
            };
            const auto add_seed = [&]()
            {
                add_float("seed", "乱数 Seed",
                    "同じ値なら同じ揺れ・ノイズになる。", -65536.0, 65536.0, 1.0);
            };

            // ---- 拡張点: マスクテクスチャの直接編集 -------------------------
            //
            // 【今は入れていない理由】
            //   画像編集には筆・消しゴム・履歴・保存先の設計が必要であり、
            //   Effect パラメータの修正と混ぜると両方の責務が曖昧になる。
            // 【入れるときにここへ足す】
            //   Asset 参照欄の隣に Image 編集画面を開くボタンを置く。
            // 【壊してはいけない前提】
            //   Effect は Image の GUID を持つだけで、未指定なら手続き的な
            //   従来経路を通る。Asset の保存形式も変えない。

            // ---- 拡張点: UI Effect 種類別プロパティ -------------------------
            //
            // 【今は入れていない理由】
            //   種類ごとの既定値は 41 種すべての挙動に影響するため、ここでは作らない。
            // 【入れるときにここへ足す】
            //   既存 UIEffect のスロットへ意味を割り当て、必要な項目だけを表示する。
            // 【壊してはいけない前提】
            //   UIEffect の保存形式と UIRenderer の定数パッキングは共有なので変更しない。
            switch (static_cast<UI::UIEffectKind>(effects[index].kind))
            {
            case UI::UIEffectKind::Blur:
                add_float("radius", "ぼかし半径", "ぼかしを広げる距離（ピクセル）。",
                    0.0, 512.0, 0.1);
                add_float("intensity", "適用量", "元画像からぼかし画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::Glow:
                add_float("radius", "光の半径", "発光を広げる距離（ピクセル）。",
                    0.0, 512.0, 0.1);
                add_float("intensity", "光の強さ", "加算する発光の強さ。",
                    0.0, 32.0, 0.01);
                add_float("threshold", "発光しきい値",
                    "この明るさ以上の部分から光を作る。0 なら色を問わず光る。",
                    0.0, 1.0, 0.01);
                add_color("光の色", "発光へ掛ける色と不透明度。");
                break;
            case UI::UIEffectKind::ColorAdjust:
                add_float("radius", "彩度", "1 が元の彩度、0 でグレースケール。",
                    0.0, 8.0, 0.01);
                add_float("intensity", "コントラスト", "1 が元のコントラスト。",
                    0.0, 8.0, 0.01);
                add_float("amount", "明るさ", "RGB へ加える量。0 が中立。",
                    -1.0, 1.0, 0.01);
                add_float("angle", "色相", "色相を回す角度（度）。",
                    -360.0, 360.0, 0.1);
                add_color("色の乗算", "色調整の最後に RGB へ掛ける色。");
                break;
            case UI::UIEffectKind::Noise:
                push(MakeEffectProperty(i, "mask", Reflection::PropertyType::AssetReference,
                    Reflection::Animatable::Step).Display("ノイズのテクスチャ")
                    .Tooltip("設定した場合は手続き的な粒の代わりにこの模様を使う。")
                    .OfAssetType("Image"));
                add_float("radius", "テクスチャの大きさ",
                    "ノイズテクスチャの繰り返し単位（ピクセル）。", 1.0, 1024.0, 0.1);
                add_float("angle", "テクスチャの向き",
                    "ノイズテクスチャを回す角度（度）。", -360.0, 360.0, 0.1);
                add_float("intensity", "ノイズ量", "加える粒状ノイズの強さ。",
                    0.0, 2.0, 0.01);
                add_float("amount", "粒の大きさ", "同じノイズ値を共有するセルの大きさ。",
                    1.0, 256.0, 0.1);
                add_float("speed", "流れる速度", "時間に対するノイズの変化速度。",
                    -32.0, 32.0, 0.01);
                add_seed();
                add_color("ノイズの色", "ノイズへ掛ける RGB。");
                break;
            case UI::UIEffectKind::Shake:
                add_float("amount", "揺れ幅", "位置をずらす最大距離（ピクセル）。",
                    0.0, 512.0, 0.1);
                add_float("intensity", "強度", "揺れ幅へ掛ける倍率。",
                    0.0, 32.0, 0.01);
                add_float("speed", "揺れる速度", "時間に対する揺れの速さ。",
                    -32.0, 32.0, 0.01);
                add_seed();
                break;
            case UI::UIEffectKind::Mask:
                add_float("amount", "形状", "0 で矩形、1 で円。途中の値は連続補間する。",
                    0.0, 1.0, 0.01);
                add_float("angle", "回転", "マスク形状の回転角度（度）。",
                    -360.0, 360.0, 0.1);
                add_float("softness", "境界の柔らかさ", "形状または画像の境界をぼかす量。",
                    0.0, 1.0, 0.001);
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable)
                    .Display("中心").Tooltip("正規化座標で指定するマスク中心。0.5, 0.5 が中央。")
                    .Step(0.01));
                add_float("seed", "横半径", "矩形または円の横半径（正規化座標）。",
                    0.0001, 1.0, 0.001);
                add_float("speed", "縦半径", "矩形または円の縦半径（正規化座標）。",
                    0.0001, 1.0, 0.001);
                push(MakeEffectProperty(i, "mask", Reflection::PropertyType::AssetReference,
                    Reflection::Animatable::Step).Display("マスク画像")
                    .Tooltip("設定した場合は画像のアルファで切り抜く。")
                    .OfAssetType("Image"));
                break;
            case UI::UIEffectKind::Wipe:
                add_float("progress", "進行", "0 から 1 でワイプを進める。",
                    0.0, 1.0, 0.001);
                add_float("angle", "方向", "ワイプ境界の角度（度）。",
                    -360.0, 360.0, 0.1);
                add_float("softness", "境界の柔らかさ", "ワイプ境界をぼかす量。",
                    0.0001, 1.0, 0.001);
                break;
            case UI::UIEffectKind::Dissolve:
                push(MakeEffectProperty(i, "mask", Reflection::PropertyType::AssetReference,
                    Reflection::Animatable::Step).Display("溶けかたのテクスチャ")
                    .Tooltip("設定した場合は明るい所から先に消える。")
                    .OfAssetType("Image"));
                add_float("radius", "テクスチャの大きさ",
                    "溶けかたテクスチャの繰り返し単位（ピクセル）。", 1.0, 1024.0, 0.1);
                add_float("angle", "テクスチャの向き",
                    "溶けかたテクスチャを回す角度（度）。", -360.0, 360.0, 0.1);
                add_float("progress", "進行", "0 から 1 で消失を進める。",
                    0.0, 1.0, 0.001);
                add_float("threshold", "縁の幅", "消え際に着色する帯の幅。",
                    0.0001, 1.0, 0.001);
                add_seed();
                add_color("縁の色", "消え際の帯へ使う色。");
                break;
            case UI::UIEffectKind::Distortion:
                add_float("threshold", "波の周波数", "画面内に作る波の細かさ。",
                    0.001, 128.0, 0.01);
                add_float("amount", "歪み幅", "UV をずらす距離（ピクセル）。",
                    -512.0, 512.0, 0.1);
                add_float("intensity", "強度", "歪み幅へ掛ける倍率。",
                    0.0, 32.0, 0.01);
                add_float("speed", "流れる速度", "波が時間方向へ進む速さ。",
                    -32.0, 32.0, 0.01);
                add_seed();
                break;
            case UI::UIEffectKind::ChromaticAberration:
                add_float("amount", "色ずれ距離", "RGB チャンネルを離す距離（ピクセル）。",
                    0.0, 512.0, 0.1);
                add_float("intensity", "強度", "色ずれ距離へ掛ける倍率。",
                    0.0, 32.0, 0.01);
                break;
            case UI::UIEffectKind::Kuwahara:
                add_float("radius", "筆面の半径", "8 セクタを調べる広がり（ピクセル）。",
                    0.0, 128.0, 0.1);
                add_float("intensity", "適用量", "元画像から平坦化画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                add_float("softness", "面の硬さ",
                    "0 でぼかし寄り、1 で分散の小さい面を強く選ぶ。",
                    0.0, 1.0, 0.001);
                break;
            case UI::UIEffectKind::Halftone:
                add_float("radius", "網の間隔", "網点セルの間隔（ピクセル）。",
                    1.0, 256.0, 0.1);
                add_float("intensity", "適用量", "元画像から網点画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                add_float("angle", "網の角度", "RGB 各版へ加える全体回転（度）。",
                    -360.0, 360.0, 0.1);
                add_float("softness", "点の柔らかさ", "網点の縁をぼかす量。",
                    0.0, 1.0, 0.001);
                add_color("網点の色", "白なら元の色相を保つ。");
                break;
            case UI::UIEffectKind::DirectionalBlur:
                add_float("angle", "流す角度", "ぼかしを伸ばす方向（度）。",
                    -360.0, 360.0, 0.1);
                add_float("amount", "距離", "進行方向へ流す距離（ピクセル）。",
                    0.0, 512.0, 0.1);
                add_float("intensity", "適用量", "元画像から方向ブラーへ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::RadialBlur:
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("中心")
                    .Tooltip("放射の中心。0.5, 0.5 が中央。").Step(0.01));
                add_float("amount", "強さ", "中心から伸ばす最大距離（ピクセル）。",
                    -512.0, 512.0, 0.1);
                add_float("intensity", "適用量", "元画像から放射ブラーへ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::RotationalBlur:
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("中心")
                    .Tooltip("回転の中心。0.5, 0.5 が中央。").Step(0.01));
                add_float("angle", "回転量", "ブラーが覆う角度（度）。",
                    -180.0, 180.0, 0.1);
                add_float("intensity", "適用量", "元画像から回転ブラーへ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::Vignette:
                add_float("radius", "中心の半径", "効果が始まる中心領域の半径。",
                    0.0, 1.5, 0.001);
                add_float("softness", "境界の柔らかさ", "中心から周辺へ移る幅。",
                    0.0001, 1.0, 0.001);
                add_float("intensity", "強さ", "周辺へ重ねる色の量。",
                    0.0, 1.0, 0.01);
                add_color("周辺の色", "黒で周辺減光、明色で中心を強調できる。");
                break;
            case UI::UIEffectKind::LightStreaks:
                add_float("amount", "光条の本数", "中心から伸ばす方向の数。最大 8 本。",
                    1.0, 8.0, 1.0);
                add_float("radius", "光条の長さ", "明部を伸ばす距離（ピクセル）。",
                    0.0, 512.0, 0.1);
                add_float("angle", "基準角度", "最初の光条の角度（度）。",
                    -360.0, 360.0, 0.1);
                add_float("threshold", "明部しきい値", "光条の発生元にする明るさ。",
                    0.0, 1.0, 0.01);
                add_float("intensity", "光の強さ", "光条を加算する強さ。",
                    0.0, 16.0, 0.01);
                add_color("光条の色", "光条へ掛ける色。");
                break;
            case UI::UIEffectKind::LensDistortion:
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("中心")
                    .Tooltip("レンズ歪みの中心。0.5, 0.5 が中央。").Step(0.01));
                add_float("amount", "歪み量", "正で樽型、負で糸巻き型。",
                    -2.0, 2.0, 0.001);
                add_float("intensity", "適用量", "元画像から歪み画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::Posterize:
                add_float("amount", "階調数", "各 RGB チャンネルに残す段階数。",
                    2.0, 64.0, 1.0);
                add_float("intensity", "適用量", "元画像から減色画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::Threshold:
                add_float("threshold", "しきい値", "白へ切り替える明るさ。",
                    0.0, 1.0, 0.001);
                add_float("softness", "境界の柔らかさ", "二値境界を連続的にする幅。",
                    0.0, 1.0, 0.001);
                add_float("intensity", "適用量", "元画像から二値画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                add_color("白側の色", "しきい値以上の領域に使う色。");
                break;
            case UI::UIEffectKind::ColorRamp:
                add_color("色 1", "明度 0 に割り当てる色。");
                add_named_color("color_2", "色 2", "第 2 色。");
                add_named_color("color_3", "色 3", "第 3 色。");
                add_named_color("color_4", "色 4", "明度 1 側の第 4 色。");
                add_float("color_stop_2", "色 2 の位置", "色 2 を置く明度位置。",
                    0.0, 1.0, 0.001);
                add_float("color_stop_3", "色 3 の位置", "色 3 を置く明度位置。",
                    0.0, 1.0, 0.001);
                add_float("color_stop_4", "色 4 の位置", "色 4 を置く明度位置。",
                    0.0, 1.0, 0.001);
                add_float("intensity", "適用量", "元画像からカラーランプへ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::Levels:
                add_float("threshold", "入力の黒点", "これ以下の入力を黒へ寄せる。",
                    0.0, 1.0, 0.001);
                add_float("amount", "入力の白点", "これ以上の入力を白へ寄せる。",
                    0.0, 1.0, 0.001);
                add_float("angle", "ガンマ補正", "0 が中立。正で明るく、負で暗くする。",
                    -4.0, 4.0, 0.001);
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("出力の黒点 / 白点")
                    .Tooltip("補正後の出力範囲。既定は 0, 1。").Step(0.001));
                add_float("intensity", "適用量", "元画像からレベル補正へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::Temperature:
                add_float("angle", "色温度", "負で寒色、正で暖色へ寄せる。",
                    -1.0, 1.0, 0.001);
                add_float("progress", "ティント", "負で緑、正でマゼンタへ寄せる。",
                    -1.0, 1.0, 0.001);
                add_float("intensity", "適用量", "元画像から色温度補正へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::EdgeDetect:
                add_float("radius", "輪郭の太さ", "Sobel のサンプル間隔（ピクセル）。",
                    0.25, 16.0, 0.01);
                add_float("intensity", "輪郭の強さ", "検出した勾配へ掛ける強さ。",
                    0.0, 32.0, 0.01);
                add_color("輪郭の色", "検出した輪郭へ使う色。");
                break;
            case UI::UIEffectKind::Outline:
                add_float("radius", "線の太さ", "元画像の外へ広げる距離（ピクセル）。",
                    0.0, 64.0, 0.1);
                add_float("intensity", "線の濃さ", "輪郭線の不透明度へ掛ける量。",
                    0.0, 4.0, 0.01);
                add_color("線の色", "元画像へ重ねる輪郭線の色。");
                break;
            case UI::UIEffectKind::LongShadow:
                add_float("angle", "影の角度", "影を押し出す方向（度）。",
                    -360.0, 360.0, 0.1);
                add_float("amount", "影の長さ", "影を伸ばす距離（ピクセル）。",
                    0.0, 1024.0, 0.1);
                add_float("intensity", "影の濃さ", "影の不透明度へ掛ける量。",
                    0.0, 4.0, 0.01);
                add_color("影の色", "押し出した影へ使う色。");
                break;
            case UI::UIEffectKind::CrossHatch:
                add_float("radius", "線の間隔", "ハッチ線どうしの間隔（ピクセル）。",
                    2.0, 128.0, 0.1);
                add_float("angle", "線の角度", "基準となる斜線の角度（度）。",
                    -360.0, 360.0, 0.1);
                add_float("amount", "濃度の段数", "明度に応じて重ねる斜線方向の数。",
                    1.0, 4.0, 1.0);
                add_float("softness", "線の柔らかさ", "線の縁をぼかす量。",
                    0.0, 1.0, 0.001);
                add_float("intensity", "適用量", "元画像から線画へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                add_color("線の色", "ハッチ線へ使う色。");
                break;
            case UI::UIEffectKind::BrushStroke:
                push(MakeEffectProperty(i, "mask", Reflection::PropertyType::AssetReference,
                    Reflection::Animatable::Step).Display("筆跡のテクスチャ")
                    .Tooltip("通常の筆跡画像、または 4 x 4 の筆跡アトラスを指定する。")
                    .OfAssetType("Image"));
                push(MakeEffectProperty(i, "brush_atlas_enabled",
                    Reflection::PropertyType::Bool, Reflection::Animatable::Step)
                    .Display("筆跡アトラスを使う")
                    .Tooltip("有効ならテクスチャを 4 x 4 の16種類アトラスとして読む。"
                        "画像欄が空なら標準の brush_masks_atlas を使う。"
                        "無効なら従来どおり画像全体を1種類の筆跡として使う。"));
                push(MakeEffectProperty(i, "brush_instanced_renderer_enabled",
                    Reflection::PropertyType::Bool, Reflection::Animatable::Step)
                    .Display("独立ストローク描画（試験）")
                    .Tooltip("調整中のインスタンス筆描画を使う。無効なら安定済みの"
                        "アトラスフィルター経路を使う。"));
                push(MakeEffectProperty(i, "brush_pattern_mode",
                    Reflection::PropertyType::Enum, Reflection::Animatable::Step)
                    .Display("アトラスの選び方")
                    .Tooltip("単体は指定番号だけ、重み付きランダムはスタンプごとに比率で選ぶ。")
                    .AsEnum({ "単体", "重み付きランダム" }));
                push(MakeEffectProperty(i, "brush_pattern_index",
                    Reflection::PropertyType::Enum, Reflection::Animatable::Step)
                    .Display("単体の筆跡")
                    .Tooltip("単体モードで使うアトラス内の筆跡。重みがすべて0の時の予備選択でもある。")
                    .AsEnum({
                        "ドライ平刷毛（右細り）", "細い横筋", "熊手ブラシ（右細り）",
                        "乾いた角平刷毛", "曲線スウィッシュ", "絵の具多めの平刷毛",
                        "粒状スカンブル", "滑らかな楕円筆", "乾いた散点ブラシ",
                        "細いドライドラッグ", "絵の具多めの熊手筆", "短い乾いた払い",
                        "粗い散布ブラシ", "平行ストリーク", "太い扇形ブラシ",
                        "広い単独テーパーブラシ" }));
                for (int brush_index = 0; brush_index < brush_pattern_count; ++brush_index)
                {
                    push(MakeEffectProperty(i,
                        "brush_pattern_weight_" + std::to_string(brush_index),
                        Reflection::PropertyType::Float,
                        Reflection::Animatable::Interpolatable)
                        .Display(std::string("比率: ") + brush_pattern_labels[
                            static_cast<std::size_t>(brush_index)])
                        .Tooltip("重み付きランダム時の相対的な選ばれやすさ。"
                            "1 と 3 なら、おおむね 1:3 で選ばれる。")
                        .Range(0.0, 100.0).Step(0.1));
                }
                add_float("progress", "筆跡の大きさ",
                    "筆跡テクスチャを貼る単位（ピクセル）。", 1.0, 1024.0, 0.1);
                add_float("softness", "筆跡のばらつき",
                    "筆跡ごとの大きさと向きの散らばり。", 0.0, 1.0, 0.001);
                add_float("angle", "筆致の輪郭",
                    "0 で柔らかく、1 で色面と毛束の縁をくっきり出す。",
                    0.0, 1.0, 0.001);
                add_float("radius", "筆の長さ", "構造に沿って平均する長軸（ピクセル）。",
                    1.0, 128.0, 0.1);
                add_float("amount", "筆の幅", "構造をまたいで平均する短軸（ピクセル）。",
                    0.5, 64.0, 0.1);
                add_float("threshold", "向きの乱れ", "輪郭方向へ加える微小な乱れ。",
                    0.0, 1.0, 0.001);
                add_float("intensity", "適用量", "元画像から筆致画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                add_seed();
                break;
            case UI::UIEffectKind::Mosaic:
                add_float("radius", "セル幅", "正方セルの大きさ（ピクセル）。",
                    1.0, 512.0, 0.1);
                add_float("intensity", "適用量", "元画像からモザイクへ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::Crystallize:
                add_float("radius", "セル幅", "ボロノイセルの大きさ（ピクセル）。",
                    2.0, 512.0, 0.1);
                add_float("threshold", "セルの乱れ", "セル中心を格子からずらす割合。",
                    0.0, 1.0, 0.001);
                add_float("intensity", "適用量", "元画像から結晶化画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                add_float("progress", "面の陰影",
                    "セル中心からの向きで付ける明暗の量。", 0.0, 1.0, 0.001);
                add_float("angle", "光の向き", "面の陰影を付ける方向（度）。",
                    -360.0, 360.0, 0.1);
                add_float("softness", "縁の輝き", "セル境界を光らせる量。",
                    0.0, 1.0, 0.001);
                add_color("縁の色", "セル境界の輝きへ使う色。");
                add_seed();
                break;
            case UI::UIEffectKind::StainedGlass:
                add_float("radius", "セル幅", "ステンドグラス片の大きさ（ピクセル）。",
                    2.0, 512.0, 0.1);
                add_float("threshold", "縁の幅", "セル境界へ描く線の太さ。",
                    0.0, 0.5, 0.001);
                add_float("softness", "縁の柔らかさ", "境界線のアンチエイリアス幅。",
                    0.0, 1.0, 0.001);
                add_float("intensity", "適用量", "元画像からステンドグラスへ混ぜる割合。",
                    0.0, 1.0, 0.01);
                add_seed();
                add_color("縁の色", "セル境界へ使う色。");
                break;
            case UI::UIEffectKind::Twirl:
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("中心")
                    .Tooltip("渦の中心。0.5, 0.5 が中央。").Step(0.01));
                add_float("angle", "渦の強さ", "中心で加える回転角度（度）。",
                    -1440.0, 1440.0, 0.1);
                add_float("radius", "渦の半径", "効果が消える中心からの半径。",
                    0.001, 1.5, 0.001);
                add_float("intensity", "適用量", "元画像から渦画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::Spherize:
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("中心")
                    .Tooltip("球面効果の中心。0.5, 0.5 が中央。").Step(0.01));
                add_float("angle", "膨らみ量", "正で膨張、負で収縮。",
                    -1.0, 1.0, 0.001);
                add_float("radius", "球面の半径", "効果が消える中心からの半径。",
                    0.001, 1.5, 0.001);
                add_float("intensity", "適用量", "元画像から球面画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::Ripple:
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("中心")
                    .Tooltip("波紋の中心。0.5, 0.5 が中央。").Step(0.01));
                add_float("radius", "波長", "波 1 周期の長さ（ピクセル）。",
                    1.0, 512.0, 0.1);
                add_float("amount", "振幅", "UV をずらす最大距離（ピクセル）。",
                    -128.0, 128.0, 0.1);
                add_float("speed", "広がる速度", "時間に対して位相を進める速さ。",
                    -32.0, 32.0, 0.01);
                add_float("intensity", "適用量", "元画像から波紋画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                add_seed();
                break;
            case UI::UIEffectKind::PolarCoordinates:
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("中心")
                    .Tooltip("極座標の中心。0.5, 0.5 が中央。").Step(0.01));
                add_float("progress", "変換量", "0 で元画像、1 で直交座標から極座標へ変換。",
                    0.0, 1.0, 0.001);
                add_float("angle", "回転", "輪へ巻く開始角度（度）。",
                    -360.0, 360.0, 0.1);
                break;
            case UI::UIEffectKind::Scanlines:
                add_float("radius", "線の間隔", "走査線の周期（ピクセル）。",
                    1.0, 128.0, 0.1);
                add_float("intensity", "線の濃さ", "走査線へ重ねる色の量。",
                    0.0, 1.0, 0.01);
                add_float("speed", "流れる速度", "走査線が縦へ動く速度。",
                    -512.0, 512.0, 0.1);
                add_color("線の色", "走査線へ重ねる色。");
                break;
            case UI::UIEffectKind::CRT:
                add_float("progress", "画面の湾曲", "ブラウン管状の樽型歪み。",
                    0.0, 1.0, 0.001);
                add_float("radius", "走査線の間隔", "横走査線の周期（ピクセル）。",
                    1.0, 64.0, 0.1);
                add_float("intensity", "走査線の濃さ", "横線による減光量。",
                    0.0, 1.0, 0.01);
                add_float("threshold", "縁の減光", "画面周辺を暗くする量。",
                    0.0, 1.0, 0.01);
                add_float("softness", "縁の柔らかさ", "画面外周のぼかし幅。",
                    0.0001, 1.0, 0.001);
                break;
            case UI::UIEffectKind::Glitch:
                add_float("radius", "帯の高さ", "横ずれを共有する帯の高さ（ピクセル）。",
                    1.0, 256.0, 0.1);
                add_float("amount", "ずれ量", "帯を横へ動かす最大距離（ピクセル）。",
                    0.0, 512.0, 0.1);
                add_float("threshold", "発生頻度", "ずれる帯の割合。",
                    0.0, 1.0, 0.001);
                add_float("intensity", "チャンネルずれ", "RGB を離す距離（ピクセル）。",
                    0.0, 64.0, 0.1);
                add_float("speed", "変化速度", "帯パターンが切り替わる速さ。",
                    0.0, 64.0, 0.01);
                add_seed();
                break;
            case UI::UIEffectKind::Dither:
                add_float("amount", "階調数", "ディザ後に残す明度段階数。",
                    2.0, 64.0, 1.0);
                add_float("radius", "行列サイズ", "Bayer 行列の大きさ。2 / 4 / 8 を使う。",
                    2.0, 8.0, 2.0);
                add_float("intensity", "適用量", "元画像からディザ画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::VHS:
                add_float("radius", "横揺れ", "走査行を横へ揺らす距離（ピクセル）。",
                    0.0, 128.0, 0.1);
                add_float("amount", "横にじみ", "過去方向へ色を引く距離（ピクセル）。",
                    0.0, 128.0, 0.1);
                add_float("threshold", "色ずれ", "RGB チャンネルを離す距離（ピクセル）。",
                    0.0, 64.0, 0.1);
                add_float("softness", "ノイズ量", "加えるテープノイズの強さ。",
                    0.0, 1.0, 0.001);
                add_float("speed", "変化速度", "揺れとノイズが流れる速さ。",
                    0.0, 64.0, 0.01);
                add_seed();
                break;
            case UI::UIEffectKind::Letterbox:
                add_float("radius", "画面比", "残す表示領域の横 / 縦比。",
                    0.1, 8.0, 0.001);
                add_float("softness", "帯の柔らかさ", "黒帯の内側境界をぼかす幅。",
                    0.0, 0.25, 0.0001);
                add_float("intensity", "帯の濃さ", "帯色の不透明度へ掛ける量。",
                    0.0, 1.0, 0.01);
                add_color("帯の色", "上下または左右へ置く額縁色。");
                break;
            case UI::UIEffectKind::Waveform:
                push(MakeEffectProperty(i, "waveform", Reflection::PropertyType::Enum,
                    Reflection::Animatable::Step)
                    .Display("波形")
                    .Tooltip("波の基本形。鋭さとうねりで変化を加える。")
                    .AsEnum({ "正弦波", "三角波", "のこぎり波", "パルス", "ランダム" }));
                add_float("amount", "振幅",
                    "波の高さ（ピクセル）。推奨: ゆらゆら 6、心電図 10。",
                    0.0, 256.0, 0.1);
                add_float("radius", "波長",
                    "山から山までの距離（ピクセル）。推奨: ゆらゆら 120、心電図 90。",
                    1.0, 2048.0, 0.1);
                add_float("speed", "流れる速度",
                    "波が進む速さ。0 で静止。推奨: ゆらゆら 2、心電図 6。",
                    -32.0, 32.0, 0.01);
                add_float("softness", "波の鋭さ",
                    "値が大きいほど波形の山谷を鋭くする。",
                    0.0, 1.0, 0.001);
                add_float("progress", "うねりの量",
                    "低周波のうねりを重ねる割合。",
                    0.0, 1.0, 0.001);
                add_float("intensity", "太さ変調",
                    "波の山は元の太さのまま、谷で細くする度合い。",
                    0.0, 1.0, 0.001);
                add_float("angle", "波の向き",
                    "波が進む向き（度）。0 で横。",
                    -360.0, 360.0, 0.1);
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable)
                    .Display("適用範囲 (開始 / 終了)")
                    .Tooltip("波の向きに沿った適用範囲。既定の 1, -1 は全域。")
                    .Range(0.0, 1.0).Step(0.001));
                add_float("threshold", "境界のぼかし",
                    "適用範囲の端で振幅と太さ変調を 0 へ落とす幅。",
                    0.0, 0.5, 0.001);
                add_seed();
                break;
            default:
                break;
            }

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
