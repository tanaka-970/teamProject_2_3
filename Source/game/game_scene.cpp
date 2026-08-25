#include "game_scene.h"

GameScene::GameScene(float aspect) noexcept
    : aspect_(aspect)
{
}

bool GameScene::Initialize()
{
    gameplay_.Initialize(aspect_);
    return true;
}

void GameScene::Update(float elapsed_time)
{
    (void)elapsed_time;
}
