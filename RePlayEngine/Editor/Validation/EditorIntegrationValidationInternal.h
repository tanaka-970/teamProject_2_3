#pragma once

#include "EditorIntegrationValidation.h"

#include "../Core/EditorContext.h"
#include "../../Assets/AssetDatabase.h"
#include "../../Object/Component/MissingComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/BuiltInComponents.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Project/ProjectSettings.h"
#include "../../Project/ProjectSettingsSerializer.h"
#include "../../Reflection/Property/References.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Runtime/API/RuntimeContext.h"
#include "../../Runtime/Behaviour/BehaviourComponent.h"
#include "../../Runtime/Behaviour/BehaviourRegistry.h"
#include "../../Runtime/Scene/RuntimeSceneService.h"
#include "../../Runtime/Scene/SceneFlowService.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Serialization/SceneData.h"
#include "../../Scene/Serialization/SceneSerializer.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// EditorIntegrationValidation の分割内部で使う Fixture であり、外部から使うものではない。
namespace ReplayEngine::Editor::Validation::Detail
{
        namespace Serialization = Scene::Serialization;
        namespace RRuntime = ReplayEngine::Runtime;

        using Core::ObjectID;
        using Reflection::PropertyValue;

        // 定数は名前空間スコープへ置く（関数ローカルの constexpr を
        // キャプチャ無しラムダから参照すると MSVC が C3493 で落ちるため）。
        constexpr const char* guid_startup = "ed17000000000000000000000000a001";
        constexpr const char* guid_second = "ed17000000000000000000000000a002";
        constexpr const char* guid_missing_asset = "ed17000000000000000000000000dead";
        constexpr const char* ghost_type_name = "EditorValidationGhostComponent";

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

        // Editor 統合の検証専用 Behaviour。
        //
        // Game Module の SceneTransitionBehaviour を使わない理由:
        //   Engine 側の Validation から Game の型を参照すると、
        //   Engine が特定のゲームを知っている依存ができる。
        //   Behaviour として扱われる仕組みそのものは型に依らないので、
        //   ここで登録した型で確かめれば十分。
        class EditorProbeBehaviour final : public RRuntime::BehaviourComponent
        {
            REPLAY_COMPONENT_BODY(EditorProbeBehaviour)

        public:
            static constexpr Reflection::TypeGUID StaticTypeGUID() noexcept
            {
                return Reflection::MakeTypeGUID("e0000000000000000000000000000001");
            }

            int marker = 0;
            float speed = 1.0f;
            std::string label;
            Reflection::ObjectReference target_object;
            Reflection::ComponentReference target_component;
            Reflection::SceneReference destination_scene;
            std::vector<float> weights;
        };

        class SecondProbeBehaviour final : public RRuntime::BehaviourComponent
        {
            REPLAY_COMPONENT_BODY(SecondProbeBehaviour)

        public:
            static constexpr Reflection::TypeGUID StaticTypeGUID() noexcept
            {
                return Reflection::MakeTypeGUID("e0000000000000000000000000000002");
            }

            int value = 0;
        };

        void RegisterProbes()
        {
            Core::RegisterBuiltInComponents();

            Core::ComponentRegistry::Register<EditorProbeBehaviour>(
                Core::ComponentTypeInfo::Describe("Editor Probe", "Internal")
                    .WithTooltip("Editor 統合の検証用。")
                    .HiddenInEditor()
                    .WithTypeGUID(EditorProbeBehaviour::StaticTypeGUID())
                    .InModule("RePlayEngine.Validation"));

            Reflection::PropertyRegistry::Register<EditorProbeBehaviour>(
                Reflection::MakeProperty("marker", &EditorProbeBehaviour::marker));
            Reflection::PropertyRegistry::Register<EditorProbeBehaviour>(
                Reflection::MakeProperty("speed", &EditorProbeBehaviour::speed));
            Reflection::PropertyRegistry::Register<EditorProbeBehaviour>(
                Reflection::MakeProperty("label", &EditorProbeBehaviour::label));
            Reflection::PropertyRegistry::Register<EditorProbeBehaviour>(
                Reflection::MakeProperty("target_object",
                    &EditorProbeBehaviour::target_object));
            Reflection::PropertyRegistry::Register<EditorProbeBehaviour>(
                Reflection::MakeProperty("target_component",
                    &EditorProbeBehaviour::target_component));
            Reflection::PropertyRegistry::Register<EditorProbeBehaviour>(
                Reflection::MakeProperty("destination_scene",
                    &EditorProbeBehaviour::destination_scene));
            Reflection::PropertyRegistry::Register<EditorProbeBehaviour>(
                Reflection::MakeProperty("weights", &EditorProbeBehaviour::weights));

            RRuntime::BehaviourRegistry::Register(EditorProbeBehaviour::StaticTypeGUID(),
                RRuntime::BehaviourRegistry::Native());

            Core::ComponentRegistry::Register<SecondProbeBehaviour>(
                Core::ComponentTypeInfo::Describe("Second Probe", "Internal")
                    .HiddenInEditor()
                    .WithTypeGUID(SecondProbeBehaviour::StaticTypeGUID())
                    .InModule("RePlayEngine.Validation"));
            Reflection::PropertyRegistry::Register<SecondProbeBehaviour>(
                Reflection::MakeProperty("value", &SecondProbeBehaviour::value));
            RRuntime::BehaviourRegistry::Register(SecondProbeBehaviour::StaticTypeGUID(),
                RRuntime::BehaviourRegistry::Native());
        }

        // GUID からパスを引くだけのテスト実装。
        class TestSceneAssetResolver final : public RRuntime::ISceneAssetResolver
        {
        public:
            void Map(std::string guid, const std::filesystem::path& path)
            {
                Entry entry;
                entry.guid = std::move(guid);
                entry.path = path.string();
                entries_.push_back(std::move(entry));
            }

            RRuntime::RuntimeStatus ResolveScenePath(const std::string& asset_guid,
                std::string& out_path) const override
            {
                for (const Entry& entry : entries_)
                {
                    if (entry.guid != asset_guid) continue;
                    out_path = entry.path;
                    return RRuntime::RuntimeStatus::Ok;
                }
                return RRuntime::RuntimeStatus::AssetMissing;
            }

        private:
            struct Entry
            {
                std::string guid;
                std::string path;
            };
            std::vector<Entry> entries_;
        };

        bool SettingsRoundTrip(const Project::ProjectSettings& source,
            Project::ProjectSettings& restored, std::string& error)
        {
            std::ostringstream out;
            if (!Project::ProjectSettingsSerializer::WriteText(source, out, error))
            {
                return false;
            }
            std::istringstream in(out.str());
            return Project::ProjectSettingsSerializer::ReadText(restored, in, error);
        }

        bool SceneRoundTrip(const Serialization::SceneData& source,
            Serialization::SceneData& restored, std::string& error)
        {
            std::ostringstream out;
            if (!Serialization::SceneSerializer::WriteText(source, out, error)) return false;
            std::istringstream in(out.str());
            return Serialization::SceneSerializer::ReadText(restored, in, error);
        }

        const Serialization::ComponentData* FindComponentData(
            const Serialization::SceneData& data, ObjectID::ValueType object_id,
            const std::string& type_name)
        {
            for (const Serialization::GameObjectData& object : data.objects)
            {
                if (object.id.Value() != object_id) continue;
                for (const Serialization::ComponentData& component : object.components)
                {
                    if (component.type_name == type_name) return &component;
                }
            }
            return nullptr;
        }
}
