#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

#include <string>

namespace ReplayEngine::Scene { class PhysicsDynamicsWorld; }

namespace ReplayEngine::Components
{
    // 動的剛体の設定と、実行中だけ更新される速度を持つ Component。
    //
    // Collider の形状はここへ複製しない。同じ GameObject に付いた
    // ColliderComponent を PhysicsDynamicsWorld が Shape の情報源として読む。
    // これにより Inspector で Collider と Rigidbody の設定が二重にならない。
    class RigidbodyComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(RigidbodyComponent)

    public:
        // 保存済みの整数値と対応するため、既存の値を途中へ挿入しない。
        static constexpr int BodyType_Static = 0;
        static constexpr int BodyType_Kinematic = 1;
        static constexpr int BodyType_Dynamic = 2;

        RigidbodyComponent() = default;

        // ---- 保存される設定 -----------------------------------------------

        int body_type = BodyType_Dynamic;
        float mass = 1.0f;
        float linear_damping = 0.05f;
        float angular_damping = 0.05f;
        float gravity_scale = 1.0f;
        float restitution = 0.0f;
        float friction = 0.5f;
        DirectX::XMFLOAT3 freeze_position{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 freeze_rotation{ 0.0f, 0.0f, 0.0f };
        bool start_asleep = false;

        // 常に掃引で判定するか。false でも 1 tick に自分の大きさ以上動けば掃引へ切り替わる。
        bool use_ccd = false;

        // ---- 実行時のみの値 -----------------------------------------------

        DirectX::XMFLOAT3 linear_velocity{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 angular_velocity{ 0.0f, 0.0f, 0.0f };
        bool is_sleeping = false;

        void AddForce(const DirectX::XMFLOAT3& force) noexcept;
        void AddTorque(const DirectX::XMFLOAT3& torque) noexcept;
        void ClearForces() noexcept;

        // Dynamic の Transform を意図的に移す入口。速度は保持する。
        void Teleport(const DirectX::XMFLOAT3& position,
            const DirectX::XMFLOAT3& rotation_euler) noexcept;

        // Collider が無い場合や、Dynamic 非対応形状を Inspector へ知らせる。
        std::string StatusMessage() const;

        const DirectX::XMFLOAT3& AccumulatedForce() const noexcept
        {
            return accumulated_force_;
        }
        const DirectX::XMFLOAT3& AccumulatedTorque() const noexcept
        {
            return accumulated_torque_;
        }
        void ClearAccumulatedForces() noexcept;

        bool RuntimeInitialized() const noexcept { return runtime_initialized_; }
        void MarkRuntimeInitialized() noexcept { runtime_initialized_ = true; }
        void ResetRuntimeState() noexcept;

        // ---- 拡張点: 外部スクリプトからの力 --------------------------------
        //
        // 【今は入れていない理由】
        //   Phase 1・2 は C++ Component API と固定刻み Solver の土台を先に
        //   安定させる段階で、C# へ直接公開する API は Phase 3 の責務だから。
        // 【入れるときにここへ足す】
        //   C# Script の HandleResolver から AddForce / Teleport を呼ぶ薄い
        //   facade を追加し、backend の型はスクリプト側へ漏らさない。
        // 【壊してはいけない前提】
        //   スクリプトが PhysicsDynamicsWorld の内部 Body や Solver へ直接
        //   触れないこと。力は必ず Component の公開 API を経由する。

    private:
        DirectX::XMFLOAT3 accumulated_force_{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 accumulated_torque_{ 0.0f, 0.0f, 0.0f };
        float sleep_timer_ = 0.0f;
        bool runtime_initialized_ = false;

        friend class ReplayEngine::Scene::PhysicsDynamicsWorld;
    };
}
