// PhysicsDynamicsWorld のうち「接触検出・Solver・Transform 同期」だけを持つ。
//
// PhysicsDynamicsWorld.cpp が用意した BodyState / ShapeProxy を受け取り、
// Solver の中身と実行順序は分割前から一文字も変更していない。

#include "PhysicsDynamicsWorld.h"

#include "../Components/Physics/BoxColliderComponent.h"
#include "../Components/Physics/CapsuleColliderComponent.h"
#include "../Components/Physics/ColliderComponent.h"
#include "../Components/Physics/MeshColliderComponent.h"
#include "../Components/Physics/RigidbodyComponent.h"
#include "../Components/Physics/SphereColliderComponent.h"
#include "../Components/Landscape/LandscapeColliderComponent.h"
#include "../Object/GameObject/GameObject.h"
#include "../Scene/Runtime/Scene.h"
#include "CollisionLayers.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

using namespace DirectX;

#include "PhysicsDynamicsWorldInternal.h"

using namespace DirectX;

namespace ReplayEngine::Scene
{
    namespace Layers = Physics::CollisionLayers;
    using namespace PhysicsDynamicsDetail;

    void PhysicsDynamicsWorld::ApplyPositionCorrection(BodyState* body_a,
        BodyState* body_b, const XMFLOAT3& normal, float penetration) const
    {
        if (penetration <= position_slop) return;
        const float inverse_a = body_a != nullptr ? body_a->inverse_mass : 0.0f;
        const float inverse_b = body_b != nullptr ? body_b->inverse_mass : 0.0f;
        const float inverse_sum = inverse_a + inverse_b;
        if (inverse_sum <= epsilon) return;

        const float correction = (penetration - position_slop) *
            position_correction_factor / inverse_sum;
        if (body_a != nullptr) body_a->position = Subtract(body_a->position,
            Scale(normal, correction * inverse_a));
        if (body_b != nullptr) body_b->position = Add(body_b->position,
            Scale(normal, correction * inverse_b));
    }

    void PhysicsDynamicsWorld::SolveContact(BodyState* body_a, BodyState* body_b,
        const ShapeProxy& shape_a, const ShapeProxy& shape_b,
        const Contact& contact)
    {
        if (!contact.valid) return;

        const float inverse_a = body_a != nullptr ? body_a->inverse_mass : 0.0f;
        const float inverse_b = body_b != nullptr ? body_b->inverse_mass : 0.0f;
        const float inverse_sum = inverse_a + inverse_b;
        if (inverse_sum <= epsilon) return;

        const XMFLOAT3 arm_a = body_a != nullptr
            ? Subtract(contact.point, shape_a.center) : XMFLOAT3{ 0.0f, 0.0f, 0.0f };
        const XMFLOAT3 arm_b = body_b != nullptr
            ? Subtract(contact.point, shape_b.center) : XMFLOAT3{ 0.0f, 0.0f, 0.0f };
        const float inverse_inertia_a = InverseInertia(shape_a, inverse_a);
        const float inverse_inertia_b = InverseInertia(shape_b, inverse_b);
        const XMFLOAT3 normal_arm_a = Cross(arm_a, contact.normal);
        const XMFLOAT3 normal_arm_b = Cross(arm_b, contact.normal);
        const float effective_inverse_sum = inverse_sum +
            inverse_inertia_a * LengthSquared(normal_arm_a) +
            inverse_inertia_b * LengthSquared(normal_arm_b);
        if (effective_inverse_sum <= epsilon) return;

        const XMFLOAT3 velocity_a = body_a != nullptr
            ? body_a->rigidbody->linear_velocity : XMFLOAT3{ 0.0f, 0.0f, 0.0f };
        const XMFLOAT3 velocity_b = body_b != nullptr
            ? body_b->rigidbody->linear_velocity : XMFLOAT3{ 0.0f, 0.0f, 0.0f };
        const XMFLOAT3 contact_velocity_a = body_a != nullptr
            ? Add(velocity_a, Cross(body_a->rigidbody->angular_velocity, arm_a)) : velocity_a;
        const XMFLOAT3 contact_velocity_b = body_b != nullptr
            ? Add(velocity_b, Cross(body_b->rigidbody->angular_velocity, arm_b)) : velocity_b;
        const XMFLOAT3 relative_velocity = Subtract(contact_velocity_b, contact_velocity_a);
        const float normal_speed = Dot(relative_velocity, contact.normal);

        if (normal_speed < 0.0f)
        {
            const float restitution = (std::min)(shape_a.restitution, shape_b.restitution);
            const float impulse_magnitude = -(1.0f + Clamp01(restitution)) * normal_speed /
                effective_inverse_sum;
            const XMFLOAT3 impulse = Scale(contact.normal, impulse_magnitude);
            if (body_a != nullptr) body_a->rigidbody->linear_velocity = Subtract(
                body_a->rigidbody->linear_velocity, Scale(impulse, inverse_a));
            if (body_b != nullptr) body_b->rigidbody->linear_velocity = Add(
                body_b->rigidbody->linear_velocity, Scale(impulse, inverse_b));
            ApplyAngularImpulse(body_a, shape_a, contact.point, Negate(impulse));
            ApplyAngularImpulse(body_b, shape_b, contact.point, impulse);

            const XMFLOAT3 tangent_velocity = Subtract(relative_velocity,
                Scale(contact.normal, normal_speed));
            const float tangent_length_squared = LengthSquared(tangent_velocity);
            if (tangent_length_squared > epsilon)
            {
                const XMFLOAT3 tangent = Scale(tangent_velocity,
                    1.0f / std::sqrt(tangent_length_squared));
                const float tangent_impulse = -Dot(relative_velocity, tangent) /
                    effective_inverse_sum;
                const float friction = std::sqrt((std::max)(0.0f,
                    shape_a.friction * shape_b.friction));
                const float limited = (std::max)(-impulse_magnitude * friction,
                    (std::min)(impulse_magnitude * friction, tangent_impulse));
                const XMFLOAT3 friction_impulse = Scale(tangent, limited);
                if (body_a != nullptr) body_a->rigidbody->linear_velocity = Subtract(
                    body_a->rigidbody->linear_velocity, Scale(friction_impulse, inverse_a));
                if (body_b != nullptr) body_b->rigidbody->linear_velocity = Add(
                    body_b->rigidbody->linear_velocity, Scale(friction_impulse, inverse_b));
                ApplyAngularImpulse(body_a, shape_a, contact.point, Negate(friction_impulse));
                ApplyAngularImpulse(body_b, shape_b, contact.point, friction_impulse);
            }
        }

        ApplyPositionCorrection(body_a, body_b, contact.normal, contact.penetration);
    }

    void PhysicsDynamicsWorld::SyncTransforms(std::vector<BodyState>& states,
        float fixed_delta_time)
    {
        for (BodyState& body : states)
        {
            if (!body.dynamic || body.owner == nullptr || body.rigidbody == nullptr) continue;

            body.owner->GetTransform().SetWorldPosition(body.position);
            XMFLOAT3 rotation = body.owner->GetTransform().LocalRotationEuler();
            rotation = Add(rotation, Scale(body.rigidbody->angular_velocity, fixed_delta_time));
            if (body.rigidbody->freeze_rotation.x > 0.5f) rotation.x =
                body.owner->GetTransform().LocalRotationEuler().x;
            if (body.rigidbody->freeze_rotation.y > 0.5f) rotation.y =
                body.owner->GetTransform().LocalRotationEuler().y;
            if (body.rigidbody->freeze_rotation.z > 0.5f) rotation.z =
                body.owner->GetTransform().LocalRotationEuler().z;
            body.owner->GetTransform().SetLocalRotationEuler(rotation);

            const float speed_squared = LengthSquared(body.rigidbody->linear_velocity);
            const float angular_squared = LengthSquared(body.rigidbody->angular_velocity);
            if (speed_squared < sleep_linear_speed * sleep_linear_speed &&
                angular_squared < sleep_angular_speed * sleep_angular_speed)
            {
                body.rigidbody->sleep_timer_ += fixed_delta_time;
                if (body.rigidbody->sleep_timer_ >= sleep_delay)
                {
                    body.rigidbody->is_sleeping = true;
                    body.rigidbody->linear_velocity = XMFLOAT3{ 0.0f, 0.0f, 0.0f };
                    body.rigidbody->angular_velocity = XMFLOAT3{ 0.0f, 0.0f, 0.0f };
                }
            }
            else
            {
                body.rigidbody->sleep_timer_ = 0.0f;
                body.rigidbody->is_sleeping = false;
            }
        }
    }

    bool PhysicsDynamicsWorld::SphereSphere(const ShapeProxy& a,
        const ShapeProxy& b, Contact& out) noexcept
    {
        const XMFLOAT3 delta = Subtract(b.center, a.center);
        const float distance_squared = LengthSquared(delta);
        const float radius_sum = a.radius + b.radius;
        if (distance_squared > radius_sum * radius_sum) return false;

        const float distance = std::sqrt((std::max)(0.0f, distance_squared));
        out.normal = Normalize(delta, XMFLOAT3{ 0.0f, 1.0f, 0.0f });
        out.penetration = radius_sum - distance;
        out.point = Add(a.center, Scale(out.normal, a.radius));
        out.valid = out.penetration > 0.0f;
        return out.valid;
    }

    bool PhysicsDynamicsWorld::SphereBox(const ShapeProxy& sphere,
        const ShapeProxy& box, Contact& out) noexcept
    {
        XMFLOAT3 closest{};
        const float distance_squared = PointBoxDistanceSquared(sphere.center, box.center,
            box.half_extents, closest);
        const float radius_squared = sphere.radius * sphere.radius;
        if (distance_squared > radius_squared) return false;

        if (distance_squared > epsilon)
        {
            out.normal = Normalize(Subtract(closest, sphere.center),
                XMFLOAT3{ 0.0f, 1.0f, 0.0f });
            out.penetration = sphere.radius - std::sqrt(distance_squared);
        }
        else
        {
            const XMFLOAT3 local = Subtract(sphere.center, box.center);
            const float distances[3]{ box.half_extents.x - std::fabs(local.x),
                box.half_extents.y - std::fabs(local.y), box.half_extents.z - std::fabs(local.z) };
            int axis = 0;
            if (distances[1] < distances[axis]) axis = 1;
            if (distances[2] < distances[axis]) axis = 2;
            out.normal = XMFLOAT3{ 0.0f, 0.0f, 0.0f };
            float sign = 1.0f;
            if ((axis == 0 && local.x >= 0.0f) || (axis == 1 && local.y >= 0.0f) ||
                (axis == 2 && local.z >= 0.0f)) sign = -1.0f;
            if (axis == 0) out.normal.x = sign;
            if (axis == 1) out.normal.y = sign;
            if (axis == 2) out.normal.z = sign;
            out.penetration = sphere.radius + distances[axis];
        }
        out.point = closest;
        out.valid = out.penetration > 0.0f;
        return out.valid;
    }

    bool PhysicsDynamicsWorld::CapsuleSphere(const ShapeProxy& capsule,
        const ShapeProxy& sphere, Contact& out) noexcept
    {
        const XMFLOAT3 closest = ClosestPointOnSegment(sphere.center,
            capsule.segment_a, capsule.segment_b);
        const XMFLOAT3 delta = Subtract(sphere.center, closest);
        const float distance_squared = LengthSquared(delta);
        const float radius_sum = capsule.radius + sphere.radius;
        if (distance_squared > radius_sum * radius_sum) return false;

        const float distance = std::sqrt((std::max)(0.0f, distance_squared));
        out.normal = Normalize(delta, XMFLOAT3{ 0.0f, 1.0f, 0.0f });
        out.penetration = radius_sum - distance;
        out.point = Add(closest, Scale(out.normal, capsule.radius));
        out.valid = out.penetration > 0.0f;
        return out.valid;
    }

    bool PhysicsDynamicsWorld::BoxBox(const ShapeProxy& a,
        const ShapeProxy& b, Contact& out) noexcept
    {
        const XMFLOAT3 delta = Subtract(b.center, a.center);
        const XMFLOAT3 total{ a.half_extents.x + b.half_extents.x,
            a.half_extents.y + b.half_extents.y, a.half_extents.z + b.half_extents.z };
        const XMFLOAT3 overlap{ total.x - std::fabs(delta.x),
            total.y - std::fabs(delta.y), total.z - std::fabs(delta.z) };
        if (overlap.x <= 0.0f || overlap.y <= 0.0f || overlap.z <= 0.0f) return false;

        int axis = 0;
        if (overlap.y < overlap.x) axis = 1;
        const float current_axis_overlap = axis == 0 ? overlap.x : overlap.y;
        if (overlap.z < current_axis_overlap) axis = 2;
        out.normal = XMFLOAT3{ 0.0f, 0.0f, 0.0f };
        const float direction = axis == 0 ? delta.x : (axis == 1 ? delta.y : delta.z);
        const float sign = direction >= 0.0f ? 1.0f : -1.0f;
        if (axis == 0) out.normal.x = sign;
        if (axis == 1) out.normal.y = sign;
        if (axis == 2) out.normal.z = sign;
        out.penetration = axis == 0 ? overlap.x : (axis == 1 ? overlap.y : overlap.z);
        out.point = XMFLOAT3{ (a.center.x + b.center.x) * 0.5f,
            (a.center.y + b.center.y) * 0.5f, (a.center.z + b.center.z) * 0.5f };
        out.valid = true;
        return true;
    }

    bool PhysicsDynamicsWorld::CapsuleCapsule(const ShapeProxy& a,
        const ShapeProxy& b, Contact& out) noexcept
    {
        XMFLOAT3 point_a{};
        XMFLOAT3 point_b{};
        SegmentClosestPoints(a.segment_a, a.segment_b, b.segment_a, b.segment_b,
            point_a, point_b);
        const XMFLOAT3 delta = Subtract(point_b, point_a);
        const float distance_squared = LengthSquared(delta);
        const float radius_sum = a.radius + b.radius;
        if (distance_squared > radius_sum * radius_sum) return false;

        const float distance = std::sqrt((std::max)(0.0f, distance_squared));
        out.normal = Normalize(delta, XMFLOAT3{ 0.0f, 1.0f, 0.0f });
        out.penetration = radius_sum - distance;
        out.point = Add(point_a, Scale(out.normal, a.radius));
        out.valid = out.penetration > 0.0f;
        return out.valid;
    }

    bool PhysicsDynamicsWorld::BoxCapsule(const ShapeProxy& box,
        const ShapeProxy& capsule, Contact& out) noexcept
    {
        XMFLOAT3 segment_point{};
        const XMFLOAT3 box_point = ClosestPointOnBoxForSegment(capsule.segment_a,
            capsule.segment_b, box.center, box.half_extents, segment_point);
        const XMFLOAT3 delta = Subtract(segment_point, box_point);
        const float distance_squared = LengthSquared(delta);
        if (distance_squared > capsule.radius * capsule.radius) return false;

        if (distance_squared > epsilon)
        {
            out.normal = Normalize(delta, XMFLOAT3{ 0.0f, 1.0f, 0.0f });
            out.penetration = capsule.radius - std::sqrt(distance_squared);
        }
        else
        {
            const XMFLOAT3 local = Subtract(segment_point, box.center);
            const float distances[3]{ box.half_extents.x - std::fabs(local.x),
                box.half_extents.y - std::fabs(local.y), box.half_extents.z - std::fabs(local.z) };
            int axis = 0;
            if (distances[1] < distances[axis]) axis = 1;
            if (distances[2] < distances[axis]) axis = 2;
            out.normal = XMFLOAT3{ 0.0f, 0.0f, 0.0f };
            if (axis == 0) out.normal.x = local.x >= 0.0f ? 1.0f : -1.0f;
            if (axis == 1) out.normal.y = local.y >= 0.0f ? 1.0f : -1.0f;
            if (axis == 2) out.normal.z = local.z >= 0.0f ? 1.0f : -1.0f;
            out.penetration = capsule.radius + distances[axis];
        }
        out.point = box_point;
        out.valid = out.penetration > 0.0f;
        return out.valid;
    }

    bool PhysicsDynamicsWorld::DetectContact(const ShapeProxy& shape_a,
        const ShapeProxy& shape_b, Contact& out) noexcept
    {
        out = Contact{};
        if (!shape_a.valid || !shape_b.valid ||
            !Layers::Interact(shape_a.layer, shape_a.mask, shape_b.layer, shape_b.mask))
        {
            return false;
        }

        if (shape_a.type == ShapeProxy::Type::Sphere && shape_b.type == ShapeProxy::Type::Sphere)
            return SphereSphere(shape_a, shape_b, out);
        if (shape_a.type == ShapeProxy::Type::Sphere && shape_b.type == ShapeProxy::Type::Box)
            return SphereBox(shape_a, shape_b, out);
        if (shape_a.type == ShapeProxy::Type::Box && shape_b.type == ShapeProxy::Type::Sphere)
        {
            const bool result = SphereBox(shape_b, shape_a, out);
            out.normal = Negate(out.normal);
            return result;
        }
        if (shape_a.type == ShapeProxy::Type::Sphere && shape_b.type == ShapeProxy::Type::Capsule)
        {
            const bool result = CapsuleSphere(shape_b, shape_a, out);
            out.normal = Negate(out.normal);
            return result;
        }
        if (shape_a.type == ShapeProxy::Type::Capsule && shape_b.type == ShapeProxy::Type::Sphere)
            return CapsuleSphere(shape_a, shape_b, out);
        if (shape_a.type == ShapeProxy::Type::Box && shape_b.type == ShapeProxy::Type::Box)
            return BoxBox(shape_a, shape_b, out);
        if (shape_a.type == ShapeProxy::Type::Capsule &&
            shape_b.type == ShapeProxy::Type::Capsule)
            return CapsuleCapsule(shape_a, shape_b, out);
        if (shape_a.type == ShapeProxy::Type::Box && shape_b.type == ShapeProxy::Type::Capsule)
            return BoxCapsule(shape_a, shape_b, out);
        if (shape_a.type == ShapeProxy::Type::Capsule && shape_b.type == ShapeProxy::Type::Box)
        {
            const bool result = BoxCapsule(shape_b, shape_a, out);
            out.normal = Negate(out.normal);
            return result;
        }
        return false;
    }

    void PhysicsDynamicsWorld::RecordContact(const BodyState* body_a,
        const BodyState* body_b, const ShapeProxy& shape_a,
        const ShapeProxy& shape_b, const Contact& contact)
    {
        if (!contact.valid || !shape_a.object.Valid() || !shape_b.object.Valid()) return;

        const auto same_pair = [&shape_a, &shape_b](const PhysicsContact& value)
        {
            return value.object_a == shape_a.object && value.collider_a == shape_a.collider &&
                value.object_b == shape_b.object && value.collider_b == shape_b.collider;
        };
        auto found = std::find_if(contacts_.begin(), contacts_.end(), same_pair);

        const XMFLOAT3 arm_a = Subtract(contact.point, shape_a.center);
        const XMFLOAT3 arm_b = Subtract(contact.point, shape_b.center);
        const XMFLOAT3 linear_a = body_a != nullptr && body_a->rigidbody != nullptr
            ? body_a->rigidbody->linear_velocity : XMFLOAT3{};
        const XMFLOAT3 linear_b = body_b != nullptr && body_b->rigidbody != nullptr
            ? body_b->rigidbody->linear_velocity : XMFLOAT3{};
        const XMFLOAT3 angular_a = body_a != nullptr && body_a->rigidbody != nullptr
            ? Cross(body_a->rigidbody->angular_velocity, arm_a) : XMFLOAT3{};
        const XMFLOAT3 angular_b = body_b != nullptr && body_b->rigidbody != nullptr
            ? Cross(body_b->rigidbody->angular_velocity, arm_b) : XMFLOAT3{};

        PhysicsContact value;
        value.object_a = shape_a.object;
        value.collider_a = shape_a.collider;
        value.object_b = shape_b.object;
        value.collider_b = shape_b.collider;
        value.point = contact.point;
        value.normal = contact.normal;
        value.relative_velocity = Subtract(Add(linear_b, angular_b), Add(linear_a, angular_a));
        value.penetration = contact.penetration;
        if (found == contacts_.end()) contacts_.push_back(value);
        else *found = value;
    }

    void PhysicsDynamicsWorld::Step(float fixed_delta_time)
    {
        // Body カウンタは BuildBodyStates() の先頭でリセットしてから実際に集計する。
        // ここで重複して 0 にすると、timeScale 0 で走らなかった step の直後に
        // 最後に実行された step 時点の Body 表が空に見える。表を空にするのは
        // AttachScene()/DetachScene() の責務なので、ここではカウンタを保持する。
        contacts_.clear();
        if (scene_ == nullptr || fixed_delta_time <= 0.0f) return;

        const std::uint32_t generation = scene_->StructureGeneration();
        if (!has_generation_ || generation != last_generation_)
        {
            last_generation_ = generation;
            has_generation_ = true;
            RebuildBodyKeys();
        }

        std::vector<BodyState> states;
        BuildBodyStates(fixed_delta_time, states);
        std::vector<ShapeProxy> static_shapes;
        BuildStaticShapes(states, static_shapes);

        std::vector<Core::ObjectID> excluded_query_objects;
        excluded_query_objects.reserve(states.size() + static_shapes.size());
        for (const BodyState& body : states)
        {
            // primitive 形状を持つ Body は Dynamic / Kinematic / Static を問わず
            // 下の pair Solver が正本なので Query から除外する。Mesh / Landscape
            // は has_shape が false のため、これまでどおり Query が正本のままになる。
            // body.dynamic は形状なし Dynamic の既存除外も保つため残している。
            if (body.dynamic || body.has_shape)
                excluded_query_objects.push_back(body.object);
        }
        for (const ShapeProxy& shape : static_shapes)
            excluded_query_objects.push_back(shape.object);

        for (BodyState& body : states)
            SweepAgainstStaticGeometry(body, excluded_query_objects);

        // 接触している Dynamic Body を同じ島として扱う。島の中に 1 体でも
        // 動いている Body があれば、眠っている Body も起こす。これを持たないと
        // 積み上げの下段だけが Sleep したまま、上段を押したときに遅れて崩れる。
        std::vector<std::size_t> island_parent(states.size());
        for (std::size_t index = 0; index < island_parent.size(); ++index)
            island_parent[index] = index;
        const auto find_island = [&island_parent](auto&& self,
            std::size_t index) -> std::size_t
        {
            if (island_parent[index] == index) return index;
            island_parent[index] = self(self, island_parent[index]);
            return island_parent[index];
        };
        const auto union_islands = [&find_island, &island_parent](std::size_t first,
            std::size_t second)
        {
            const std::size_t first_root = find_island(find_island, first);
            const std::size_t second_root = find_island(find_island, second);
            if (first_root != second_root) island_parent[second_root] = first_root;
        };

        for (int iteration = 0; iteration < solver_iterations_; ++iteration)
        {
            for (BodyState& body : states)
            {
                if (!body.dynamic || !body.has_shape) continue;
                ShapeProxy dynamic_shape;
                if (!BuildShapeAt(body, dynamic_shape)) continue;

                for (const ShapeProxy& static_shape : static_shapes)
                {
                    Contact contact;
                    if (DetectContact(dynamic_shape, static_shape, contact))
                    {
                        RecordContact(&body, nullptr, dynamic_shape, static_shape, contact);
                        SolveContact(&body, nullptr, dynamic_shape, static_shape, contact);
                    }
                }
            }

            for (std::size_t first = 0; first < states.size(); ++first)
            {
                BodyState& body_a = states[first];
                if (!body_a.has_shape) continue;
                ShapeProxy shape_a;
                if (!BuildShapeAt(body_a, shape_a)) continue;

                for (std::size_t second = first + 1; second < states.size(); ++second)
                {
                    BodyState& body_b = states[second];
                    if (!body_b.has_shape || (!body_a.dynamic && !body_b.dynamic)) continue;
                    ShapeProxy shape_b;
                    if (!BuildShapeAt(body_b, shape_b)) continue;

                    Contact contact;
                    if (DetectContact(shape_a, shape_b, contact))
                    {
                        RecordContact(&body_a, &body_b, shape_a, shape_b, contact);
                        if (body_a.dynamic && body_b.dynamic)
                            union_islands(first, second);
                        SolveContact(&body_a, &body_b, shape_a, shape_b, contact);
                    }
                }
            }
        }

        std::vector<bool> active_island(states.size(), false);
        for (std::size_t index = 0; index < states.size(); ++index)
        {
            const BodyState& body = states[index];
            if (!body.dynamic || body.rigidbody == nullptr || body.rigidbody->is_sleeping) continue;
            const std::size_t root = find_island(find_island, index);
            if (LengthSquared(body.rigidbody->linear_velocity) >
                    sleep_linear_speed * sleep_linear_speed ||
                LengthSquared(body.rigidbody->angular_velocity) >
                    sleep_angular_speed * sleep_angular_speed)
            {
                active_island[root] = true;
            }
        }
        for (std::size_t index = 0; index < states.size(); ++index)
        {
            BodyState& body = states[index];
            if (!body.dynamic || body.rigidbody == nullptr || !body.rigidbody->is_sleeping) continue;
            const std::size_t root = find_island(find_island, index);
            if (active_island[root])
            {
                body.rigidbody->is_sleeping = false;
                body.rigidbody->sleep_timer_ = 0.0f;
            }
        }

        SyncTransforms(states, fixed_delta_time);
    }
}
