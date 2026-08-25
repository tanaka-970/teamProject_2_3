#include "game_scene.h"

GameScene::GameScene(float aspect) noexcept
    : aspect_(aspect)
{
}

bool GameScene::Initialize(ID3D11Device* device)
{
    // GameObject Sceneとは独立したカメラ操作だけを初期化する。
    // 操作対象の初期配置はここでは行わない。Scene ファイルの内容が正式な情報源。
    gameplay_.Initialize(aspect_);

    // sinotake 側で追加された UI。旧 Player とは無関係なのでそのまま残す。
    // 旧UIマネージャーはD3D11のSpriteを所有するため、DX12起動時は初期化しない。
    // DX12のRuntime UIはframeworkの共通UI提出経路が所有する。
    if (device != nullptr) uiManager.Initalize(device);

    // 任意モデルの欠落だけでシーン管理全体を無効にしない。
    // 個別の失敗はロード画面へ通知し、エディタでは残りの空間を確認できるようにする。
    return true;
}

void GameScene::Update(float elapsed_time)
{
    (void)elapsed_time;
}
