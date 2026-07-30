#pragma once

#include "scene_game.h"
#include "../../RePlayEngine/Scene/IScene.h"

class skinned_mesh;

// ゲーム側が所有するシーン。描画パスの分割中は互換レンダラーを使うが、
// 生存期間と更新処理は汎用SceneManagerが管理する。
class GameScene final : public ReplayEngine::Scene::IScene
{
public:
    GameScene(skinned_mesh* player_mesh, skinned_mesh* stage_mesh, float aspect,
        int idle_clip = -1, int walk_clip = -1, int jump_clip = -1) noexcept;

    bool Initialize(ID3D11Device* device) override;
    void Update(float elapsed_time) override;
    void Render(const ReplayEngine::Scene::RenderContext&) override {}
    bool IsFinished() const noexcept override { return false; }

    SceneGame& Gameplay() noexcept { return gameplay_; }
    const SceneGame& Gameplay() const noexcept { return gameplay_; }

private:
    skinned_mesh* player_mesh_ = nullptr;
    skinned_mesh* stage_mesh_ = nullptr;
    float aspect_ = 16.0f / 9.0f;
    int idle_clip_ = -1;
    int walk_clip_ = -1;
    int jump_clip_ = -1;
    SceneGame gameplay_;
};
