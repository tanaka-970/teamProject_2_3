#include "game_scene.h"

GameScene::GameScene(skinned_mesh* stage_mesh, float aspect) noexcept
    : stage_mesh_(stage_mesh), aspect_(aspect)
{
}

bool GameScene::Initialize(ID3D11Device* device)
{
    // カメラと旧ステージだけを初期化する。
    // 操作対象の初期配置はここでは行わない。Scene ファイルの内容が正式な情報源。
    gameplay_.Initialize(stage_mesh_, aspect_);

    // sinotake 側で追加された UI。旧 Player とは無関係なのでそのまま残す。
    uiManager.Initalize(device);

    // 任意モデルの欠落だけでシーン管理全体を無効にしない。
    // 個別の失敗はロード画面へ通知し、エディタでは残りの空間を確認できるようにする。
    return true;
}

void GameScene::Update(float elapsed_time)
{
    gameplay_.Update(elapsed_time);
}
