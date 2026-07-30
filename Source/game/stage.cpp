#include "stage.h"
#include "skinned_mesh.h"

#include <vector>

void Stage::SetModel(skinned_mesh* value)
{
    model = value;
    RebuildCollisionMesh();
}

void Stage::ResetTransform()
{
    position = { 0.0f, 0.0f, 0.0f };
    angle = { 0.0f, 0.0f, 0.0f };
    scale = { 1.0f, 1.0f, 1.0f };
    UpdateTransform();
    RebuildCollisionMesh();
}

void Stage::RebuildCollisionMesh()
{
    using namespace DirectX;
    using ReplayEngine::Physics::Triangle;
    if (!model)
    {
        collision_mesh.Clear();
        return;
    }

    std::vector<Triangle> triangles;
    std::size_t count = 0;
    for (const auto& mesh : model->meshes) count += mesh.indices.size() / 3;
    triangles.reserve(count);
    const XMMATRIX stage_world = XMLoadFloat4x4(&transform);
    for (const auto& mesh : model->meshes)
    {
        const XMMATRIX mesh_world = XMLoadFloat4x4(&mesh.default_global_transform) * stage_world;
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        {
            const std::uint32_t indices[3]{ mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2] };
            if (indices[0] >= mesh.vertices.size() || indices[1] >= mesh.vertices.size() ||
                indices[2] >= mesh.vertices.size()) continue;
            Triangle triangle{};
            for (int vertex = 0; vertex < 3; ++vertex)
                XMStoreFloat3(&triangle.vertices[vertex], XMVector3TransformCoord(
                    XMLoadFloat3(&mesh.vertices[indices[vertex]].position), mesh_world));
            triangles.push_back(triangle);
        }
    }
    collision_mesh.Build(std::move(triangles), 4.0f);
}
