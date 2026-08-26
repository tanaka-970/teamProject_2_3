#include "framework.h"
#include "shader.h"
#include "texture.h"
#include "skinned_mesh.h"
#include "gltf_model.h"
#include "../../../RePlayEngine/Components/Core/TransformComponent.h"
#include "../../../RePlayEngine/Components/Camera/CameraComponent.h"
#include "../../../RePlayEngine/Components/Rendering/ParticleEmitterComponent.h"
#include "../../../RePlayEngine/Components/Rendering/PostProcessVolumeComponent.h"
#include "../../../RePlayEngine/Components/Rendering/ScreenEffectStackComponent.h"
#include "../../../RePlayEngine/Components/Rendering/ModelEffectStackComponent.h"
#include "../../../RePlayEngine/Rendering/Shaders/BuiltInShaders.h"
#include "../../../RePlayEngine/Rendering/ShaderStack/BuiltInShaderLayers.h"
#include "../../../RePlayEngine/Rendering/Materials/ShaderLayerBinding.h"
#include "../../../RePlayEngine/UI/UILayout.h"
#include "../../../RePlayEngine/Components/UI/UIEffectStackComponent.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// 分割一覧（framework_render.cpp）:
//   framework_render_setup.inl       … backbuffer/Camera/frame constants
//   framework_render_scene_setup.inl … Particle/Shadow/Depth/GBuffer 準備
//   framework_render_deferred.inl    … Deferred Lighting/Material Layer
//   framework_render_forward.inl     … Outline/TAA/Forward 経路
//   framework_render_effects.inl     … Model/Screen Effect と PostProcess 前半
//   framework_render_present.inl     … UI/統計/履歴/Golden Capture/Present
// 関数へ再分割せず本文を連続断片のまま include するため、描画順は変更しない。

namespace
{
    using ReplayEngine::Components::ParticleEmitterComponent;
    using ReplayEngine::Components::PostProcessVolumeComponent;
    using ReplayEngine::Components::ScreenEffectStackComponent;
    using ReplayEngine::Components::ModelEffectStackComponent;
    using ReplayEngine::Components::TransformComponent;

    float clamp_finite(float value, float fallback, float low, float high) noexcept
    {
        if (!std::isfinite(value)) return fallback;
        return (std::min)((std::max)(value, low), high);
    }

    std::uint64_t buffer_byte_width(ID3D11Buffer* buffer) noexcept
    {
        if (buffer == nullptr) return 0;
        D3D11_BUFFER_DESC desc{};
        buffer->GetDesc(&desc);
        return desc.ByteWidth;
    }

    DirectX::XMFLOAT4 clamp_color(const DirectX::XMFLOAT4& value) noexcept
    {
        return {
            clamp_finite(value.x, 1.0f, 0.0f, 8.0f),
            clamp_finite(value.y, 1.0f, 0.0f, 8.0f),
            clamp_finite(value.z, 1.0f, 0.0f, 8.0f),
            clamp_finite(value.w, 1.0f, 0.0f, 1.0f)
        };
    }

    DirectX::XMFLOAT3 normalize_or_up(const DirectX::XMFLOAT3& value) noexcept
    {
        const DirectX::XMVECTOR vector = DirectX::XMLoadFloat3(&value);
        const float length_sq = DirectX::XMVectorGetX(
            DirectX::XMVector3LengthSq(vector));
        if (!std::isfinite(length_sq) || length_sq <= 1.0e-8f)
        {
            return { 0.0f, 1.0f, 0.0f };
        }

        DirectX::XMFLOAT3 normalized{};
        DirectX::XMStoreFloat3(&normalized,
            DirectX::XMVector3Normalize(vector));
        return normalized;
    }

    struct ParticleEmitterSelection
    {
        const ReplayEngine::Core::GameObject* object = nullptr;
        const ParticleEmitterComponent* component = nullptr;

        bool Valid() const noexcept { return object != nullptr && component != nullptr; }
    };

    ParticleEmitterSelection select_particle_emitter(
        const ReplayEngine::Scene::Scene& scene)
    {
        ParticleEmitterSelection best{};
        for (std::size_t object_index = 0; object_index < scene.GameObjectCount();
            ++object_index)
        {
            const ReplayEngine::Core::GameObject* object =
                scene.GameObjectAt(object_index);
            if (object == nullptr || object->PendingDestroy()) continue;

            for (std::size_t component_index = 0;
                component_index < object->ComponentCount(); ++component_index)
            {
                const auto* emitter = dynamic_cast<const ParticleEmitterComponent*>(
                    object->ComponentAt(component_index));
                if (emitter == nullptr || !emitter->emitting ||
                    !emitter->ActiveInHierarchy())
                {
                    continue;
                }
                if (!best.Valid() || emitter->priority > best.component->priority)
                {
                    best = { object, emitter };
                }
            }
        }
        return best;
    }

    // 読み込んだFBXの軸方向を、エンジンの左手座標系へ合わせる。
    DirectX::XMMATRIX fbx_coordinate_transform()
    {
        const DirectX::XMFLOAT4X4 coordinate_system_transform
        {
            -1, 0,  0, 0,
             0, 0, -1, 0,
             0, 1,  0, 0,
             0, 0,  0, 1
        };
        return DirectX::XMLoadFloat4x4(&coordinate_system_transform);
    }
}

// エディタのデバッグ用メッシュ (static_meshes[0]) を置くワールド行列。
//
// かつてここには「ゲーム実行中は旧 Player の Transform を使う」という分岐があり、
// それが旧 Player を画面へ出す固定行列だった。その分岐は撤去した。
// GameObject の描画行列は SceneRenderCollector が RenderItem::world として作る。
void framework::store_debug_mesh_world(DirectX::XMFLOAT4X4& world) const
{
    DirectX::XMMATRIX C = fbx_coordinate_transform();

    DirectX::XMMATRIX S = DirectX::XMMatrixScaling(scaling.x, scaling.y, scaling.z);
    DirectX::XMMATRIX R = DirectX::XMMatrixRotationRollPitchYaw(
        DirectX::XMConvertToRadians(rotation.x),
        DirectX::XMConvertToRadians(rotation.y),
        DirectX::XMConvertToRadians(rotation.z));
    DirectX::XMMATRIX T = DirectX::XMMatrixTranslation(translation.x, translation.y, translation.z);
    DirectX::XMStoreFloat4x4(&world, C * S * R * T);
}

void framework::update_frame_constants(const DirectX::XMMATRIX& view,
    const DirectX::XMMATRIX& projection, float elapsed_time, bool advance_effect_time)
{
    if (!frame_constants_cb) return;

    const DirectX::XMMATRIX view_projection = view * projection;
    DirectX::XMStoreFloat4x4(&frame_constants.view, view);
    DirectX::XMStoreFloat4x4(&frame_constants.projection, projection);
    DirectX::XMStoreFloat4x4(&frame_constants.view_projection, view_projection);
    DirectX::XMStoreFloat4x4(&frame_constants.inv_view,
        DirectX::XMMatrixInverse(nullptr, view));
    DirectX::XMStoreFloat4x4(&frame_constants.inv_projection,
        DirectX::XMMatrixInverse(nullptr, projection));
    DirectX::XMStoreFloat4x4(&frame_constants.inv_view_projection,
        DirectX::XMMatrixInverse(nullptr, view_projection));

    // 初回フレームは前フレームが無いので、今フレームで埋めて再投影を無効化する。
    if (!previous_view_projection_valid)
    {
        DirectX::XMStoreFloat4x4(&previous_view_projection, view_projection);
        previous_view_projection_valid = true;
    }
    frame_constants.prev_view_projection = previous_view_projection;

    const DirectX::XMFLOAT4X4& p = frame_constants.projection;
    // projection._22 = 1/tan(fovY/2)、_11 = 1/(tan(fovY/2)*aspect)。
    const float tan_half_fov_y = p._22 != 0.0f ? 1.0f / p._22 : 1.0f;
    const float aspect = p._11 != 0.0f ? p._22 / p._11 : 1.0f;
    // LH透視射影は _33 = far/(far-near)、_43 = -near*far/(far-near)。
    const float near_plane = p._33 != 0.0f ? -p._43 / p._33 : 0.1f;
    const float far_plane = (p._33 - 1.0f) != 0.0f ? p._43 / (p._33 - 1.0f) : 10000.0f;

    // CameraComponent / 補助 View / 従来 Camera のどれを描いていても、
    // view/projection と同じ窓口から Eye を取る。ここだけ旧 Gameplay Camera を
    // 直接読むと、分割 Viewport で鏡面・SSAO の視点だけ別 Camera になる。
    const DirectX::XMFLOAT3 frame_eye = viewport_eye_position();
    frame_constants.camera_position =
        { frame_eye.x, frame_eye.y, frame_eye.z, 1.0f };
    const float width = static_cast<float>(SCREEN_WIDTH);
    const float height = static_cast<float>(SCREEN_HEIGHT);
    frame_constants.screen_size = { width, height, 1.0f / width, 1.0f / height };
    frame_constants.camera_planes = { near_plane, far_plane, tan_half_fov_y, aspect };
    // z is accumulated effect/composer time. Golden capture passes elapsed_time=0,
    // so visual regression remains deterministic instead of advancing while capturing.
    if (advance_effect_time) shader_composer_time += (std::max)(0.0f, elapsed_time);
    frame_constants.frame_params = { static_cast<float>(frame_index), elapsed_time, shader_composer_time, 0.0f };

    // TAAのジッター量(NDC)。射影行列へ加算済みの値をそのまま共有し、
    // モーションベクター側で打ち消せるようにしておく。
    frame_constants.jitter = { taa_jitter_ndc.x, taa_jitter_ndc.y,
        previous_taa_jitter_ndc.x, previous_taa_jitter_ndc.y };

    // メッシュ側がモーションベクターを書くために必要なフレーム共通の情報。
    motion_vectors::FrameContext& motion_frame = motion_vectors::Frame();
    motion_frame.previous_view_projection = previous_view_projection;
    motion_frame.current_jitter = taa_jitter_ndc;
    motion_frame.previous_jitter = previous_taa_jitter_ndc;
    motion_frame.enabled = previous_view_projection_valid;
    motion_frame.frame_id = frame_index + 1; // 0は「未描画」を表すため使わない

    immediate_context->UpdateSubresource(frame_constants_cb.Get(), 0, nullptr,
        &frame_constants, 0, 0);
    // b4はこのフレーム定数の専用スロット。他のパスが上書きしないため、
    // フレーム先頭で一度貼れば SSAO/SSR/TAA/タイルド照明すべてから読める。
    ID3D11Buffer* buffers[1]{ frame_constants_cb.Get() };
    immediate_context->PSSetConstantBuffers(4, 1, buffers);
    immediate_context->CSSetConstantBuffers(4, 1, buffers);
}

// 【マテリアルが唯一の真実】
//
// 以前はここでグローバルフラグ（use_pbr_skin / enable_toon_shader など）を
// 見て、false なら nullptr を返したり SHADING_MODEL_UNLIT へ降格させていた。
//
// その結果、
//   「描画確認」タブのチェックを外す
//     -> 全マテリアルの指定が無視されて Unlit になる
//     -> 画面にもログにも理由が出ない
// という状態になっていた。マテリアルでトゥーンを選んだのに
// 反映されない原因がこれ。
//
// Unity ではマテリアルが指定した絵柄が必ず使われる。
// グローバルなスイッチがマテリアルを黙って上書きすることはない。
// ここもそれに合わせ、**指定された絵柄をそのまま返す**。
//
// フラグは描画を止める役目をやめ、診断表示だけに使う
// （framework_editor.cpp の draw_runtime_mode_banner で警告を出す）。

ID3D11PixelShader* framework::skinned_forward_shader(int shading) const
{
    // nullptrは各メッシュが持つ標準ピクセルシェーダーを使う指定になる。
    switch (shading)
    {
    case SHADING_MODEL_PBR:      return pbr.skinned_mesh_ps();
    case SHADING_MODEL_TOON:     return toon.skinned_mesh_ps();
    case SHADING_MODEL_UNLIT:    return skinned_mesh_unlit_ps.Get();
    case SHADING_MODEL_PIXELATE: return object_pixelate_ps.Get();
    default:                     return nullptr;
    }
}

ID3D11PixelShader* framework::static_forward_shader(int shading) const
{
    switch (shading)
    {
    case SHADING_MODEL_PBR:      return pbr.static_mesh_ps();
    case SHADING_MODEL_TOON:     return toon.static_mesh_ps();
    case SHADING_MODEL_UNLIT:    return static_mesh_unlit_ps.Get();
    case SHADING_MODEL_PIXELATE: return object_pixelate_ps.Get();
    default:                     return nullptr;
    }
}

ReplayEngine::Rendering::ShaderLightingModel framework::deferred_lighting_model(
    int shading) const
{
    using ReplayEngine::Rendering::ShaderLightingModel;

    ShaderLightingModel model = ShaderLightingModel::Pbr;
    if (ReplayEngine::Rendering::BuiltInShaders::
        TryGetLightingModelFromShadingModel(shading, model))
    {
        return model;
    }

    // 旧データの不明値は従来どおり Unlit へ落とす。
    // MaterialにShader GUIDがある場合は resolve_render_item_material() が
    // Catalogの replay_lighting で上書きする。
    return ShaderLightingModel::Unlit;
}

void framework::bind_gbuffer_material(
    ReplayEngine::Rendering::ShaderLightingModel lighting_model,
    bool stage_surface, bool pixelate_enabled,
    float pixelate_size, float pixelate_strength, float metallic, float roughness,
    float ambient_occlusion, float emissive_strength,
    const DirectX::XMFLOAT4& base_color_factor,
    const DirectX::XMFLOAT3& emissive_color,
    std::uint32_t texture_mask,
    bool receive_shadow)
{
    material_override_constants constants{};
    constants.base_color_factor = base_color_factor;
    constants.emissive_factor = DirectX::XMFLOAT4{
        emissive_color.x, emissive_color.y, emissive_color.z, emissive_strength };
    constants.mat_params = stage_surface
        ? DirectX::XMFLOAT4{ 0.0f, 0.88f, 1.0f, 0.0f }
        : DirectX::XMFLOAT4{ metallic, roughness, ambient_occlusion, 0.0f };
    constants.lighting_model = static_cast<unsigned int>(lighting_model);
    constants.texture_contrast = stage_surface ? stage_texture_contrast : 1.0f;
    constants.pixelate_size = pixelate_enabled ? pixelate_size : 0.0f;
    constants.pixelate_strength = pixelate_enabled ? pixelate_strength : 0.0f;
    constants.texture_mask = texture_mask;
    constants.receive_shadow = receive_shadow ? 1.0f : 0.0f;

    immediate_context->UpdateSubresource(material_override_cb.Get(), 0,
        nullptr, &constants, 0, 0);
    immediate_context->PSSetConstantBuffers(9, 1,
        material_override_cb.GetAddressOf());
}

void framework::render(float elapsed_time)
{
    // render() の処理順・ローカル状態を変えず、連続断片を内部 .inl へ移動する。
#include "framework_render_setup.inl"
#include "framework_render_scene_setup.inl"
#include "framework_render_deferred.inl"
#include "framework_render_forward.inl"
#include "framework_render_effects.inl"
#include "framework_render_present.inl"
}
