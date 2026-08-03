#pragma once

#include <DirectXMath.h>

namespace ReplayEngine::Scene
{
    // 「カメラがどちらを向いているか」だけを Component へ渡すための境界。
    //
    // なぜこれを挟むか:
    //   旧 Player::Update(dt, const Camera&) は移動処理が Camera 具象型を
    //   直接受け取っていた。そのため Gameplay の移動ロジックが
    //   カメラ実装の変更に巻き込まれる構造になっていた。
    //
    //   PlayerControllerComponent はこのインターフェイスだけを見る。
    //   既存 Camera クラスを include しないので、依存が 1 か所（Bridge）へ閉じる。
    //
    // 返す値はワールド空間の単位ベクトル。XZ 平面への射影は利用側が行う。
    class ICameraBasisProvider
    {
    public:
        virtual ~ICameraBasisProvider() = default;

        virtual DirectX::XMFLOAT3 CameraForward() const = 0;
        virtual DirectX::XMFLOAT3 CameraRight() const = 0;

        // カメラが利用可能か。起動直後などまだ用意できていない場合は false。
        virtual bool CameraBasisAvailable() const { return true; }
    };
}
