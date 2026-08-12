#pragma once

#include "ScriptCoreValidation.h"

#include "../Core/MockScriptBackend.h"
#include "../Core/ScriptComponent.h"
#include "../Core/ScriptRuntime.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/BuiltInComponents.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Serialization/SceneData.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// ScriptCoreValidation の分割内部で共有する Fixture であり、外部から使うものではない。
namespace ReplayEngine::Scripting::Validation::Detail
{
        using Core::GameObject;
        using Core::ObjectID;
        using Reflection::PropertyRegistry;
        using Reflection::PropertyValue;

        namespace Serialization = Scene::Serialization;

        // 既存 Validation と同じ形。検査 1 件ごとに固有の終了コードを割り当て、
        // 失敗したら「最初に失敗したコード」を返す。
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

        // 検証用の一式。Play セッションの出入りを手で回せるようにしてある。
        //
        // RuntimeSceneService を通さないのは、D3D11 も Window も要らない形で
        // 順序だけを確かめたいため。フックの「呼ばれ方」は
        // RuntimeSceneService.cpp 側の 3 か所で固定されており、
        // ここではその 3 つを同じ順序で手で呼ぶ。
        class Fixture final
        {
        public:
            Fixture()
                : world("ScriptValidationWorld")
            {
                runtime = std::make_unique<ScriptRuntime>();

                auto lua = std::make_unique<MockScriptBackend>(ScriptLanguage::Lua);
                auto csharp = std::make_unique<MockScriptBackend>(ScriptLanguage::CSharp);
                lua_backend = lua.get();
                csharp_backend = csharp.get();

                runtime->InstallBackend(std::move(lua));
                runtime->InstallBackend(std::move(csharp));
                runtime->Initialize();

                lua_backend->SetTypeFields(MockScriptTypes::RotatingObjectTypeID(),
                    MockScriptTypes::RotatingObjectFields());
                csharp_backend->SetTypeFields(MockScriptTypes::DoorControllerTypeID(),
                    MockScriptTypes::DoorControllerFields());

                runtime->RegisterScriptType(MockScriptTypes::RotatingObject());
                runtime->RegisterScriptType(MockScriptTypes::DoorController());

                // Edit Mode 相当。Play セッションはまだ無いが、
                // Inspector が Schema を引けるよう接続だけしておく。
                world.Services().SetScripts(runtime.get());
            }

            ~Fixture()
            {
                EndPlaySession();
                world.Clear();
                runtime.reset();
            }

            // RuntimeSceneService::SwapWorlds が Scene::Start() の直前で行うのと同じこと。
            void BeginPlaySession()
            {
                runtime->OnWorldActivating(world);
                world.Start();
            }

            // RuntimeSceneService::ResetToEmptyWorld と同じ 3 点。
            void EndPlaySession()
            {
                if (!world.Started()) return;
                runtime->OnWorldUnloading(world);
                world.Clear();
                runtime->OnWorldUnloaded(world);
            }

            ScriptComponent* AddScript(GameObject& object, ScriptTypeID type,
                ScriptLanguage language, const std::string& asset_guid,
                const std::string& class_name = std::string())
            {
                auto* script = object.AddComponent<ScriptComponent>();
                if (script == nullptr) return nullptr;

                if (const ScriptTypeDescriptor* descriptor = runtime->Catalog().Find(type))
                {
                    script->AssignScriptType(*descriptor);
                }
                else
                {
                    script->SetLanguage(language);
                    script->SetScriptAssetGUID(asset_guid);
                    script->SetClassName(class_name);
                    script->RestoreScriptType(type);
                    script->ResolveSchema();
                }
                return script;
            }

            ScriptComponent* AddRotating(GameObject& object)
            {
                return AddScript(object, MockScriptTypes::RotatingObjectTypeID(),
                    ScriptLanguage::Lua, MockScriptTypes::RotatingObjectAssetGUID());
            }

            ScriptComponent* AddDoor(GameObject& object)
            {
                return AddScript(object, MockScriptTypes::DoorControllerTypeID(),
                    ScriptLanguage::CSharp, MockScriptTypes::DoorControllerAssetGUID(),
                    MockScriptTypes::DoorControllerClassName());
            }

            Scene::Scene world;
            std::unique_ptr<ScriptRuntime> runtime;
            MockScriptBackend* lua_backend = nullptr;
            MockScriptBackend* csharp_backend = nullptr;
        };

    void EnsureRegistries();
}
