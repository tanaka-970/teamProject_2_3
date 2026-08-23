#include "BehaviourValidationInternal.h"

namespace ReplayEngine::Runtime::Validation
{
    namespace Detail::BehaviourValidation
    {
    // =====================================================================
    // Collision Event 配送
    // =====================================================================


        // 返す Hit を検証側から指定できる問い合わせサービス。
        //
        // 本物の SceneCollisionWorld を使わない理由:
        //   接触の有無・相手・位置を意図した順序で切り替えたいのに、
        //   実際の地形を使うと「その状況を作る」こと自体が難しい。
        //   Motor から先（Dispatcher の状態遷移）を確かめるのが目的なので、
        //   入口の問い合わせだけ差し替える。
        //
        //   Motor の移動計算そのものは本物をそのまま通る。
        class ScriptedPhysics final : public Scene::IPhysicsQueryService
        {
        public:
            bool ground_hit = false;
            Core::ObjectID ground_object;
            Scene::ColliderID ground_collider = 11;
            float ground_y = 0.0f;

            bool wall_hit = false;
            Core::ObjectID wall_object;
            Scene::ColliderID wall_collider = 22;

            bool ray_hit = false;
            Core::ObjectID ray_object;
            Scene::ColliderID ray_collider = 33;

            bool CollisionAvailable() const override { return true; }

            bool QueryGround(const DirectX::XMFLOAT3& origin, float /*radius*/,
                float /*up_offset*/, float /*down_distance*/, float /*walkable_normal_y*/,
                Scene::GroundHit& hit) const override
            {
                if (!ground_hit) return false;
                hit.valid = true;
                hit.position = DirectX::XMFLOAT3{ origin.x, ground_y, origin.z };
                hit.normal = DirectX::XMFLOAT3{ 0.0f, 1.0f, 0.0f };
                hit.source.backend = Scene::CollisionBackend::SceneCollider;
                hit.source.object = ground_object;
                hit.source.collider = ground_collider;
                return true;
            }

            bool SweepSphere(const DirectX::XMFLOAT3& start, const DirectX::XMFLOAT3& /*end*/,
                float /*radius*/, float /*maximum_normal_y*/,
                Scene::SphereSweepHit& hit) const override
            {
                if (!wall_hit) return false;
                hit.valid = true;
                hit.center = start;
                hit.normal = DirectX::XMFLOAT3{ 1.0f, 0.0f, 0.0f };
                hit.fraction = 0.5f;
                hit.source.backend = Scene::CollisionBackend::SceneCollider;
                hit.source.object = wall_object;
                hit.source.collider = wall_collider;
                return true;
            }

            bool RaycastFiltered(const DirectX::XMFLOAT3& origin,
                const DirectX::XMFLOAT3& direction, float /*max_distance*/,
                const Scene::CollisionQueryFilter& /*filter*/,
                Scene::RaycastHit& hit) const override
            {
                hit = Scene::RaycastHit{};
                if (!ray_hit) return false;
                hit.valid = true;
                hit.point = DirectX::XMFLOAT3{
                    origin.x + direction.x * 2.0f,
                    origin.y + direction.y * 2.0f,
                    origin.z + direction.z * 2.0f };
                hit.normal = DirectX::XMFLOAT3{ 0.0f, 1.0f, 0.0f };
                hit.distance = 2.0f;
                hit.source.backend = Scene::CollisionBackend::SceneCollider;
                hit.source.object = ray_object;
                hit.source.collider = ray_collider;
                return true;
            }
        };

        class ScriptedDynamics final : public Scene::IPhysicsDynamicsService
        {
        public:
            std::vector<Scene::PhysicsContact> contacts;

            void AttachScene(Scene::Scene* scene) override { scene_ = scene; }
            void DetachScene() override { scene_ = nullptr; contacts.clear(); }
            const Scene::Scene* AttachedScene() const noexcept override { return scene_; }
            void Step(float) override {}
            std::size_t BodyCount() const noexcept override { return 0; }
            std::size_t DynamicBodyCount() const noexcept override { return 0; }
            std::size_t SleepingBodyCount() const noexcept override { return 0; }
            int SolverIterations() const noexcept override { return 0; }
            const std::vector<Scene::PhysicsContact>& Contacts() const noexcept override
            {
                return contacts;
            }

        private:
            Scene::Scene* scene_ = nullptr;
        };

        // 受け取った CollisionEvent を記録するだけの Behaviour。
        class CollisionProbeBehaviour final : public BehaviourComponent
        {
            REPLAY_COMPONENT_BODY(CollisionProbeBehaviour)

        public:
            static constexpr Reflection::TypeGUID StaticTypeGUID() noexcept
            {
                return Reflection::MakeTypeGUID("c0000000000000000000000000000002");
            }

            struct Record
            {
                ContactPhase phase = ContactPhase::Enter;
                CollisionHitKind kind = CollisionHitKind::Unknown;
                Core::ObjectID other;
                Scene::ColliderID self_collider = Scene::invalid_collider_id;
                Scene::ColliderID other_collider = Scene::invalid_collider_id;
                DirectX::XMFLOAT3 relative_velocity{ 0.0f, 0.0f, 0.0f };
                float penetration = 0.0f;
                bool other_valid = false;
                bool self_valid = false;
            };

            std::vector<Record> records;

            void Clear() { records.clear(); }

            int CountOf(ContactPhase phase, CollisionHitKind kind) const
            {
                int total = 0;
                for (const Record& record : records)
                {
                    if (record.phase == phase && record.kind == kind) ++total;
                }
                return total;
            }

        protected:
            void OnCollisionEnter(const CollisionEvent& event) override { Push(event); }
            void OnCollisionStay(const CollisionEvent& event) override { Push(event); }
            void OnCollisionExit(const CollisionEvent& event) override { Push(event); }

            // Trigger 側も記録しておき、Collision と混ざっていないことを確かめる。
            void OnTriggerEnter(const TriggerEvent&) override { ++trigger_calls; }
            void OnTriggerStay(const TriggerEvent&) override { ++trigger_calls; }
            void OnTriggerExit(const TriggerEvent&) override { ++trigger_calls; }

        public:
            int trigger_calls = 0;

        private:
            void Push(const CollisionEvent& event)
            {
                Record record;
                record.phase = event.phase;
                record.kind = event.hit_kind;
                record.other = event.other.object;
                record.self_collider = event.self_collider;
                record.other_collider = event.other_collider;
                record.relative_velocity = event.relative_velocity;
                record.penetration = event.penetration_depth;
                record.other_valid = event.other_valid;
                record.self_valid = !event.self.IsEmpty();
                records.push_back(record);
            }
        };

        void RegisterCollisionProbe()
        {
            Core::RegisterBuiltInComponents();
            Core::ComponentRegistry::Register<CollisionProbeBehaviour>(
                Core::ComponentTypeInfo::Describe("Collision Probe", "Internal")
                    .HiddenInEditor()
                    .WithTypeGUID(CollisionProbeBehaviour::StaticTypeGUID())
                    .InModule("RePlayEngine.Validation"));
            BehaviourRegistry::Register(CollisionProbeBehaviour::StaticTypeGUID(),
                BehaviourRegistry::Native());
        }
    }

    using namespace Detail::BehaviourValidation;

    int RunCollisionValidation()
    {
        RegisterCollisionProbe();
        Checker check(370);

        Scene::Scene world("CollisionWorld");
        RuntimeContext runtime(world);
        world.Services().SetRuntime(&runtime);

        ScriptedPhysics physics;
        world.Services().SetPhysics(&physics);

        // 接触相手として置く GameObject。実体があることを確かめるために作る。
        Core::GameObject* floor_a = world.CreateGameObject("FloorA");
        Core::GameObject* floor_b = world.CreateGameObject("FloorB");
        Core::GameObject* wall = world.CreateGameObject("Wall");
        check.Expect(floor_a != nullptr && floor_b != nullptr && wall != nullptr,
            "接触相手の GameObject を作れる");
        if (floor_a == nullptr || floor_b == nullptr || wall == nullptr)
        {
            return check.Report("Collision validation");
        }
        physics.ground_object = floor_a->ID();
        physics.wall_object = wall->ID();
        physics.ray_object = wall->ID();

        // Runtime Raycast は同じ Physics Service を通り、Object/Collider 情報まで返す。
        physics.ray_hit = true;
        Scene::RaycastHit raycast_hit{};
        check.Expect(runtime.Raycast(DirectX::XMFLOAT3{ 0, 1, 0 },
            DirectX::XMFLOAT3{ 0, 0, 1 }, 100.0f, 0, -1,
            ObjectHandle::None(), raycast_hit) == RuntimeStatus::Ok &&
            raycast_hit.valid && raycast_hit.source.object == wall->ID() &&
            raycast_hit.source.collider == physics.ray_collider,
            "Runtime Raycast が Hit Object / Collider を返す");
        physics.ray_hit = false;
        check.Expect(runtime.Raycast(DirectX::XMFLOAT3{ 0, 1, 0 },
            DirectX::XMFLOAT3{ 0, 0, 1 }, 100.0f, 0, -1,
            ObjectHandle::None(), raycast_hit) == RuntimeStatus::Ok && !raycast_hit.valid,
            "Runtime Raycast の miss は成功した問い合わせ + invalid hit として返る");

        // 動く側。Collider と Motor と Probe を付ける。
        Core::GameObject* character = world.CreateGameObject("Character");
        auto* collider = character->AddComponent<Components::SphereColliderComponent>();
        auto* motor = character->AddComponent<Components::CharacterMotorComponent>();
        auto* probe = character->AddComponent<CollisionProbeBehaviour>();
        check.Expect(collider != nullptr && motor != nullptr && probe != nullptr,
            "Collider / Motor / Probe を追加できる");
        if (collider == nullptr || motor == nullptr || probe == nullptr)
        {
            return check.Report("Collision validation");
        }
        motor->SetPrimaryCollider(*collider);

        world.Start();

        CollisionEventDispatcher dispatcher;

        // 1 ステップ進めて配送する、を 1 回分にまとめる。
        const auto step = [&](std::uint64_t frame)
        {
            world.FixedUpdate(1.0f / 60.0f);
            dispatcher.Dispatch(world, frame);
        };

        // ---- 接地なし。何も起きない --------------------------------------

        physics.ground_hit = false;
        physics.wall_hit = false;
        probe->Clear();
        step(1);
        check.Expect(probe->records.empty(), "接触が無ければイベントは配られない");

        // ---- Ground Enter -------------------------------------------------

        physics.ground_hit = true;
        probe->Clear();
        step(2);
        check.Expect(probe->CountOf(ContactPhase::Enter,
            CollisionHitKind::CharacterGround) == 1, "接地で Ground Enter が 1 回");
        check.Expect(probe->records.size() == 1, "Enter 以外は配られない");
        check.Expect(!probe->records.empty() && probe->records[0].other == floor_a->ID(),
            "Ground Enter の相手が正しい");
        check.Expect(!probe->records.empty() && probe->records[0].other_valid,
            "相手が生きていれば other_valid が true");
        check.Expect(!probe->records.empty() && probe->records[0].self_valid,
            "self は必ず Handle として渡される");

        // ---- Ground Stay ---------------------------------------------------

        probe->Clear();
        step(3);
        check.Expect(probe->CountOf(ContactPhase::Stay,
            CollisionHitKind::CharacterGround) == 1, "接触が続けば Ground Stay");
        check.Expect(probe->CountOf(ContactPhase::Enter,
            CollisionHitKind::CharacterGround) == 0, "続いている間 Enter は再送されない");

        // ---- 接触相手の変更で Exit -> Enter ---------------------------------

        physics.ground_object = floor_b->ID();
        probe->Clear();
        step(4);
        check.Expect(probe->records.size() == 2,
            "相手が変わったら Exit と Enter の 2 件");
        check.Expect(probe->records.size() == 2 &&
            probe->records[0].phase == ContactPhase::Exit &&
            probe->records[0].other == floor_a->ID(),
            "先に前の相手への Exit が来る");
        check.Expect(probe->records.size() == 2 &&
            probe->records[1].phase == ContactPhase::Enter &&
            probe->records[1].other == floor_b->ID(),
            "次に新しい相手への Enter が来る");

        // ---- 地面から離れたら Exit -------------------------------------------

        physics.ground_hit = false;
        probe->Clear();
        step(5);
        check.Expect(probe->CountOf(ContactPhase::Exit,
            CollisionHitKind::CharacterGround) == 1, "地面から離れたら Ground Exit");

        probe->Clear();
        step(6);
        check.Expect(probe->records.empty(), "離れたあとは何も配られない");

        // ---- Wall Enter / Stay / Exit ----------------------------------------

        physics.wall_hit = true;
        probe->Clear();
        step(7);
        check.Expect(probe->CountOf(ContactPhase::Enter,
            CollisionHitKind::CharacterWall) == 1, "壁接触で Wall Enter");
        check.Expect(probe->CountOf(ContactPhase::Enter,
            CollisionHitKind::CharacterGround) == 0,
            "Wall の接触が Ground として配られない");

        probe->Clear();
        step(8);
        check.Expect(probe->CountOf(ContactPhase::Stay,
            CollisionHitKind::CharacterWall) == 1, "壁接触が続けば Wall Stay");

        physics.wall_hit = false;
        probe->Clear();
        step(9);
        check.Expect(probe->CountOf(ContactPhase::Exit,
            CollisionHitKind::CharacterWall) == 1, "壁から離れたら Wall Exit");

        // ---- Ground と Wall が同時に起きても混ざらない --------------------------

        physics.ground_hit = true;
        physics.wall_hit = true;
        probe->Clear();
        step(10);
        check.Expect(probe->CountOf(ContactPhase::Enter,
            CollisionHitKind::CharacterGround) == 1, "同時接触でも Ground Enter が 1 回");
        check.Expect(probe->CountOf(ContactPhase::Enter,
            CollisionHitKind::CharacterWall) == 1, "同時接触でも Wall Enter が 1 回");
        check.Expect(probe->records.size() == 2, "同時接触で余分なイベントが出ない");

        // ---- Trigger と混ざらない ----------------------------------------------

        check.Expect(probe->trigger_calls == 0,
            "Collision の配送が Trigger のコールバックへ流れ込まない");

        // ---- 無効な Behaviour へは配送しない -------------------------------------

        probe->SetEnabled(false);
        world.FixedUpdate(1.0f / 60.0f);   // SetEnabled を同期点へ反映させる
        probe->Clear();
        step(11);
        check.Expect(probe->records.empty(), "無効な Behaviour へは配送しない");
        check.Expect(dispatcher.SkippedCount() > 0, "配送しなかった件数が記録される");

        probe->SetEnabled(true);
        world.FixedUpdate(1.0f / 60.0f);
        probe->Clear();
        step(12);
        check.Expect(!probe->records.empty(), "再有効化すると配送が再開する");

        // ---- 削除予約済みへは配送しない -------------------------------------------

        probe->Destroy();
        probe->Clear();

        // 予約直後、まだ実体があるうちに配送を試す。
        // step() を挟むと FixedUpdate の同期点で実体が解放され、
        // そのあとに probe を読むと解放済みメモリへ触ることになる。
        dispatcher.Dispatch(world, 13);
        check.Expect(probe->records.empty(), "削除予約済みの Behaviour へは配送しない");

        // ここで probe の実体が解放される。以降このポインタは使わない。
        world.ProcessPendingOperations();
        probe = nullptr;

        // ---- World が入れ替わったら接触状態を捨てる ---------------------------------

        const std::size_t before_clear = dispatcher.ActiveContactCount();
        check.Expect(before_clear > 0, "接触状態が保持されている");

        const std::uint64_t exit_before = dispatcher.ExitCount();
        world.Clear();
        dispatcher.Dispatch(world, 14);
        check.Expect(dispatcher.ActiveContactCount() == 0,
            "World の入れ替えで接触状態が捨てられる");
        check.Expect(dispatcher.ExitCount() == exit_before,
            "消えた World の接触へ Exit を配らない（配送先が存在しないため）");

        // ---- 明示的な Reset --------------------------------------------------------

        dispatcher.Reset();
        check.Expect(dispatcher.ActiveContactCount() == 0, "Reset で接触状態が空になる");

        // ---- 一般 Rigidbody 衝突の Enter / Stay / Exit -------------------------------
        {
            Scene::Scene rigid_world("RigidBodyWorld");
            RuntimeContext rigid_runtime(rigid_world);
            rigid_world.Services().SetRuntime(&rigid_runtime);
            rigid_world.Services().SetPhysics(&physics);
            ScriptedDynamics dynamics;
            dynamics.AttachScene(&rigid_world);
            rigid_world.Services().SetPhysicsDynamics(&dynamics);

            Core::GameObject* a = rigid_world.CreateGameObject("A");
            Core::GameObject* b = rigid_world.CreateGameObject("B");
            a->AddComponent<Components::SphereColliderComponent>();
            b->AddComponent<Components::SphereColliderComponent>();
            auto* probe_a = a->AddComponent<CollisionProbeBehaviour>();
            auto* probe_b = b->AddComponent<CollisionProbeBehaviour>();

            Scene::PhysicsContact contact;
            contact.object_a = a->ID();
            contact.collider_a = 101;
            contact.object_b = b->ID();
            contact.collider_b = 202;
            contact.point = { 1.0f, 2.0f, 3.0f };
            contact.normal = { 1.0f, 0.0f, 0.0f };
            contact.relative_velocity = { -4.0f, 0.0f, 0.0f };
            contact.penetration = 0.25f;
            dynamics.contacts.push_back(contact);

            rigid_world.Start();

            CollisionEventDispatcher rigid_dispatcher;
            rigid_dispatcher.Dispatch(rigid_world, 1);
            check.Expect(probe_a != nullptr && probe_a->CountOf(ContactPhase::Enter,
                CollisionHitKind::Rigidbody) == 1 && probe_b != nullptr &&
                probe_b->CountOf(ContactPhase::Enter, CollisionHitKind::Rigidbody) == 1,
                "一般 Rigidbody 接触が両方へ Enter を配る");
            check.Expect(probe_a != nullptr && !probe_a->records.empty() &&
                probe_a->records[0].self_collider == 101 &&
                probe_a->records[0].other_collider == 202 &&
                probe_a->records[0].relative_velocity.x == -4.0f &&
                probe_a->records[0].penetration == 0.25f,
                "Rigidbody 接触の Collider・相対速度・貫通量が保たれる");
            check.Expect(probe_b != nullptr && !probe_b->records.empty() &&
                probe_b->records[0].self_collider == 202 &&
                probe_b->records[0].other_collider == 101 &&
                probe_b->records[0].relative_velocity.x == 4.0f,
                "反対側の Rigidbody 接触は向きと Collider が反転する");

            probe_a->Clear();
            probe_b->Clear();
            rigid_dispatcher.Dispatch(rigid_world, 2);
            check.Expect(probe_a->CountOf(ContactPhase::Stay,
                CollisionHitKind::Rigidbody) == 1 && probe_b->CountOf(ContactPhase::Stay,
                CollisionHitKind::Rigidbody) == 1,
                "一般 Rigidbody 接触が続けば Stay を配る");

            dynamics.contacts.clear();
            probe_a->Clear();
            probe_b->Clear();
            rigid_dispatcher.Dispatch(rigid_world, 3);
            check.Expect(probe_a->CountOf(ContactPhase::Exit,
                CollisionHitKind::Rigidbody) == 1 && probe_b->CountOf(ContactPhase::Exit,
                CollisionHitKind::Rigidbody) == 1,
                "一般 Rigidbody 接触が消えれば Exit を配る");
            check.Expect(rigid_dispatcher.ActiveContactCount() == 0,
                "一般 Rigidbody 接触の状態が Exit 後に破棄される");

            rigid_world.Services().SetPhysicsDynamics(nullptr);
            dynamics.DetachScene();
            rigid_world.Services().SetRuntime(nullptr);
            rigid_world.Services().SetPhysics(nullptr);
        }

        world.Services().SetPhysics(nullptr);
        world.Services().SetRuntime(nullptr);
        return check.Report("Collision validation");
    }
}
