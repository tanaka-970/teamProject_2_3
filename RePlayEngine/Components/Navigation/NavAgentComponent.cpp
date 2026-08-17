#include "NavAgentComponent.h"

#include "../Gameplay/CharacterMotorComponent.h"
#include "../../Object/GameObject/GameObject.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ReplayEngine::Components
{
    namespace
    {
        constexpr float DirectionEpsilon = 0.0001f;
        constexpr float DestinationChangeEpsilonSquared = 0.000001f;
        constexpr float TrailSampleSeconds = 0.05f;
        constexpr std::size_t MaximumTrailPoints = 120;

        float Length3(const DirectX::XMFLOAT3& value) noexcept
        {
            return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
        }

        float LengthPlanar(const DirectX::XMFLOAT3& value) noexcept
        {
            return std::sqrt(value.x * value.x + value.z * value.z);
        }
    }

    void NavAgentComponent::OnStart()
    {
        recent_trail_.clear();
        trail_sample_accumulator_ = TrailSampleSeconds;
        RecordTrail(0.0f);
    }

    void NavAgentComponent::MoveTo(const DirectX::XMFLOAT3& world_position) noexcept
    {
        const DirectX::XMFLOAT3 delta{
            world_position.x - destination_.x,
            world_position.y - destination_.y,
            world_position.z - destination_.z };
        const float changed = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;

        const bool destination_was_set = has_destination_;
        destination_ = world_position;
        has_destination_ = true;
        if (!destination_was_set || changed > DestinationChangeEpsilonSquared)
            arrived_ = false;
    }

    void NavAgentComponent::Stop() noexcept
    {
        has_destination_ = false;
        arrived_ = true;

        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;
        if (CharacterMotorComponent* motor = owner->GetComponent<CharacterMotorComponent>())
            motor->Move({ 0.0f, 0.0f, 0.0f }, 0.0f);
    }

    void NavAgentComponent::OnDisable()
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;
        if (CharacterMotorComponent* motor = owner->GetComponent<CharacterMotorComponent>())
            motor->Move({ 0.0f, 0.0f, 0.0f }, 0.0f);
    }

    void NavAgentComponent::RotateTowards(const DirectX::XMFLOAT3& direction,
        float delta_time)
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;

        const float length = LengthPlanar(direction);
        if (length <= DirectionEpsilon) return;

        Core::Transform& transform = owner->GetTransform();
        DirectX::XMFLOAT3 euler = transform.LocalRotationEuler();
        const float target_yaw = std::atan2(direction.x / length, direction.z / length);
        float delta_yaw = target_yaw - euler.y;
        while (delta_yaw > DirectX::XM_PI) delta_yaw -= DirectX::XM_2PI;
        while (delta_yaw < -DirectX::XM_PI) delta_yaw += DirectX::XM_2PI;

        const float maximum = (std::max)(0.0f, turn_speed_degrees) *
            (DirectX::XM_PI / 180.0f) * (std::max)(0.0f, delta_time);
        if (delta_yaw > maximum) delta_yaw = maximum;
        if (delta_yaw < -maximum) delta_yaw = -maximum;

        euler.y += delta_yaw;
        transform.SetLocalRotationEuler(euler);
    }

    void NavAgentComponent::RecordTrail(float delta_time)
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;

        trail_sample_accumulator_ += (std::max)(0.0f, delta_time);
        if (!recent_trail_.empty() && trail_sample_accumulator_ < TrailSampleSeconds) return;
        trail_sample_accumulator_ = 0.0f;

        const DirectX::XMFLOAT3 position = owner->GetTransform().WorldPosition();
        if (!recent_trail_.empty())
        {
            const DirectX::XMFLOAT3& previous = recent_trail_.back();
            const float dx = position.x - previous.x;
            const float dy = position.y - previous.y;
            const float dz = position.z - previous.z;
            if (dx * dx + dy * dy + dz * dz < 0.000025f) return;
        }

        recent_trail_.push_back(position);
        if (recent_trail_.size() > MaximumTrailPoints)
        {
            const std::size_t remove_count = recent_trail_.size() - MaximumTrailPoints;
            recent_trail_.erase(recent_trail_.begin(), recent_trail_.begin() +
                static_cast<std::ptrdiff_t>(remove_count));
        }
    }

    void NavAgentComponent::OnUpdate(float delta_time)
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;

        RecordTrail(delta_time);
        if (!has_destination_) return;

        CharacterMotorComponent* motor = owner->GetComponent<CharacterMotorComponent>();
        const DirectX::XMFLOAT3 current = owner->GetTransform().WorldPosition();
        DirectX::XMFLOAT3 direction{
            destination_.x - current.x,
            destination_.y - current.y,
            destination_.z - current.z };

        const float stop = (std::max)(0.0f, stopping_distance);
        const float distance = motor != nullptr ? LengthPlanar(direction) : Length3(direction);
        if (distance <= stop)
        {
            arrived_ = true;
            if (motor != nullptr) motor->Move({ 0.0f, 0.0f, 0.0f }, 0.0f);
            return;
        }

        arrived_ = false;
        RotateTowards(direction, delta_time);

        if (motor != nullptr)
        {
            // CharacterMotor がある場合、重力・接地・斜面・衝突は既存 Motor に任せる。
            direction.y = 0.0f;
            const float planar_length = LengthPlanar(direction);
            if (planar_length <= DirectionEpsilon) return;
            direction.x /= planar_length;
            direction.z /= planar_length;

            const float motor_speed = motor->move_speed;
            const float speed_multiplier = motor_speed > DirectionEpsilon
                ? (std::max)(0.0f, move_speed) / motor_speed : 0.0f;
            motor->Move(direction, speed_multiplier);
            return;
        }

        // Motor が無い用途（飛行物体・2D 的な配置・演出オブジェクト等）だけの
        // フォールバック。Collision / Gravity はここでは作らない。
        const float full_length = Length3(direction);
        if (full_length <= DirectionEpsilon) return;
        direction.x /= full_length;
        direction.y /= full_length;
        direction.z /= full_length;

        const float maximum_step = (std::max)(0.0f, move_speed) *
            (std::max)(0.0f, delta_time);
        const float move_distance = (std::min)(maximum_step, (std::max)(0.0f, full_length - stop));
        DirectX::XMFLOAT3 next{
            current.x + direction.x * move_distance,
            current.y + direction.y * move_distance,
            current.z + direction.z * move_distance };
        owner->GetTransform().SetWorldPosition(next);

        if (full_length - move_distance <= stop) arrived_ = true;
    }

    void NavAgentComponent::BuildDebugPath(std::vector<DirectX::XMFLOAT3>& out_points) const
    {
        out_points.clear();
        const Core::GameObject* owner = Owner();
        if (!has_destination_ || owner == nullptr) return;
        out_points.push_back(owner->GetTransform().WorldPosition());
        out_points.push_back(destination_);
    }
}
