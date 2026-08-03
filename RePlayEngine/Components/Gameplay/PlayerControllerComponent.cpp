#include "PlayerControllerComponent.h"

#include "CharacterMotorComponent.h"
#include "PlayerInputComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"

#include <cmath>

// 依存 Component の不足をログへ出すためだけに使う。
#include <windows.h>

namespace ReplayEngine::Components
{
    void PlayerControllerComponent::OnStart()
    {
        requirement_warning_logged_ = false;
    }

    CharacterMotorComponent* PlayerControllerComponent::FindMotor() const
    {
        Core::GameObject* owner = Owner();
        return owner != nullptr ? owner->GetComponent<CharacterMotorComponent>() : nullptr;
    }

    PlayerInputComponent* PlayerControllerComponent::FindInput() const
    {
        Core::GameObject* owner = Owner();
        return owner != nullptr ? owner->GetComponent<PlayerInputComponent>() : nullptr;
    }

    bool PlayerControllerComponent::HasRequiredComponents() const
    {
        return FindInput() != nullptr && FindMotor() != nullptr;
    }

    const char* PlayerControllerComponent::MissingRequirementText() const
    {
        const bool has_input = FindInput() != nullptr;
        const bool has_motor = FindMotor() != nullptr;
        if (!has_input && !has_motor) return "Player Input と Character Motor が必要です";
        if (!has_input) return "Player Input が必要です";
        if (!has_motor) return "Character Motor が必要です";
        return "";
    }

    DirectX::XMFLOAT3 PlayerControllerComponent::ResolveWorldDirection(
        float input_x, float input_y) const
    {
        // 既定はワールド軸基準。カメラが使えない場合もこれで動く。
        float forward_x = 0.0f;
        float forward_z = 1.0f;
        float right_x = 1.0f;
        float right_z = 0.0f;

        if (camera_relative)
        {
            const Scene::Scene* scene = GetScene();
            const Scene::ICameraBasisProvider* basis =
                scene != nullptr ? scene->Services().CameraBasis() : nullptr;

            if (basis != nullptr && basis->CameraBasisAvailable())
            {
                // カメラの前方・右方を XZ 平面へ射影して正規化する。
                // 旧 Player::Update と同じ計算。
                const DirectX::XMFLOAT3 forward = basis->CameraForward();
                const DirectX::XMFLOAT3 right = basis->CameraRight();

                const float forward_length = std::sqrt(forward.x * forward.x + forward.z * forward.z);
                const float right_length = std::sqrt(right.x * right.x + right.z * right.z);

                forward_x = forward_length > 0.0001f ? forward.x / forward_length : 0.0f;
                forward_z = forward_length > 0.0001f ? forward.z / forward_length : 1.0f;
                right_x = right_length > 0.0001f ? right.x / right_length : 1.0f;
                right_z = right_length > 0.0001f ? right.z / right_length : 0.0f;
            }
        }

        return DirectX::XMFLOAT3{
            forward_x * input_y + right_x * input_x,
            0.0f,
            forward_z * input_y + right_z * input_x };
    }

    void PlayerControllerComponent::RotateTowards(const DirectX::XMFLOAT3& direction,
        float delta_time)
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;

        const float length = std::sqrt(direction.x * direction.x + direction.z * direction.z);
        if (length <= 0.0001f) return;

        Core::Transform& transform = owner->GetTransform();
        DirectX::XMFLOAT3 euler = transform.LocalRotationEuler();

        const float target_yaw = std::atan2(direction.x / length, direction.z / length);
        float delta_yaw = target_yaw - euler.y;
        while (delta_yaw > DirectX::XM_PI) delta_yaw -= DirectX::XM_2PI;
        while (delta_yaw < -DirectX::XM_PI) delta_yaw += DirectX::XM_2PI;

        const float maximum = turn_speed_degrees * (DirectX::XM_PI / 180.0f) * delta_time;
        if (delta_yaw > maximum) delta_yaw = maximum;
        if (delta_yaw < -maximum) delta_yaw = -maximum;

        euler.y += delta_yaw;
        transform.SetLocalRotationEuler(euler);
    }

    void PlayerControllerComponent::OnUpdate(float delta_time)
    {
        PlayerInputComponent* input = FindInput();
        CharacterMotorComponent* motor = FindMotor();

        if (input == nullptr || motor == nullptr)
        {
            // クラッシュさせず、原因が分かる形で一度だけ知らせる。
            if (!requirement_warning_logged_)
            {
                requirement_warning_logged_ = true;
                const Core::GameObject* owner = Owner();
                const std::string name = owner != nullptr ? owner->Name() : std::string("(不明)");
                ::OutputDebugStringA(("[PlayerController] " + name + ": " +
                    MissingRequirementText() + "\n").c_str());
            }
            return;
        }
        requirement_warning_logged_ = false;

        // 入力 Component が無効なら移動要求を出さない。
        // Motor は要求が来なければ減速するだけなので、その場で止まる。
        if (!input->ActiveInHierarchy()) return;

        const DirectX::XMFLOAT3 direction =
            ResolveWorldDirection(input->MoveX(), input->MoveY());

        // ダッシュは方向とは別に速度倍率として渡す。
        // 斜め入力の正規化と Dash 倍率が混ざらないようにする。
        const float scale = input->Dashing() ? dash_multiplier : 1.0f;
        motor->Move(direction, scale);

        // ジャンプは 1 回だけ取り出して Motor へ渡す。
        // 取り出した時点でラッチが下りるので、多重消費が起きない。
        if (input->ConsumeJump()) motor->RequestJump();

        if (rotate_towards_movement) RotateTowards(direction, delta_time);
    }
}
