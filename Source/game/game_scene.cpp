#include "game_scene.h"

GameScene::GameScene(skinned_mesh* player_mesh, skinned_mesh* stage_mesh, float aspect,
    int idle_clip, int walk_clip, int jump_clip) noexcept
    : player_mesh_(player_mesh), stage_mesh_(stage_mesh), aspect_(aspect),
      idle_clip_(idle_clip), walk_clip_(walk_clip), jump_clip_(jump_clip)
{
}

bool GameScene::Initialize(ID3D11Device*device)
{
    gameplay_.Initialize(player_mesh_, stage_mesh_, aspect_);
    gameplay_.SetAnimationClipMapping(idle_clip_, walk_clip_, jump_clip_);
    Player& player = gameplay_.GetPlayer();
    player.ResetPlacement();
    uiManager.Initalize(device);
        // 任意モデルの欠落だけでシーン管理全体を無効にしない。
        // 個別の失敗はロード画面へ通知し、エディタでは残りの空間を確認できるようにする。
    return true;
}

void GameScene::Update(float elapsed_time)
{
    gameplay_.Update(elapsed_time);
}
