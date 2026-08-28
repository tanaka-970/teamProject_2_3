#pragma once

#include "scene_game.h"
#include "../../RePlayEngine/Scene/IScene.h"

class GameScene final : public ReplayEngine::Scene::IScene
{
public:
    explicit GameScene(float aspect) noexcept;

    bool Initialize() override;
    void Update(float elapsed_time) override;
    bool IsFinished() const noexcept override { return false; }

    SceneGame& Gameplay() noexcept { return gameplay_; }
    const SceneGame& Gameplay() const noexcept { return gameplay_; }

private:
    float aspect_ = 16.0f / 9.0f;
    SceneGame gameplay_;
};
