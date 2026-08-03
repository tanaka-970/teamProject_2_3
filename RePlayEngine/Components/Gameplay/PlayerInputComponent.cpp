#include "PlayerInputComponent.h"

#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"

// 入力デバイスの読み取りはここ 1 か所に閉じる。
// ヘッダーへは持ち込まないので、他の Component が Windows API へ引きずられない。
//
// 将来キーコンフィグや Gamepad へ対応する場合は、
// SceneServices へ IInputProvider を足してここを差し替える。
// 現状は既存 GameInput と同じ操作（WASD / 矢印 / Space）を維持することを優先している。
#include <windows.h>

namespace ReplayEngine::Components
{
    namespace
    {
        bool Held(int virtual_key) noexcept
        {
            return (::GetAsyncKeyState(virtual_key) & 0x8000) != 0;
        }
    }

    void PlayerInputComponent::OnUpdate(float)
    {
        // Play 中だけ入力を拾う。
        // Edit Mode で Editor を触っている間に GameObject が動き出さないようにする。
        const Scene::Scene* scene = GetScene();
        const bool playing = scene != nullptr && scene->Services().Playing();

        if (!input_enabled || !playing)
        {
            move_x_ = 0.0f;
            move_y_ = 0.0f;
            dash_held_ = false;
            // ラッチは落とす。停止中に押していた分を再開時へ持ち越さない。
            jump_latched_ = false;
            return;
        }

        move_x_ = 0.0f;
        if (Held('D') || Held(VK_RIGHT)) move_x_ += 1.0f;
        if (Held('A') || Held(VK_LEFT))  move_x_ -= 1.0f;

        move_y_ = 0.0f;
        if (Held('W') || Held(VK_UP))    move_y_ += 1.0f;
        if (Held('S') || Held(VK_DOWN))  move_y_ -= 1.0f;

        dash_held_ = Held(VK_SHIFT);

        // 押した瞬間をラッチする。
        // GetAsyncKeyState の下位ビットは「前回呼び出し以降に押されたか」を返すので、
        // 毎フレーム 1 回だけ呼ぶこの場所で読むと押下エッジになる。
        if ((::GetAsyncKeyState(VK_SPACE) & 0x0001) != 0)
        {
            jump_latched_ = true;
        }
    }

    void PlayerInputComponent::OnDisable()
    {
        move_x_ = 0.0f;
        move_y_ = 0.0f;
        dash_held_ = false;
        jump_latched_ = false;
    }

    bool PlayerInputComponent::ConsumeJump() noexcept
    {
        const bool latched = jump_latched_;
        jump_latched_ = false;
        return latched;
    }
}
