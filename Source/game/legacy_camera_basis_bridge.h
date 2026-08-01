#pragma once

#include "../../RePlayEngine/Scene/Services/ICameraBasisProvider.h"

class Camera;

// 【移行用】既存 Camera の向きを、汎用のカメラ基準情報として見せる橋渡し。
//
// これは最終設計ではない。
//   PlayerControllerComponent は ICameraBasisProvider しか知らず、
//   Camera クラスを include していない。
//   Camera への依存はこのクラス 1 か所へ閉じている。
//
// 【削除条件】
//   カメラを CameraComponent として GameObject 化し、
//   Scene 側から直接 forward / right を取得できるようになった時点で削除する。
//   その際 PlayerControllerComponent は 1 行も変更しなくてよい。
class LegacyCameraBasisBridge final : public ReplayEngine::Scene::ICameraBasisProvider
{
public:
    // camera は非所有参照。実体は SceneGame が持つ。
    void Attach(const Camera* camera) noexcept { camera_ = camera; }
    void Detach() noexcept { camera_ = nullptr; }

    bool CameraBasisAvailable() const override { return camera_ != nullptr; }

    DirectX::XMFLOAT3 CameraForward() const override;
    DirectX::XMFLOAT3 CameraRight() const override;

private:
    const Camera* camera_ = nullptr;
};
