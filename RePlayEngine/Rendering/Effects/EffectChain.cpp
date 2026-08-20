#include "EffectChain.h"
#include "../RenderStats.h"

#include "../../Assets/AssetDatabase.h"
#include "../Shaders/ShaderAsset.h"
#include "../Shaders/ShaderCatalog.h"
#include "../Shaders/ShaderConstantPacker.h"
#include "../../../Source/core/shader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>

namespace ReplayEngine::Rendering::Effects
{
    namespace
    {
        bool LookupEffectShaderProperty(const std::string& saved_name,
            DirectX::XMFLOAT4& out, void* user)
        {
            if (user == nullptr) return false;
            const auto* bag = static_cast<const Reflection::PropertyBag*>(user);
            const Reflection::PropertyValue* value = bag->Find(saved_name);
            if (value == nullptr) return false;

            switch (value->Type())
            {
            case Reflection::PropertyType::Float:
                out = { value->AsFloat(), 0.0f, 0.0f, 0.0f };
                return true;
            case Reflection::PropertyType::Double:
                out = { static_cast<float>(value->AsDouble()), 0.0f, 0.0f, 0.0f };
                return true;
            case Reflection::PropertyType::Int:
            case Reflection::PropertyType::Enum:
                out = { static_cast<float>(value->AsInt()), 0.0f, 0.0f, 0.0f };
                return true;
            case Reflection::PropertyType::Bool:
                out = { value->AsBool() ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
                return true;
            case Reflection::PropertyType::Vector2:
            {
                const DirectX::XMFLOAT2 v = value->AsVector2();
                out = { v.x, v.y, 0.0f, 0.0f };
                return true;
            }
            case Reflection::PropertyType::Vector3:
            {
                const DirectX::XMFLOAT3 v = value->AsVector3();
                out = { v.x, v.y, v.z, 0.0f };
                return true;
            }
            case Reflection::PropertyType::Vector4:
            case Reflection::PropertyType::Color:
                out = value->AsVector4();
                return true;
            default:
                return false;
            }
        }

        float Clamp01(float value) noexcept
        {
            return (std::max)(0.0f, (std::min)(1.0f, value));
        }

        const Rendering::ShaderCatalog::Entry* CustomEffectEntry(
            const std::string& shader_guid,
            const Assets::AssetDatabase* asset_database,
            const Rendering::ShaderCatalog* shader_catalog)
        {
            if (shader_guid.empty() || asset_database == nullptr || shader_catalog == nullptr)
                return nullptr;
            const Assets::AssetRecord* record = asset_database->FindByGuid(shader_guid);
            if (record == nullptr || record->kind != Assets::AssetKind::Shader) return nullptr;

            const auto normalize = [](std::filesystem::path path)
            {
                std::error_code error;
                std::filesystem::path absolute = path.is_absolute()
                    ? path : std::filesystem::absolute(path, error);
                if (error) absolute = path;
                error.clear();
                const std::filesystem::path canonical =
                    std::filesystem::weakly_canonical(absolute, error);
                return error ? absolute.lexically_normal() : canonical.lexically_normal();
            };

            const std::filesystem::path source = normalize(record->source_path);
            for (const Rendering::ShaderCatalog::Entry& entry : shader_catalog->All())
            {
                if (entry.info.domain != Rendering::ShaderDomain::PostProcess) continue;
                if (normalize(entry.info.source_path) == source) return &entry;
            }
            return nullptr;
        }
    }

    bool EffectChain::Initialize(ID3D11Device* device)
    {
        Release();
        if (device == nullptr) return false;
        device_ = device;

        brush_stroke_vertex_shader_.Reset();
        brush_stroke_pixel_shader_.Reset();
        if (FAILED(create_vs_from_cso(device, "ui_brush_stroke_instances_vs.cso",
            brush_stroke_vertex_shader_.GetAddressOf(), nullptr, nullptr, 0)) ||
            FAILED(create_ps_from_cso(device, "ui_brush_stroke_instances_ps.cso",
                brush_stroke_pixel_shader_.GetAddressOf())))
        {
            return false;
        }

        const std::array<const char*, effect_shader_count> effect_cso_names{
            "ui_effect_blur.cso",
            "ui_effect_glow.cso",
            "ui_effect_color_adjust.cso",
            "ui_effect_noise.cso",
            "ui_effect_shake.cso",
            "ui_effect_mask.cso",
            "ui_effect_wipe.cso",
            "ui_effect_dissolve.cso",
            "ui_effect_distortion.cso",
            "ui_effect_chromatic_aberration.cso",
            "ui_effect_kuwahara.cso",
            "ui_effect_halftone.cso",
            "ui_effect_directional_blur.cso",
            "ui_effect_radial_blur.cso",
            "ui_effect_rotational_blur.cso",
            "ui_effect_vignette.cso",
            "ui_effect_light_streaks.cso",
            "ui_effect_lens_distortion.cso",
            "ui_effect_posterize.cso",
            "ui_effect_threshold.cso",
            "ui_effect_color_ramp.cso",
            "ui_effect_levels.cso",
            "ui_effect_temperature.cso",
            "ui_effect_edge_detect.cso",
            "ui_effect_outline.cso",
            "ui_effect_long_shadow.cso",
            "ui_effect_cross_hatch.cso",
            "ui_effect_brush_stroke.cso",
            "ui_effect_mosaic.cso",
            "ui_effect_crystallize.cso",
            "ui_effect_stained_glass.cso",
            "ui_effect_twirl.cso",
            "ui_effect_spherize.cso",
            "ui_effect_ripple.cso",
            "ui_effect_polar_coordinates.cso",
            "ui_effect_scanlines.cso",
            "ui_effect_crt.cso",
            "ui_effect_glitch.cso",
            "ui_effect_dither.cso",
            "ui_effect_vhs.cso",
            "ui_effect_letterbox.cso",
            "ui_effect_waveform.cso",
            "ui_effect_displacement_map.cso",
            "ui_effect_turbulent_displace.cso",
            "ui_effect_fractal_noise.cso",
            "ui_effect_motion_blur.cso",
            "ui_effect_echo.cso",
            "ui_effect_drop_shadow.cso",
            "ui_effect_inner_shadow.cso",
            "ui_effect_lut.cso",
            "ui_effect_tone_curve.cso",
            "ui_effect_matte_composite.cso",
        };
        static_assert(static_cast<std::size_t>(UI::UIEffectKind::MatteComposite) + 1 ==
            effect_shader_count, "UIEffectKind and EffectChain shader table must stay aligned");
        for (std::size_t index = 0; index < effect_cso_names.size(); ++index)
        {
            effect_pixel_shaders_[index].Reset();
            if (FAILED(create_ps_from_cso(device, effect_cso_names[index],
                effect_pixel_shaders_[index].GetAddressOf())))
            {
                return false;
            }
        }

        D3D11_BUFFER_DESC cb_desc{};
        cb_desc.ByteWidth = sizeof(EffectConstants);
        cb_desc.Usage = D3D11_USAGE_DEFAULT;
        cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        effect_constant_buffer_.Reset();
        if (FAILED(device->CreateBuffer(&cb_desc, nullptr,
            effect_constant_buffer_.GetAddressOf())))
        {
            return false;
        }
        return true;
    }

    void EffectChain::Release() noexcept
    {
        custom_effect_shader_cache_.clear();
        custom_effect_constant_buffer_.Reset();
        custom_effect_constant_buffer_size_ = 0;
        brush_stroke_instance_srv_.Reset();
        brush_stroke_instance_buffer_.Reset();
        brush_stroke_instance_capacity_ = 0;
        effect_constant_buffer_.Reset();
        for (Microsoft::WRL::ComPtr<ID3D11PixelShader>& shader : effect_pixel_shaders_)
            shader.Reset();
        brush_stroke_pixel_shader_.Reset();
        brush_stroke_vertex_shader_.Reset();
        device_.Reset();
    }

    bool EffectChain::EnsureBrushStrokeInstanceCapacity(std::size_t instance_count)
    {
        if (device_ == nullptr) return false;
        if (instance_count <= brush_stroke_instance_capacity_ &&
            brush_stroke_instance_buffer_ != nullptr && brush_stroke_instance_srv_ != nullptr)
        {
            return true;
        }

        std::size_t next_capacity = (std::max)(std::size_t{ 64 },
            brush_stroke_instance_capacity_);
        while (next_capacity < instance_count) next_capacity *= 2;
        if (next_capacity > static_cast<std::size_t>((std::numeric_limits<UINT>::max)() /
            sizeof(BrushStrokeInstance)))
        {
            return false;
        }

        brush_stroke_instance_srv_.Reset();
        brush_stroke_instance_buffer_.Reset();
        brush_stroke_instance_capacity_ = 0;

        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(sizeof(BrushStrokeInstance) * next_capacity);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(BrushStrokeInstance);
        brush_stroke_instance_buffer_.Reset();
        if (FAILED(device_->CreateBuffer(&desc, nullptr,
            brush_stroke_instance_buffer_.GetAddressOf())))
        {
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_UNKNOWN;
        view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        view.Buffer.FirstElement = 0;
        view.Buffer.NumElements = static_cast<UINT>(next_capacity);
        brush_stroke_instance_srv_.Reset();
        if (FAILED(device_->CreateShaderResourceView(brush_stroke_instance_buffer_.Get(),
            &view, brush_stroke_instance_srv_.GetAddressOf())))
        {
            brush_stroke_instance_buffer_.Reset();
            return false;
        }
        brush_stroke_instance_capacity_ = next_capacity;
        return true;
    }

    bool EffectChain::EnsureCustomEffectConstantBuffer(std::uint32_t byte_width)
    {
        if (device_ == nullptr) return false;
        const std::uint32_t aligned = (std::max)(16u,
            Rendering::ShaderConstantPacker::Align16(byte_width));
        if (custom_effect_constant_buffer_ != nullptr &&
            custom_effect_constant_buffer_size_ == aligned)
        {
            return true;
        }

        custom_effect_constant_buffer_.Reset();
        custom_effect_constant_buffer_size_ = 0;
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = aligned;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device_->CreateBuffer(&desc, nullptr,
            custom_effect_constant_buffer_.GetAddressOf())))
        {
            return false;
        }
        custom_effect_constant_buffer_size_ = aligned;
        return true;
    }

    ID3D11PixelShader* EffectChain::EffectShaderFor(UI::UIEffectKind kind) const noexcept
    {
        const int index = static_cast<int>(kind);
        if (index < 0 || index >= static_cast<int>(effect_pixel_shaders_.size()))
            return nullptr;
        return effect_pixel_shaders_[static_cast<std::size_t>(index)].Get();
    }

    ID3D11PixelShader* EffectChain::CustomEffectShaderFor(
        const std::string& shader_guid,
        const Assets::AssetDatabase* asset_database,
        const Rendering::ShaderCatalog* shader_catalog)
    {
        if (shader_guid.empty() || asset_database == nullptr ||
            shader_catalog == nullptr || device_ == nullptr)
        {
            return nullptr;
        }
        const Assets::AssetRecord* record = asset_database->FindByGuid(shader_guid);
        if (record == nullptr || record->kind != Assets::AssetKind::Shader)
            return nullptr;

        const auto normalize = [](std::filesystem::path path)
        {
            std::error_code error;
            std::filesystem::path absolute = path.is_absolute()
                ? path : std::filesystem::absolute(path, error);
            if (error) absolute = path;
            error.clear();
            const std::filesystem::path canonical =
                std::filesystem::weakly_canonical(absolute, error);
            return error ? absolute.lexically_normal() : canonical.lexically_normal();
        };

        const std::filesystem::path source_path = normalize(record->source_path);
        const Rendering::ShaderCatalog::Entry* matched = nullptr;
        for (const Rendering::ShaderCatalog::Entry& entry : shader_catalog->All())
        {
            if (entry.info.domain != Rendering::ShaderDomain::PostProcess) continue;
            if (normalize(entry.info.source_path) == source_path)
            {
                matched = &entry;
                break;
            }
        }
        if (matched == nullptr) return nullptr;

        const Rendering::ShaderCatalog::VariantResult& variant =
            matched->At(Rendering::ShaderVariant::Static);
        if (!variant.bytecode) return nullptr;

        CachedCustomEffectShader& cached = custom_effect_shader_cache_[shader_guid];
        const std::size_t bytecode_size = variant.bytecode->size();
        if (cached.shader && cached.bytecode_identity == variant.bytecode.get() &&
            cached.bytecode_size == bytecode_size)
        {
            return cached.shader.Get();
        }

        Microsoft::WRL::ComPtr<ID3D11PixelShader> replacement;
        if (FAILED(device_->CreatePixelShader(variant.bytecode->data(),
            bytecode_size, nullptr, replacement.GetAddressOf())) || !replacement)
        {
            return cached.shader.Get();
        }

        cached.shader = replacement;
        cached.bytecode_identity = variant.bytecode.get();
        cached.bytecode_size = bytecode_size;
        return cached.shader.Get();
    }

    UI::UIRenderTarget* EffectChain::Apply(const Context& context,
        const std::vector<UI::UIEffect>& effects,
        UI::UIRenderTarget* current,
        UI::UIRenderTarget* first,
        UI::UIRenderTarget* second)
    {
        if (context.device_context == nullptr || first == nullptr || second == nullptr ||
            current == nullptr || !context.configure_target ||
            !context.draw_plain_fullscreen || !context.draw_effect_fullscreen)
        {
            return current;
        }

        ID3D11DeviceContext* d3d = context.device_context;
        REPLAY_PROFILE_GPU_SCOPE(d3d, "EffectChain");
        const FLOAT clear[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
        for (const UI::UIEffect& effect : effects)
        {
            if (!effect.enabled) continue;
            const Rendering::ShaderCatalog::Entry* custom_entry = CustomEffectEntry(
                effect.custom_shader, context.asset_database, context.shader_catalog);
            ID3D11PixelShader* effect_shader = custom_entry != nullptr
                ? CustomEffectShaderFor(effect.custom_shader,
                    context.asset_database, context.shader_catalog)
                : nullptr;
            const bool using_custom_shader = effect_shader != nullptr && custom_entry != nullptr;
            if (effect_shader == nullptr)
                effect_shader = EffectShaderFor(static_cast<UI::UIEffectKind>(effect.kind));
            if (effect_shader == nullptr) continue;

            const std::string profile_effect_name = "EffectPass/" + std::to_string(effect.kind);
            RenderStats::ScopedGpu profile_effect_scope(d3d, profile_effect_name.c_str());
            UI::UIRenderTarget* destination = current == first ? second : first;
            Rendering::Stats().CountEffectPass();
            Rendering::Stats().CountRenderTargetBind();
            // draw callback は depth/rasterizer/blend/sampler を設定する。
            Rendering::Stats().CountStateSet(Rendering::RenderStats::StateKind::DepthStencil, false);
            Rendering::Stats().CountStateSet(Rendering::RenderStats::StateKind::Rasterizer, false);
            Rendering::Stats().CountStateSet(Rendering::RenderStats::StateKind::Blend, false);
            Rendering::Stats().CountStateSet(Rendering::RenderStats::StateKind::ShaderResource, false);
            context.configure_target(*destination);
            d3d->ClearRenderTargetView(destination->rtv.Get(), clear);

            const float width = (std::max)(1.0f, static_cast<float>(destination->width));
            const float height = (std::max)(1.0f, static_cast<float>(destination->height));
            EffectConstants effect_constants{};
            effect_constants.effect_color = effect.color;
            effect_constants.effect_params0 = {
                effect.radius, effect.intensity, effect.threshold, effect.amount };
            effect_constants.effect_params1 = {
                effect.angle, effect.progress, effect.softness, effect.speed };
            effect_constants.effect_params2 = {
                effect.direction.x, effect.direction.y, effect.seed, context.time };
            effect_constants.effect_params3 = {
                static_cast<float>(effect.waveform), 0.0f, 0.0f, 0.0f };
            effect_constants.effect_color_2 = effect.color_2;
            effect_constants.effect_color_3 = effect.color_3;
            effect_constants.effect_color_4 = effect.color_4;
            effect_constants.effect_color_stops = {
                effect.color_stop_2, effect.color_stop_3, effect.color_stop_4, 0.0f };

            const UI::UIEffectKind effect_kind =
                static_cast<UI::UIEffectKind>(effect.kind);
            const bool is_mask_effect = effect_kind == UI::UIEffectKind::Mask;
            const bool is_temporal_effect = effect_kind == UI::UIEffectKind::MotionBlur ||
                effect_kind == UI::UIEffectKind::Echo;
            ID3D11ShaderResourceView* mask_texture = nullptr;
            std::string mask_guid = effect.mask;
            const bool is_brush_effect = static_cast<UI::UIEffectKind>(effect.kind) ==
                UI::UIEffectKind::BrushStroke;
            if (is_brush_effect && effect.brush_atlas_enabled && mask_guid.empty() &&
                context.asset_database != nullptr)
            {
                if (const Assets::AssetRecord* atlas = context.asset_database->FindByPath(
                    std::filesystem::path("resources") / "BrushMasks" /
                    "brush_masks_atlas.png"))
                {
                    if (atlas->kind == Assets::AssetKind::Image) mask_guid = atlas->guid;
                }
            }
            if (is_temporal_effect && context.runtime_history_texture != nullptr)
            {
                // t1 を前フレームの結果として使う。MotionBlur / Echo は
                // これで同一フレーム内の擬似残像ではなく時間方向に蓄積できる。
                mask_texture = context.runtime_history_texture;
            }
            else if (mask_guid == "__runtime_ui_matte")
            {
                mask_texture = context.runtime_mask_texture;
            }
            else if (!mask_guid.empty() && context.resolve_texture)
            {
                mask_texture = context.resolve_texture(mask_guid);
            }

            effect_constants.effect_params3.y = mask_texture != nullptr ? 1.0f : 0.0f;
            effect_constants.effect_params3.z = context.runtime_mask_luma ? 1.0f : 0.0f;
            effect_constants.effect_params3.w = context.runtime_mask_invert ? 1.0f : 0.0f;
            const bool brush_atlas = is_brush_effect && effect.brush_atlas_enabled &&
                mask_texture != nullptr;
            if (brush_atlas)
            {
                effect_constants.effect_params3.z = 1.0f;
                effect_constants.brush_pattern_settings = {
                    static_cast<float>((std::max)(0, (std::min)(15,
                        effect.brush_pattern_index))),
                    static_cast<float>((std::max)(0, (std::min)(1,
                        effect.brush_pattern_mode))), 0.0f, 0.0f };
                for (std::size_t group = 0;
                    group < effect_constants.brush_pattern_weights.size(); ++group)
                {
                    const std::size_t first_index = group * 4;
                    effect_constants.brush_pattern_weights[group] = {
                        (std::max)(0.0f, effect.brush_pattern_weights[first_index]),
                        (std::max)(0.0f, effect.brush_pattern_weights[first_index + 1]),
                        (std::max)(0.0f, effect.brush_pattern_weights[first_index + 2]),
                        (std::max)(0.0f, effect.brush_pattern_weights[first_index + 3]) };
                }
            }
            if (is_mask_effect)
            {
                const float center_x = effect.direction.x > 0.0f && effect.direction.x < 1.0f
                    ? effect.direction.x : 0.5f;
                const float center_y = effect.direction.y > 0.0f && effect.direction.y < 1.0f
                    ? effect.direction.y : 0.5f;
                const float half_width = effect.seed > 0.0f && effect.seed < 1.0f
                    ? effect.seed : 0.5f;
                const float half_height = effect.speed > 0.0f && effect.speed < 1.0f
                    ? effect.speed : 0.5f;
                effect_constants.effect_params2 = {
                    center_x, center_y, half_width, half_height };
                effect_constants.effect_params1.w = mask_texture != nullptr ? 1.0f : 0.0f;
            }
            effect_constants.target_size = { width, height, 1.0f / width, 1.0f / height };
            d3d->UpdateSubresource(effect_constant_buffer_.Get(), 0, nullptr,
                &effect_constants, 0, 0);
            d3d->PSSetShaderResources(1, 1, &mask_texture);

            std::vector<std::uint32_t> custom_texture_slots;
            if (using_custom_shader && custom_entry->schema)
            {
                const Rendering::ShaderPropertySchema& schema = *custom_entry->schema;
                std::vector<std::uint8_t> packed;
                Rendering::ShaderConstantPacker::Pack(schema, &LookupEffectShaderProperty,
                    const_cast<Reflection::PropertyBag*>(&effect.custom_parameters), packed);
                if (!packed.empty() && EnsureCustomEffectConstantBuffer(
                    static_cast<std::uint32_t>(packed.size())))
                {
                    d3d->UpdateSubresource(custom_effect_constant_buffer_.Get(), 0, nullptr,
                        packed.data(), 0, 0);
                    ID3D11Buffer* custom_cb = custom_effect_constant_buffer_.Get();
                    d3d->PSSetConstantBuffers(
                        Rendering::ShaderConstantPacker::material_constant_register,
                        1, &custom_cb);
                }

                for (const Rendering::ShaderProperty& property : schema.Properties())
                {
                    if (property.kind != Rendering::ShaderPropertyKind::Texture) continue;
                    const Reflection::PropertyValue* value =
                        effect.custom_parameters.Find(property.SavedName());
                    const std::string guid = value != nullptr
                        ? value->AsAssetReference().guid : std::string{};
                    ID3D11ShaderResourceView* custom_texture = context.resolve_texture
                        ? context.resolve_texture(guid) : nullptr;
                    d3d->PSSetShaderResources(property.texture_slot, 1, &custom_texture);
                    custom_texture_slots.push_back(property.texture_slot);
                }
            }

            if (brush_atlas && effect.brush_instanced_renderer_enabled && !using_custom_shader &&
                brush_stroke_vertex_shader_ != nullptr && brush_stroke_pixel_shader_ != nullptr)
            {
                const float grid_x = (std::max)(effect.progress * 0.78f, 8.0f);
                const float grid_y = (std::max)(effect.amount * 1.35f, 6.0f);
                const int columns = static_cast<int>(std::ceil(width / grid_x)) + 2;
                const int rows = static_cast<int>(std::ceil(height / grid_y)) + 2;
                std::vector<BrushStrokeInstance> instances;
                instances.reserve(static_cast<std::size_t>(columns) *
                    static_cast<std::size_t>(rows));

                const auto hash = [](std::uint32_t value) noexcept
                {
                    value ^= value >> 16;
                    value *= 0x7feb352du;
                    value ^= value >> 15;
                    value *= 0x846ca68bu;
                    value ^= value >> 16;
                    return value;
                };
                const auto random01 = [&hash](std::uint32_t value) noexcept
                {
                    return static_cast<float>(hash(value) & 0x00ffffffu) /
                        static_cast<float>(0x01000000u);
                };
                const auto profile_for = [](int pattern) noexcept
                {
                    static const std::array<DirectX::XMFLOAT4, 16> profiles{
                        DirectX::XMFLOAT4{ 1.10f, 1.08f, 0.55f, 0.92f },
                        { 0.68f, 0.88f, 0.20f, 0.78f }, { 1.05f, 1.10f, 0.45f, 0.90f },
                        { 0.95f, 1.05f, 0.35f, 0.95f }, { 1.15f, 1.10f, 0.85f, 0.90f },
                        { 1.00f, 1.12f, 0.35f, 1.00f }, { 0.90f, 0.98f, 0.70f, 0.78f },
                        { 1.12f, 1.15f, 0.35f, 1.00f }, { 1.00f, 1.08f, 0.55f, 0.82f },
                        { 0.75f, 0.90f, 0.22f, 0.75f }, { 1.05f, 1.15f, 0.40f, 1.00f },
                        { 0.82f, 0.92f, 0.80f, 0.78f }, { 0.95f, 0.98f, 0.70f, 0.75f },
                        { 1.00f, 1.08f, 0.25f, 0.88f }, { 1.08f, 1.16f, 0.60f, 1.00f },
                        { 1.25f, 1.20f, 0.35f, 1.00f } };
                    return profiles[static_cast<std::size_t>((std::max)(0,
                        (std::min)(15, pattern)))];
                };
                float total_weight = 0.0f;
                for (const float weight : effect.brush_pattern_weights)
                    total_weight += (std::max)(0.0f, weight);

                for (int row = -1; row < rows - 1; ++row)
                {
                    for (int column = -1; column < columns - 1; ++column)
                    {
                        const std::uint32_t seed =
                            static_cast<std::uint32_t>(column * 73856093) ^
                            static_cast<std::uint32_t>(row * 19349663) ^
                            static_cast<std::uint32_t>(effect.seed * 65535.0f);
                        int pattern = effect.brush_pattern_index;
                        if (effect.brush_pattern_mode != 0 && total_weight > 0.0f)
                        {
                            float pick = random01(seed) * total_weight;
                            for (int index = 0; index < 16; ++index)
                            {
                                pick -= (std::max)(0.0f,
                                    effect.brush_pattern_weights[static_cast<std::size_t>(index)]);
                                if (pick <= 0.0f)
                                {
                                    pattern = index;
                                    break;
                                }
                            }
                        }
                        const DirectX::XMFLOAT4 profile = profile_for(pattern);
                        const float variation = Clamp01(effect.softness);
                        const float jitter_x = (random01(seed + 1u) - 0.5f) *
                            grid_x * (0.12f + variation * 0.50f);
                        const float jitter_y = (random01(seed + 2u) - 0.5f) *
                            grid_y * (0.12f + variation * 0.50f);
                        BrushStrokeInstance instance{};
                        instance.center = { (column + 0.5f) * grid_x + jitter_x,
                            (row + 0.5f) * grid_y + jitter_y };
                        instance.size = { (std::max)(effect.progress * profile.x, 8.0f),
                            (std::max)(effect.amount * 2.2f * profile.y, 5.0f) };
                        instance.pattern = static_cast<std::uint32_t>((std::max)(0,
                            (std::min)(15, pattern)));
                        instances.push_back(instance);
                    }
                }

                if (!instances.empty() && EnsureBrushStrokeInstanceCapacity(instances.size()))
                {
                    D3D11_MAPPED_SUBRESOURCE mapped{};
                    if (SUCCEEDED(d3d->Map(brush_stroke_instance_buffer_.Get(), 0,
                        D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                    {
                        std::memcpy(mapped.pData, instances.data(),
                            sizeof(BrushStrokeInstance) * instances.size());
                        d3d->Unmap(brush_stroke_instance_buffer_.Get(), 0);

                        context.draw_plain_fullscreen(width, height, current->srv.Get(),
                            context.blend_none != nullptr ? context.blend_none : context.blend_alpha);

                        ID3D11Buffer* effect_cb = effect_constant_buffer_.Get();
                        d3d->VSSetConstantBuffers(0, 1, &effect_cb);
                        d3d->PSSetConstantBuffers(0, 1, &effect_cb);
                        ID3D11ShaderResourceView* vertex_srvs[]{ current->srv.Get(),
                            brush_stroke_instance_srv_.Get() };
                        ID3D11ShaderResourceView* pixel_srvs[]{ current->srv.Get(), mask_texture };
                        d3d->VSSetShaderResources(0, 2, vertex_srvs);
                        d3d->PSSetShaderResources(0, 2, pixel_srvs);
                        ID3D11SamplerState* sampler = context.sampler;
                        d3d->VSSetSamplers(0, 1, &sampler);
                        d3d->PSSetSamplers(0, 1, &sampler);
                        d3d->IASetInputLayout(nullptr);
                        d3d->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
                        d3d->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
                        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
                        d3d->VSSetShader(brush_stroke_vertex_shader_.Get(), nullptr, 0);
                        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
                        d3d->PSSetShader(brush_stroke_pixel_shader_.Get(), nullptr, 0);
                        d3d->OMSetDepthStencilState(context.depth_disabled, 0);
                        d3d->OMSetBlendState(context.blend_alpha, nullptr, 0xffffffff);
                        d3d->RSSetState(context.rasterizer);
                        d3d->DrawInstanced(4, static_cast<UINT>(instances.size()), 0, 0);

                        ID3D11ShaderResourceView* null_srvs[2]{};
                        d3d->VSSetShaderResources(0, 2, null_srvs);
                        d3d->PSSetShaderResources(0, 2, null_srvs);
                        current = destination;
                        continue;
                    }
                }
            }

            context.draw_effect_fullscreen(width, height, current->srv.Get(),
                context.blend_none != nullptr ? context.blend_none : context.blend_alpha,
                effect_shader, effect_constant_buffer_.Get());

            if (using_custom_shader)
            {
                ID3D11Buffer* null_custom_cb = nullptr;
                d3d->PSSetConstantBuffers(
                    Rendering::ShaderConstantPacker::material_constant_register,
                    1, &null_custom_cb);
                ID3D11ShaderResourceView* null_custom_srv = nullptr;
                for (const std::uint32_t slot : custom_texture_slots)
                    d3d->PSSetShaderResources(slot, 1, &null_custom_srv);
            }

            ID3D11ShaderResourceView* null_srv = nullptr;
            d3d->PSSetShaderResources(0, 1, &null_srv);
            d3d->PSSetShaderResources(1, 1, &null_srv);
            current = destination;
        }

        ID3D11Buffer* null_cb = nullptr;
        d3d->PSSetConstantBuffers(0, 1, &null_cb);
        return current;
    }

    DirectX::XMFLOAT4 EffectChain::ExpandBounds(const std::vector<UI::UIEffect>& effects,
        float target_width, float target_height) noexcept
    {
        DirectX::XMFLOAT4 expansion{ 0.0f, 0.0f, 0.0f, 0.0f };
        for (const UI::UIEffect& effect : effects)
        {
            if (!effect.enabled) continue;
            const float current_width = target_width + expansion.x + expansion.z;
            const float current_height = target_height + expansion.y + expansion.w;
            const DirectX::XMFLOAT4 current = effect.ExpandBounds(current_width, current_height);
            expansion.x += current.x;
            expansion.y += current.y;
            expansion.z += current.z;
            expansion.w += current.w;
        }

        constexpr float max_expansion = 2048.0f;
        expansion.x = (std::min)(expansion.x, max_expansion);
        expansion.y = (std::min)(expansion.y, max_expansion);
        expansion.z = (std::min)(expansion.z, max_expansion);
        expansion.w = (std::min)(expansion.w, max_expansion);
        return expansion;
    }

    std::uint64_t EffectChain::AllocatedBufferBytes() const noexcept
    {
        std::uint64_t total = 0;
        if (effect_constant_buffer_) total += sizeof(EffectConstants);
        if (brush_stroke_instance_buffer_)
        {
            total += static_cast<std::uint64_t>(brush_stroke_instance_capacity_) *
                sizeof(BrushStrokeInstance);
        }
        if (custom_effect_constant_buffer_)
            total += custom_effect_constant_buffer_size_;
        return total;
    }

}
