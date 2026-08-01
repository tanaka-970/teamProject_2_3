#include "TransformComponent.h"

namespace ReplayEngine::Components
{
    namespace
    {
        // 所有 GameObject が外れている間だけ使う退避先。
        // Inspector の描画中に GameObject が削除されても、
        // 無効ポインタを触らずに済ませるための安全網。
        Core::Transform& OrphanTransform() noexcept
        {
            static Core::Transform orphan;
            return orphan;
        }

        constexpr float degrees_per_radian = 180.0f / DirectX::XM_PI;
        constexpr float radians_per_degree = DirectX::XM_PI / 180.0f;
    }

    Core::Transform& TransformComponent::Target() noexcept
    {
        Core::GameObject* owner = Owner();
        return owner != nullptr ? owner->GetTransform() : OrphanTransform();
    }

    const Core::Transform& TransformComponent::Target() const noexcept
    {
        const Core::GameObject* owner = Owner();
        return owner != nullptr ? owner->GetTransform() : OrphanTransform();
    }

    DirectX::XMFLOAT3 TransformComponent::RotationDegrees() const noexcept
    {
        const DirectX::XMFLOAT3& radians = Target().LocalRotationEuler();
        return DirectX::XMFLOAT3{
            radians.x * degrees_per_radian,
            radians.y * degrees_per_radian,
            radians.z * degrees_per_radian };
    }

    void TransformComponent::SetRotationDegrees(const DirectX::XMFLOAT3& value) noexcept
    {
        Target().SetLocalRotationEuler(DirectX::XMFLOAT3{
            value.x * radians_per_degree,
            value.y * radians_per_degree,
            value.z * radians_per_degree });
    }
}
