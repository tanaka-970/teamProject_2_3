#include "CharacterMotorComponent.h"

#include "../Physics/BoxColliderComponent.h"
#include "../Physics/CapsuleColliderComponent.h"
#include "../Physics/ColliderComponent.h"
#include "../Physics/SphereColliderComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"

#include <algorithm>
#include <cmath>

namespace ReplayEngine::Components
{
    namespace
    {
        float Length2D(const DirectX::XMFLOAT3& value) noexcept
        {
            return std::sqrt(value.x * value.x + value.z * value.z);
        }

        float SanitizeNonNegative(float value) noexcept
        {
            if (!std::isfinite(value)) return 0.0f;
            return (std::max)(0.0f, value);
        }
    }

    void CharacterMotorComponent::OnStart()
    {
        // Play 開始時は速度を必ず 0 から始める。
        // Edit Mode で触った値や前回の Play の残りを持ち越さないため。
        velocity_ = { 0.0f, 0.0f, 0.0f };
        pending_move_ = { 0.0f, 0.0f, 0.0f };
        pending_speed_multiplier_ = 1.0f;
        jump_requested_ = false;
        grounded_ = true;
        last_ground_source_ = Scene::CollisionSourceInfo{};
        last_wall_source_ = Scene::CollisionSourceInfo{};
        has_wall_contact_ = false;
    }

    void CharacterMotorComponent::OnDisable()
    {
        // 無効化された瞬間に動きを止める。
        // 再有効化したときに古い速度で飛び出さないようにする。
        velocity_ = { 0.0f, 0.0f, 0.0f };
        pending_move_ = { 0.0f, 0.0f, 0.0f };
        pending_speed_multiplier_ = 1.0f;
        jump_requested_ = false;
    }

    void CharacterMotorComponent::Move(const DirectX::XMFLOAT3& world_direction) noexcept
    {
        Move(world_direction, 1.0f);
    }

    void CharacterMotorComponent::Move(const DirectX::XMFLOAT3& world_direction,
        float speed_multiplier) noexcept
    {
        // 同じフレームに複数回呼ばれた場合は最後の要求を採用する。
        pending_move_ = world_direction;
        pending_speed_multiplier_ = SanitizeNonNegative(speed_multiplier);
    }

    void CharacterMotorComponent::Teleport(const DirectX::XMFLOAT3& world_position)
    {
        if (Core::GameObject* owner = Owner())
        {
            owner->GetTransform().SetWorldPosition(world_position);
        }
    }

    float CharacterMotorComponent::PlanarSpeed() const noexcept
    {
        return Length2D(velocity_);
    }

    // -----------------------------------------------------------------------
    // Primary Collider
    // -----------------------------------------------------------------------

    ColliderComponent* CharacterMotorComponent::ResolvePrimaryCollider() const
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr) return nullptr;

        // 未設定のときに勝手に別の Collider を選ばない。
        // 「なんとなく動いているが、どれで動いているか分からない」状態を作らない。
        ColliderComponent* collider = FindColliderByKey(*owner, primary_collider_key);
        if (collider == nullptr) return nullptr;

        // Mesh や Trigger は移動用として使えないので、指定されていても受け付けない。
        if (!collider->UsableAsCharacterShape()) return nullptr;
        if (collider->is_trigger) return nullptr;
        return collider;
    }

    void CharacterMotorComponent::SetPrimaryCollider(const ColliderComponent& collider) noexcept
    {
        primary_collider_key = collider.collider_key;
    }

    std::string CharacterMotorComponent::PrimaryColliderStatus() const
    {
        if (primary_collider_key <= 0)
        {
            return "移動用 Collider が設定されていません。"
                "地形との当たり判定は行われず、重力と接地も働きません。";
        }

        Core::GameObject* owner = Owner();
        if (owner == nullptr) return std::string();

        ColliderComponent* collider = FindColliderByKey(*owner, primary_collider_key);
        if (collider == nullptr)
        {
            return "指定されていた Collider が見つかりません（Missing Collider）。"
                "削除されたか、別の GameObject へ移された可能性があります。";
        }
        if (!collider->UsableAsCharacterShape())
        {
            return "この形状は移動用 Collider に使えません。"
                "Sphere / Capsule / Box のいずれかを選んでください。";
        }
        if (collider->is_trigger)
        {
            return "Trigger の Collider は移動用に使えません。"
                "Trigger を外すか、別の Collider を選んでください。";
        }
        return std::string();
    }

    CharacterMotorComponent::MotionSphere CharacterMotorComponent::BuildMotionSphere() const
    {
        MotionSphere shape;

        const ColliderComponent* collider = ResolvePrimaryCollider();
        if (collider == nullptr || !collider->ActiveInHierarchy()) return shape;

        shape.filter.layer = collider->collision_layer;
        shape.filter.mask = collider->collision_mask;

        // 自分自身の Collider を除外する。
        // ここを外すと、下向きキャストが自分の Collider に当たり、
        // 毎フレーム宙へ持ち上がる（実際に起きた不具合）。
        if (const Core::GameObject* owner = Owner()) shape.filter.ignore_object = owner->ID();

        shape.ground_offset = collider->center_offset;
        shape.wall_offset = collider->center_offset;

        switch (collider->Shape())
        {
        case ColliderShape::Sphere:
        {
            const auto& sphere = static_cast<const SphereColliderComponent&>(*collider);
            shape.radius = sphere.EffectiveRadius();
            shape.skin_width = sphere.skin_width;
            shape.walkable_normal_y = sphere.walkable_normal_y;
            shape.valid = shape.radius > 0.0f;
            break;
        }
        case ColliderShape::Capsule:
        {
            const auto& capsule = static_cast<const CapsuleColliderComponent&>(*collider);
            shape.radius = capsule.EffectiveRadius();

            // 接地は「一番下の半球」で見る。
            // 中心で見ると、カプセルの下半分ぶん床へめり込んだ位置で接地判定になる。
            const float half_cylinder =
                std::max(0.0f, capsule.EffectiveHeight() * 0.5f - capsule.EffectiveRadius());
            shape.ground_offset.y -= half_cylinder;
            shape.valid = shape.radius > 0.0f;
            break;
        }
        case ColliderShape::Box:
        {
            const auto& box = static_cast<const BoxColliderComponent&>(*collider);
            const DirectX::XMFLOAT3 half = box.WorldHalfExtents();

            // 内接球。角は拾えないが、本来より小さい球なので
            // 「本当は当たらない場所で止まる」ことはない。
            shape.radius = std::min({ half.x, half.y, half.z });
            shape.ground_offset.y -= std::max(0.0f, half.y - shape.radius);
            shape.valid = shape.radius > 0.0f;
            break;
        }
        case ColliderShape::Mesh:
        case ColliderShape::Landscape:
            // ここへは来ない（ResolvePrimaryCollider が弾く）。静的環境用。
            break;
        }
        return shape;
    }

    // -----------------------------------------------------------------------
    // 更新
    // -----------------------------------------------------------------------

    void CharacterMotorComponent::OnFixedUpdate(float fixed_delta_time)
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr || fixed_delta_time <= 0.0f) return;

        Core::Transform& transform = owner->GetTransform();
        const DirectX::XMFLOAT3 previous_position = transform.LocalPosition();

        // ---- 水平方向 -------------------------------------------------------

        DirectX::XMFLOAT3 direction = pending_move_;
        const float speed_multiplier = pending_speed_multiplier_;
        pending_move_ = { 0.0f, 0.0f, 0.0f };   // 要求は 1 回で消費する
        pending_speed_multiplier_ = 1.0f;

        const float direction_length = Length2D(direction);
        const float control = grounded_ ? 1.0f : air_control;
        const float requested_speed = SanitizeNonNegative(move_speed) * speed_multiplier;

        if (direction_length > 0.0001f && requested_speed > 0.0001f)
        {
            direction.x /= direction_length;
            direction.z /= direction_length;
            last_move_direction_ = DirectX::XMFLOAT3{ direction.x, 0.0f, direction.z };

            const float step = SanitizeNonNegative(acceleration) * control * fixed_delta_time;
            velocity_.x += direction.x * step;
            velocity_.z += direction.z * step;

            // 水平速度を上限へ収める。
            const float speed = Length2D(velocity_);
            if (speed > requested_speed && speed > 0.0001f)
            {
                velocity_.x = velocity_.x / speed * requested_speed;
                velocity_.z = velocity_.z / speed * requested_speed;
            }
        }
        else
        {
            last_move_direction_ = { 0.0f, 0.0f, 0.0f };

            // 入力が無ければ滑らかに減速する。
            const float step = SanitizeNonNegative(deceleration) * control * fixed_delta_time;
            const float speed = Length2D(velocity_);
            if (speed > step && speed > 0.0001f)
            {
                velocity_.x -= velocity_.x / speed * step;
                velocity_.z -= velocity_.z / speed * step;
            }
            else
            {
                velocity_.x = 0.0f;
                velocity_.z = 0.0f;
            }
        }

        // ---- 垂直方向 -------------------------------------------------------

        const MotionSphere shape = BuildMotionSphere();

        if (vertical_physics)
        {
            // ジャンプ要求は接地している時だけ消費する。
            // フラグ方式なので、可変フレームで何度押されても 1 回、
            // FixedUpdate が複数回走っても 1 回しか実行されない。
            if (jump_requested_ && grounded_)
            {
                velocity_.y = jump_power;
                grounded_ = false;
            }
            jump_requested_ = false;

            velocity_.y -= gravity * fixed_delta_time;
            if (velocity_.y < -maximum_fall_speed) velocity_.y = -maximum_fall_speed;
        }
        else
        {
            // 旧 Player と同じく、垂直物理を切っている間は常に接地扱い。
            velocity_.y = 0.0f;
            jump_requested_ = false;
            grounded_ = true;
        }

        // ---- 位置へ反映 -----------------------------------------------------

        DirectX::XMFLOAT3 position = previous_position;
        position.x += velocity_.x * fixed_delta_time;
        position.z += velocity_.z * fixed_delta_time;
        if (vertical_physics) position.y += velocity_.y * fixed_delta_time;
        transform.SetLocalPosition(position);

        // ---- 地形との解決 ---------------------------------------------------
        // 順序は旧 SceneGame と同じ「壁 -> 接地」。
        ResolveWalls(shape, previous_position);
        ResolveGround(shape);
    }

    void CharacterMotorComponent::ResolveWalls(const MotionSphere& shape,
        const DirectX::XMFLOAT3& previous_position)
    {
        // 接触の記録は毎回ここで落とす。
        // 途中で return する経路が複数あるため、先頭でまとめて false にしておかないと
        // 「前のフレームの接触が残り続ける」= Exit が永遠に来ない状態になる。
        has_wall_contact_ = false;

        Core::GameObject* owner = Owner();
        if (owner == nullptr || !shape.valid) return;

        Scene::Scene* scene = GetScene();
        if (scene == nullptr) return;
        const Scene::IPhysicsQueryService* physics = scene->Services().Physics();
        if (physics == nullptr || !physics->CollisionAvailable()) return;

        Core::Transform& transform = owner->GetTransform();
        const DirectX::XMFLOAT3 current = transform.LocalPosition();

        const DirectX::XMFLOAT3 start{
            previous_position.x + shape.wall_offset.x,
            previous_position.y + shape.wall_offset.y,
            previous_position.z + shape.wall_offset.z };
        const DirectX::XMFLOAT3 end{
            current.x + shape.wall_offset.x,
            current.y + shape.wall_offset.y,
            current.z + shape.wall_offset.z };

        // 床は下向きキャストが扱うので、ここでは壁だけを対象にする。
        // Trigger は SceneCollisionWorld 側で除外される。
        Scene::SphereSweepHit hit{};
        if (!physics->SweepSphereFiltered(start, end, shape.radius,
            shape.walkable_normal_y - 0.001f, shape.filter, hit))
        {
            return;
        }

        last_wall_source_ = hit.source;

        // Collision Event 配送用に記録するだけ。押し戻しの計算には使わない。
        has_wall_contact_ = true;
        last_wall_point_ = hit.center;
        last_wall_normal_ = hit.normal;

        // 面から skin_width だけ離した位置へ押し戻す。
        transform.SetLocalPosition(DirectX::XMFLOAT3{
            hit.center.x + hit.normal.x * shape.skin_width - shape.wall_offset.x,
            hit.center.y + hit.normal.y * shape.skin_width - shape.wall_offset.y,
            hit.center.z + hit.normal.z * shape.skin_width - shape.wall_offset.z });
    }

    void CharacterMotorComponent::ResolveGround(const MotionSphere& shape)
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;

        const Scene::Scene* scene = GetScene();
        const Scene::IPhysicsQueryService* physics =
            scene != nullptr ? scene->Services().Physics() : nullptr;

        has_ground_ = false;
        ground_height_ = fallback_ground_y;
        ground_normal_ = { 0.0f, 1.0f, 0.0f };

        if (shape.valid && physics != nullptr && physics->CollisionAvailable())
        {
            const DirectX::XMFLOAT3 local = owner->GetTransform().LocalPosition();
            const DirectX::XMFLOAT3 origin{
                local.x + shape.ground_offset.x,
                local.y + shape.ground_offset.y,
                local.z + shape.ground_offset.z };

            Scene::GroundHit hit{};
            if (physics->QueryGroundFiltered(origin, shape.radius, 80.0f, 300.0f,
                shape.walkable_normal_y, shape.filter, hit))
            {
                has_ground_ = true;

                // 接地点から「GameObject をどの高さへ置くか」を求める。
                //
                //   落とした球の中心は  local.y + ground_offset.y
                //   球が床へ乗ったとき  球の中心 = 床の高さ + 半径
                //   よって              local.y = 床の高さ + 半径 - ground_offset.y
                //
                // 旧 Player は center_offset.y と半径が同じ値（0.38）だったため、
                // この式は「床の高さそのもの」に一致する。挙動は変わらない。
                ground_height_ = hit.position.y + shape.radius - shape.ground_offset.y;
                ground_normal_ = hit.normal;
                last_ground_source_ = hit.source;

                // Collision Event 配送用の記録。接地高さの計算とは独立。
                last_ground_point_ = hit.position;
            }
        }

        if (!vertical_physics)
        {
            // 垂直物理が無効な間は、旧 Player の SnapToGround と同じく
            // 地面の高さへ吸着させるだけにする。
            if (has_ground_)
            {
                DirectX::XMFLOAT3 snapped = owner->GetTransform().LocalPosition();
                snapped.y = ground_height_;
                owner->GetTransform().SetLocalPosition(snapped);
            }
            grounded_ = true;
            return;
        }

        DirectX::XMFLOAT3 current = owner->GetTransform().LocalPosition();
        if (current.y <= ground_height_ && velocity_.y <= 0.0f)
        {
            current.y = ground_height_;
            owner->GetTransform().SetLocalPosition(current);
            velocity_.y = 0.0f;
            grounded_ = true;
        }
        else
        {
            grounded_ = false;
        }
    }
}
