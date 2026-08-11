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

namespace ReplayEngine::Scene
{
    namespace
    {
        namespace Layers = Physics::CollisionLayers;

        constexpr float gravity_y = -9.81f;
        constexpr float sleep_linear_speed = 0.05f;
        constexpr float sleep_angular_speed = 0.05f;
        constexpr float sleep_delay = 0.5f;
        constexpr float position_slop = 0.005f;
        constexpr float position_correction_factor = 0.2f;
        constexpr float epsilon = 1.0e-6f;

        float Clamp01(float value) noexcept
        {
            return (std::max)(0.0f, (std::min)(1.0f, value));
        }

        float SafeFloat(float value, float fallback) noexcept
        {
            return std::isfinite(value) ? value : fallback;
        }

        XMFLOAT3 Negate(const XMFLOAT3& value) noexcept
        {
            return XMFLOAT3{ -value.x, -value.y, -value.z };
        }

        XMFLOAT3 ClampPointToBox(const XMFLOAT3& point,
            const XMFLOAT3& center, const XMFLOAT3& half) noexcept
        {
            return XMFLOAT3{
                (std::max)(center.x - half.x, (std::min)(center.x + half.x, point.x)),
                (std::max)(center.y - half.y, (std::min)(center.y + half.y, point.y)),
                (std::max)(center.z - half.z, (std::min)(center.z + half.z, point.z)) };
        }

        float PointBoxDistanceSquared(const XMFLOAT3& point,
            const XMFLOAT3& center, const XMFLOAT3& half,
            XMFLOAT3& closest) noexcept
        {
            closest = ClampPointToBox(point, center, half);
            const XMFLOAT3 difference{
                point.x - closest.x, point.y - closest.y, point.z - closest.z };
            return difference.x * difference.x + difference.y * difference.y +
                difference.z * difference.z;
        }

        XMFLOAT3 ClosestPointOnSegment(const XMFLOAT3& point,
            const XMFLOAT3& a, const XMFLOAT3& b) noexcept
        {
            const XMFLOAT3 edge{ b.x - a.x, b.y - a.y, b.z - a.z };
            const float denominator = edge.x * edge.x + edge.y * edge.y + edge.z * edge.z;
            if (denominator <= epsilon) return a;

            const XMFLOAT3 from_a{ point.x - a.x, point.y - a.y, point.z - a.z };
            const float parameter = Clamp01((from_a.x * edge.x + from_a.y * edge.y +
                from_a.z * edge.z) / denominator);
            return XMFLOAT3{ a.x + edge.x * parameter, a.y + edge.y * parameter,
                a.z + edge.z * parameter };
        }

        float DistanceSquared(const XMFLOAT3& a, const XMFLOAT3& b) noexcept
        {
            const XMFLOAT3 difference{ a.x - b.x, a.y - b.y, a.z - b.z };
            return difference.x * difference.x + difference.y * difference.y +
                difference.z * difference.z;
        }

        void SegmentClosestPoints(const XMFLOAT3& a0, const XMFLOAT3& a1,
            const XMFLOAT3& b0, const XMFLOAT3& b1,
            XMFLOAT3& point_a, XMFLOAT3& point_b) noexcept
        {
            const XMFLOAT3 d1{ a1.x - a0.x, a1.y - a0.y, a1.z - a0.z };
            const XMFLOAT3 d2{ b1.x - b0.x, b1.y - b0.y, b1.z - b0.z };
            const XMFLOAT3 r{ a0.x - b0.x, a0.y - b0.y, a0.z - b0.z };
            const float a = d1.x * d1.x + d1.y * d1.y + d1.z * d1.z;
            const float e = d2.x * d2.x + d2.y * d2.y + d2.z * d2.z;
            const float f = d2.x * r.x + d2.y * r.y + d2.z * r.z;

            float s = 0.0f;
            float t = 0.0f;
            if (a <= epsilon && e <= epsilon)
            {
                point_a = a0;
                point_b = b0;
                return;
            }

            if (a <= epsilon)
            {
                s = 0.0f;
                t = Clamp01(f / e);
            }
            else
            {
                const float c = d1.x * r.x + d1.y * r.y + d1.z * r.z;
                if (e <= epsilon)
                {
                    t = 0.0f;
                    s = Clamp01(-c / a);
                }
                else
                {
                    const float b = d1.x * d2.x + d1.y * d2.y + d1.z * d2.z;
                    const float denominator = a * e - b * b;
                    s = denominator > epsilon ? Clamp01((b * f - c * e) / denominator) : 0.0f;
                    t = (b * s + f) / e;
                    if (t < 0.0f)
                    {
                        t = 0.0f;
                        s = Clamp01(-c / a);
                    }
                    else if (t > 1.0f)
                    {
                        t = 1.0f;
                        s = Clamp01((b - c) / a);
                    }
                }
            }

            point_a = XMFLOAT3{ a0.x + d1.x * s, a0.y + d1.y * s, a0.z + d1.z * s };
            point_b = XMFLOAT3{ b0.x + d2.x * t, b0.y + d2.y * t, b0.z + d2.z * t };
        }

        XMFLOAT3 ClosestPointOnBoxForSegment(const XMFLOAT3& a,
            const XMFLOAT3& b, const XMFLOAT3& center,
            const XMFLOAT3& half, XMFLOAT3& segment_point) noexcept
        {
            // 点と AABB の距離は凸関数なので、三分探索で線分上の最近接点を
            // 求める。回転箱は Collider のワールド AABB として安全側に扱う。
            float low = 0.0f;
            float high = 1.0f;
            const XMFLOAT3 edge{ b.x - a.x, b.y - a.y, b.z - a.z };
            for (int iteration = 0; iteration < 12; ++iteration)
            {
                const float first = low + (high - low) / 3.0f;
                const float second = high - (high - low) / 3.0f;
                const XMFLOAT3 first_point{ a.x + edge.x * first, a.y + edge.y * first,
                    a.z + edge.z * first };
                const XMFLOAT3 second_point{ a.x + edge.x * second, a.y + edge.y * second,
                    a.z + edge.z * second };
                XMFLOAT3 first_closest{};
                XMFLOAT3 second_closest{};
                const float first_distance = PointBoxDistanceSquared(first_point, center, half,
                    first_closest);
                const float second_distance = PointBoxDistanceSquared(second_point, center, half,
                    second_closest);
                if (first_distance < second_distance) high = second;
                else low = first;
            }

            const float parameter = (low + high) * 0.5f;
            segment_point = XMFLOAT3{ a.x + edge.x * parameter,
                a.y + edge.y * parameter, a.z + edge.z * parameter };
            return ClampPointToBox(segment_point, center, half);
        }
    }

    XMFLOAT3 PhysicsDynamicsWorld::Add(const XMFLOAT3& a, const XMFLOAT3& b) noexcept
    {
        return XMFLOAT3{ a.x + b.x, a.y + b.y, a.z + b.z };
    }

    XMFLOAT3 PhysicsDynamicsWorld::Subtract(const XMFLOAT3& a, const XMFLOAT3& b) noexcept
    {
        return XMFLOAT3{ a.x - b.x, a.y - b.y, a.z - b.z };
    }

    XMFLOAT3 PhysicsDynamicsWorld::Scale(const XMFLOAT3& value, float factor) noexcept
    {
        return XMFLOAT3{ value.x * factor, value.y * factor, value.z * factor };
    }

    float PhysicsDynamicsWorld::Dot(const XMFLOAT3& a, const XMFLOAT3& b) noexcept
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    float PhysicsDynamicsWorld::LengthSquared(const XMFLOAT3& value) noexcept
    {
        return Dot(value, value);
    }

    XMFLOAT3 PhysicsDynamicsWorld::Normalize(const XMFLOAT3& value,
        const XMFLOAT3& fallback) noexcept
    {
        const float length_squared = LengthSquared(value);
        if (length_squared <= epsilon) return fallback;
        return Scale(value, 1.0f / std::sqrt(length_squared));
    }

    XMFLOAT3 PhysicsDynamicsWorld::Cross(const XMFLOAT3& a, const XMFLOAT3& b) noexcept
    {
        return XMFLOAT3{ a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }

    float PhysicsDynamicsWorld::InverseInertia(const ShapeProxy& shape,
        float inverse_mass) noexcept
    {
        if (inverse_mass <= 0.0f) return 0.0f;

        float unit_inertia = 1.0f;
        switch (shape.type)
        {
        case ShapeProxy::Type::Sphere:
            unit_inertia = 0.4f * shape.radius * shape.radius;
            break;
        case ShapeProxy::Type::Box:
            unit_inertia = (shape.half_extents.x * shape.half_extents.x +
                shape.half_extents.y * shape.half_extents.y +
                shape.half_extents.z * shape.half_extents.z) / 3.0f;
            break;
        case ShapeProxy::Type::Capsule:
        {
            const float half_length = 0.5f * std::sqrt(
                LengthSquared(Subtract(shape.segment_b, shape.segment_a)));
            unit_inertia = 0.4f * shape.radius * shape.radius +
                half_length * half_length;
            break;
        }
        }
        return 1.0f / (std::max)(epsilon, unit_inertia / inverse_mass);
    }

    void PhysicsDynamicsWorld::ApplyAngularImpulse(BodyState* body,
        const ShapeProxy& shape, const XMFLOAT3& point,
        const XMFLOAT3& impulse) noexcept
    {
        if (body == nullptr || body->rigidbody == nullptr || body->inverse_mass <= 0.0f)
            return;

        const XMFLOAT3 arm = Subtract(point, shape.center);
        body->rigidbody->angular_velocity = Add(body->rigidbody->angular_velocity,
            Scale(Cross(arm, impulse), InverseInertia(shape, body->inverse_mass)));
        if (body->rigidbody->freeze_rotation.x > 0.5f)
            body->rigidbody->angular_velocity.x = 0.0f;
        if (body->rigidbody->freeze_rotation.y > 0.5f)
            body->rigidbody->angular_velocity.y = 0.0f;
        if (body->rigidbody->freeze_rotation.z > 0.5f)
            body->rigidbody->angular_velocity.z = 0.0f;
    }

    void PhysicsDynamicsWorld::SetSolverIterations(int value) noexcept
    {
        solver_iterations_ = (std::max)(1, (std::min)(32, value));
    }

    void PhysicsDynamicsWorld::AttachScene(Scene* scene)
    {
        if (scene_ == scene) return;
        scene_ = scene;
        body_keys_.clear();
        last_generation_ = 0;
        has_generation_ = false;
        dynamic_body_count_ = 0;
        sleeping_body_count_ = 0;
    }

    void PhysicsDynamicsWorld::DetachScene()
    {
        AttachScene(nullptr);
    }

    void PhysicsDynamicsWorld::RebuildBodyKeys()
    {
        body_keys_.clear();
        if (scene_ == nullptr) return;

        for (std::size_t index = 0; index < scene_->GameObjectCount(); ++index)
        {
            Core::GameObject* object = scene_->GameObjectAt(index);
            if (object == nullptr || object->PendingDestroy()) continue;

            auto* rigidbody = object->GetComponent<Components::RigidbodyComponent>();
            if (rigidbody == nullptr) continue;

            BodyKey key;
            key.object = object->ID();
            for (std::size_t slot = 0; slot < object->ComponentCount(); ++slot)
            {
                auto* collider = dynamic_cast<Components::ColliderComponent*>(
                    object->ComponentAt(slot));
                if (collider != nullptr && !collider->PendingDestroy())
                {
                    key.collider = collider->GetColliderID();
                    break;
                }
            }
            body_keys_.push_back(key);
        }
    }

    bool PhysicsDynamicsWorld::BuildShape(const Components::ColliderComponent& collider,
        const XMFLOAT3& center_delta, ShapeProxy& out) const
    {
        if (!collider.ActiveInHierarchy() || collider.is_trigger) return false;

        out = ShapeProxy{};
        out.object = collider.Owner() != nullptr
            ? collider.Owner()->ID() : Core::ObjectID::Invalid();
        out.collider = collider.GetColliderID();
        out.layer = Layers::ClampLayer(collider.collision_layer);
        out.mask = collider.collision_mask;
        out.friction = 0.5f;
        out.restitution = 0.0f;

        switch (collider.Shape())
        {
        case Components::ColliderShape::Sphere:
        {
            const auto& sphere = static_cast<const Components::SphereColliderComponent&>(collider);
            out.type = ShapeProxy::Type::Sphere;
            out.center = Add(sphere.WorldCenter(), center_delta);
            out.radius = SafeFloat(sphere.EffectiveRadius(), 0.0f);
            out.valid = out.radius > 0.0f;
            break;
        }
        case Components::ColliderShape::Box:
        {
            const auto& box = static_cast<const Components::BoxColliderComponent&>(collider);
            XMFLOAT3 minimum{};
            XMFLOAT3 maximum{};
            if (!box.ComputeWorldBounds(minimum, maximum)) return false;
            out.type = ShapeProxy::Type::Box;
            out.center = Add(XMFLOAT3{ (minimum.x + maximum.x) * 0.5f,
                (minimum.y + maximum.y) * 0.5f, (minimum.z + maximum.z) * 0.5f }, center_delta);
            out.half_extents = XMFLOAT3{ (maximum.x - minimum.x) * 0.5f,
                (maximum.y - minimum.y) * 0.5f, (maximum.z - minimum.z) * 0.5f };
            out.valid = out.half_extents.x > 0.0f && out.half_extents.y > 0.0f &&
                out.half_extents.z > 0.0f;
            break;
        }
        case Components::ColliderShape::Capsule:
        {
            const auto& capsule = static_cast<const Components::CapsuleColliderComponent&>(collider);
            out.type = ShapeProxy::Type::Capsule;
            capsule.WorldSegment(out.segment_a, out.segment_b);
            out.segment_a = Add(out.segment_a, center_delta);
            out.segment_b = Add(out.segment_b, center_delta);
            out.center = XMFLOAT3{ (out.segment_a.x + out.segment_b.x) * 0.5f,
                (out.segment_a.y + out.segment_b.y) * 0.5f,
                (out.segment_a.z + out.segment_b.z) * 0.5f };
            out.radius = SafeFloat(capsule.EffectiveRadius(), 0.0f);
            out.valid = out.radius > 0.0f;
            break;
        }
        case Components::ColliderShape::Mesh:
        case Components::ColliderShape::Landscape:
            return false;
        }
        return out.valid;
    }

    bool PhysicsDynamicsWorld::BuildShapeAt(const BodyState& body, ShapeProxy& out) const
    {
        if (body.shape_source == nullptr || body.owner == nullptr) return false;
        const XMFLOAT3 center_delta = Subtract(body.position, body.initial_position);
        return BuildShape(*body.shape_source, center_delta, out);
    }

    void PhysicsDynamicsWorld::BuildBodyStates(float fixed_delta_time,
        std::vector<BodyState>& out_states)
    {
        out_states.clear();
        dynamic_body_count_ = 0;
        sleeping_body_count_ = 0;
        if (scene_ == nullptr) return;

        for (const BodyKey& key : body_keys_)
        {
            Core::GameObject* owner = scene_->FindGameObjectByID(key.object);
            if (owner == nullptr || owner->PendingDestroy() || !owner->ActiveInHierarchy()) continue;

            auto* rigidbody = owner->GetComponent<Components::RigidbodyComponent>();
            if (rigidbody == nullptr || !rigidbody->ActiveInHierarchy()) continue;

            if (!rigidbody->RuntimeInitialized())
            {
                rigidbody->is_sleeping = rigidbody->start_asleep;
                rigidbody->MarkRuntimeInitialized();
            }

            BodyState body;
            body.object = key.object;
            body.collider = key.collider;
            body.owner = owner;
            body.rigidbody = rigidbody;
            body.shape_source = Components::FindColliderByID(*owner, key.collider);
            body.initial_position = owner->GetTransform().WorldPosition();
            body.position = body.initial_position;

            int body_type = rigidbody->body_type;
            if (body_type < Components::RigidbodyComponent::BodyType_Static ||
                body_type > Components::RigidbodyComponent::BodyType_Dynamic)
            {
                body_type = Components::RigidbodyComponent::BodyType_Dynamic;
            }

            body.dynamic = body_type == Components::RigidbodyComponent::BodyType_Dynamic;
            body.kinematic = body_type == Components::RigidbodyComponent::BodyType_Kinematic;

            if (body.shape_source != nullptr &&
                (body.shape_source->Shape() == Components::ColliderShape::Mesh ||
                 body.shape_source->Shape() == Components::ColliderShape::Landscape))
            {
                // 任意三角形の Dynamic は慣性・CCD・接触安定性を保証できない。
                // 保存値は残し、実効種別だけ Kinematic へ倒して警告を表示する。
                if (body.dynamic)
                {
                    body.dynamic = false;
                    body.kinematic = true;
                }
            }

            const float safe_mass = SafeFloat(rigidbody->mass, 1.0f);
            body.inverse_mass = body.dynamic && safe_mass > 0.001f
                ? 1.0f / (std::max)(0.001f, safe_mass) : 0.0f;
            body.has_shape = body.shape_source != nullptr &&
                BuildShape(*body.shape_source, XMFLOAT3{ 0.0f, 0.0f, 0.0f },
                    body.shape_at_start);

            if (body.dynamic)
            {
                ++dynamic_body_count_;
                if (rigidbody->is_sleeping)
                {
                    ++sleeping_body_count_;
                    rigidbody->ClearAccumulatedForces();
                }
                else
                {
                    const XMFLOAT3 force = rigidbody->AccumulatedForce();
                    const XMFLOAT3 acceleration = XMFLOAT3{
                        force.x * body.inverse_mass,
                        force.y * body.inverse_mass + gravity_y * rigidbody->gravity_scale,
                        force.z * body.inverse_mass };
                    rigidbody->linear_velocity = Add(rigidbody->linear_velocity,
                        Scale(acceleration, fixed_delta_time));

                    const XMFLOAT3 torque = rigidbody->AccumulatedTorque();
                    rigidbody->angular_velocity = Add(rigidbody->angular_velocity,
                        Scale(torque, body.inverse_mass * fixed_delta_time));

                    const float linear_damping = (std::max)(0.0f,
                        SafeFloat(rigidbody->linear_damping, 0.0f));
                    const float angular_damping = (std::max)(0.0f,
                        SafeFloat(rigidbody->angular_damping, 0.0f));
                    rigidbody->linear_velocity = Scale(rigidbody->linear_velocity,
                        std::exp(-linear_damping * fixed_delta_time));
                    rigidbody->angular_velocity = Scale(rigidbody->angular_velocity,
                        std::exp(-angular_damping * fixed_delta_time));

                    if (rigidbody->freeze_position.x > 0.5f) rigidbody->linear_velocity.x = 0.0f;
                    if (rigidbody->freeze_position.y > 0.5f) rigidbody->linear_velocity.y = 0.0f;
                    if (rigidbody->freeze_position.z > 0.5f) rigidbody->linear_velocity.z = 0.0f;
                    if (rigidbody->freeze_rotation.x > 0.5f) rigidbody->angular_velocity.x = 0.0f;
                    if (rigidbody->freeze_rotation.y > 0.5f) rigidbody->angular_velocity.y = 0.0f;
                    if (rigidbody->freeze_rotation.z > 0.5f) rigidbody->angular_velocity.z = 0.0f;

                    body.position = Add(body.position,
                        Scale(rigidbody->linear_velocity, fixed_delta_time));
                    if (rigidbody->freeze_position.x > 0.5f) body.position.x = body.initial_position.x;
                    if (rigidbody->freeze_position.y > 0.5f) body.position.y = body.initial_position.y;
                    if (rigidbody->freeze_position.z > 0.5f) body.position.z = body.initial_position.z;
                    rigidbody->ClearAccumulatedForces();
                }
            }
            else
            {
                // Kinematic は Transform が正本。Static / Kinematic の速度は
                // Solver のための逆質量を持たないためここでは積分しない。
                rigidbody->ClearAccumulatedForces();
            }

            out_states.push_back(body);
        }
    }

    void PhysicsDynamicsWorld::BuildStaticShapes(const std::vector<BodyState>& states,
        std::vector<ShapeProxy>& out_shapes) const
    {
        out_shapes.clear();
        if (scene_ == nullptr) return;

        std::unordered_set<Core::ObjectID> rigidbody_objects;
        rigidbody_objects.reserve(states.size());
        for (const BodyState& body : states) rigidbody_objects.insert(body.object);

        for (std::size_t index = 0; index < scene_->GameObjectCount(); ++index)
        {
            Core::GameObject* object = scene_->GameObjectAt(index);
            if (object == nullptr || object->PendingDestroy() ||
                rigidbody_objects.find(object->ID()) != rigidbody_objects.end()) continue;

            for (std::size_t slot = 0; slot < object->ComponentCount(); ++slot)
            {
                auto* collider = dynamic_cast<Components::ColliderComponent*>(
                    object->ComponentAt(slot));
                if (collider == nullptr || collider->PendingDestroy()) continue;

                ShapeProxy shape;
                if (BuildShape(*collider, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, shape))
                {
                    out_shapes.push_back(shape);
                }
            }
        }
    }

    void PhysicsDynamicsWorld::SweepAgainstStaticGeometry(BodyState& body,
        const std::vector<Core::ObjectID>& excluded_objects) const
    {
        if (scene_ == nullptr || !body.dynamic || !body.has_shape || body.shape_source == nullptr)
            return;

        const IPhysicsQueryService* query = scene_->Services().Physics();
        if (query == nullptr) return;

        ShapeProxy start_shape = body.shape_at_start;
        ShapeProxy end_shape;
        if (!BuildShapeAt(body, end_shape)) return;

        float cast_radius = end_shape.radius;
        switch (end_shape.type)
        {
        case ShapeProxy::Type::Sphere:
            cast_radius = end_shape.radius;
            break;
        case ShapeProxy::Type::Box:
            cast_radius = end_shape.half_extents.y;
            break;
        case ShapeProxy::Type::Capsule:
            cast_radius = end_shape.radius + (std::max)(
                std::fabs(end_shape.segment_a.y - end_shape.center.y),
                std::fabs(end_shape.segment_b.y - end_shape.center.y));
            break;
        }
        if (cast_radius <= 0.0f) return;

        CollisionQueryFilter filter;
        filter.layer = start_shape.layer;
        filter.mask = start_shape.mask;
        filter.ignore_object = body.object;

        SphereSweepHit hit;
        if (!query->SweepSphereFiltered(start_shape.center, end_shape.center,
            cast_radius, 1.0f, filter, hit) || !hit.valid)
        {
            return;
        }

        // Primitive Collider / Rigidbody 同士は下の接触 Solver が正本なので、
        // Query が返した同じ面を二重に解かない。Mesh / Landscape だけここで扱う。
        for (const Core::ObjectID excluded : excluded_objects)
        {
            if (excluded == hit.source.object) return;
        }

        const XMFLOAT3 movement = Subtract(hit.center, start_shape.center);
        body.position = Add(body.initial_position, movement);

        XMFLOAT3 normal = Normalize(hit.normal, XMFLOAT3{ 0.0f, 1.0f, 0.0f });
        const float normal_speed = Dot(body.rigidbody->linear_velocity, normal);
        if (normal_speed < 0.0f)
        {
            body.rigidbody->linear_velocity = Subtract(body.rigidbody->linear_velocity,
                Scale(normal, (1.0f + Clamp01(body.rigidbody->restitution)) * normal_speed));

            const XMFLOAT3 tangent = Subtract(body.rigidbody->linear_velocity,
                Scale(normal, Dot(body.rigidbody->linear_velocity, normal)));
            body.rigidbody->linear_velocity = Subtract(body.rigidbody->linear_velocity,
                Scale(tangent, (std::max)(0.0f, (std::min)(1.0f,
                    body.rigidbody->friction * 8.0f * 1.0f / 60.0f))));
        }
    }

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

    void PhysicsDynamicsWorld::Step(float fixed_delta_time)
    {
        dynamic_body_count_ = 0;
        sleeping_body_count_ = 0;
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
            // Dynamic body 同士は下の pair Solver が正本。Kinematic の primitive も
            // pair Solver で扱うため Query から除外する。
            if (body.dynamic || (body.kinematic && body.has_shape))
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
