// PhysicsDynamicsWorld の責務を 2 つのファイルへ分けている:
//   PhysicsDynamicsWorld.cpp         … Scene 接続・Body 表・Shape 準備と静的形状 Query（このファイル）
//   PhysicsDynamicsWorldSolver.cpp  … 接触検出・Solver・Transform 同期
//
// 接触 Solver の経路は PhysicsDynamicsWorldSolver.cpp に移しただけで、
// 数式と実行順序は変更していない。
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
        const Core::GameObject* owner = collider.Owner();
        const auto* rigidbody = owner != nullptr
            ? owner->GetComponent<Components::RigidbodyComponent>() : nullptr;
        // ShapeProxy は Solver に渡る前の唯一の関門なので、ここで材質値を正常化する。
        // Solver 側では値が正常だと仮定し、両側で二重にクランプして正本を曖昧にしない。
        out.friction = rigidbody != nullptr
            ? (std::max)(0.0f, SafeFloat(rigidbody->friction, 0.5f)) : 0.5f;
        out.restitution = rigidbody != nullptr
            ? Clamp01(SafeFloat(rigidbody->restitution, 0.0f)) : 0.0f;

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


}
