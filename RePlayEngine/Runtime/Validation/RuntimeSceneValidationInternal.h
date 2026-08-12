#pragma once

#include "RuntimeSceneValidation.h"

#include "../API/RuntimeContext.h"
#include "../Behaviour/BehaviourComponent.h"
#include "../Behaviour/BehaviourRegistry.h"
#include "../Core/RuntimeResult.h"
#include "../Events/CollisionEventDispatcher.h"
#include "../Events/EventBus.h"
#include "../Handles/HandleResolver.h"
#include "../Scene/RuntimeSceneService.h"
#include "../../Components/Gameplay/CharacterMotorComponent.h"
#include "../../Components/Physics/SphereColliderComponent.h"
#include "../../Object/Component/MissingComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/BuiltInComponents.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Serialization/SceneData.h"
#include "../../Scene/Serialization/SceneSerializer.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

// RuntimeSceneValidation の分割内部で使う検証 Fixture であり、外部から使うものではない。
namespace ReplayEngine::Runtime::Validation::Detail::RuntimeSceneValidation
{
        namespace Serialization = Scene::Serialization;

        using Core::ObjectID;
        using Reflection::PropertyValue;

        // ------------------------------------------------------------------
        // 定数は名前空間スコープへ置く。
        //
        // 関数ローカルの constexpr をキャプチャ無しラムダから参照すると
        // MSVC が C3493 で落ちるため、この検証で使う固定値はすべてここに集める。
        // ------------------------------------------------------------------

        constexpr const char* guid_scene_a = "5ce4e000000000000000000000000a01";
        constexpr const char* guid_scene_b = "5ce4e000000000000000000000000b01";
        constexpr const char* guid_missing_component = "5ce4e000000000000000000000000c01";
        constexpr const char* guid_unknown_property = "5ce4e000000000000000000000000d01";
        constexpr const char* guid_reference = "5ce4e000000000000000000000000e01";
        constexpr const char* guid_corrupt = "5ce4e000000000000000000000000f01";
        constexpr const char* guid_future_version = "5ce4e00000000000000000000000fd01";
        constexpr const char* guid_not_registered = "5ce4e00000000000000000000000ff01";
        constexpr const char* guid_wrong_asset_type = "5ce4e00000000000000000000000fe01";

        constexpr const char* ghost_type_name = "ValidationGhostComponent";

        constexpr float fixed_step = 1.0f / 60.0f;

        // 検査の記録係。最初の失敗で打ち切らず、全項目を実行してから
        // 最初の失敗番号を返す。1 回のビルド確認で見つかる不具合を増やすため。
        class Checker final
        {
        public:
            explicit Checker(int first_code) : next_code_(first_code) {}

            void Expect(bool condition, const char* what)
            {
                const int code = next_code_++;
                ++total_;
                if (condition) return;

                ++failures_;
                if (first_failure_ == 0) first_failure_ = code;
                std::fprintf(stderr, "  [FAIL %d] %s\n", code, what);
            }

            int Report(const char* title) const
            {
                if (first_failure_ == 0)
                {
                    std::fprintf(stderr, "%s OK: %d checks passed\n", title, total_);
                    return 0;
                }
                std::fprintf(stderr, "%s FAILED: %d/%d checks failed (first=%d)\n",
                    title, failures_, total_, first_failure_);
                return first_failure_;
            }

        private:
            int next_code_ = 0;
            int first_failure_ = 0;
            int total_ = 0;
            int failures_ = 0;
        };

        // ------------------------------------------------------------------
        // 検証専用の Behaviour
        //
        // 本番コードへは登録せず、この検証の中だけで Registry へ入れる。
        // World の入れ替えで OnAwake / OnDestroy が正しく流れることを、
        // 全インスタンス共通のカウンタで観測する。
        // ------------------------------------------------------------------
        class SceneProbeBehaviour final : public BehaviourComponent
        {
            REPLAY_COMPONENT_BODY(SceneProbeBehaviour)

        public:
            static constexpr Reflection::TypeGUID StaticTypeGUID() noexcept
            {
                return Reflection::MakeTypeGUID("c0000000000000000000000000000010");
            }

            // 保存・復元を確かめるための値。
            int marker = 0;

            // 参照解決を確かめるための保存済み参照。
            Reflection::ObjectReference target_object;
            Reflection::ComponentReference target_component;

            static int awake_total;
            static int destroy_total;
            static int live_count;

            static void ResetCounters() noexcept
            {
                awake_total = 0;
                destroy_total = 0;
                live_count = 0;
            }

        protected:
            void OnAwake() override
            {
                ++awake_total;
                ++live_count;
            }
            void OnDestroy() override
            {
                ++destroy_total;
                --live_count;
            }
        };

        int SceneProbeBehaviour::awake_total = 0;
        int SceneProbeBehaviour::destroy_total = 0;
        int SceneProbeBehaviour::live_count = 0;

        void RegisterProbe()
        {
            Core::RegisterBuiltInComponents();
            Core::ComponentRegistry::Register<SceneProbeBehaviour>(
                Core::ComponentTypeInfo::Describe("Runtime Scene Probe", "Internal")
                    .HiddenInEditor()
                    .WithTypeGUID(SceneProbeBehaviour::StaticTypeGUID())
                    .InModule("RePlayEngine.Validation"));

            Reflection::PropertyRegistry::Register<SceneProbeBehaviour>(
                Reflection::MakeProperty("marker", &SceneProbeBehaviour::marker)
                    .Display("目印"));
            Reflection::PropertyRegistry::Register<SceneProbeBehaviour>(
                Reflection::MakeProperty("target_object",
                    &SceneProbeBehaviour::target_object).Display("参照先 GameObject"));
            Reflection::PropertyRegistry::Register<SceneProbeBehaviour>(
                Reflection::MakeProperty("target_component",
                    &SceneProbeBehaviour::target_component).Display("参照先 Component"));

            BehaviourRegistry::Register(SceneProbeBehaviour::StaticTypeGUID(),
                BehaviourRegistry::Native());
        }

        // ------------------------------------------------------------------
        // 検証用の Scene Asset 解決。
        //
        // GUID からパスへの対応を検証側から差し替えられるようにする。
        // AssetDatabase を使わないので、この検証は Asset 管理の状態に依存しない。
        // ------------------------------------------------------------------
        class TestSceneAssetResolver final : public ISceneAssetResolver
        {
        public:
            void Map(std::string guid, const std::filesystem::path& path)
            {
                Entry entry;
                entry.guid = std::move(guid);
                entry.path = path.string();
                entries_.push_back(std::move(entry));
            }

            // 「GUID はあるが Scene Asset ではない」状態を作る。
            void MapWrongType(std::string guid)
            {
                wrong_type_.push_back(std::move(guid));
            }

            RuntimeStatus ResolveScenePath(const std::string& asset_guid,
                std::string& out_path) const override
            {
                ++resolve_calls_;
                for (const std::string& guid : wrong_type_)
                {
                    if (guid == asset_guid) return RuntimeStatus::InvalidAssetType;
                }
                for (const Entry& entry : entries_)
                {
                    if (entry.guid != asset_guid) continue;
                    out_path = entry.path;
                    return RuntimeStatus::Ok;
                }
                // 推測でパスを組み立てない。無ければ AssetMissing で止める。
                return RuntimeStatus::AssetMissing;
            }

            std::size_t ResolveCallCount() const noexcept { return resolve_calls_; }

        private:
            struct Entry
            {
                std::string guid;
                std::string path;
            };

            std::vector<Entry> entries_;
            std::vector<std::string> wrong_type_;
            mutable std::size_t resolve_calls_ = 0;
        };

        // 返す Hit を検証側から指定できる問い合わせサービス。
        // Collision の接触状態を意図した瞬間に作るためだけに使う。
        class ScriptedPhysics final : public Scene::IPhysicsQueryService
        {
        public:
            bool ground_hit = false;
            ObjectID ground_object;
            Scene::ColliderID ground_collider = 11;

            bool CollisionAvailable() const override { return true; }

            bool QueryGround(const DirectX::XMFLOAT3& origin, float /*radius*/,
                float /*up_offset*/, float /*down_distance*/, float /*walkable_normal_y*/,
                Scene::GroundHit& hit) const override
            {
                if (!ground_hit) return false;
                hit.valid = true;
                hit.position = DirectX::XMFLOAT3{ origin.x, 0.0f, origin.z };
                hit.normal = DirectX::XMFLOAT3{ 0.0f, 1.0f, 0.0f };
                hit.source.backend = Scene::CollisionBackend::SceneCollider;
                hit.source.object = ground_object;
                hit.source.collider = ground_collider;
                return true;
            }

            bool SweepSphere(const DirectX::XMFLOAT3& /*start*/,
                const DirectX::XMFLOAT3& /*end*/, float /*radius*/,
                float /*maximum_normal_y*/, Scene::SphereSweepHit& /*hit*/) const override
            {
                return false;
            }
        };

        // ------------------------------------------------------------------
        // Scene データの組み立て
        // ------------------------------------------------------------------

        Serialization::ComponentData MakeProbeComponent(int marker,
            Core::ComponentStableID stable_id)
        {
            Serialization::ComponentData component;
            component.type_name = SceneProbeBehaviour::StaticTypeName();
            component.type_id = SceneProbeBehaviour::StaticTypeID();
            component.type_guid = SceneProbeBehaviour::StaticTypeGUID();
            component.module_id = "RePlayEngine.Validation";
            component.type_version = 1;
            component.stable_id = stable_id;
            component.properties.Set("marker", PropertyValue::MakeInt(marker));
            return component;
        }

        Serialization::ComponentData MakeSimpleComponent(const char* type_name)
        {
            Serialization::ComponentData component;
            component.type_name = type_name;
            component.type_id = Core::MakeComponentTypeID(type_name);
            component.stable_id = 1;
            return component;
        }

        Serialization::GameObjectData MakeObject(ObjectID::ValueType id,
            const char* name, ObjectID::ValueType parent = 0)
        {
            Serialization::GameObjectData object;
            object.id = ObjectID{ id };
            object.parent_id = ObjectID{ parent };
            object.name = name;
            return object;
        }

        // Scene A: 親子・Behaviour・CharacterMotor を含む「普通の Scene」。
        Serialization::SceneData BuildSceneA()
        {
            Serialization::SceneData data;
            data.scene_name = "RuntimeSceneA";

            Serialization::GameObjectData alpha = MakeObject(1, "Alpha");
            alpha.components.push_back(MakeProbeComponent(11, 1));
            data.objects.push_back(std::move(alpha));

            Serialization::GameObjectData beta = MakeObject(2, "Beta", 1);
            beta.components.push_back(MakeProbeComponent(22, 1));
            data.objects.push_back(std::move(beta));

            Serialization::GameObjectData character = MakeObject(3, "Character");
            character.components.push_back(MakeSimpleComponent(
                Components::SphereColliderComponent::StaticTypeName()));
            Serialization::ComponentData motor = MakeSimpleComponent(
                Components::CharacterMotorComponent::StaticTypeName());
            motor.stable_id = 2;
            character.components.push_back(std::move(motor));
            data.objects.push_back(std::move(character));

            return data;
        }

        // Scene B: 中身の違う別 Scene。GameObject 数で切替を判別する。
        Serialization::SceneData BuildSceneB()
        {
            Serialization::SceneData data;
            data.scene_name = "RuntimeSceneB";

            Serialization::GameObjectData solo = MakeObject(1, "Solo");
            solo.components.push_back(MakeProbeComponent(99, 1));
            data.objects.push_back(std::move(solo));

            return data;
        }

        // 登録されていない型を含む Scene。MissingComponent として預かられる。
        Serialization::SceneData BuildMissingComponentScene()
        {
            Serialization::SceneData data;
            data.scene_name = "RuntimeSceneMissing";

            Serialization::GameObjectData object = MakeObject(1, "Ghost");
            Serialization::ComponentData ghost;
            ghost.type_name = ghost_type_name;
            ghost.type_id = Core::MakeComponentTypeID(ghost_type_name);
            ghost.type_guid =
                Reflection::MakeTypeGUID("ee000000000000000000000000000001");
            ghost.module_id = "Game.NotLoaded";
            ghost.type_version = 4;
            ghost.stable_id = 1;
            ghost.properties.Set("ghost_int", PropertyValue::MakeInt(1234));
            ghost.properties.Set("ghost_text", PropertyValue::MakeString("消えてはいけない値"));
            object.components.push_back(std::move(ghost));
            data.objects.push_back(std::move(object));

            return data;
        }

        // 型は解決できるが、その型が知らないプロパティを含む Scene。
        Serialization::SceneData BuildUnknownPropertyScene()
        {
            Serialization::SceneData data;
            data.scene_name = "RuntimeSceneUnknownProperty";

            Serialization::GameObjectData object = MakeObject(1, "Holder");
            Serialization::ComponentData probe = MakeProbeComponent(7, 1);
            probe.properties.Set("ghost_property",
                PropertyValue::MakeString("未知のまま保持されるべき値"));
            object.components.push_back(std::move(probe));
            data.objects.push_back(std::move(object));

            return data;
        }

        // ObjectReference / ComponentReference を含む Scene。
        Serialization::SceneData BuildReferenceScene()
        {
            Serialization::SceneData data;
            data.scene_name = "RuntimeSceneReference";

            Serialization::GameObjectData source = MakeObject(1, "Source");
            Serialization::ComponentData probe = MakeProbeComponent(1, 1);
            probe.properties.Set("target_object",
                PropertyValue::MakeObjectReference(ObjectID{ 2 }));

            Reflection::ComponentReference reference;
            reference.owner = ObjectID{ 2 };
            reference.component = 1;
            probe.properties.Set("target_component",
                PropertyValue::MakeComponentReference(reference));
            source.components.push_back(std::move(probe));
            data.objects.push_back(std::move(source));

            Serialization::GameObjectData target = MakeObject(2, "Target");
            target.components.push_back(MakeProbeComponent(2, 1));
            data.objects.push_back(std::move(target));

            return data;
        }

        bool WriteRawFile(const std::filesystem::path& path, const char* text)
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream) return false;
            stream << text;
            return static_cast<bool>(stream);
        }

        // 直近に読み込んだ World から、名前で GameObject を引く小道具。
        Core::GameObject* Find(RuntimeSceneService& service, const char* name)
        {
            return service.ActiveWorld().FindGameObjectByName(name);
        }

        SceneProbeBehaviour* FindProbe(RuntimeSceneService& service, const char* name)
        {
            Core::GameObject* object = Find(service, name);
            if (object == nullptr) return nullptr;
            return object->GetComponent<SceneProbeBehaviour>();
        }
}
