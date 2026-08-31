// Effect Stack 共通実装のうち、Property 値変換・Effect 初期値・直列化前半を保持する。
// 分割内部専用。EffectStackComponentCommon.inl からだけ include する。


#include "../../Reflection/Property/PropertyBag.h"
#include "../../Reflection/Property/PropertyValue.h"
#include "../../Rendering/Materials/MaterialSchema.h"
#include "../../UI/Effects/UIEffect.h"
#include "../../UI/Effects/EffectKindLabels.h"
#include "../../Rendering/Effects/EffectPresetAsset.h"
#include "../../Rendering/Effects/EffectChain.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

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
            if (property == "region_enabled")
                return Reflection::PropertyValue::MakeBool(effect.region_enabled);
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
            else if (property == "region_enabled")
                effect.region_enabled = value.AsBool(effect.region_enabled);
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
                if (component.TypeID() != REPLAY_EFFECT_STACK_COMPONENT_TYPE::StaticTypeID())
                    return Reflection::PropertyValue{};
                const REPLAY_EFFECT_STACK_COMPONENT_TYPE& stack =
                    static_cast<const REPLAY_EFFECT_STACK_COMPONENT_TYPE&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= stack.effects.size())
                    return Reflection::PropertyValue{};
                return ReadEffectValue(stack.effects[static_cast<std::size_t>(index)],
                    property);
            };
            desc.setter = [index, property](Core::Component& component,
                const Reflection::PropertyValue& value)
            {
                if (component.TypeID() != REPLAY_EFFECT_STACK_COMPONENT_TYPE::StaticTypeID())
                    return;
                REPLAY_EFFECT_STACK_COMPONENT_TYPE& stack =
                    static_cast<REPLAY_EFFECT_STACK_COMPONENT_TYPE&>(component);
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
            desc.display_name = property.DisplayName() + std::string(u8" [未対応]");
            desc.category = property.category.empty() ? "Custom Shader" : property.category;
            desc.tooltip = property.tooltip.empty()
                ? std::string(u8"DX12 の UI Effect 描画では未対応です。設定値は保存されます。")
                : property.tooltip + std::string(u8" DX12 の UI Effect 描画では未対応です。設定値は保存されます。");
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
