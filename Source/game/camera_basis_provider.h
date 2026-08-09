#pragma once

#include "../../RePlayEngine/Scene/Services/ICameraBasisProvider.h"

class Camera;

// 既存の描画カメラを、GameObject側が利用する汎用カメラ基準情報として公開する。
// PlayerControllerComponentはこの抽象インターフェースだけを参照し、Cameraの実装へ依存しない。
class CameraBasisProvider final : public ReplayEngine::Scene::ICameraBasisProvider
{
public:
    // cameraは非所有参照。実体はSceneGameが所有する。
    void Attach(const Camera* camera) noexcept
    {
        camera_ = camera;
        has_explicit_basis_ = false;
    }
    void AttachBasis(const DirectX::XMFLOAT3& forward,
        const DirectX::XMFLOAT3& right) noexcept;
    void Detach() noexcept
    {
        camera_ = nullptr;
        has_explicit_basis_ = false;
    }

    bool CameraBasisAvailable() const override
    {
        return has_explicit_basis_ || camera_ != nullptr;
    }

    DirectX::XMFLOAT3 CameraForward() const override;
    DirectX::XMFLOAT3 CameraRight() const override;

private:
    const Camera* camera_ = nullptr;
    bool has_explicit_basis_ = false;
    DirectX::XMFLOAT3 explicit_forward_{ 0.0f, 0.0f, 1.0f };
    DirectX::XMFLOAT3 explicit_right_{ 1.0f, 0.0f, 0.0f };
};
