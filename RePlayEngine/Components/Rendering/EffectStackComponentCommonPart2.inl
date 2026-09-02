// Effect Stack 共通実装のうち、直列化・変更通知・DynamicProperties 構築前半を保持する。
// 分割内部専用。EffectStackComponentCommon.inl からだけ include する。

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
            case UI::UIEffectKind::DisplacementMap:
                effect.amount = 24.0f;
                effect.intensity = 1.0f;
                effect.direction = { 1.0f, 1.0f };
                break;
            case UI::UIEffectKind::TurbulentDisplace:
                effect.amount = 18.0f;
                effect.radius = 180.0f;
                effect.speed = 0.8f;
                effect.intensity = 1.0f;
                effect.seed = 1.0f;
                break;
            case UI::UIEffectKind::FractalNoise:
                effect.radius = 5.0f;
                effect.amount = 4.0f;
                effect.speed = 0.25f;
                effect.intensity = 1.0f;
                effect.seed = 1.0f;
                effect.color = { 0.0f, 0.0f, 0.0f, 1.0f };
                effect.color_2 = { 1.0f, 1.0f, 1.0f, 1.0f };
                break;
            case UI::UIEffectKind::MotionBlur:
                effect.radius = 24.0f;
                effect.angle = 0.0f;
                effect.intensity = 1.0f;
                break;
            case UI::UIEffectKind::Echo:
                effect.amount = 18.0f;
                effect.angle = 0.0f;
                effect.radius = 6.0f;
                effect.intensity = 0.65f;
                break;
            case UI::UIEffectKind::DropShadow:
                effect.amount = 12.0f;
                effect.angle = 45.0f;
                effect.radius = 8.0f;
                effect.intensity = 0.75f;
                effect.color = { 0.0f, 0.0f, 0.0f, 1.0f };
                break;
            case UI::UIEffectKind::InnerShadow:
                effect.amount = 8.0f;
                effect.angle = 45.0f;
                effect.radius = 6.0f;
                effect.intensity = 0.6f;
                effect.color = { 0.0f, 0.0f, 0.0f, 1.0f };
                break;
            case UI::UIEffectKind::LUT:
                effect.intensity = 1.0f;
                effect.radius = 16.0f;
                break;
            case UI::UIEffectKind::ToneCurve:
                effect.radius = 0.0f;
                effect.intensity = 1.0f;
                effect.threshold = 1.0f;
                effect.amount = 0.0f;
                break;
            case UI::UIEffectKind::MatteComposite:
                effect.amount = 0.0f;
                break;
            case UI::UIEffectKind::MatteMorphology:
                effect.radius = 4.0f;
                effect.intensity = 1.0f;
                effect.waveform = 0;
                break;
            case UI::UIEffectKind::BevelEmboss:
                effect.radius = 2.0f;
                effect.amount = 1.0f;
                effect.angle = 45.0f;
                effect.intensity = 0.8f;
                effect.color = { 1.0f, 1.0f, 1.0f, 1.0f };
                effect.color_2 = { 0.0f, 0.0f, 0.0f, 1.0f };
                break;
            case UI::UIEffectKind::Kaleidoscope:
                effect.radius = 8.0f;
                effect.amount = 1.0f;
                effect.intensity = 1.0f;
                effect.angle = 0.0f;
                effect.direction = { 0.5f, 0.5f };
                effect.waveform = 0;
                break;
            case UI::UIEffectKind::PageCurl:
                effect.radius = 48.0f;
                effect.amount = 0.85f;
                effect.intensity = 1.0f;
                effect.progress = 0.0f;
                effect.softness = 0.35f;
                effect.angle = 0.0f;
                effect.direction = { 1.0f, 1.0f };
                effect.waveform = 0;
                effect.color = { 0.78f, 0.82f, 0.9f, 1.0f };
                break;
            case UI::UIEffectKind::AsciiLedMatrix:
                effect.radius = 8.0f;
                effect.amount = 0.85f;
                effect.threshold = 0.0f;
                effect.intensity = 1.0f;
                effect.softness = 0.15f;
                effect.color = { 0.2f, 1.0f, 0.65f, 1.0f };
                effect.waveform = 0;
                break;
            case UI::UIEffectKind::FeedbackZoom:
                effect.radius = 2.0f;
                effect.amount = 0.08f;
                effect.intensity = 0.65f;
                effect.angle = 0.0f;
                effect.softness = 0.5f;
                effect.direction = { 0.5f, 0.5f };
                break;
            case UI::UIEffectKind::LiquidGlass:
                effect.radius = 3.0f;
                effect.amount = 10.0f;
                effect.progress = 0.35f;
                effect.softness = 0.65f;
                effect.intensity = 0.85f;
                effect.angle = 35.0f;
                effect.color = { 0.82f, 0.92f, 1.0f, 0.22f };
                effect.color_2 = { 0.78f, 0.94f, 1.0f, 0.85f };
                break;
            case UI::UIEffectKind::LightSweep:
                effect.radius = 42.0f;
                effect.amount = 1.15f;
                effect.threshold = 0.0f;
                effect.progress = 0.0f;
                effect.softness = 0.35f;
                effect.speed = 0.0f;
                effect.intensity = 1.0f;
                effect.angle = 0.0f;
                effect.color = { 1.0f, 0.96f, 0.82f, 0.9f };
                effect.color_2 = { 0.55f, 0.82f, 1.0f, 0.35f };
                break;
            case UI::UIEffectKind::Shockwave:
                effect.radius = 18.0f;
                effect.amount = 20.0f;
                effect.threshold = 2.0f;
                effect.progress = 0.0f;
                effect.softness = 0.35f;
                effect.speed = 0.0f;
                effect.intensity = 1.0f;
                effect.direction = { 0.5f, 0.5f };
                effect.color = { 0.55f, 0.85f, 1.0f, 0.9f };
                break;
            case UI::UIEffectKind::PixelSort:
                effect.radius = 48.0f;
                effect.amount = 1.0f;
                effect.threshold = 0.15f;
                effect.progress = 0.0f;
                effect.softness = 0.08f;
                effect.speed = 0.0f;
                effect.intensity = 1.0f;
                effect.angle = 0.0f;
                effect.color_stop_2 = 0.85f;
                effect.waveform = 0;
                break;
            case UI::UIEffectKind::Hologram:
                effect.radius = 10.0f;
                effect.amount = 0.35f;
                effect.threshold = 0.20f;
                effect.softness = 0.35f;
                effect.speed = 1.8f;
                effect.intensity = 1.0f;
                effect.color = { 0.20f, 0.90f, 1.0f, 1.0f };
                break;
            case UI::UIEffectKind::IridescentFoil:
                effect.radius = 1.5f;
                effect.amount = 0.65f;
                effect.progress = 0.0f;
                effect.speed = 0.35f;
                effect.intensity = 1.0f;
                effect.color = { 1.0f, 0.25f, 0.85f, 1.0f };
                effect.color_2 = { 0.15f, 0.85f, 1.0f, 1.0f };
                effect.color_3 = { 1.0f, 0.90f, 0.15f, 1.0f };
                effect.color_4 = { 0.35f, 0.18f, 1.0f, 1.0f };
                break;
            case UI::UIEffectKind::RadarSweep:
                effect.radius = 0.22f;
                effect.amount = 1.15f;
                effect.progress = 0.0f;
                effect.speed = 0.65f;
                effect.intensity = 1.0f;
                effect.direction = { 0.5f, 0.5f };
                effect.color = { 0.15f, 1.0f, 0.35f, 1.0f };
                break;
            case UI::UIEffectKind::EnergyPulse:
                effect.radius = 0.18f;
                effect.amount = 0.75f;
                effect.progress = 0.0f;
                effect.speed = 0.8f;
                effect.softness = 0.16f;
                effect.intensity = 1.0f;
                effect.direction = { 0.5f, 0.5f };
                effect.color = { 0.25f, 0.70f, 1.0f, 1.0f };
                break;
            case UI::UIEffectKind::CircuitFlow:
                effect.radius = 34.0f;
                effect.amount = 0.85f;
                effect.threshold = 0.12f;
                effect.speed = 1.25f;
                effect.intensity = 1.0f;
                effect.color = { 0.10f, 0.75f, 1.0f, 1.0f };
                break;
            case UI::UIEffectKind::HeatHaze:
                effect.radius = 22.0f;
                effect.amount = 8.0f;
                effect.threshold = 0.5f;
                effect.speed = 0.7f;
                effect.intensity = 0.65f;
                effect.seed = 0.37f;
                break;
            case UI::UIEffectKind::WaterCaustics:
                effect.radius = 44.0f;
                effect.amount = 0.65f;
                effect.speed = 0.8f;
                effect.intensity = 0.85f;
                effect.seed = 0.41f;
                effect.color = { 0.15f, 0.65f, 1.0f, 1.0f };
                break;
            case UI::UIEffectKind::VoronoiShatter:
                effect.radius = 36.0f;
                effect.amount = 20.0f;
                effect.progress = 0.0f;
                effect.speed = 0.35f;
                effect.softness = 0.12f;
                effect.intensity = 1.0f;
                effect.seed = 0.13f;
                break;
            case UI::UIEffectKind::InkBleed:
                effect.radius = 18.0f;
                effect.amount = 0.75f;
                effect.progress = 0.0f;
                effect.speed = 0.15f;
                effect.softness = 0.22f;
                effect.intensity = 1.0f;
                effect.color = { 0.03f, 0.02f, 0.06f, 1.0f };
                break;
            case UI::UIEffectKind::BurnReveal:
                effect.radius = 20.0f;
                effect.amount = 0.8f;
                effect.progress = 1.0f;
                effect.speed = 0.0f;
                effect.softness = 0.08f;
                effect.intensity = 1.0f;
                effect.color = { 1.0f, 0.18f, 0.015f, 1.0f };
                effect.color_2 = { 1.0f, 0.85f, 0.18f, 1.0f };
                break;
            case UI::UIEffectKind::PortalVortex:
                effect.radius = 0.48f;
                effect.amount = 0.24f;
                effect.threshold = 0.35f;
                effect.progress = 0.0f;
                effect.angle = 24.0f;
                effect.speed = 0.55f;
                effect.softness = 0.22f;
                effect.intensity = 1.0f;
                effect.direction = { 0.5f, 0.5f };
                effect.color = { 0.25f, 0.15f, 1.0f, 1.0f };
                break;
            case UI::UIEffectKind::FrostCrack:
                effect.radius = 24.0f;
                effect.amount = 0.8f;
                effect.progress = 1.0f;
                effect.threshold = 0.45f;
                effect.speed = 0.0f;
                effect.softness = 0.08f;
                effect.intensity = 1.0f;
                effect.color = { 0.70f, 0.92f, 1.0f, 1.0f };
                break;
            case UI::UIEffectKind::SpeedLines:
                effect.radius = 90.0f;
                effect.intensity = 1.0f;
                effect.threshold = 0.30f;
                effect.amount = 0.45f;
                effect.angle = 0.0f;
                effect.progress = 0.55f;
                effect.softness = 0.20f;
                effect.speed = 6.0f;
                effect.seed = 1.0f;
                effect.direction = { 0.5f, 0.5f };
                effect.color = { 0.05f, 0.05f, 0.06f, 1.0f };
                break;
            default:
                break;
            }
        }
    }

    REPLAY_EFFECT_STACK_COMPONENT_TYPE::REPLAY_EFFECT_STACK_COMPONENT_TYPE()
    {
        ResizeEffects();
        RebuildDynamicProperties();
    }

    const std::vector<Reflection::PropertyDesc>*
        REPLAY_EFFECT_STACK_COMPONENT_TYPE::DynamicProperties() const noexcept
    {
        return dynamic_properties_.empty() ? nullptr : &dynamic_properties_;
    }

    void REPLAY_EFFECT_STACK_COMPONENT_TYPE::OnSerialize(Reflection::PropertyBag& output) const
    {
#if REPLAY_EFFECT_STACK_HAS_CAPTURE_BACKDROP
        output.Set("capture_backdrop",
            Reflection::PropertyValue::MakeBool(capture_backdrop));
#endif
#if defined(REPLAY_EFFECT_STACK_HAS_TARGET_SLOT) && REPLAY_EFFECT_STACK_HAS_TARGET_SLOT
        output.Set("target_slot_index",
            Reflection::PropertyValue::MakeEnum(target_slot_index));
#endif
        output.Set("effect_region_enabled",
            Reflection::PropertyValue::MakeBool(effect_region.enabled));
        output.Set("effect_region_shape",
            Reflection::PropertyValue::MakeEnum(effect_region.shape));
        output.Set("effect_region_scope",
            Reflection::PropertyValue::MakeEnum(effect_region.scope));
        output.Set("effect_region_invert",
            Reflection::PropertyValue::MakeBool(effect_region.invert));
        output.Set("effect_region_center",
            Reflection::PropertyValue::MakeVector2(effect_region.center));
        output.Set("effect_region_size",
            Reflection::PropertyValue::MakeVector2(effect_region.size));
        output.Set("effect_region_rotation",
            Reflection::PropertyValue::MakeFloat(effect_region.rotation));
        output.Set("effect_region_feather",
            Reflection::PropertyValue::MakeFloat(effect_region.feather));
        output.Set("effect_region_strength",
            Reflection::PropertyValue::MakeFloat(effect_region.strength));
        output.Set("effect_region_mask",
            Reflection::PropertyValue::MakeAssetReference(effect_region.mask));
        output.Set("effect_region_path_count",
            Reflection::PropertyValue::MakeInt(static_cast<int>((std::min)(
                effect_region.path_points.size(),
                static_cast<std::size_t>(32)))));
        for (std::size_t index = 0; index < effect_region.path_points.size() && index < 32;
            ++index)
        {
            output.Set("effect_region_path_" + std::to_string(index),
                Reflection::PropertyValue::MakeVector2(effect_region.path_points[index]));
        }
        output.Set("effect_region_additional_count",
            Reflection::PropertyValue::MakeInt(static_cast<int>(effect_region.additional.size())));
        for (std::size_t index = 0; index < effect_region.additional.size(); ++index)
        {
            const int i = static_cast<int>(index) + 1;
            const UI::UIEffectRegionData& region = effect_region.additional[index];
            const std::string prefix = "effect_region_" + std::to_string(i) + "_";
            output.Set(prefix + "enabled", Reflection::PropertyValue::MakeBool(region.enabled));
            output.Set(prefix + "shape", Reflection::PropertyValue::MakeEnum(region.shape));
            output.Set(prefix + "scope", Reflection::PropertyValue::MakeEnum(region.scope));
            output.Set(prefix + "invert", Reflection::PropertyValue::MakeBool(region.invert));
            output.Set(prefix + "center", Reflection::PropertyValue::MakeVector2(region.center));
            output.Set(prefix + "size", Reflection::PropertyValue::MakeVector2(region.size));
            output.Set(prefix + "rotation", Reflection::PropertyValue::MakeFloat(region.rotation));
            output.Set(prefix + "feather", Reflection::PropertyValue::MakeFloat(region.feather));
            output.Set(prefix + "strength", Reflection::PropertyValue::MakeFloat(region.strength));
            output.Set(prefix + "mask", Reflection::PropertyValue::MakeAssetReference(region.mask));
            output.Set(prefix + "path_count", Reflection::PropertyValue::MakeInt(static_cast<int>(
                (std::min)(region.path_points.size(), static_cast<std::size_t>(32)))));
            for (std::size_t point_index = 0;
                point_index < region.path_points.size() && point_index < 32; ++point_index)
            {
                output.Set(prefix + "path_" + std::to_string(point_index),
                    Reflection::PropertyValue::MakeVector2(region.path_points[point_index]));
            }
        }
        output.Set("effect_count",
            Reflection::PropertyValue::MakeInt(static_cast<int>(effects.size())));
        for (std::size_t index = 0; index < effects.size(); ++index)
        {
            const int i = static_cast<int>(index);
            const UI::UIEffect& effect = effects[index];
            output.Set(EffectPropertyName(i, "enabled"),
                Reflection::PropertyValue::MakeBool(effect.enabled));
            output.Set(EffectPropertyName(i, "region_enabled"),
                Reflection::PropertyValue::MakeBool(effect.region_enabled));
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

    void REPLAY_EFFECT_STACK_COMPONENT_TYPE::OnDeserialize(const Reflection::PropertyBag& input)
    {
#if REPLAY_EFFECT_STACK_HAS_CAPTURE_BACKDROP
        if (const Reflection::PropertyValue* backdrop = input.Find("capture_backdrop"))
        {
            capture_backdrop = backdrop->AsBool(false);
        }
#endif
        // 旧 Scene はヘッダーの有効チェックが外れたまま保存されていることがある。
        if (const Reflection::PropertyValue* value = input.Find("enabled"))
        {
            if (value->AsBool(enabled)) SetEnabled(true);
        }
#if defined(REPLAY_EFFECT_STACK_HAS_TARGET_SLOT) && REPLAY_EFFECT_STACK_HAS_TARGET_SLOT
        if (const Reflection::PropertyValue* value = input.Find("target_slot_index"))
            target_slot_index = (std::max)(0, (std::min)(
                max_mesh_material_slots - 1, value->AsInt(target_slot_index)));
#endif
        if (const Reflection::PropertyValue* value = input.Find("effect_region_enabled"))
            effect_region.enabled = value->AsBool(effect_region.enabled);
        if (const Reflection::PropertyValue* value = input.Find("effect_region_shape"))
            effect_region.shape = value->AsInt(effect_region.shape);
        if (const Reflection::PropertyValue* value = input.Find("effect_region_scope"))
            effect_region.scope = value->AsInt(effect_region.scope);
        if (const Reflection::PropertyValue* value = input.Find("effect_region_invert"))
            effect_region.invert = value->AsBool(effect_region.invert);
        if (const Reflection::PropertyValue* value = input.Find("effect_region_center"))
            effect_region.center = value->AsVector2();
        if (const Reflection::PropertyValue* value = input.Find("effect_region_size"))
            effect_region.size = value->AsVector2();
        if (const Reflection::PropertyValue* value = input.Find("effect_region_rotation"))
            effect_region.rotation = value->AsFloat(effect_region.rotation);
        if (const Reflection::PropertyValue* value = input.Find("effect_region_feather"))
            effect_region.feather = value->AsFloat(effect_region.feather);
        if (const Reflection::PropertyValue* value = input.Find("effect_region_strength"))
            effect_region.strength = value->AsFloat(effect_region.strength);
        if (const Reflection::PropertyValue* value = input.Find("effect_region_mask"))
            effect_region.mask = value->AsAssetReference().guid;
        int path_count = 0;
        if (const Reflection::PropertyValue* value = input.Find("effect_region_path_count"))
            path_count = value->AsInt(0);
        path_count = (std::max)(0, (std::min)(32, path_count));
        effect_region.path_points.resize(static_cast<std::size_t>(path_count));
        for (int index = 0; index < path_count; ++index)
        {
            const Reflection::PropertyValue* value = input.Find(
                "effect_region_path_" + std::to_string(index));
            if (value != nullptr) effect_region.path_points[static_cast<std::size_t>(index)] =
                value->AsVector2();
        }
        if (effect_region.shape == static_cast<int>(UI::UIEffectRegionShape::Freeform))
            UI::EnsureUIEffectRegionPath(effect_region);
        int additional_count = 0;
        if (const Reflection::PropertyValue* value = input.Find("effect_region_additional_count"))
            additional_count = value->AsInt(0);
        additional_count = (std::max)(0, (std::min)(
            UI::UIEffectRegion::MaxAdditionalCount,
            additional_count));
        effect_region.additional.resize(static_cast<std::size_t>(additional_count));
        for (int index = 0; index < additional_count; ++index)
        {
            UI::UIEffectRegionData& region = effect_region.additional[
                static_cast<std::size_t>(index)];
            const std::string prefix = "effect_region_" + std::to_string(index + 1) + "_";
            if (const Reflection::PropertyValue* value = input.Find(prefix + "enabled"))
                region.enabled = value->AsBool(region.enabled);
            if (const Reflection::PropertyValue* value = input.Find(prefix + "shape"))
                region.shape = value->AsInt(region.shape);
            if (const Reflection::PropertyValue* value = input.Find(prefix + "scope"))
                region.scope = value->AsInt(region.scope);
            if (const Reflection::PropertyValue* value = input.Find(prefix + "invert"))
                region.invert = value->AsBool(region.invert);
            if (const Reflection::PropertyValue* value = input.Find(prefix + "center"))
                region.center = value->AsVector2();
            if (const Reflection::PropertyValue* value = input.Find(prefix + "size"))
                region.size = value->AsVector2();
            if (const Reflection::PropertyValue* value = input.Find(prefix + "rotation"))
                region.rotation = value->AsFloat(region.rotation);
            if (const Reflection::PropertyValue* value = input.Find(prefix + "feather"))
                region.feather = value->AsFloat(region.feather);
            if (const Reflection::PropertyValue* value = input.Find(prefix + "strength"))
                region.strength = value->AsFloat(region.strength);
            if (const Reflection::PropertyValue* value = input.Find(prefix + "mask"))
                region.mask = value->AsAssetReference().guid;
            int region_path_count = 0;
            if (const Reflection::PropertyValue* value = input.Find(prefix + "path_count"))
                region_path_count = value->AsInt(0);
            region_path_count = (std::max)(0, (std::min)(32, region_path_count));
            region.path_points.resize(static_cast<std::size_t>(region_path_count));
            for (int point_index = 0; point_index < region_path_count; ++point_index)
            {
                const Reflection::PropertyValue* value = input.Find(prefix + "path_" +
                    std::to_string(point_index));
                if (value != nullptr) region.path_points[
                    static_cast<std::size_t>(point_index)] = value->AsVector2();
            }
            if (region.shape == static_cast<int>(UI::UIEffectRegionShape::Freeform))
                UI::EnsureUIEffectRegionPath(region);
        }
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

    void REPLAY_EFFECT_STACK_COMPONENT_TYPE::OnPropertyChanged(const char* property_name)
    {
        if (property_name == nullptr)
        {
            ResizeEffects();
            RebuildDynamicProperties();
            return;
        }

        const std::string changed_property(property_name);
        // 「効果を適用」とヘッダーの有効チェックの二重管理をやめる。
        if (changed_property == "enabled" && enabled) SetEnabled(true);
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

    void REPLAY_EFFECT_STACK_COMPONENT_TYPE::SetCustomShaderSchema(std::size_t effect_index,
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

    const std::vector<UI::UIEffect>& REPLAY_EFFECT_STACK_COMPONENT_TYPE::EffectiveEffects(
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

    bool REPLAY_EFFECT_STACK_COMPONENT_TYPE::HasActiveEffects() const noexcept
    {
        if (!enabled) return false;
        for (const UI::UIEffect& effect : effects)
        {
            if (effect.enabled) return true;
        }
        return false;
    }

    bool REPLAY_EFFECT_STACK_COMPONENT_TYPE::HasActiveEffects(const Assets::AssetDatabase* database) const noexcept
    {
        if (!enabled) return false;
        for (const UI::UIEffect& effect : EffectiveEffects(database))
        {
            if (effect.enabled) return true;
        }
        return false;
    }

    DirectX::XMFLOAT4 REPLAY_EFFECT_STACK_COMPONENT_TYPE::ExpandBounds(float target_width,
        float target_height) const noexcept
    {
        if (!enabled) return { 0.0f, 0.0f, 0.0f, 0.0f };
        return Rendering::Effects::EffectChain::ExpandBounds(
            effects, target_width, target_height);
    }

    DirectX::XMFLOAT4 REPLAY_EFFECT_STACK_COMPONENT_TYPE::ExpandBounds(float target_width, float target_height,
        const Assets::AssetDatabase* database) const noexcept
    {
        if (!enabled) return { 0.0f, 0.0f, 0.0f, 0.0f };
        return Rendering::Effects::EffectChain::ExpandBounds(
            EffectiveEffects(database), target_width, target_height);
    }

    void REPLAY_EFFECT_STACK_COMPONENT_TYPE::ResizeEffects()
    {
        effect_count = (std::max)(0, (std::min)(max_effect_count, effect_count));
        effects.resize(static_cast<std::size_t>(effect_count));
        custom_schemas_.resize(static_cast<std::size_t>(effect_count));
    }

    void REPLAY_EFFECT_STACK_COMPONENT_TYPE::RebuildDynamicProperties()
    {
        dynamic_properties_.clear();
        dynamic_properties_.reserve(effects.size() * 24 + 10);

#if defined(REPLAY_EFFECT_STACK_HAS_TARGET_SLOT) && REPLAY_EFFECT_STACK_HAS_TARGET_SLOT
        {
            // スロットは番号ではなく Mesh Renderer に登録された名前で選ばせる。
            std::vector<std::string> slot_labels;
            const std::vector<MeshMaterialSlot>* slots = nullptr;
            if (const Core::GameObject* owner = Owner())
            {
                if (const auto* skinned = owner->GetComponent<SkinnedMeshRendererComponent>())
                    slots = &skinned->material_slots;
                else if (const auto* mesh = owner->GetComponent<MeshRendererComponent>())
                    slots = &mesh->material_slots;
                else if (const auto* primitive = owner->GetComponent<PrimitiveMeshRendererComponent>())
                    slots = &primitive->material_slots;
            }
            const std::size_t slot_count = slots != nullptr
                ? (std::min)(slots->size(), static_cast<std::size_t>(max_mesh_material_slots))
                : static_cast<std::size_t>(max_mesh_material_slots);
            for (std::size_t index = 0; index < slot_count; ++index)
            {
                const std::string number = std::to_string(index) + " 番";
                const bool named = slots != nullptr && !(*slots)[index].name.empty();
                slot_labels.push_back(named ? (*slots)[index].name : number);
            }
            Reflection::PropertyDesc desc;
            desc.name = "target_slot_index";
            desc.type = Reflection::PropertyType::Enum;
            desc.animatable = Reflection::Animatable::Step;
            desc.serializable = true;
            desc.display_name = "マテリアルスロット";
            desc.tooltip = "対象が「マテリアルスロット」のときに効果を掛けるスロット。";
            desc.category = "Effect Stack / 対象";
            desc.enum_labels = std::move(slot_labels);
            desc.getter = [](const Core::Component& component)
            {
                if (component.TypeID() != REPLAY_EFFECT_STACK_COMPONENT_TYPE::StaticTypeID())
                    return Reflection::PropertyValue{};
                const auto& stack =
                    static_cast<const REPLAY_EFFECT_STACK_COMPONENT_TYPE&>(component);
                return Reflection::PropertyValue::MakeEnum(stack.target_slot_index);
            };
            desc.setter = [](Core::Component& component,
                const Reflection::PropertyValue& value)
            {
                if (component.TypeID() != REPLAY_EFFECT_STACK_COMPONENT_TYPE::StaticTypeID())
                    return;
                auto& stack = static_cast<REPLAY_EFFECT_STACK_COMPONENT_TYPE&>(component);
                stack.target_slot_index = (std::max)(0, (std::min)(
                    max_mesh_material_slots - 1, value.AsInt(stack.target_slot_index)));
            };
            dynamic_properties_.push_back(std::move(desc));
        }
#endif

        const auto add_region_property = [&](const char* name,
            Reflection::PropertyType type, Reflection::Animatable animatable,
            const char* display, const char* tooltip)
        {
            Reflection::PropertyDesc desc;
            desc.name = name;
            desc.type = type;
            desc.animatable = animatable;
            desc.serializable = true;
            desc.display_name = display;
            desc.tooltip = tooltip;
            desc.category = "Effect Stack / 適用範囲";
            if (std::string(name) == "effect_region_shape")
            {
                desc.enum_labels = { "矩形", "円 / 楕円", "画像マスク（投げ縄）", "自由形状" };
            }
            if (std::string(name) == "effect_region_scope")
            {
                desc.enum_labels = { "全 Effect へ適用", "個別に選択" };
            }
            if (std::string(name) == "effect_region_mask") desc.asset_type = "Image";
            if (std::string(name) == "effect_region_center")
                desc.Range(0.0, 1.0).Step(0.01);
            if (std::string(name) == "effect_region_size")
                desc.Range(0.001, 1.0).Step(0.01);
            if (std::string(name) == "effect_region_rotation")
                desc.Range(-360.0, 360.0).Step(0.1);
            if (std::string(name) == "effect_region_feather" ||
                std::string(name) == "effect_region_strength")
                desc.Range(0.0, 1.0).Step(0.01);
            if (std::string(name) == "effect_region_additional_count")
                desc.Range(0.0, UI::UIEffectRegion::MaxAdditionalCount).Step(1.0);
            desc.getter = [name](const Core::Component& component)
            {
                if (component.TypeID() != REPLAY_EFFECT_STACK_COMPONENT_TYPE::StaticTypeID())
                    return Reflection::PropertyValue{};
                const auto& stack = static_cast<const REPLAY_EFFECT_STACK_COMPONENT_TYPE&>(component);
                const std::string property(name);
                if (property == "effect_region_enabled")
                    return Reflection::PropertyValue::MakeBool(stack.effect_region.enabled);
                if (property == "effect_region_shape")
                    return Reflection::PropertyValue::MakeEnum(stack.effect_region.shape);
                if (property == "effect_region_scope")
                    return Reflection::PropertyValue::MakeEnum(stack.effect_region.scope);
                if (property == "effect_region_invert")
                    return Reflection::PropertyValue::MakeBool(stack.effect_region.invert);
                if (property == "effect_region_center")
                    return Reflection::PropertyValue::MakeVector2(stack.effect_region.center);
                if (property == "effect_region_size")
                    return Reflection::PropertyValue::MakeVector2(stack.effect_region.size);
                if (property == "effect_region_rotation")
                    return Reflection::PropertyValue::MakeFloat(stack.effect_region.rotation);
                if (property == "effect_region_feather")
                    return Reflection::PropertyValue::MakeFloat(stack.effect_region.feather);
                if (property == "effect_region_strength")
                    return Reflection::PropertyValue::MakeFloat(stack.effect_region.strength);
                if (property == "effect_region_mask")
                    return Reflection::PropertyValue::MakeAssetReference(stack.effect_region.mask);
                if (property == "effect_region_additional_count")
                    return Reflection::PropertyValue::MakeInt(
                        static_cast<int>(stack.effect_region.additional.size()));
                return Reflection::PropertyValue{};
            };
            desc.setter = [name](Core::Component& component,
                const Reflection::PropertyValue& value)
            {
                if (component.TypeID() != REPLAY_EFFECT_STACK_COMPONENT_TYPE::StaticTypeID())
                    return;
                auto& stack = static_cast<REPLAY_EFFECT_STACK_COMPONENT_TYPE&>(component);
                const std::string property(name);
                if (property == "effect_region_enabled")
                    stack.effect_region.enabled = value.AsBool(stack.effect_region.enabled);
                else if (property == "effect_region_shape")
                {
                    stack.effect_region.shape = value.AsInt(stack.effect_region.shape);
                    if (stack.effect_region.shape == static_cast<int>(UI::UIEffectRegionShape::Freeform))
                        UI::EnsureUIEffectRegionPath(stack.effect_region);
                }
                else if (property == "effect_region_scope")
                    stack.effect_region.scope = value.AsInt(stack.effect_region.scope);
                else if (property == "effect_region_invert")
                    stack.effect_region.invert = value.AsBool(stack.effect_region.invert);
                else if (property == "effect_region_center")
                    stack.effect_region.center = value.AsVector2();
                else if (property == "effect_region_size")
                    stack.effect_region.size = value.AsVector2();
                else if (property == "effect_region_rotation")
                    stack.effect_region.rotation = value.AsFloat(stack.effect_region.rotation);
                else if (property == "effect_region_feather")
                    stack.effect_region.feather = value.AsFloat(stack.effect_region.feather);
                else if (property == "effect_region_strength")
                    stack.effect_region.strength = value.AsFloat(stack.effect_region.strength);
                else if (property == "effect_region_mask")
                    stack.effect_region.mask = value.AsAssetReference().guid;
                else if (property == "effect_region_additional_count")
                {
                    const int requested = (std::max)(0, (std::min)(
                        UI::UIEffectRegion::MaxAdditionalCount,
                        value.AsInt(static_cast<int>(stack.effect_region.additional.size()))));
                    const std::size_t previous = stack.effect_region.additional.size();
                    stack.effect_region.additional.resize(static_cast<std::size_t>(requested));
                    for (std::size_t index = previous;
                        index < stack.effect_region.additional.size(); ++index)
                    {
                        UI::UIEffectRegionData& region = stack.effect_region.additional[index];
                        region.enabled = true;
                        region.center = {
                            (std::min)(0.9f, 0.35f + 0.12f * static_cast<float>(index)),
                            (std::min)(0.8f, 0.35f + 0.10f * static_cast<float>(index)) };
                        region.size = { 0.18f, 0.18f };
                    }
                }
            };
            dynamic_properties_.push_back(std::move(desc));
        };

        add_region_property("effect_region_enabled", Reflection::PropertyType::Bool,
            Reflection::Animatable::Step, "範囲制限", "Effect Stack 全体を指定範囲だけへ適用する。");
        add_region_property("effect_region_shape", Reflection::PropertyType::Enum,
            Reflection::Animatable::Step, "範囲形状", "矩形・円/楕円・画像マスク（投げ縄）・自由形状から選ぶ。");
        add_region_property("effect_region_scope", Reflection::PropertyType::Enum,
            Reflection::Animatable::Step, "適用対象", "全 Effect へ強制適用するか、各 Effect の範囲適用スイッチで個別に選ぶ。");
        add_region_property("effect_region_invert", Reflection::PropertyType::Bool,
            Reflection::Animatable::Step, "範囲外へ適用", "ON にすると指定範囲の外側へだけ Effect を適用する。");
        add_region_property("effect_region_center", Reflection::PropertyType::Vector2,
            Reflection::Animatable::Interpolatable, "範囲中心", "正規化座標。0,0 が左上、1,1 が右下。");
        add_region_property("effect_region_size", Reflection::PropertyType::Vector2,
            Reflection::Animatable::Interpolatable, "範囲サイズ / 半径", "矩形の半サイズ、または楕円の半径を正規化座標で指定する。");
        add_region_property("effect_region_rotation", Reflection::PropertyType::Float,
            Reflection::Animatable::Interpolatable, "範囲回転", "矩形/楕円を回転する角度（度）。");
        add_region_property("effect_region_feather", Reflection::PropertyType::Float,
            Reflection::Animatable::Interpolatable, "境界ぼかし", "0 は範囲をきっぱり切り替え、値を上げるとグラデーションになる。");
        add_region_property("effect_region_strength", Reflection::PropertyType::Float,
            Reflection::Animatable::Interpolatable, "範囲強度", "範囲内での Effect 混合率。0 で無効、1 で全適用。");
        add_region_property("effect_region_mask", Reflection::PropertyType::AssetReference,
            Reflection::Animatable::Step, "投げ縄マスク", "白が適用、黒が非適用の Image。画像マスク形状のときに使う。");
        add_region_property("effect_region_additional_count", Reflection::PropertyType::Int,
            Reflection::Animatable::Step, "追加範囲数", "同じEffect Stackへ重ねる追加の適用範囲数。範囲内のどれかに入れば適用する。");

        for (std::size_t index = 0; index < effects.size(); ++index)
        {
            const int i = static_cast<int>(index);
            const UI::UIEffectKind current_kind =
                static_cast<UI::UIEffectKind>(effects[index].kind);
            const std::string effect_category = "Effect " +
                std::to_string(index + 1) + " / " + UI::EffectKindLabel(current_kind);
            auto push = [&](Reflection::PropertyDesc desc)
            {
                if (desc.category.empty()) desc.category = effect_category;
                else desc.category = effect_category + " / " + desc.category;
                dynamic_properties_.push_back(std::move(desc));
            };

            push(MakeEffectProperty(i, "enabled", Reflection::PropertyType::Bool,
                Reflection::Animatable::Step).Display("有効"));
            push(MakeEffectProperty(i, "region_enabled", Reflection::PropertyType::Bool,
                Reflection::Animatable::Step).Display("範囲へ適用")
                .Tooltip("範囲制限を個別選択モードでこの Effect に掛けるか。全 Effect モードでは無視される。"));
            std::vector<std::string> effect_kind_labels = UI::MakeEffectKindLabels();
            push(MakeEffectProperty(i, "type", Reflection::PropertyType::Enum,
                Reflection::Animatable::Step)
                .Display(UI::IsTimeDrivenEffect(current_kind) ? "種類  [M]" : "種類")
                .Tooltip("適用する Effect の種類。変更すると、その種類が使う項目だけを表示する。")
                .AsEnum(std::move(effect_kind_labels)));

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
