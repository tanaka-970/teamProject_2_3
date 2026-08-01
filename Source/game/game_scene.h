#pragma once

#include "scene_game.h"
#include "../../RePlayEngine/Scene/IScene.h"
#include "UI.h"

class skinned_mesh;

// ゲーム側が所有するシーン。描画パスの分割中は互換レンダラーを使うが、
// 生存期間と更新処理は汎用SceneManagerが管理する。
//
// 【プレイヤーは持たない】
//   以前はここが起動時に Player の初期配置をやり直していた。
//   操作対象は Scene ファイル (.replayscene) 内の GameObject が正式な情報源であり、
//   ここから位置や姿勢を上書きすることは一切しない。
class GameScene final : public ReplayEngine::Scene::IScene
{
public:
    GameScene(skinned_mesh* stage_mesh, float aspect) noexcept;

    bool Initialize(ID3D11Device* device) override;
    void Update(float elapsed_time) override;
    void Render(const ReplayEngine::Scene::RenderContext&) override {}
    bool IsFinished() const noexcept override { return false; }

    SceneGame& Gameplay() noexcept { return gameplay_; }
    const SceneGame& Gameplay() const noexcept { return gameplay_; }

private:
    skinned_mesh* stage_mesh_ = nullptr;
    float aspect_ = 16.0f / 9.0f;
    SceneGame gameplay_;
    UIManager uiManager;
};
