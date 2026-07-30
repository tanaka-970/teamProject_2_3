#pragma once

#include <DirectXMath.h>
#include "../../RePlayEngine/Core/Components/MeshColliderComponent.h"

class skinned_mesh;

class Stage
{
public:
    Stage() = default;
    ~Stage() = default;

    void SetModel(skinned_mesh* m);
    skinned_mesh* GetModel() const { return model; }
    const ReplayEngine::Core::MeshColliderComponent& GetCollisionMesh() const { return collision_mesh; }
    ReplayEngine::Core::MeshColliderComponent& GetCollisionMesh() { return collision_mesh; }

    void Update(float /*elapsed_time*/) {}
    void ResetTransform();

    const DirectX::XMFLOAT4X4& GetTransform() const { return transform; }
    const DirectX::XMFLOAT3& GetPosition() const { return position; }
    const DirectX::XMFLOAT3& GetAngle() const { return angle; }
    const DirectX::XMFLOAT3& GetScale() const { return scale; }

    void SetPosition(const DirectX::XMFLOAT3& p) { position = p; UpdateTransform(); RebuildCollisionMesh(); }
    void SetAngle(const DirectX::XMFLOAT3& a) { angle = a; UpdateTransform(); RebuildCollisionMesh(); }
    void SetScale(const DirectX::XMFLOAT3& s) { scale = s; UpdateTransform(); RebuildCollisionMesh(); }

private:
    void UpdateTransform()
    {
        DirectX::XMMATRIX S = DirectX::XMMatrixScaling(scale.x, scale.y, scale.z);
        DirectX::XMMATRIX R = DirectX::XMMatrixRotationRollPitchYaw(angle.x, angle.y, angle.z);
        DirectX::XMMATRIX T = DirectX::XMMatrixTranslation(position.x, position.y, position.z);
        DirectX::XMStoreFloat4x4(&transform, S * R * T);
    }
    void RebuildCollisionMesh();

    DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 angle{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT4X4 transform = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    skinned_mesh* model = nullptr;
    ReplayEngine::Core::MeshColliderComponent collision_mesh{};
};
