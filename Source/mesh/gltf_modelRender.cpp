#include "gltf_model.h"

#include "../render/motion_vector_context.h"

#include "../../RePlayEngine/Rendering/RenderStats.h"
#include "../../RePlayEngine/Rendering/Frustum.h"

#include <algorithm>

using namespace DirectX;

void gltf_model::render(ID3D11DeviceContext* context, const XMFLOAT4X4& world,
    const XMFLOAT4& tint, ID3D11PixelShader* alternative_pixel_shader,
    bool write_motion_vectors, bool depth_only)
{
    if (!loaded_ || !context) return;
    const motion_vectors::FrameContext& motion_frame = motion_vectors::Frame();
    const bool emit_motion = write_motion_vectors && motion_object_constant_buffer_ && !depth_only;
    const bool advance_motion_history =
        emit_motion && motion_frame_id_ != motion_frame.frame_id;
    if (emit_motion) previous_primitive_worlds_.resize(primitives_.size());
    size_t motion_primitive_index = 0;
    ReplayEngine::Rendering::Stats().TrackStateSet(
        ReplayEngine::Rendering::RenderStats::StateKind::InputLayout,
        input_layout_.Get());
    context->IASetInputLayout(input_layout_.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
    context->VSSetShader(vertex_shader_.Get(), nullptr, 0);
    // 深度プリパスではピクセルシェーダーを外す。これがオーバードロー削減の要。
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
    context->PSSetShader(depth_only ? nullptr
        : (alternative_pixel_shader ? alternative_pixel_shader : pixel_shader_.Get()), nullptr, 0);
    auto& culling = ReplayEngine::Rendering::Culling();
    // 生成スレッドが書き終わったかを1回だけ読む。trueになった後は
    // lods は変更されないため、以降のループ内はロック不要で安全。
    const bool lods_ready = lods_ready_.load();
    // 統計表示用。生成中かどうかと、使えるLOD段数を申告する。
    if (!lods_ready) culling.lod_building = true;
    else
    {
        for (const auto& primitive : primitives_)
        {
            culling.lod_available = (std::max)(culling.lod_available,
                static_cast<unsigned int>(primitive.lods.size()));
        }
    }
    for (const auto& primitive : primitives_)
    {
        // 視錐台の外にあるプリミティブは丸ごと飛ばす。Sponzaは405個の
        // プリミティブに分かれているため、これだけで頂点処理とドローコールが
        // 大幅に減る(通過率40%台=6割が無駄になっていた)。
        if (culling.enabled && culling.frustum.Valid())
        {
            ++culling.tested;
            if (!culling.frustum.IntersectsTransformedAabb(
                primitive.bounds_minimum, primitive.bounds_maximum, world))
            {
                ++culling.culled;
                // モーションベクターの履歴は描画しなくても進めないと、
                // 画面へ戻ってきた瞬間に誤った速度が出る。
                if (emit_motion)
                {
                    if (advance_motion_history &&
                        motion_primitive_index < previous_primitive_worlds_.size())
                    {
                        XMFLOAT4X4 skipped_world{};
                        XMStoreFloat4x4(&skipped_world,
                            XMLoadFloat4x4(&primitive.node_transform) * XMLoadFloat4x4(&world));
                        previous_primitive_worlds_[motion_primitive_index] = skipped_world;
                    }
                    ++motion_primitive_index;
                }
                continue;
            }
        }

        const Material* material = primitive.material >= 0 && primitive.material < static_cast<int>(materials_.size())
            ? &materials_[primitive.material] : &materials_[0];

        // 画面上の投影サイズからLODを選ぶ。遠くの小さいプリミティブは
        // 粗いメッシュへ差し替えて頂点処理を削る。
        // 生成中(lods_ready_==false)はLOD0で描く。
        const int lod_level = lods_ready
            ? culling.SelectLod(primitive.bounds_minimum,
                primitive.bounds_maximum, world, primitive.lods.size())
            : 0;
        culling.CountLodDraw(lod_level);

        ID3D11Buffer* vertex_buffer = primitive.vertex_buffer.Get();
        ID3D11Buffer* index_buffer = primitive.index_buffer.Get();
        UINT draw_index_count = primitive.index_count;
        UINT draw_vertex_count = primitive.vertex_count;
        if (lod_level > 0 && lod_level <= static_cast<int>(primitive.lods.size()))
        {
            const LodLevel& lod = primitive.lods[lod_level - 1];
            if (lod.vertex_buffer && lod.index_buffer)
            {
                vertex_buffer = lod.vertex_buffer.Get();
                index_buffer = lod.index_buffer.Get();
                draw_index_count = lod.index_count;
                draw_vertex_count = lod.vertex_count;
            }
        }

        UINT stride = sizeof(Vertex), offset = 0;
        context->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);
        context->IASetIndexBuffer(index_buffer, DXGI_FORMAT_R32_UINT, 0);
        // t0=baseColor, t1=法線, t2=ORM。未設定のスロットは明示的にnullへ落として
        // 直前のマテリアルのテクスチャが残らないようにする。
        ID3D11ShaderResourceView* textures[3]{
            material->base_color_texture ? material->base_color_texture.Get()
                                         : white_texture_.Get(),
            material->normal_texture ? material->normal_texture.Get()
                                     : neutral_normal_texture_.Get(),
            material->occlusion_roughness_metalness_texture.Get() };
        if (!depth_only) context->PSSetShaderResources(0, 3, textures);
        Constants constants{};
        XMStoreFloat4x4(&constants.world,
            XMLoadFloat4x4(&primitive.node_transform) * XMLoadFloat4x4(&world));
        XMStoreFloat4(&constants.material_color,
            XMLoadFloat4(&tint) * XMLoadFloat4(&material->base_color));
        context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr, &constants, 0, 0);
        context->VSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
        context->PSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());

        if (emit_motion)
        {
            // 剛体なのでプリミティブごとの前フレームのワールド行列を渡すだけでよい。
            const bool has_history = motion_history_valid_ &&
                motion_primitive_index < previous_primitive_worlds_.size();
            motion_vectors::ObjectConstants motion_object{};
            motion_object.previous_world = has_history
                ? previous_primitive_worlds_[motion_primitive_index] : constants.world;
            motion_object.previous_view_projection = motion_frame.previous_view_projection;
            motion_object.params = { motion_frame.enabled && has_history ? 1.0f : 0.0f,
                motion_frame.current_jitter.x, motion_frame.current_jitter.y, 0.0f };
            motion_object.params2 = { motion_frame.previous_jitter.x,
                motion_frame.previous_jitter.y, 0.0f, 0.0f };
            context->UpdateSubresource(
                motion_object_constant_buffer_.Get(), 0, nullptr, &motion_object, 0, 0);
            context->VSSetConstantBuffers(
                6, 1, motion_object_constant_buffer_.GetAddressOf());

            // PS へも同じ Buffer を渡す。
            // G-Buffer の Pixel Shader が compute_motion_vector() 経由で
            // b6 の motion_params を読むため、PS も 160 バイトを期待している。
            context->PSSetConstantBuffers(
                6, 1, motion_object_constant_buffer_.GetAddressOf());

            if (advance_motion_history)
                previous_primitive_worlds_[motion_primitive_index] = constants.world;
            ++motion_primitive_index;
        }

        // 深度プリパスは同じ形状を二度数えないよう統計から除く。
        if (!depth_only)
            ReplayEngine::Rendering::Stats().CountDrawIndexed(
                draw_index_count, draw_vertex_count);
        context->DrawIndexed(draw_index_count, 0, 0);
    }

    // 同一フレーム内で二度呼ばれても履歴は一度だけ進める。
    if (advance_motion_history)
    {
        motion_frame_id_ = motion_frame.frame_id;
        motion_history_valid_ = true;
    }
}

bool gltf_model::HasAlphaMaskMaterials() const noexcept
{
    for (const Material& material : materials_)
    {
        if (material.alpha_mode != 0) return true;
    }
    return false;
}

void gltf_model::render_shadow(ID3D11DeviceContext* context, const XMFLOAT4X4& world,
    ID3D11VertexShader* caster_vertex_shader, ID3D11InputLayout* caster_input_layout,
    ID3D11PixelShader* alpha_clip_pixel_shader, ID3D11Buffer* alpha_constants,
    int override_alpha_mode, float override_alpha_cutoff,
    bool override_uses_replay_base_map, bool force_pixel_shader)
{
    if (!loaded_ || !context || !caster_vertex_shader) return;

    ID3D11InputLayout* selected_layout =
        caster_input_layout ? caster_input_layout : input_layout_.Get();
    ReplayEngine::Rendering::Stats().TrackStateSet(
        ReplayEngine::Rendering::RenderStats::StateKind::InputLayout, selected_layout);
    context->IASetInputLayout(selected_layout);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ReplayEngine::Rendering::Stats().CountStateSet(
        ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
    context->VSSetShader(caster_vertex_shader, nullptr, 0);

    const bool can_alpha_clip =
        alpha_clip_pixel_shader != nullptr && alpha_constants != nullptr;
    ID3D11PixelShader* bound_pixel_shader = nullptr;
    context->PSSetShader(nullptr, nullptr, 0);

    for (const auto& primitive : primitives_)
    {
        ID3D11Buffer* vertex_buffer = primitive.vertex_buffer.Get();
        ID3D11Buffer* index_buffer = primitive.index_buffer.Get();
        if (vertex_buffer == nullptr || index_buffer == nullptr) continue;

        const Material* material = primitive.material >= 0 &&
            primitive.material < static_cast<int>(materials_.size())
            ? &materials_[primitive.material] : &materials_[0];

        // Material Asset の指定があればそれを、無ければ glTF 内蔵の alphaMode を使う。
        const int alpha_mode = override_alpha_mode >= 0
            ? override_alpha_mode : material->alpha_mode;
        const float alpha_cutoff = override_alpha_mode >= 0
            ? override_alpha_cutoff : material->alpha_cutoff;
        const bool needs_alpha_clip = can_alpha_clip && alpha_mode != 0;
        const bool needs_pixel_shader = needs_alpha_clip ||
            (force_pixel_shader && alpha_clip_pixel_shader != nullptr);

        ID3D11PixelShader* wanted = needs_pixel_shader ? alpha_clip_pixel_shader : nullptr;
        if (wanted != bound_pixel_shader)
        {
            ReplayEngine::Rendering::Stats().CountStateSet(
                ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
            context->PSSetShader(wanted, nullptr, 0);
            bound_pixel_shader = wanted;
        }

        // 抜かない primitive でも PS を貼るときは、抜き方 0 の定数で上書きする。
        if (needs_pixel_shader && alpha_constants != nullptr)
        {
            const float constants[4] = { needs_alpha_clip ? 1.0f : 0.0f,
                alpha_mode == 1 ? alpha_cutoff : 0.01f,
                override_uses_replay_base_map ? 1.0f : 0.0f, 0.0f };
            context->UpdateSubresource(alpha_constants, 0, nullptr, constants, 0, 0);
            context->PSSetConstantBuffers(7, 1, &alpha_constants);
            ID3D11ShaderResourceView* base_color[1]{
                material->base_color_texture ? material->base_color_texture.Get()
                                             : white_texture_.Get() };
            context->PSSetShaderResources(0, 1, base_color);
        }

        // LOD は使わない。影だけ粗いメッシュになると輪郭が本体からずれる。
        UINT stride = sizeof(Vertex), offset = 0;
        context->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);
        context->IASetIndexBuffer(index_buffer, DXGI_FORMAT_R32_UINT, 0);

        Constants constants{};
        XMStoreFloat4x4(&constants.world,
            XMLoadFloat4x4(&primitive.node_transform) * XMLoadFloat4x4(&world));
        constants.material_color = { 1.0f, 1.0f, 1.0f, 1.0f };
        context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr, &constants, 0, 0);
        context->VSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());

        // 影の描画は画面に見えるジオメトリの統計へ混ぜない。
        context->DrawIndexed(primitive.index_count, 0, 0);
    }

    if (bound_pixel_shader != nullptr) context->PSSetShader(nullptr, nullptr, 0);
}
