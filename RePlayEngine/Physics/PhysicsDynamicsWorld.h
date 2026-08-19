#pragma once

#include "../Scene/Services/IPhysicsDynamicsService.h"
#include "../Scene/Services/IPhysicsQueryService.h"
#include "../Core/ObjectID/ObjectID.h"

#include <DirectXMath.h>

#include <cstdint>
#include <vector>

namespace ReplayEngine::Components
{
    class ColliderComponent;
    class RigidbodyComponent;
}

namespace ReplayEngine::Core
{
    class GameObject;
}

namespace ReplayEngine::Scene
{
    class Scene;

    // 外部依存を持たない Rigidbody Solver。
    //
    // Shape の持ち方は ColliderComponent を正本にし、ここには Body の ID と
    // Solver の実行時状態だけを置く。Scene 切替時に AttachScene が全表を捨てる
    // ため、古い World の生ポインタや ColliderID が残らない。
    class PhysicsDynamicsWorld final : public IPhysicsDynamicsService
    {
    public:
        PhysicsDynamicsWorld() = default;
        ~PhysicsDynamicsWorld() override = default;

        void AttachScene(Scene* scene) override;
        void DetachScene() override;
        const Scene* AttachedScene() const noexcept override { return scene_; }
        void Step(float fixed_delta_time) override;

        std::size_t BodyCount() const noexcept { return body_keys_.size(); }
        std::size_t DynamicBodyCount() const noexcept { return dynamic_body_count_; }
        std::size_t SleepingBodyCount() const noexcept { return sleeping_body_count_; }
        int SolverIterations() const noexcept { return solver_iterations_; }

        // 調整可能な設定。Solver の品質をビルドごとの定数へ閉じ込めない。
        void SetSolverIterations(int value) noexcept;

    private:
        struct BodyKey
        {
            Core::ObjectID object;
            ColliderID collider = invalid_collider_id;
        };

        struct ShapeProxy
        {
            enum class Type { Sphere, Box, Capsule };

            Type type = Type::Sphere;
            Core::ObjectID object;
            ColliderID collider = invalid_collider_id;
            DirectX::XMFLOAT3 center{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 half_extents{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 segment_a{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 segment_b{ 0.0f, 0.0f, 0.0f };
            float radius = 0.0f;
            int layer = 0;
            int mask = -1;
            float friction = 0.5f;
            float restitution = 0.0f;
            bool valid = false;
        };

        struct BodyState
        {
            Core::ObjectID object;
            ColliderID collider = invalid_collider_id;
            Core::GameObject* owner = nullptr;
            Components::RigidbodyComponent* rigidbody = nullptr;
            Components::ColliderComponent* shape_source = nullptr;
            ShapeProxy shape_at_start;
            DirectX::XMFLOAT3 initial_position{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
            float inverse_mass = 0.0f;
            bool dynamic = false;
            bool kinematic = false;
            bool has_shape = false;
        };

        struct Contact
        {
            DirectX::XMFLOAT3 normal{ 0.0f, 1.0f, 0.0f };
            DirectX::XMFLOAT3 point{ 0.0f, 0.0f, 0.0f };
            float penetration = 0.0f;
            bool valid = false;
        };

        void RebuildBodyKeys();
        void BuildBodyStates(float fixed_delta_time,
            std::vector<BodyState>& out_states);
        void BuildStaticShapes(const std::vector<BodyState>& states,
            std::vector<ShapeProxy>& out_shapes) const;
        bool BuildShape(const Components::ColliderComponent& collider,
            const DirectX::XMFLOAT3& center_delta, ShapeProxy& out) const;
        bool BuildShapeAt(const BodyState& body, ShapeProxy& out) const;

        void SweepAgainstStaticGeometry(BodyState& body,
            const std::vector<Core::ObjectID>& excluded_objects) const;
        void SolveContact(BodyState* body_a, BodyState* body_b,
            const ShapeProxy& shape_a, const ShapeProxy& shape_b,
            const Contact& contact);
        void ApplyPositionCorrection(BodyState* body_a, BodyState* body_b,
            const DirectX::XMFLOAT3& normal, float penetration) const;
        void SyncTransforms(std::vector<BodyState>& states, float fixed_delta_time);
        static bool DetectContact(const ShapeProxy& shape_a,
            const ShapeProxy& shape_b, Contact& out) noexcept;

        static bool SphereSphere(const ShapeProxy& a, const ShapeProxy& b,
            Contact& out) noexcept;
        static bool SphereBox(const ShapeProxy& sphere, const ShapeProxy& box,
            Contact& out) noexcept;
        static bool CapsuleSphere(const ShapeProxy& capsule,
            const ShapeProxy& sphere, Contact& out) noexcept;
        static bool BoxBox(const ShapeProxy& a, const ShapeProxy& b,
            Contact& out) noexcept;
        static bool CapsuleCapsule(const ShapeProxy& a, const ShapeProxy& b,
            Contact& out) noexcept;
        static bool BoxCapsule(const ShapeProxy& box, const ShapeProxy& capsule,
            Contact& out) noexcept;

        static DirectX::XMFLOAT3 Add(const DirectX::XMFLOAT3& a,
            const DirectX::XMFLOAT3& b) noexcept;
        static DirectX::XMFLOAT3 Subtract(const DirectX::XMFLOAT3& a,
            const DirectX::XMFLOAT3& b) noexcept;
        static DirectX::XMFLOAT3 Scale(const DirectX::XMFLOAT3& value,
            float factor) noexcept;
        static float Dot(const DirectX::XMFLOAT3& a,
            const DirectX::XMFLOAT3& b) noexcept;
        static float LengthSquared(const DirectX::XMFLOAT3& value) noexcept;
        static DirectX::XMFLOAT3 Normalize(const DirectX::XMFLOAT3& value,
            const DirectX::XMFLOAT3& fallback) noexcept;
        static DirectX::XMFLOAT3 Cross(const DirectX::XMFLOAT3& a,
            const DirectX::XMFLOAT3& b) noexcept;
        static float InverseInertia(const ShapeProxy& shape,
            float inverse_mass) noexcept;
        static void ApplyAngularImpulse(BodyState* body,
            const ShapeProxy& shape, const DirectX::XMFLOAT3& point,
            const DirectX::XMFLOAT3& impulse) noexcept;

        Scene* scene_ = nullptr;
        std::vector<BodyKey> body_keys_;
        std::uint32_t last_generation_ = 0;
        bool has_generation_ = false;
        int solver_iterations_ = 8;
        std::size_t dynamic_body_count_ = 0;
        std::size_t sleeping_body_count_ = 0;
    };
}
