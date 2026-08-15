#include "misc.h"
#include "skinned_mesh.h"
#include<fstream>
#include <sstream>
#include <functional>
#include <algorithm>
#include"shader.h"
#include"static_mesh.h"
#include"sprite_batch.h"

#include"texture.h"
#include"../render/motion_vector_context.h"
#include"../../RePlayEngine/Rendering/RenderStats.h"
#include <cstring>
#include <filesystem>
#include <stdexcept>
using namespace DirectX;

void skinned_mesh::render(ID3D11DeviceContext* immediate_context,
    const XMFLOAT4X4& world, const XMFLOAT4& material_color,
    const animation::keyframe* keyframe,
    ID3D11PixelShader* alternative_pixel_shader,
    ID3D11VertexShader* alternative_vertex_shader,
    ID3D11InputLayout* alternative_input_layout,
    bool bind_pixel_shader,
    bool write_motion_vectors,
    bool use_embedded_gltf_materials)
{
    const motion_vectors::FrameContext& motion_frame = motion_vectors::Frame();
    const bool emit_motion = write_motion_vectors && motion_object_constant_buffer &&
        motion_bone_constant_buffer;

    for (mesh& mesh : meshes)
    {
        uint32_t stride{ sizeof(vertex) };
        uint32_t offset{ 0 };
        immediate_context->IASetVertexBuffers(0, 1, mesh.vertex_buffer.GetAddressOf(), &stride, &offset);
        immediate_context->IASetIndexBuffer(mesh.index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        immediate_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        immediate_context->IASetInputLayout(alternative_input_layout ? alternative_input_layout : input_layout.Get());

        immediate_context->VSSetShader(alternative_vertex_shader ? alternative_vertex_shader : vertex_shader.Get(), nullptr, 0);
        if (bind_pixel_shader)
        {
            immediate_context->PSSetShader(alternative_pixel_shader ? alternative_pixel_shader : pixel_shader.Get(), nullptr, 0);
        }
        else
        {
            immediate_context->PSSetShader(nullptr, nullptr, 0);
        }

        constants data{};

        if (keyframe && keyframe->nodes.size() > 0)
        {
            const animation::keyframe::node& mesh_node{ keyframe->nodes.at(mesh.node_index) };
            XMStoreFloat4x4(&data.world, XMLoadFloat4x4(&mesh_node.global_transform) * XMLoadFloat4x4(&world));

            const size_t bone_count{ mesh.bind_pose.bones.size() };
            _ASSERT_EXPR(bone_count < MAX_BONES, L"The value of the 'bone_count' has exceeded MAX_BONES.");

            for (size_t bone_index = 0; bone_index < bone_count; ++bone_index)
            {
                const skeleton::bone& bone{ mesh.bind_pose.bones.at(bone_index) };
                const animation::keyframe::node& bone_node{ keyframe->nodes.at(bone.node_index) };

                // スキニング行列の計算: Offset * Global * Inverse(MeshGlobal)
                XMStoreFloat4x4(&data.bone_transforms[bone_index],
                    XMLoadFloat4x4(&bone.offset_transform) *
                    XMLoadFloat4x4(&bone_node.global_transform) *
                    XMMatrixInverse(nullptr, XMLoadFloat4x4(&mesh_node.global_transform))
                );
            }
        }
        else
        {
            XMStoreFloat4x4(&data.world, XMLoadFloat4x4(&mesh.default_global_transform) * XMLoadFloat4x4(&world));
            for (size_t bone_index = 0; bone_index < MAX_BONES; ++bone_index)
            {
                data.bone_transforms[bone_index] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
            }
        }

        if (emit_motion)
        {
            // 前フレームの姿勢をVSへ渡す。初回は今フレームの値を入れて動きゼロにする。
            const bool has_history = mesh.motion_history_valid &&
                mesh.previous_bone_transforms.size() == MAX_BONES;

            motion_vectors::ObjectConstants motion_object{};
            motion_object.previous_world = has_history ? mesh.previous_world : data.world;
            motion_object.previous_view_projection = motion_frame.previous_view_projection;
            motion_object.params = { motion_frame.enabled && has_history ? 1.0f : 0.0f,
                motion_frame.current_jitter.x, motion_frame.current_jitter.y, 0.0f };
            motion_object.params2 = { motion_frame.previous_jitter.x,
                motion_frame.previous_jitter.y, 0.0f, 0.0f };
            immediate_context->UpdateSubresource(
                motion_object_constant_buffer.Get(), 0, nullptr, &motion_object, 0, 0);
            immediate_context->VSSetConstantBuffers(
                6, 1, motion_object_constant_buffer.GetAddressOf());

            // PS へも同じ Buffer を渡す。
            // G-Buffer の Pixel Shader が compute_motion_vector() 経由で
            // b6 の motion_params を読むため、PS も 160 バイトを期待している。
            // VS だけに Bind すると PS の b6 にトゥーン材質 (80 バイト) が
            // 残り、毎フレーム定数バッファのサイズ不一致警告が出る。
            immediate_context->PSSetConstantBuffers(
                6, 1, motion_object_constant_buffer.GetAddressOf());

            motion_bone_constants motion_bones{};
            if (has_history)
            {
                std::memcpy(motion_bones.bone_transforms,
                    mesh.previous_bone_transforms.data(),
                    sizeof(DirectX::XMFLOAT4X4) * MAX_BONES);
            }
            else
            {
                std::memcpy(motion_bones.bone_transforms, data.bone_transforms,
                    sizeof(DirectX::XMFLOAT4X4) * MAX_BONES);
            }
            immediate_context->UpdateSubresource(
                motion_bone_constant_buffer.Get(), 0, nullptr, &motion_bones, 0, 0);
            immediate_context->VSSetConstantBuffers(
                8, 1, motion_bone_constant_buffer.GetAddressOf());

            // 同一フレーム内で二度呼ばれても履歴は一度だけ進める。
            if (mesh.motion_frame_id != motion_frame.frame_id)
            {
                mesh.motion_frame_id = motion_frame.frame_id;
                mesh.previous_world = data.world;
                mesh.previous_bone_transforms.resize(MAX_BONES);
                std::memcpy(mesh.previous_bone_transforms.data(), data.bone_transforms,
                    sizeof(DirectX::XMFLOAT4X4) * MAX_BONES);
                mesh.motion_history_valid = true;
            }
        }

        for (const mesh::subset& subset : mesh.subsets)
        {
            const material& material{ materials.at(subset.material_unique_id) };
            XMStoreFloat4(&data.material_color, XMLoadFloat4(&material_color) * XMLoadFloat4(&material.Kd));
            data.gltf_pbr = { 0.0f, 0.55f, 1.0f, 0.0f };
            data.gltf_emissive = { 0.0f, 0.0f, 0.0f, 0.0f };
            data.gltf_alpha = { 0.0f, 0.5f, 0.0f, 1.0f };
            if (use_embedded_gltf_materials)
            {
                if (const gltf_material_info* info = GltfMaterial(subset.material_unique_id))
                {
                    data.gltf_pbr = { info->metallic, info->roughness,
                        info->occlusion, 1.0f };
                    data.gltf_emissive = { info->emissive.x, info->emissive.y,
                        info->emissive.z, info->emissive_strength };
                    data.gltf_alpha = { static_cast<float>(info->alpha_mode),
                        info->alpha_cutoff, info->unlit ? 1.0f : 0.0f,
                        info->normal_scale };
                }
            }

            immediate_context->UpdateSubresource(constant_buffer.Get(), 0, 0, &data, 0, 0);
            immediate_context->VSSetConstantBuffers(0, 1, constant_buffer.GetAddressOf());
            immediate_context->PSSetConstantBuffers(0, 1, constant_buffer.GetAddressOf());

            ID3D11ShaderResourceView* texture_views[4]{
                material.shader_resource_views[0].Get(), material.shader_resource_views[1].Get(),
                material.shader_resource_views[2].Get(), material.shader_resource_views[3].Get() };
            immediate_context->PSSetShaderResources(0, 4, texture_views);

            ReplayEngine::Rendering::Stats().CountDrawIndexed(
                subset.index_count, static_cast<uint32_t>(mesh.vertices.size()));
            immediate_context->DrawIndexed(subset.index_count, subset.start_index_location, 0);
        }
    }
}
