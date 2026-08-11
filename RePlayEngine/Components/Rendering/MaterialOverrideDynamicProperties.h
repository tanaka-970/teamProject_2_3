#pragma once

#include "../../Reflection/Property/PropertyBag.h"
#include "../../Reflection/Property/PropertyDesc.h"
#include "../../Reflection/Property/PropertyValue.h"
#include "../../Rendering/Materials/MaterialAsset.h"
#include "../../Rendering/Materials/MaterialSchema.h"
#include "../../Rendering/Shaders/ShaderAsset.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace ReplayEngine::Components
{
    // Material Asset 自体を壊さず、Motion がこのフレームだけ上書きする値を持つ。
    // Scene/Prefab へは保存しない。Motion 停止時は次フレームの Prepare で完全に消える。
    struct MaterialMotionOverrideState final
    {
        Reflection::PropertyBag values;
        Reflection::PropertyBag active_values;
        std::vector<Reflection::PropertyDesc> schema_properties;
        std::string schema_shader_guid;
        std::uint32_t schema_revision = 0;
        std::uint32_t fixed_active_mask = 0;

        // 固定 PBR 7 項目は既存 Scene/Prefab に保存される Renderer の値を正本にする。
        // Motion setter が一度 Component を書き換えても、OnMotionPropertyApplied で
        // driven_* へ退避して base_* へ即復元するため、再生停止後に値を残さない。
        DirectX::XMFLOAT4 base_base_color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float base_metallic = 0.0f;
        float base_roughness = 0.55f;
        float base_ambient_occlusion = 1.0f;
        DirectX::XMFLOAT3 base_emissive_color{ 0.0f, 0.0f, 0.0f };
        float base_emissive_strength = 0.0f;
        bool base_double_sided = false;

        DirectX::XMFLOAT4 driven_base_color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float driven_metallic = 0.0f;
        float driven_roughness = 0.55f;
        float driven_ambient_occlusion = 1.0f;
        DirectX::XMFLOAT3 driven_emissive_color{ 0.0f, 0.0f, 0.0f };
        float driven_emissive_strength = 0.0f;
        bool driven_double_sided = false;

        void BeginFrame() noexcept
        {
            fixed_active_mask = 0;
            active_values.Clear();
        }
    };

    enum MaterialMotionFixedMask : std::uint32_t
    {
        MaterialMotionBaseColor = 1u << 0,
        MaterialMotionMetallic = 1u << 1,
        MaterialMotionRoughness = 1u << 2,
        MaterialMotionAmbientOcclusion = 1u << 3,
        MaterialMotionEmissiveColor = 1u << 4,
        MaterialMotionEmissiveStrength = 1u << 5,
        MaterialMotionDoubleSided = 1u << 6,
    };

    inline std::uint32_t MaterialMotionMaskForProperty(const std::string& name) noexcept
    {
        if (name == "material.base_color") return MaterialMotionBaseColor;
        if (name == "material.metallic") return MaterialMotionMetallic;
        if (name == "material.roughness") return MaterialMotionRoughness;
        if (name == "material.ambient_occlusion") return MaterialMotionAmbientOcclusion;
        if (name == "material.emissive_color") return MaterialMotionEmissiveColor;
        if (name == "material.emissive_strength") return MaterialMotionEmissiveStrength;
        if (name == "material.double_sided") return MaterialMotionDoubleSided;
        return 0;
    }

    template<class T>
    const std::vector<Reflection::PropertyDesc>*
        FixedMaterialOverrideDynamicProperties()
    {
        using Reflection::Animatable;
        using Reflection::PropertyDesc;
        using Reflection::PropertyType;
        using Reflection::PropertyValue;

        static const std::vector<PropertyDesc> properties = []
        {
            std::vector<PropertyDesc> result;
            result.reserve(7);

            auto push_float = [&](const char* name, float T::* member,
                const char* display, const char* tooltip,
                double minimum, double maximum)
            {
                PropertyDesc desc;
                desc.name = name;
                desc.display_name = display;
                desc.tooltip = tooltip;
                desc.type = PropertyType::Float;
                desc.animatable = Animatable::Interpolatable;
                desc.has_range = true;
                desc.minimum = minimum;
                desc.maximum = maximum;
                desc.getter = [member](const Core::Component& component)
                {
                    if (component.TypeID() != T::StaticTypeID()) return PropertyValue{};
                    return PropertyValue::MakeFloat(static_cast<const T&>(component).*member);
                };
                desc.setter = [member](Core::Component& component,
                    const PropertyValue& value)
                {
                    if (component.TypeID() != T::StaticTypeID()) return;
                    static_cast<T&>(component).*member =
                        value.AsFloat(static_cast<T&>(component).*member);
                };
                result.push_back(desc);
            };

            auto push_color = [&](const char* name,
                DirectX::XMFLOAT4 T::* member, const char* display,
                const char* tooltip)
            {
                PropertyDesc desc;
                desc.name = name;
                desc.display_name = display;
                desc.tooltip = tooltip;
                desc.type = PropertyType::Color;
                desc.animatable = Animatable::Interpolatable;
                desc.getter = [member](const Core::Component& component)
                {
                    if (component.TypeID() != T::StaticTypeID()) return PropertyValue{};
                    return PropertyValue::MakeColor(static_cast<const T&>(component).*member);
                };
                desc.setter = [member](Core::Component& component,
                    const PropertyValue& value)
                {
                    if (component.TypeID() != T::StaticTypeID()) return;
                    static_cast<T&>(component).*member = value.AsVector4();
                };
                result.push_back(desc);
            };

            auto push_vector3 = [&](const char* name,
                DirectX::XMFLOAT3 T::* member, const char* display,
                const char* tooltip)
            {
                PropertyDesc desc;
                desc.name = name;
                desc.display_name = display;
                desc.tooltip = tooltip;
                desc.type = PropertyType::Vector3;
                desc.animatable = Animatable::Interpolatable;
                desc.getter = [member](const Core::Component& component)
                {
                    if (component.TypeID() != T::StaticTypeID()) return PropertyValue{};
                    return PropertyValue::MakeVector3(static_cast<const T&>(component).*member);
                };
                desc.setter = [member](Core::Component& component,
                    const PropertyValue& value)
                {
                    if (component.TypeID() != T::StaticTypeID()) return;
                    static_cast<T&>(component).*member = value.AsVector3();
                };
                result.push_back(desc);
            };

            auto push_bool = [&](const char* name, bool T::* member,
                const char* display, const char* tooltip)
            {
                PropertyDesc desc;
                desc.name = name;
                desc.display_name = display;
                desc.tooltip = tooltip;
                desc.type = PropertyType::Bool;
                desc.animatable = Animatable::Step;
                desc.getter = [member](const Core::Component& component)
                {
                    if (component.TypeID() != T::StaticTypeID()) return PropertyValue{};
                    return PropertyValue::MakeBool(static_cast<const T&>(component).*member);
                };
                desc.setter = [member](Core::Component& component,
                    const PropertyValue& value)
                {
                    if (component.TypeID() != T::StaticTypeID()) return;
                    static_cast<T&>(component).*member =
                        value.AsBool(static_cast<T&>(component).*member);
                };
                result.push_back(desc);
            };

            push_color("material.base_color", &T::material_base_color,
                "マテリアル色", "Material Asset を置き換えるベース色。");
            push_float("material.metallic", &T::material_metallic,
                "金属度", "GBuffer へ渡す金属度。", 0.0, 1.0);
            push_float("material.roughness", &T::material_roughness,
                "粗さ", "GBuffer へ渡すラフネス。", 0.0, 1.0);
            push_float("material.ambient_occlusion",
                &T::material_ambient_occlusion,
                "環境遮蔽", "GBuffer へ渡す AO 係数。", 0.0, 1.0);
            push_vector3("material.emissive_color", &T::material_emissive_color,
                "発光色", "GBuffer へ渡す発光色。");
            push_float("material.emissive_strength",
                &T::material_emissive_strength,
                "発光強度", "GBuffer へ渡す発光強度。", 0.0, 16.0);
            push_bool("material.double_sided", &T::material_double_sided,
                "両面描画", "Material override 時だけ両面描画を有効にする。");

            return result;
        }();

        return &properties;
    }

    inline Reflection::Animatable MaterialAnimatableFor(
        Rendering::ShaderPropertyKind kind) noexcept
    {
        switch (kind)
        {
        case Rendering::ShaderPropertyKind::Texture:
            return Reflection::Animatable::None;
        case Rendering::ShaderPropertyKind::Toggle:
        case Rendering::ShaderPropertyKind::Enum:
            return Reflection::Animatable::Step;
        default:
            return Reflection::Animatable::Interpolatable;
        }
    }

    template<class T>
    void PrepareMaterialMotionProperties(T& component,
        const Rendering::MaterialAsset* material,
        const Rendering::ShaderPropertySchema* schema)
    {
        MaterialMotionOverrideState& state = component.material_motion_state;
        state.BeginFrame();
        state.base_base_color = component.material_base_color;
        state.base_metallic = component.material_metallic;
        state.base_roughness = component.material_roughness;
        state.base_ambient_occlusion = component.material_ambient_occlusion;
        state.base_emissive_color = component.material_emissive_color;
        state.base_emissive_strength = component.material_emissive_strength;
        state.base_double_sided = component.material_double_sided;
        state.driven_base_color = state.base_base_color;
        state.driven_metallic = state.base_metallic;
        state.driven_roughness = state.base_roughness;
        state.driven_ambient_occlusion = state.base_ambient_occlusion;
        state.driven_emissive_color = state.base_emissive_color;
        state.driven_emissive_strength = state.base_emissive_strength;
        state.driven_double_sided = state.base_double_sided;
        if (material == nullptr || schema == nullptr)
        {
            state.schema_properties.clear();
            component.material_dynamic_properties_cache.clear();
            state.schema_shader_guid.clear();
            state.schema_revision = 0;
            state.values.Clear();
            return;
        }

        const bool schema_changed = state.schema_shader_guid != material->shader_guid ||
            state.schema_revision != schema->Revision();
        if (schema_changed)
        {
            state.schema_shader_guid = material->shader_guid;
            state.schema_revision = schema->Revision();
            state.schema_properties.clear();
            component.material_dynamic_properties_cache.clear();
            state.schema_properties.reserve(schema->Properties().size());

            for (const Rendering::ShaderProperty& property : schema->Properties())
            {
                // 7 個の互換 PBR 名は既存の material.* と重複させない。
                // それ以外の Shader 固有 Property だけを Schema から動的に足す。
                const std::string lower_name = [&property]
                {
                    std::string value = property.name;
                    std::transform(value.begin(), value.end(), value.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    return value;
                }();
                if (lower_name == "basecolor" || lower_name == "metallic" ||
                    lower_name == "roughness" || lower_name == "ambientocclusion" ||
                    lower_name == "emissive" || lower_name == "emissivestrength" ||
                    lower_name == "doublesided")
                {
                    continue;
                }

                Reflection::PropertyDesc desc;
                desc.name = "material." + property.name;
                desc.display_name = property.DisplayName();
                desc.category = property.category.empty()
                    ? "Shader Properties" : property.category;
                desc.tooltip = property.tooltip;
                desc.type = Rendering::MaterialSchema::PropertyTypeFor(property.kind);
                desc.animatable = MaterialAnimatableFor(property.kind);
                desc.serializable = false;
                desc.editor_visible = desc.animatable != Reflection::Animatable::None;
                if (property.kind == Rendering::ShaderPropertyKind::Range)
                {
                    desc.has_range = true;
                    desc.minimum = property.minimum;
                    desc.maximum = property.maximum;
                }
                if (property.kind == Rendering::ShaderPropertyKind::Enum)
                    desc.enum_labels = property.enum_names;

                const std::string saved_name = property.SavedName();
                const Reflection::PropertyValue default_value =
                    Rendering::MaterialSchema::DefaultValueFor(property);
                desc.getter = [saved_name, default_value](const Core::Component& base)
                {
                    if (base.TypeID() != T::StaticTypeID()) return Reflection::PropertyValue{};
                    const T& typed = static_cast<const T&>(base);
                    const Reflection::PropertyValue* value =
                        typed.material_motion_state.values.Find(saved_name);
                    return value != nullptr ? *value : default_value;
                };
                desc.setter = [saved_name](Core::Component& base,
                    const Reflection::PropertyValue& value)
                {
                    if (base.TypeID() != T::StaticTypeID()) return;
                    static_cast<T&>(base).material_motion_state.values.Set(saved_name, value);
                };
                state.schema_properties.push_back(std::move(desc));
            }
        }

        // 毎フレーム Material Asset の現在値から開始する。Motion が停止したら
        // active_values が空になるため Asset の値へ完全に戻る。
        state.values.Clear();
        for (const Rendering::ShaderProperty& property : schema->Properties())
        {
            const std::string saved_name = property.SavedName();
            const Reflection::PropertyValue* material_value = material->properties.Find(saved_name);
            state.values.Set(saved_name, material_value != nullptr
                ? *material_value : Rendering::MaterialSchema::DefaultValueFor(property));
        }
    }

    template<class T>
    const std::vector<Reflection::PropertyDesc>*
        MaterialOverrideDynamicProperties(const T& component)
    {
        // PropertyRegistry は 1 本の配列しか受け取れないため、固定 7 項目と
        // Shader Schema 項目を Component ごとのキャッシュへ合流する。
        // Schema が無い間も固定項目は従来どおり必ず見える。
        auto& combined = component.material_dynamic_properties_cache;
        const auto* fixed = FixedMaterialOverrideDynamicProperties<T>();
        const std::size_t required = fixed->size() +
            component.material_motion_state.schema_properties.size();
        if (combined.size() != required ||
            (required > fixed->size() &&
                combined.back().name != component.material_motion_state.schema_properties.back().name))
        {
            combined.assign(fixed->begin(), fixed->end());
            combined.insert(combined.end(),
                component.material_motion_state.schema_properties.begin(),
                component.material_motion_state.schema_properties.end());
        }
        return &combined;
    }

    template<class T>
    void MarkMaterialMotionProperty(T& component, const char* property_name)
    {
        if (property_name == nullptr) return;
        const std::string name(property_name);
        const std::uint32_t fixed = MaterialMotionMaskForProperty(name);
        if (fixed != 0)
        {
            MaterialMotionOverrideState& state = component.material_motion_state;
            state.fixed_active_mask |= fixed;

            // PropertyDesc の既存 setter は Scene/Prefab の正本フィールドへ書く。
            // Motion 専用 setter を別系統に増やさず、適用直後に値を一時領域へ移して
            // 正本をフレーム開始時の値へ戻すことで既存 PropertyRegistry をそのまま使う。
            if (fixed == MaterialMotionBaseColor)
            {
                state.driven_base_color = component.material_base_color;
                component.material_base_color = state.base_base_color;
            }
            else if (fixed == MaterialMotionMetallic)
            {
                state.driven_metallic = component.material_metallic;
                component.material_metallic = state.base_metallic;
            }
            else if (fixed == MaterialMotionRoughness)
            {
                state.driven_roughness = component.material_roughness;
                component.material_roughness = state.base_roughness;
            }
            else if (fixed == MaterialMotionAmbientOcclusion)
            {
                state.driven_ambient_occlusion = component.material_ambient_occlusion;
                component.material_ambient_occlusion = state.base_ambient_occlusion;
            }
            else if (fixed == MaterialMotionEmissiveColor)
            {
                state.driven_emissive_color = component.material_emissive_color;
                component.material_emissive_color = state.base_emissive_color;
            }
            else if (fixed == MaterialMotionEmissiveStrength)
            {
                state.driven_emissive_strength = component.material_emissive_strength;
                component.material_emissive_strength = state.base_emissive_strength;
            }
            else if (fixed == MaterialMotionDoubleSided)
            {
                state.driven_double_sided = component.material_double_sided;
                component.material_double_sided = state.base_double_sided;
            }
            return;
        }
        constexpr const char prefix[] = "material.";
        if (name.rfind(prefix, 0) != 0) return;
        const std::string shader_name = name.substr(sizeof(prefix) - 1);
        const std::string saved_name = "prop." + shader_name;
        const Reflection::PropertyValue* value =
            component.material_motion_state.values.Find(saved_name);
        if (value != nullptr)
            component.material_motion_state.active_values.Set(saved_name, *value);
    }
}
