#include "framework.h"
#include "gltf_model.h"
#include "skinned_mesh.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

using namespace ReplayEngine::VertexPaint;
using namespace ReplayEngine::Assets;
using namespace ReplayEngine::Rendering::DX12;
using namespace DirectX;

namespace
{
    bool SameColors(const VertexColorAsset& a, const VertexColorAsset& b)
    {
        if (a.meshes.size() != b.meshes.size()) return false;
        for (std::size_t i = 0; i < a.meshes.size(); ++i)
        {
            const auto& x = a.meshes[i];
            const auto& y = b.meshes[i];
            if (x.mesh_index != y.mesh_index || !(x.fingerprint == y.fingerprint) ||
                x.colors.size() != y.colors.size()) return false;
            if (!x.colors.empty() && std::memcmp(x.colors.data(), y.colors.data(), x.colors.size() * sizeof(VertexColorRgba8)) != 0)
                return false;
        }
        return true;
    }
}

bool framework::vertex_paint_active() const
{
    return vertex_paint_enabled && editor_mode && edit_mode_active &&
        active_editor_workspace == editor_workspace::modeling &&
        active_editor_view == editor_view::scene && !object_scene_play_mode && object_editor_context.CanEdit();
}

bool framework::prepare_vertex_paint_model(const ReplayEngine::Rendering::RenderItem& item)
{
    if (item.mesh_asset.empty() || item.mesh_asset.rfind("builtin:", 0) == 0)
    {
        vertex_paint_status = u8"モデルファイルを持つ MeshRenderer / SkinnedMeshRenderer を選択してください。";
        return false;
    }
    auto found = vertex_paint_models.find(item.mesh_asset);
    if (found != vertex_paint_models.end() && found->second.skinned == item.skinned) return true;
    vertex_paint_model model;
    std::string reason;
    if (resolve_model_source(item.mesh_asset, model.path, reason) == model_source_format::unsupported)
    {
        vertex_paint_status = reason;
        return false;
    }
    model.skinned = item.skinned;
    std::vector<VertexColorFingerprint> expected;
    const auto append = [&](const auto& meshes)
    {
        for (std::size_t i = 0; i < meshes.size(); ++i)
        {
            const auto& input = meshes[i];
            if (input.vertices.size() > UINT32_MAX || input.indices.size() > UINT32_MAX) return false;
            vertex_paint_mesh mesh;
            mesh.mesh_index = static_cast<std::uint32_t>(i);
            mesh.fingerprint = VertexColorAsset::Fingerprint(input.vertices.empty() ? nullptr : &input.vertices.front().position,
                static_cast<std::uint32_t>(input.vertices.size()), sizeof(input.vertices.front()),
                static_cast<std::uint32_t>(input.indices.size()));
            for (const auto& vertex : input.vertices) mesh.bind_positions.push_back(vertex.position);
            if (!mesh.bind_positions.empty() && !mesh.surface.Initialize(mesh.bind_positions, input.indices)) return false;
            expected.push_back(mesh.fingerprint);
            model.meshes.push_back(std::move(mesh));
        }
        return !meshes.empty();
    };
    bool valid = false;
    gltf_model* gltf = item.skinned ? nullptr : resolve_object_gltf(item.mesh_asset);
    if (gltf && !gltf->HasSkins() && !gltf->HasAnimations())
    {
        std::vector<gltf_model::StaticPrimitiveExport> exported;
        model.suffix = "#gltf:";
        valid = gltf->ExportStaticPrimitives(exported) && append(exported);
    }
    else if (skinned_mesh* mesh = resolve_object_mesh(item.mesh_asset))
    {
        model.suffix = item.skinned ? "#skinned:" : "#mesh:";
        valid = append(mesh->meshes);
    }
    if (!valid)
    {
        vertex_paint_status = u8"CPU メッシュを取得できないか、頂点・インデックスが不正です。";
        return false;
    }
    model.asset = found != vertex_paint_models.end() ? found->second.asset : std::make_shared<PaintAsset>();
    if (found == vertex_paint_models.end())
    {
        VertexColorAsset loaded;
        const auto sidecar = VertexColorAsset::SidecarPath(model.path);
        std::error_code ec;
        if (std::filesystem::exists(sidecar, ec))
        {
            if (!VertexColorAsset::LoadFromFile(sidecar, expected, loaded, vertex_paint_status)) return false;
        }
        else if (ec) { vertex_paint_status = ec.message(); return false; }
        else if (item.vertex_colors) loaded = *item.vertex_colors;
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            VertexColorMeshBlock block;
            block.mesh_index = static_cast<std::uint32_t>(i);
            block.fingerprint = expected[i];
            if (const auto* existing = loaded.FindMesh(block.mesh_index, block.fingerprint)) block.colors = existing->colors;
            else block.colors.resize(block.fingerprint.vertex_count);
            model.asset->colors.meshes.push_back(std::move(block));
        }
    }
    else
    {
        for (std::size_t i = 0; i < expected.size(); ++i)
            if (!model.asset->colors.FindMesh(static_cast<std::uint32_t>(i), expected[i]))
            {
                vertex_paint_status = u8"描画形式の切替で頂点番号が変わったため、ペイントを中止しました。";
                return false;
            }
    }
    vertex_paint_models.insert_or_assign(item.mesh_asset, std::move(model));
    vertex_paint_status = u8"白は１、黒は０。Scene View の左ドラッグで選択モデルを塗ります。";
    return true;
}

void framework::submit_vertex_paint(D3D12StaticSceneSubmission& submission,
    const ReplayEngine::Rendering::RenderItemList& items, bool capture)
{
    capture = capture && editor_mode && ImGui::GetCurrentContext() != nullptr;
    const bool preview = capture && vertex_paint_active();
    if (capture)
    {
        vertex_paint_frame = ImGui::GetFrameCount();
        vertex_paint_owner = 0;
        for (auto& model : vertex_paint_models)
            for (auto& mesh : model.second.meshes) mesh.visible = false;
        if (preview)
            for (const auto& item : items.Items())
                if (item.owner == object_editor_context.Selection().Primary())
                {
                    if (prepare_vertex_paint_model(item))
                    {
                        vertex_paint_owner = item.owner.Value();
                        vertex_paint_target = item.mesh_asset;
                    }
                    break;
                }
    }
    std::unordered_set<std::string> submitted;
    const auto update = [&](const D3D12StaticDrawItem& draw, bool skinned)
    {
        for (auto& entry : vertex_paint_models)
        {
            auto& model = entry.second;
            std::string prefix = entry.first + (skinned ? "#skinned:" : model.suffix == "#gltf:" ? "#gltf:" : "#mesh:");
            if (draw.mesh_key.rfind(prefix, 0) != 0) continue;
            for (auto& mesh : model.meshes)
            {
                if (draw.mesh_key != prefix + std::to_string(mesh.mesh_index)) continue;
                if (model.snapshot_revision != model.asset->revision)
                {
                    model.snapshot = std::make_shared<const VertexColorAsset>(model.asset->colors);
                    model.snapshot_revision = model.asset->revision;
                }
                if (submitted.insert(draw.mesh_key).second)
                    submission.vertex_color_updates.push_back({ draw.mesh_key, model.snapshot,
                        mesh.mesh_index, model.asset->revision, skinned });
                if (preview && draw.owner_id == vertex_paint_owner && entry.first == vertex_paint_target && !skinned)
                    mesh.visible = mesh.surface.SetPositions(mesh.bind_positions, draw.world);
                break;
            }
            break;
        }
    };
    for (const auto& draw : submission.draws) update(draw, false);
    for (const auto& draw : submission.skinned_draws)
    {
        update(draw.surface, true);
        if (!preview || draw.surface.owner_id != vertex_paint_owner || !draw.bone_palette) continue;
        auto model = vertex_paint_models.find(vertex_paint_target);
        if (model == vertex_paint_models.end()) continue;
        auto* source = resolve_object_mesh(vertex_paint_target);
        if (!source) continue;
        for (auto& mesh : model->second.meshes)
        {
            if (mesh.visible || draw.surface.mesh_key != vertex_paint_target + "#skinned:" + std::to_string(mesh.mesh_index) ||
                mesh.mesh_index >= source->meshes.size()) continue;
            const auto& vertices = source->meshes[mesh.mesh_index].vertices;
            if (vertices.size() != mesh.bind_positions.size()) continue;
            std::vector<XMFLOAT3> positions;
            positions.reserve(vertices.size());
            const auto world = XMLoadFloat4x4(&draw.surface.world);
            bool valid = true;
            for (const auto& vertex : vertices)
            {
                const auto p = XMVectorSetW(XMLoadFloat3(&vertex.position) +
                    XMLoadFloat3(&vertex.morph_position) * draw.morph_weight, 1.0f);
                auto skinned = XMVectorZero();
                for (std::uint32_t influence = 0; influence < 4; ++influence)
                {
                    const auto bone = vertex.bone_indices[influence];
                    if (bone >= draw.bone_palette->size()) { valid = false; break; }
                    skinned += XMVector4Transform(p, XMLoadFloat4x4(&(*draw.bone_palette)[bone])) * vertex.bone_weights[influence];
                }
                if (!valid) break;
                XMFLOAT3 position;
                XMStoreFloat3(&position, XMVector4Transform(skinned, world));
                positions.push_back(position);
            }
            XMFLOAT4X4 identity;
            XMStoreFloat4x4(&identity, XMMatrixIdentity());
            mesh.visible = valid && mesh.surface.SetPositions(positions, identity);
        }
    }
    if (preview && vertex_paint_owner != 0)
    {
        submission.vertex_color_debug_channel = static_cast<std::uint32_t>(vertex_paint_brush.channel + 1);
        const auto clear_material_preview = [](D3D12StaticDrawItem& draw)
        {
            draw.shader_key.clear();
            draw.layer_passes.clear();
        };
        for (auto& draw : submission.draws) clear_material_preview(draw);
        for (auto& draw : submission.skinned_draws) clear_material_preview(draw.surface);
    }
}

void framework::finish_vertex_paint_stroke(bool cancel)
{
    if (!vertex_paint_stroke) return;
    auto command = std::move(vertex_paint_stroke);
    vertex_paint_values.clear();
    if (cancel || !object_editor_context.CanEdit())
    {
        if (!SameColors(command->before, command->target->colors)) command->Apply(false);
        command->target->dirty = vertex_paint_dirty_before;
        return;
    }
    command->after = command->target->colors;
    if (!SameColors(command->before, command->after)) object_editor_context.CommitVertexColorEdit(std::move(command));
}

bool framework::handle_vertex_paint_viewport()
{
    const bool active = vertex_paint_active();
    if (vertex_paint_stroke && (!active || !ImGui::IsMouseDown(ImGuiMouseButton_Left)))
        finish_vertex_paint_stroke(false);
    if (!active) return false;
    viewport_drag_selecting = false;
    if (ImGui::IsKeyPressed(VK_ESCAPE))
    {
        finish_vertex_paint_stroke(true);
        vertex_paint_enabled = false;
        return true;
    }
    if (vertex_paint_owner != object_editor_context.Selection().Primary().Value() ||
        (vertex_paint_stroke && vertex_paint_stroke_owner != vertex_paint_owner))
    {
        finish_vertex_paint_stroke(false);
        return true;
    }
    if (!scene_view_hovered || editor_camera_consumed_input || ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyAlt ||
        ImGui::IsMouseDown(ImGuiMouseButton_Right) || vertex_paint_frame < ImGui::GetFrameCount() - 1) return true;
    auto found = vertex_paint_models.find(vertex_paint_target);
    if (found == vertex_paint_models.end() || vertex_paint_owner == 0) return true;
    auto& model = found->second;
    if (vertex_paint_stroke && vertex_paint_stroke->target != model.asset)
    {
        finish_vertex_paint_stroke(false);
        return true;
    }
    const auto mouse = ImGui::GetMousePos();
    const auto ray = viewport_picking_ray(mouse.x - scene_view_min_x, mouse.y - scene_view_min_y);
    RayHit nearest;
    float distance = 1000000.0f;
    vertex_paint_mesh* picked = nullptr;
    for (auto& mesh : model.meshes)
    {
        if (!mesh.visible) continue;
        const auto hit = mesh.surface.Raycast(ray.origin, ray.direction, distance);
        if (hit.hit) { nearest = hit; distance = hit.distance; picked = &mesh; }
    }
    if (!picked) return true;
    ImGui::GetForegroundDrawList()->AddCircle(mouse, 6.0f, IM_COL32(255, 220, 60, 255), 16, 2.0f);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gizmo_gesture_active())
    {
        vertex_paint_stroke = std::make_unique<ColorEdit>();
        vertex_paint_stroke->target = model.asset;
        vertex_paint_stroke_owner = vertex_paint_owner;
        vertex_paint_dirty_before = model.asset->dirty;
        vertex_paint_stroke->before = model.asset->colors;
        vertex_paint_values.resize(model.meshes.size());
    }
    if (!vertex_paint_stroke || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) return true;
    const auto region = picked->surface.QuerySurface(nearest.position, vertex_paint_brush.radius, nearest.face);
    const auto index = picked->mesh_index;
    if (index >= model.asset->colors.meshes.size() || index >= vertex_paint_values.size()) return true;
    if (picked->surface.Paint(model.asset->colors.meshes[index].colors, region, nearest.position,
        vertex_paint_brush, (std::min)(ImGui::GetIO().DeltaTime, 0.1f), vertex_paint_values[index]))
    {
        ++model.asset->revision;
        model.asset->dirty = true;
    }
    vertex_paint_status = u8"対象頂点: " + std::to_string(region.vertices.size()) + u8" / 面: " + std::to_string(nearest.face);
    return true;
}

bool framework::save_vertex_paint(bool reload)
{
    finish_vertex_paint_stroke(false);
    const auto found = vertex_paint_models.find(vertex_paint_target);
    if (found == vertex_paint_models.end() || vertex_paint_owner != object_editor_context.Selection().Primary().Value() ||
        !object_editor_context.CanEdit()) return false;
    auto& model = found->second;
    const auto path = VertexColorAsset::SidecarPath(model.path);
    if (reload)
    {
        std::vector<VertexColorFingerprint> expected;
        for (const auto& mesh : model.meshes) expected.push_back(mesh.fingerprint);
        VertexColorAsset loaded;
        if (!VertexColorAsset::LoadFromFile(path, expected, loaded, vertex_paint_status)) return false;
        VertexColorAsset complete = model.asset->colors;
        for (auto& block : complete.meshes)
        {
            if (const auto* saved = loaded.FindMesh(block.mesh_index, block.fingerprint)) block.colors = saved->colors;
            else block.colors.assign(block.fingerprint.vertex_count, {});
        }
        auto command = std::make_unique<ColorEdit>();
        command->target = model.asset;
        command->before = model.asset->colors;
        command->after = std::move(complete);
        if (!SameColors(command->before, command->after))
        {
            command->Apply(true);
            object_editor_context.CommitVertexColorEdit(std::move(command));
        }
        model.asset->dirty = false;
        vertex_paint_status = u8"再読込しました: " + path.u8string();
        return true;
    }
    if (!VertexColorAsset::SaveToFile(path, model.asset->colors, vertex_paint_status)) return false;
    std::vector<VertexColorFingerprint> expected;
    for (const auto& mesh : model.meshes) expected.push_back(mesh.fingerprint);
    VertexColorAsset verified;
    if (!VertexColorAsset::LoadFromFile(path, expected, verified, vertex_paint_status)) return false;
    if (!SameColors(model.asset->colors, verified))
    {
        vertex_paint_status = u8"保存後の色照合に失敗しました。";
        return false;
    }
    model.asset->dirty = false;
    vertex_paint_status = u8"保存・色一致を確認しました: " + path.u8string();
    return true;
}

void framework::draw_vertex_paint_panel()
{
    ImGui::Separator();
    if (ImGui::Checkbox(u8"頂点カラーペイント", &vertex_paint_enabled))
    {
        finish_vertex_paint_stroke(false);
        if (vertex_paint_enabled && landscape_stroke_transaction)
        {
            vertex_paint_enabled = false;
            vertex_paint_status = u8"Landscape のストロークを終了してから切り替えてください。";
        }
        if (vertex_paint_enabled && gizmo_gesture_active()) cancel_gizmo_gesture();
    }
    int channel = vertex_paint_brush.channel;
    int mode = static_cast<int>(vertex_paint_brush.mode);
    if (ImGui::Combo(u8"チャンネル", &channel, "R\0G\0B\0A\0"))
    {
        finish_vertex_paint_stroke(false);
        vertex_paint_brush.channel = channel;
    }
    if (ImGui::Combo(u8"モード", &mode, u8"加算\0減算\0一定値で置換\0ならし\0"))
    {
        finish_vertex_paint_stroke(false);
        vertex_paint_brush.mode = static_cast<PaintMode>(mode);
    }
    ImGui::DragFloat(u8"半径（ワールド）", &vertex_paint_brush.radius, 0.01f, 0.001f, 10000.0f, "%.3f");
    ImGui::SliderFloat(u8"強さ（毎秒）", &vertex_paint_brush.strength, 0.0f, 10.0f);
    ImGui::SliderFloat(u8"減衰", &vertex_paint_brush.falloff, 0.01f, 8.0f);
    if (vertex_paint_brush.mode == PaintMode::Replace)
        ImGui::SliderFloat(u8"置換値", &vertex_paint_brush.value, 0.0f, 1.0f);
    const auto found = vertex_paint_models.find(vertex_paint_target);
    const bool ready = found != vertex_paint_models.end() && vertex_paint_owner != 0 &&
        vertex_paint_owner == object_editor_context.Selection().Primary().Value() && object_editor_context.CanEdit();
    if (ready)
    {
        ImGui::TextWrapped(u8"保存先: %s", VertexColorAsset::SidecarPath(found->second.path).u8string().c_str());
        ImGui::TextUnformatted(found->second.asset->dirty ? u8"未保存の変更あり" : u8"変更なし");
        if (ImGui::Button(u8"頂点カラーを保存")) save_vertex_paint(false);
        ImGui::SameLine();
        if (ImGui::Button(u8"頂点カラーを再読込")) save_vertex_paint(true);
    }
    else ImGui::TextDisabled(u8"モデルを選択し、ペイントをオンにしてください。");
    ImGui::TextWrapped(u8"選択モデルを左ドラッグ。白=1 / 黒=0。Esc はストローク取消・終了。同じモデル資産の全インスタンスで色を共有します。");
    ImGui::TextWrapped("%s", vertex_paint_status.c_str());
}
