#include "NavAgentComponent.h"

#include "../Gameplay/CharacterMotorComponent.h"
#include "../Physics/BoxColliderComponent.h"
#include "../Physics/CapsuleColliderComponent.h"
#include "../Physics/ColliderComponent.h"
#include "../Physics/SphereColliderComponent.h"
#include "../../Navigation/GridPathfinder.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Services/SceneServices.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace ReplayEngine::Components
{
    namespace
    {
        constexpr float DirectionEpsilon = 0.0001f;
        constexpr float DestinationChangeEpsilonSquared = 0.000001f;
        constexpr float TrailSampleSeconds = 0.05f;
        constexpr std::size_t MaximumTrailPoints = 120;
        constexpr float DefaultWalkableNormalY = 0.25f;

        struct PathQueryShape
        {
            float radius = 0.38f;
            float walkable_normal_y = DefaultWalkableNormalY;
            float ground_probe_up = 0.4f;
            DirectX::XMFLOAT3 ground_offset{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 wall_offset{ 0.0f, 0.0f, 0.0f };
            Scene::CollisionQueryFilter filter;
            bool valid = false;
        };

        float Length3(const DirectX::XMFLOAT3& value) noexcept
        {
            return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
        }

        float LengthPlanar(const DirectX::XMFLOAT3& value) noexcept
        {
            return std::sqrt(value.x * value.x + value.z * value.z);
        }

        float SanitizeGridSize(float value) noexcept
        {
            if (!std::isfinite(value) || value <= 0.0f) return 1.0f;
            return (std::max)(0.05f, value);
        }

        float SanitizeMaximumRange(float value, float grid_size) noexcept
        {
            if (!std::isfinite(value) || value <= 0.0f) return 24.0f;
            return (std::max)(grid_size, value);
        }

        int SanitizeMaximumCells(int value) noexcept
        {
            return (std::min)(65536, (std::max)(16, value));
        }

        bool BuildPathQueryShape(const CharacterMotorComponent& motor,
            PathQueryShape& out_shape) noexcept
        {
            out_shape = PathQueryShape{};
            const ColliderComponent* collider = motor.ResolvePrimaryCollider();
            const Core::GameObject* owner = motor.Owner();
            if (collider == nullptr || owner == nullptr || !collider->ActiveInHierarchy())
                return false;

            out_shape.filter.layer = collider->collision_layer;
            out_shape.filter.mask = collider->collision_mask;
            out_shape.filter.ignore_object = owner->ID();
            out_shape.ground_offset = collider->center_offset;
            out_shape.wall_offset = collider->center_offset;
            out_shape.ground_probe_up = (std::max)(0.0f, motor.max_step_height);

            switch (collider->Shape())
            {
            case ColliderShape::Sphere:
            {
                const auto& sphere = static_cast<const SphereColliderComponent&>(*collider);
                out_shape.radius = sphere.EffectiveRadius();
                out_shape.walkable_normal_y = sphere.walkable_normal_y;
                out_shape.valid = out_shape.radius > 0.0f;
                break;
            }
            case ColliderShape::Capsule:
            {
                const auto& capsule = static_cast<const CapsuleColliderComponent&>(*collider);
                out_shape.radius = capsule.EffectiveRadius();
                const float half_cylinder = (std::max)(0.0f,
                    capsule.EffectiveHeight() * 0.5f - capsule.EffectiveRadius());
                out_shape.ground_offset.y -= half_cylinder;
                out_shape.valid = out_shape.radius > 0.0f;
                break;
            }
            case ColliderShape::Box:
            {
                const auto& box = static_cast<const BoxColliderComponent&>(*collider);
                const DirectX::XMFLOAT3 half = box.WorldHalfExtents();
                out_shape.radius = (std::min)({ half.x, half.y, half.z });
                out_shape.ground_offset.y -= (std::max)(0.0f, half.y - out_shape.radius);
                out_shape.valid = out_shape.radius > 0.0f;
                break;
            }
            case ColliderShape::Mesh:
            case ColliderShape::Landscape:
                break;
            }
            return out_shape.valid;
        }
    }

    void NavAgentComponent::OnStart()
    {
        path_.clear();
        path_index_ = 0;
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

        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;

        const float grid_size = SanitizeGridSize(path_grid_size);
        const float maximum_range = SanitizeMaximumRange(path_max_range, grid_size);
        const int maximum_cells = SanitizeMaximumCells(path_max_search_cells);
        const bool settings_changed =
            std::fabs(planned_grid_size_ - grid_size) > DirectionEpsilon ||
            std::fabs(planned_max_range_ - maximum_range) > DirectionEpsilon ||
            planned_max_search_cells_ != maximum_cells;

        // Chase は毎フレーム MoveTo を呼ぶ。ターゲットが同じグリッド内で数 cm 動くたびに
        // A* を作り直すと Editor が重くなるため、半升未満なら終点だけ追従させる。
        const float replan_distance = (std::max)(0.05f, grid_size * 0.5f);
        if (destination_was_set && !settings_changed && !path_.empty() &&
            changed <= replan_distance * replan_distance)
        {
            path_.back() = world_position;
            return;
        }

        planned_grid_size_ = grid_size;
        planned_max_range_ = maximum_range;
        planned_max_search_cells_ = maximum_cells;

        // 公開 API が noexcept なので、経路用 vector の確保失敗も外へ出さない。
        // path_ が空なら OnUpdate は destination_ へ直接進み、Phase 1 相当の挙動を維持する。
        try
        {
            const DirectX::XMFLOAT3 current = owner->GetTransform().WorldPosition();
            path_.clear();
            path_index_ = 0;

            CharacterMotorComponent* motor = owner->GetComponent<CharacterMotorComponent>();
            Scene::Scene* scene = GetScene();
            const Scene::IPhysicsQueryService* physics =
                scene != nullptr ? scene->Services().Physics() : nullptr;

            PathQueryShape shape;
            if (motor != nullptr && physics != nullptr && physics->CollisionAvailable() &&
                BuildPathQueryShape(*motor, shape))
            {
                Navigation::GridPathfinderSettings settings;
                settings.grid_size = grid_size;
                settings.maximum_range = maximum_range;
                settings.maximum_search_cells = static_cast<std::size_t>(maximum_cells);
                settings.agent_radius = shape.radius;
                settings.walkable_normal_y = shape.walkable_normal_y;
                settings.ground_probe_up = shape.ground_probe_up;
                settings.ground_probe_down = shape.ground_probe_up +
                    (std::max)(0.5f, grid_size * 1.5f);
                settings.ground_offset = shape.ground_offset;
                settings.wall_offset = shape.wall_offset;
                settings.filter = shape.filter;

                std::vector<DirectX::XMFLOAT3> found_path;
                if (Navigation::FindGridPath(*physics, current, destination_, settings,
                    found_path) && found_path.size() >= 2)
                {
                    path_ = std::move(found_path);
                    path_index_ = 1;
                    return;
                }
            }

            // 探索不可・上限到達・経路無しは止まらず Phase 1 の直進へ落とす。
            path_.reserve(2);
            path_.push_back(current);
            path_.push_back(destination_);
            path_index_ = 1;
        }
        catch (...)
        {
            path_.clear();
            path_index_ = 0;
        }
    }

    void NavAgentComponent::Stop() noexcept
    {
        has_destination_ = false;
        arrived_ = true;
        path_.clear();
        path_index_ = 0;

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
        const float stop = (std::max)(0.0f, stopping_distance);
        const float intermediate_threshold =
            (std::max)(0.05f, SanitizeGridSize(path_grid_size) * 0.45f);

        DirectX::XMFLOAT3 target = destination_;
        bool final_point = true;

        if (!path_.empty() && path_index_ < path_.size())
        {
            // 中間点は stopping_distance ではなく升目由来の別しきい値で通過する。
            // 角で停止距離ぶん大回りして壁へ寄るのを防ぐため。
            while (path_index_ < path_.size())
            {
                target = path_[path_index_];
                final_point = path_index_ + 1 >= path_.size();
                DirectX::XMFLOAT3 to_point{
                    target.x - current.x,
                    target.y - current.y,
                    target.z - current.z };
                const float distance = motor != nullptr
                    ? LengthPlanar(to_point) : Length3(to_point);
                const float threshold = final_point ? stop : intermediate_threshold;
                if (distance > threshold) break;

                if (final_point)
                {
                    arrived_ = true;
                    if (motor != nullptr) motor->Move({ 0.0f, 0.0f, 0.0f }, 0.0f);
                    return;
                }
                ++path_index_;
            }
        }

        if (!path_.empty() && path_index_ >= path_.size())
        {
            arrived_ = true;
            if (motor != nullptr) motor->Move({ 0.0f, 0.0f, 0.0f }, 0.0f);
            return;
        }

        if (!path_.empty() && path_index_ < path_.size())
        {
            target = path_[path_index_];
            final_point = path_index_ + 1 >= path_.size();
        }

        DirectX::XMFLOAT3 direction{
            target.x - current.x,
            target.y - current.y,
            target.z - current.z };

        const float distance = motor != nullptr ? LengthPlanar(direction) : Length3(direction);
        const float threshold = final_point ? stop : intermediate_threshold;
        if (distance <= threshold)
        {
            if (final_point)
            {
                arrived_ = true;
                if (motor != nullptr) motor->Move({ 0.0f, 0.0f, 0.0f }, 0.0f);
                return;
            }
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

        // Motor が無い用途は Phase 1 の Transform 直接移動を維持する。
        // 経路探索は Primary Collider / CharacterMotor の移動条件と揃えられる場合だけ行う。
        const float full_length = Length3(direction);
        if (full_length <= DirectionEpsilon) return;
        direction.x /= full_length;
        direction.y /= full_length;
        direction.z /= full_length;

        const float maximum_step = (std::max)(0.0f, move_speed) *
            (std::max)(0.0f, delta_time);
        const float remaining = final_point
            ? (std::max)(0.0f, full_length - stop) : full_length;
        const float move_distance = (std::min)(maximum_step, remaining);
        DirectX::XMFLOAT3 next{
            current.x + direction.x * move_distance,
            current.y + direction.y * move_distance,
            current.z + direction.z * move_distance };
        owner->GetTransform().SetWorldPosition(next);

        if (final_point && full_length - move_distance <= stop) arrived_ = true;
    }

    void NavAgentComponent::BuildDebugPath(std::vector<DirectX::XMFLOAT3>& out_points) const
    {
        out_points.clear();
        const Core::GameObject* owner = Owner();
        if (!has_destination_ || owner == nullptr) return;

        out_points.push_back(owner->GetTransform().WorldPosition());
        if (!path_.empty() && path_index_ < path_.size())
        {
            for (std::size_t index = path_index_; index < path_.size(); ++index)
                out_points.push_back(path_[index]);
            return;
        }
        out_points.push_back(destination_);
    }
}
