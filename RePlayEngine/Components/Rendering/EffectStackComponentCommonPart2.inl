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

    void REPLAY_EFFECT_STACK_COMPONENT_TYPE::OnDeserialize(const Reflection::PropertyBag& input)
    {
#if REPLAY_EFFECT_STACK_HAS_CAPTURE_BACKDROP
        if (const Reflection::PropertyValue* backdrop = input.Find("capture_backdrop"))
        {
            capture_backdrop = backdrop->AsBool(false);
        }
#endif
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
