// LineRendererComponent / TrailComponent の更新と透明 3D 描画を持つ。
// Mesh の GBuffer 経路は変更せず、これらを持つ Object だけが共通リボン描画へ入る。
#include "framework.h"

#include "../../../RePlayEngine/Components/Rendering/LineRendererComponent.h"
#include "../../../RePlayEngine/Components/Rendering/TrailComponent.h"

#include <vector>

namespace
{
    std::vector<DirectX::XMFLOAT3> TransformLinePoints(
        const std::vector<DirectX::XMFLOAT3>& points,
        const DirectX::XMFLOAT4X4& matrix)
    {
        std::vector<DirectX::XMFLOAT3> transformed;
        try
        {
            transformed.reserve(points.size());
            const DirectX::XMMATRIX transform = DirectX::XMLoadFloat4x4(&matrix);
            for (const DirectX::XMFLOAT3& point : points)
            {
                DirectX::XMFLOAT3 result{};
                DirectX::XMStoreFloat3(&result, DirectX::XMVector3TransformCoord(
                    DirectX::XMLoadFloat3(&point), transform));
                transformed.push_back(result);
            }
        }
        catch (...)
        {
            transformed.clear();
        }
        return transformed;
    }

    DirectX::XMFLOAT3 TransformOrigin(const DirectX::XMFLOAT4X4& matrix)
    {
        const DirectX::XMVECTOR origin = DirectX::XMVector3TransformCoord(
            DirectX::XMVectorZero(), DirectX::XMLoadFloat4x4(&matrix));
        DirectX::XMFLOAT3 result{};
        DirectX::XMStoreFloat3(&result, origin);
        return result;
    }
}

void framework::update_line_trails(float elapsed_time)
{
    ReplayEngine::Scene::Scene& scene = active_object_scene();
    for (std::size_t object_index = 0; object_index < scene.GameObjectCount();
        ++object_index)
    {
        ReplayEngine::Core::GameObject* object = scene.GameObjectAt(object_index);
        if (object == nullptr || object->PendingDestroy() ||
            !object->ActiveInHierarchy())
        {
            continue;
        }

        for (std::size_t component_index = 0;
            component_index < object->ComponentCount(); ++component_index)
        {
            auto* trail = dynamic_cast<ReplayEngine::Components::TrailComponent*>(
                object->ComponentAt(component_index));
            if (trail == nullptr || !trail->ActiveInHierarchy()) continue;

            // render() は Motion と Property Link、親子 Transform が確定した後に来る。
            // OnUpdate で採らず、ここで同フレームの最終位置を履歴へ足す。
            const DirectX::XMFLOAT3 position = trail->world_space
                ? TransformOrigin(object->GetTransform().WorldMatrixFloat4x4())
                : object->GetTransform().LocalPosition();
            trail->UpdateRuntime(elapsed_time, position);
        }
    }
}

void framework::draw_line_strokes()
{
    ReplayEngine::Scene::Scene& scene = active_object_scene();
    bool render_state_configured = false;
    const DirectX::XMFLOAT3 camera_position{
        frame_constants.camera_position.x,
        frame_constants.camera_position.y,
        frame_constants.camera_position.z };
    std::vector<DirectX::XMFLOAT3> path;
    std::vector<float> alpha;

    const auto configure_render_state = [&]()
    {
        if (render_state_configured) return;
        immediate_context->OMSetBlendState(
            blend_states[(size_t)BLEND_STATE::ALPHA].Get(), nullptr, 0xFFFFFFFF);
        immediate_context->OMSetDepthStencilState(
            depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_OFF].Get(), 0);
        immediate_context->RSSetState(
            rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
        render_state_configured = true;
    };

    for (std::size_t object_index = 0; object_index < scene.GameObjectCount();
        ++object_index)
    {
        ReplayEngine::Core::GameObject* object = scene.GameObjectAt(object_index);
        if (object == nullptr || object->PendingDestroy() ||
            !object->ActiveInHierarchy())
        {
            continue;
        }

        for (std::size_t component_index = 0;
            component_index < object->ComponentCount(); ++component_index)
        {
            ReplayEngine::Core::Component* component =
                object->ComponentAt(component_index);
            if (auto* line = dynamic_cast<ReplayEngine::Components::LineRendererComponent*>(
                component))
            {
                if (!line->ActiveInHierarchy() || line->points.size() < 2) continue;
                path = ReplayEngine::Rendering::BuildCatmullRomLinePath(
                    line->points, line->smoothing, line->closed);
                if (path.size() < 2) continue;
                path = TransformLinePoints(path,
                    object->GetTransform().WorldMatrixFloat4x4());
                if (path.size() < 2) continue;
                try
                {
                    alpha.assign(path.size(), 1.0f);
                }
                catch (...)
                {
                    alpha.clear();
                    continue;
                }
                configure_render_state();
                line_stroke_renderer.Draw(device.Get(), immediate_context.Get(),
                    &asset_database,
                    sampler_states[(size_t)SAMPLER_STATE::LINEAR].Get(),
                    path, alpha, line->StrokeStyle(), camera_position);
                continue;
            }

            auto* trail = dynamic_cast<ReplayEngine::Components::TrailComponent*>(component);
            if (trail == nullptr || !trail->ActiveInHierarchy()) continue;
            trail->RuntimePath(path, alpha);
            if (path.size() < 2) continue;
            if (!trail->world_space)
            {
                DirectX::XMFLOAT4X4 parent_world{};
                if (ReplayEngine::Core::GameObject* parent = object->Parent())
                    parent_world = parent->GetTransform().WorldMatrixFloat4x4();
                else
                    DirectX::XMStoreFloat4x4(&parent_world,
                        DirectX::XMMatrixIdentity());
                path = TransformLinePoints(path, parent_world);
                if (path.size() < 2) continue;
            }
            configure_render_state();
            line_stroke_renderer.Draw(device.Get(), immediate_context.Get(),
                &asset_database,
                sampler_states[(size_t)SAMPLER_STATE::LINEAR].Get(),
                path, alpha, trail->StrokeStyle(), camera_position);
        }
    }
}
