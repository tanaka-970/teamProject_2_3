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

namespace ReplayEngine::Scripting::Validation
{
    namespace
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

        void EnsureRegistries()
        {
            // 二重に呼んでも ComponentRegistry が重複を弾く。
            Core::RegisterBuiltInComponents();
        }
    }

    // -----------------------------------------------------------------------
    // 620-679  script-core
    // -----------------------------------------------------------------------

    int RunScriptCoreValidation()
    {
        EnsureRegistries();
        Checker check(620);

        // ---- ScriptTypeID の導出 --------------------------------------------

        const ScriptTypeID lua_id = MockScriptTypes::RotatingObjectTypeID();
        const ScriptTypeID csharp_id = MockScriptTypes::DoorControllerTypeID();

        check.Expect(lua_id.IsValid(), "Lua の ScriptTypeID が有効");
        check.Expect(csharp_id.IsValid(), "C# の ScriptTypeID が有効");
        check.Expect(lua_id != csharp_id, "言語が違えば ScriptTypeID も違う");

        // Lua は AssetGUID をそのまま読む。ハッシュしないので可逆。
        check.Expect(lua_id.ToString() == MockScriptTypes::RotatingObjectAssetGUID(),
            "Lua の ScriptTypeID は AssetGUID と一致する");

        // 何度導出しても同じ値。保存できる条件。
        check.Expect(MakeLuaScriptTypeID(MockScriptTypes::RotatingObjectAssetGUID()) == lua_id,
            "Lua の ScriptTypeID 導出は決定的");
        check.Expect(MakeCSharpScriptTypeID(MockScriptTypes::DoorControllerAssetGUID(),
            MockScriptTypes::DoorControllerClassName()) == csharp_id,
            "C# の ScriptTypeID 導出は決定的");

        // 同じ Asset でもクラスが違えば別の型。
        check.Expect(MakeCSharpScriptTypeID(MockScriptTypes::DoorControllerAssetGUID(),
            "Game.OtherClass") != csharp_id,
            "C# は Asset が同じでもクラスが違えば別の ScriptTypeID");

        check.Expect(!MakeLuaScriptTypeID("").IsValid(), "空の AssetGUID は無効な ScriptTypeID");
        check.Expect(!MakeCSharpScriptTypeID("abc", "").IsValid(),
            "クラス名が空なら C# の ScriptTypeID は無効");

        Fixture fixture;

        // ---- 目録 -------------------------------------------------------------

        check.Expect(fixture.runtime->Catalog().Count() == 2, "目録へ 2 種類が登録される");
        check.Expect(fixture.runtime->Catalog().Find(lua_id) != nullptr,
            "目録から Lua の型を引ける");
        check.Expect(fixture.runtime->Catalog().All().size() == 2,
            "目録の Script Type 一覧を列挙できる");
        check.Expect(fixture.world.Services().Scripts() != nullptr &&
            fixture.world.Services().Scripts()->Catalog().All().size() == 2,
            "Editor は SceneServices 経由で Script Type 一覧を読める");
        check.Expect(fixture.runtime->ResolveDisplayName(lua_id) == "Rotating Object",
            "目録の表示名が引ける");
        check.Expect(fixture.runtime->ResolveDisplayName(csharp_id) == "Door Controller",
            "C# の表示名が引ける");
        const ScriptTypeDescriptor* rotating_descriptor = fixture.runtime->Catalog().Find(lua_id);
        check.Expect(rotating_descriptor != nullptr &&
            rotating_descriptor->ResolvedCategory() == "Scripts/Lua",
            "Lua Script は Add Component 用カテゴリを持つ");
        const ScriptTypeDescriptor* door_descriptor = fixture.runtime->Catalog().Find(csharp_id);
        check.Expect(door_descriptor != nullptr &&
            door_descriptor->ResolvedCategory() == "Scripts/C#",
            "C# Script は Add Component 用カテゴリを持つ");

        // ---- Schema の共有（改訂 2 の要件） -----------------------------------

        GameObject* host = fixture.world.CreateGameObject("SchemaShareHost");
        check.Expect(host != nullptr, "検証用 GameObject を作れる");
        if (host == nullptr) return check.Report("script-core");

        constexpr int share_count = 100;
        std::vector<ScriptComponent*> shared;
        shared.reserve(share_count);

        for (int index = 0; index < share_count; ++index)
        {
            GameObject* object = fixture.world.CreateGameObject(
                "Shared" + std::to_string(index));
            if (object == nullptr) continue;
            shared.push_back(fixture.AddRotating(*object));
        }

        check.Expect(shared.size() == share_count, "同じ型の ScriptComponent を 100 体作れる");

        bool all_same_pointer = !shared.empty() && shared.front() != nullptr;
        const std::vector<Reflection::PropertyDesc>* first_descs =
            all_same_pointer ? shared.front()->DynamicProperties() : nullptr;

        for (const ScriptComponent* script : shared)
        {
            if (script == nullptr || script->DynamicProperties() != first_descs)
            {
                all_same_pointer = false;
                break;
            }
        }

        check.Expect(first_descs != nullptr, "動的プロパティが取得できる");
        check.Expect(all_same_pointer,
            "同じ ScriptTypeID の 100 インスタンスが同一の PropertyDesc 配列を共有する");
        check.Expect(first_descs != nullptr && first_descs->size() == 2,
            "RotatingObject の Field は 2 個");
        check.Expect(!shared.empty() && shared.front() != nullptr &&
            shared.front()->Language() == ScriptLanguage::Lua &&
            shared.front()->ScriptAssetGUID() == MockScriptTypes::RotatingObjectAssetGUID() &&
            shared.front()->ClassName().empty() &&
            shared.front()->ScriptType() == lua_id &&
            shared.front()->Status() == ScriptStatus::Loaded,
            "Catalog の Lua Script Type を ScriptComponent へ割り当てられる");

        // Schema 実体も共有されていること。
        bool same_schema = true;
        for (const ScriptComponent* script : shared)
        {
            if (script == nullptr || script->Schema().get() != shared.front()->Schema().get())
            {
                same_schema = false;
                break;
            }
        }
        check.Expect(same_schema, "Schema の実体も 100 インスタンスで 1 つだけ");

        // ---- 型ごとに Field が違うこと -----------------------------------------

        GameObject* door_object = fixture.world.CreateGameObject("Door");
        ScriptComponent* door = door_object != nullptr ? fixture.AddDoor(*door_object) : nullptr;

        check.Expect(door != nullptr, "C# の ScriptComponent を作れる");
        if (door != nullptr)
        {
            const auto* door_descs = door->DynamicProperties();
            check.Expect(door_descs != nullptr && door_descs->size() == 2,
                "DoorController の Field は 2 個");
            check.Expect(door_descs != first_descs,
                "Script Type が違えば PropertyDesc 配列も別");
            check.Expect(door->Language() == ScriptLanguage::CSharp &&
                door->ScriptAssetGUID() == MockScriptTypes::DoorControllerAssetGUID() &&
                door->ClassName() == MockScriptTypes::DoorControllerClassName() &&
                door->ScriptType() == csharp_id,
                "Catalog の C# Script Type を ScriptComponent へ割り当てられる");

            const bool has_open_angle = door->Schema() &&
                door->Schema()->FindField("OpenAngle") != nullptr;
            const bool has_rotation_speed = door->Schema() &&
                door->Schema()->FindField("RotationSpeed") != nullptr;
            check.Expect(has_open_angle, "DoorController は OpenAngle を持つ");
            check.Expect(!has_rotation_speed, "DoorController は RotationSpeed を持たない");
        }

        // ---- 表示名 ------------------------------------------------------------

        check.Expect(HumanizeFieldName("RotationSpeed") == "Rotation Speed",
            "RotationSpeed が Rotation Speed へ整形される");
        check.Expect(HumanizeFieldName("openAngle") == "Open Angle",
            "openAngle が Open Angle へ整形される");
        check.Expect(HumanizeFieldName("maxHP") == "Max HP",
            "連続する大文字は割らない");
        check.Expect(HumanizeFieldName("target_object") == "Target Object",
            "アンダースコアが空白になる");

        if (first_descs != nullptr && first_descs->size() == 2)
        {
            check.Expect((*first_descs)[0].DisplayName() == "Rotation Speed",
                "Inspector の表示名に接頭辞が出ない");
            check.Expect((*first_descs)[0].name == "field.RotationSpeed",
                "保存名には field. 接頭辞が付く");
        }

        // ---- 予約接頭辞の衝突回避 ------------------------------------------------

        const ScriptTypeID probe_id = MakeLuaScriptTypeID("cccccccccccccccccccccccccccccccc");
        fixture.lua_backend->SetTypeFields(probe_id, MockScriptTypes::ReservedNameProbeFields());

        ScriptTypeDescriptor probe_descriptor;
        probe_descriptor.type_id = probe_id;
        probe_descriptor.language = ScriptLanguage::Lua;
        probe_descriptor.script_name = "ReservedNameProbe";
        probe_descriptor.asset_guid = "cccccccccccccccccccccccccccccccc";
        check.Expect(fixture.runtime->RegisterScriptType(probe_descriptor),
            "予約名を宣言した型も登録できる");

        GameObject* probe_object = fixture.world.CreateGameObject("ReservedProbe");
        ScriptComponent* probe = probe_object != nullptr
            ? fixture.AddScript(*probe_object, probe_id, ScriptLanguage::Lua,
                "cccccccccccccccccccccccccccccccc")
            : nullptr;

        check.Expect(probe != nullptr, "予約名 Field を持つ ScriptComponent を作れる");
        if (probe != nullptr)
        {
            probe->SetLanguage(ScriptLanguage::CSharp);
            probe->RestoreScriptType(probe_id);

            // ユーザーが宣言した language は field.language として別に保存される。
            Reflection::PropertyBag bag;
            PropertyRegistry::Capture(*probe, bag);

            const PropertyValue* internal_language = bag.Find(ScriptNames::language);
            const PropertyValue* user_language = bag.Find("field.language");

            check.Expect(internal_language != nullptr,
                "__script.language が保存される");
            check.Expect(user_language != nullptr,
                "ユーザーが宣言した language は field.language として保存される");
            check.Expect(internal_language != nullptr &&
                internal_language->AsInt(-1) == static_cast<int>(ScriptLanguage::CSharp),
                "管理情報の language がユーザー Field に潰されない");
            check.Expect(user_language != nullptr &&
                user_language->AsString() == "nihongo",
                "ユーザー Field の値が管理情報に潰されない");

            check.Expect(bag.Find("field.class_name") != nullptr,
                "class_name という Field 名も衝突しない");
            check.Expect(bag.Find("field.script_asset") != nullptr,
                "script_asset という Field 名も衝突しない");
            check.Expect(bag.Find("field.execution_order") != nullptr,
                "execution_order という Field 名も衝突しない");
        }

        // ---- 解決できない型でも落ちないこと ---------------------------------------

        const ScriptTypeID missing_id = MakeLuaScriptTypeID("dddddddddddddddddddddddddddddddd");
        GameObject* missing_object = fixture.world.CreateGameObject("MissingScript");
        ScriptComponent* missing = missing_object != nullptr
            ? fixture.AddScript(*missing_object, missing_id, ScriptLanguage::Lua,
                "dddddddddddddddddddddddddddddddd")
            : nullptr;

        check.Expect(missing != nullptr, "未登録の型でも ScriptComponent は作れる");
        if (missing != nullptr)
        {
            check.Expect(missing->DynamicProperties() == nullptr,
                "Schema が無いときは動的プロパティを返さない");
            check.Expect(missing->Status() == ScriptStatus::Unresolved,
                "未解決の状態になる");
        }

        fixture.BeginPlaySession();
        fixture.world.Update(0.016f);
        fixture.world.Update(0.016f);
        check.Expect(true, "未解決のスクリプトがあっても Update が完走する");

        return check.Report("script-core");
    }

    // -----------------------------------------------------------------------
    // 680-739  script-lifecycle
    // -----------------------------------------------------------------------

    int RunScriptLifecycleValidation()
    {
        EnsureRegistries();
        Checker check(680);

        Fixture fixture;

        // 要件 1: Active な GameObject 上の Disabled な ScriptComponent
        GameObject* active_object = fixture.world.CreateGameObject("ActiveObject");
        // 要件 2: Inactive な GameObject 上の ScriptComponent
        GameObject* inactive_object = fixture.world.CreateGameObject("InactiveObject");
        // 通常
        GameObject* normal_object = fixture.world.CreateGameObject("NormalObject");

        check.Expect(active_object != nullptr && inactive_object != nullptr &&
            normal_object != nullptr, "検証用 GameObject を 3 体作れる");
        if (active_object == nullptr || inactive_object == nullptr || normal_object == nullptr)
        {
            return check.Report("script-lifecycle");
        }

        ScriptComponent* disabled_script = fixture.AddRotating(*active_object);
        ScriptComponent* inactive_script = fixture.AddRotating(*inactive_object);
        ScriptComponent* normal_script = fixture.AddRotating(*normal_object);

        check.Expect(disabled_script != nullptr && inactive_script != nullptr &&
            normal_script != nullptr, "ScriptComponent を 3 つ作れる");
        if (disabled_script == nullptr || inactive_script == nullptr ||
            normal_script == nullptr)
        {
            return check.Report("script-lifecycle");
        }

        disabled_script->SetEnabled(false);   // Component を無効化（GameObject は有効）
        inactive_object->SetEnabled(false);   // GameObject ごと無効化

        MockScriptBackend& lua = *fixture.lua_backend;

        check.Expect(lua.CallLog().empty(), "Scene 開始前は Callback が 1 つも呼ばれない");

        // ---- Play セッション開始 ------------------------------------------------

        fixture.BeginPlaySession();

        const ScriptInstanceHandle normal_handle = normal_script->InstanceHandle();
        const ScriptInstanceHandle disabled_handle = disabled_script->InstanceHandle();

        check.Expect(normal_handle != invalid_script_instance_handle,
            "有効なスクリプトのインスタンスが作られる");

        // 要件 1-a / 1-b
        check.Expect(disabled_handle != invalid_script_instance_handle,
            "要件1: 有効な GameObject 上の無効な ScriptComponent にもインスタンスが作られる");
        check.Expect(lua.CountCalls(disabled_handle, ScriptCallback::Awake) == 1,
            "要件1: 無効な ScriptComponent でも Awake が 1 回呼ばれる");
        check.Expect(lua.CountCalls(disabled_handle, ScriptCallback::OnEnable) == 0,
            "要件1: 無効な ScriptComponent に OnEnable は呼ばれない");
        check.Expect(lua.CountCalls(disabled_handle, ScriptCallback::Start) == 0,
            "要件1: 無効な ScriptComponent に Start は呼ばれない");

        // 要件 2-a / 2-b
        check.Expect(inactive_script->InstanceHandle() == invalid_script_instance_handle,
            "要件2: 無効な GameObject 上ではインスタンスが作られない");
        check.Expect(inactive_script->Status() != ScriptStatus::Running,
            "要件2: 無効な GameObject 上のスクリプトは Running にならない");

        // 通常の順序
        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::Awake) == 1,
            "Awake が 1 回");
        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::OnEnable) == 1,
            "OnEnable が 1 回");
        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::Start) == 1,
            "Start が 1 回");
        check.Expect(lua.CallLogContainsInOrder(
            { ScriptCallback::Awake, ScriptCallback::OnEnable, ScriptCallback::Start }),
            "Awake -> OnEnable -> Start の順");

        // ---- 更新 -----------------------------------------------------------------

        fixture.world.Update(0.016f);
        fixture.world.FixedUpdate(0.016f);
        fixture.world.LateUpdate(0.016f);

        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::Update) == 1,
            "Update が 1 回");
        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::FixedUpdate) == 1,
            "FixedUpdate が 1 回");
        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::LateUpdate) == 1,
            "LateUpdate が 1 回");
        check.Expect(lua.CountCalls(disabled_handle, ScriptCallback::Update) == 0,
            "無効な ScriptComponent は Update されない");

        // 二重更新が無いこと。専用の更新経路を作っていれば 2 になる。
        fixture.world.Update(0.016f);
        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::Update) == 2,
            "1 フレームにつき Update は 1 回だけ（二重更新が無い）");

        // ---- 要件 1-c: 後から有効化 ------------------------------------------------

        disabled_script->SetEnabled(true);
        fixture.world.Update(0.016f);

        check.Expect(lua.CountCalls(disabled_handle, ScriptCallback::OnEnable) == 1,
            "要件1: 有効化で OnEnable が 1 回");
        check.Expect(lua.CountCalls(disabled_handle, ScriptCallback::Start) == 1,
            "要件1: 有効化で Start が 1 回");
        check.Expect(lua.CountCalls(disabled_handle, ScriptCallback::Awake) == 1,
            "要件1: 有効化しても Awake は増えない");

        // ---- 要件 2-c: GameObject を有効化 -----------------------------------------

        inactive_object->SetEnabled(true);
        fixture.world.Update(0.016f);

        const ScriptInstanceHandle inactive_handle = inactive_script->InstanceHandle();
        check.Expect(inactive_handle != invalid_script_instance_handle,
            "要件2: GameObject が有効になるとインスタンスが作られる");
        check.Expect(lua.CountCalls(inactive_handle, ScriptCallback::Awake) == 1,
            "要件2: このタイミングで Awake が 1 回");
        check.Expect(lua.CountCalls(inactive_handle, ScriptCallback::OnEnable) == 1,
            "要件2: 続いて OnEnable が 1 回");
        check.Expect(lua.CountCalls(inactive_handle, ScriptCallback::Start) == 1,
            "要件2: 続いて Start が 1 回");

        // 同じ同期点で Awake -> OnEnable -> Start の順に並んでいること。
        {
            std::vector<ScriptCallback> observed;
            for (const MockScriptBackend::CallEntry& entry : lua.CallLog())
            {
                if (entry.instance == inactive_handle) observed.push_back(entry.callback);
            }
            const bool ordered = observed.size() >= 3 &&
                observed[0] == ScriptCallback::Awake &&
                observed[1] == ScriptCallback::OnEnable &&
                observed[2] == ScriptCallback::Start;
            check.Expect(ordered, "要件2: Awake -> OnEnable -> Start の順で呼ばれる");
        }

        // ---- 要件 3: Disable / Enable の反復 ----------------------------------------

        const std::size_t enable_before = lua.CountCalls(normal_handle, ScriptCallback::OnEnable);
        const std::size_t disable_before = lua.CountCalls(normal_handle, ScriptCallback::OnDisable);

        for (int index = 0; index < 10; ++index)
        {
            normal_script->SetEnabled(false);
            fixture.world.Update(0.016f);
            normal_script->SetEnabled(true);
            fixture.world.Update(0.016f);
        }

        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::OnDisable) ==
            disable_before + 10, "要件3: OnDisable がちょうど 10 回増える");
        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::OnEnable) ==
            enable_before + 10, "要件3: OnEnable がちょうど 10 回増える");
        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::Awake) == 1,
            "要件3: 反復しても Awake は 1 回のまま");
        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::Start) == 1,
            "要件3: 反復しても Start は 1 回のまま");

        // GameObject 側での反復も同じ結果になること。
        const std::size_t go_enable_before =
            lua.CountCalls(inactive_handle, ScriptCallback::OnEnable);
        for (int index = 0; index < 5; ++index)
        {
            inactive_object->SetEnabled(false);
            fixture.world.Update(0.016f);
            inactive_object->SetEnabled(true);
            fixture.world.Update(0.016f);
        }
        check.Expect(lua.CountCalls(inactive_handle, ScriptCallback::OnEnable) ==
            go_enable_before + 5, "要件3: GameObject 側の反復でも OnEnable が対で増える");
        check.Expect(lua.CountCalls(inactive_handle, ScriptCallback::Awake) == 1,
            "要件3: GameObject 側の反復でも Awake は 1 回のまま");

        // ---- execution_order の安定性 -----------------------------------------------

        {
            std::vector<ObjectID> first_order;
            for (const MockScriptBackend::CallEntry& entry : lua.CallLog())
            {
                if (entry.callback != ScriptCallback::Update) continue;
                first_order.push_back(entry.object);
            }
            check.Expect(!first_order.empty(), "Update の呼び出し記録が取れる");

            lua.ClearCallLog();
            fixture.world.Update(0.016f);
            std::vector<ObjectID> second;
            for (const MockScriptBackend::CallEntry& entry : lua.CallLog())
            {
                if (entry.callback != ScriptCallback::Update) continue;
                second.push_back(entry.object);
            }

            lua.ClearCallLog();
            fixture.world.Update(0.016f);
            std::vector<ObjectID> third;
            for (const MockScriptBackend::CallEntry& entry : lua.CallLog())
            {
                if (entry.callback != ScriptCallback::Update) continue;
                third.push_back(entry.object);
            }

            check.Expect(second == third,
                "execution_order が同じとき、呼び出し順は毎フレーム安定している");
        }

        // ---- 要件 2-c の続き: Awake していないスクリプトの破棄 ------------------------

        {
            GameObject* never = fixture.world.CreateGameObject("NeverAwake");
            check.Expect(never != nullptr, "検証用 GameObject を作れる");
            if (never != nullptr)
            {
                never->SetEnabled(false);
                ScriptComponent* script = fixture.AddRotating(*never);
                fixture.world.Update(0.016f);

                check.Expect(script != nullptr &&
                    script->InstanceHandle() == invalid_script_instance_handle,
                    "無効な GameObject 上ではインスタンスが作られない");

                const std::size_t destroy_before = lua.CountCalls(ScriptCallback::OnDestroy);
                const std::uint64_t destroyed_before = lua.DestroyedCount();

                never->Destroy();
                fixture.world.Update(0.016f);

                check.Expect(lua.CountCalls(ScriptCallback::OnDestroy) == destroy_before,
                    "Awake していないスクリプトへ OnDestroy は呼ばれない");
                check.Expect(lua.DestroyedCount() == destroyed_before,
                    "作っていないインスタンスを破棄しようとしない");
            }
        }

        // ---- Play セッション終了 -------------------------------------------------------

        const std::size_t destroy_before_end = lua.CountCalls(ScriptCallback::OnDestroy);
        const std::size_t live_before_end = lua.LiveInstanceCount();
        check.Expect(live_before_end > 0, "終了前は生存インスタンスがある");

        fixture.EndPlaySession();

        check.Expect(lua.CountCalls(ScriptCallback::OnDestroy) > destroy_before_end,
            "終了時に OnDestroy が呼ばれる");
        check.Expect(lua.LiveInstanceCount() == 0,
            "終了後に Lua 側の生存インスタンスが 0 になる");
        check.Expect(fixture.csharp_backend->LiveInstanceCount() == 0,
            "終了後に C# 側の生存インスタンスが 0 になる");
        check.Expect(fixture.runtime->LastLeakedInstanceCount() == 0,
            "解放漏れが 0 と記録される");
        check.Expect(fixture.runtime->World() == nullptr,
            "ScriptWorld が破棄されている");
        check.Expect(!fixture.runtime->PlaySessionActive(),
            "Play セッションが終わっている");

        return check.Report("script-lifecycle");
    }

    // -----------------------------------------------------------------------
    // 740-799  script-serialization
    // -----------------------------------------------------------------------

    int RunScriptSerializationValidation()
    {
        EnsureRegistries();
        Checker check(740);

        Fixture fixture;

        GameObject* object = fixture.world.CreateGameObject("SaveTarget");
        check.Expect(object != nullptr, "検証用 GameObject を作れる");
        if (object == nullptr) return check.Report("script-serialization");

        ScriptComponent* script = fixture.AddRotating(*object);
        check.Expect(script != nullptr, "ScriptComponent を作れる");
        if (script == nullptr) return check.Report("script-serialization");

        script->WriteField("field.RotationSpeed", PropertyValue::MakeFloat(123.5f));
        script->WriteField("field.LocalSpace", PropertyValue::MakeBool(false));

        // ---- 保存 -------------------------------------------------------------------

        Reflection::PropertyBag saved;
        PropertyRegistry::Capture(*script, saved);

        check.Expect(saved.Find(ScriptNames::language) != nullptr, "language が保存される");
        check.Expect(saved.Find(ScriptNames::asset) != nullptr, "asset が保存される");
        check.Expect(saved.Find(ScriptNames::execution_order) != nullptr,
            "execution_order が保存される");
        check.Expect(saved.Find(ScriptNames::type_id) != nullptr, "type_id が保存される");
        check.Expect(saved.Find("field.RotationSpeed") != nullptr,
            "ユーザー Field が保存される");

        const PropertyValue* saved_speed = saved.Find("field.RotationSpeed");
        check.Expect(saved_speed != nullptr && saved_speed->AsFloat(0.0f) == 123.5f,
            "float の Field 値が保存される");

        const PropertyValue* saved_local = saved.Find("field.LocalSpace");
        check.Expect(saved_local != nullptr && saved_local->AsBool(true) == false,
            "bool の Field 値が保存される");

        // ---- 読み込み ----------------------------------------------------------------

        GameObject* restored_object = fixture.world.CreateGameObject("Restored");
        auto* restored = restored_object != nullptr
            ? restored_object->AddComponent<ScriptComponent>() : nullptr;
        check.Expect(restored != nullptr, "復元先の ScriptComponent を作れる");

        if (restored != nullptr)
        {
            PropertyRegistry::Apply(*restored, saved);

            check.Expect(restored->ScriptType() == script->ScriptType(),
                "ScriptTypeID が復元される");
            check.Expect(restored->Language() == ScriptLanguage::Lua, "language が復元される");
            check.Expect(restored->DynamicProperties() != nullptr,
                "復元後に Schema が引けている");
            check.Expect(restored->ReadField("field.RotationSpeed").AsFloat(0.0f) == 123.5f,
                "float の Field 値が復元される");
            check.Expect(restored->ReadField("field.LocalSpace").AsBool(true) == false,
                "bool の Field 値が復元される");
            check.Expect(restored->DynamicProperties() == script->DynamicProperties(),
                "復元後も Schema を共有している");
        }

        // ---- 型が解決できないときの保護 -------------------------------------------------

        {
            // 【状況 A】一度も読み込みに成功していない型。
            //
            // 別ブランチにしか無いスクリプトを含む Scene を開いた場合など。
            // Schema がまったく無いので、Field 値は預かり箱で守るしかない。
            const char* orphan_guid = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
            const ScriptTypeID orphan_type = MakeLuaScriptTypeID(orphan_guid);

            fixture.lua_backend->SetTypeResolvable(orphan_type, false);

            ScriptTypeDescriptor orphan_descriptor;
            orphan_descriptor.type_id = orphan_type;
            orphan_descriptor.language = ScriptLanguage::Lua;
            orphan_descriptor.script_name = "NeverLoaded";
            orphan_descriptor.asset_guid = orphan_guid;
            check.Expect(!fixture.runtime->RegisterScriptType(orphan_descriptor),
                "読み込めない型の登録は失敗として返る");
            check.Expect(!fixture.runtime->ResolveSchema(orphan_type),
                "一度も成功していない型には Schema が無い");

            // その型を指す保存データを作る。値は上で保存したものを流用する。
            Reflection::PropertyBag orphan_saved = saved;
            orphan_saved.Set(ScriptNames::asset,
                PropertyValue::MakeAssetReference(orphan_guid));
            orphan_saved.Set(ScriptNames::type_id,
                PropertyValue::MakeString(orphan_type.ToString()));

            GameObject* orphan_object = fixture.world.CreateGameObject("Orphan");
            auto* orphan = orphan_object != nullptr
                ? orphan_object->AddComponent<ScriptComponent>() : nullptr;
            check.Expect(orphan != nullptr, "未解決状態の復元先を作れる");

            if (orphan != nullptr)
            {
                PropertyRegistry::Apply(*orphan, orphan_saved);

                check.Expect(orphan->DynamicProperties() == nullptr,
                    "解決不能なので動的プロパティは無い");
                check.Expect(orphan->Status() != ScriptStatus::Running,
                    "解決不能な状態が記録される");
                check.Expect(!orphan->PendingValues().Empty(),
                    "Field 値が預かり箱へ入る");

                // ここが最重要。開いて保存しただけで値が消えないこと。
                Reflection::PropertyBag round_trip;
                PropertyRegistry::Capture(*orphan, round_trip);

                const PropertyValue* kept_speed = round_trip.Find("field.RotationSpeed");
                const PropertyValue* kept_local = round_trip.Find("field.LocalSpace");

                check.Expect(kept_speed != nullptr && kept_speed->AsFloat(0.0f) == 123.5f,
                    "解決不能でも float の Field 値が保存し直される");
                check.Expect(kept_local != nullptr && kept_local->AsBool(true) == false,
                    "解決不能でも bool の Field 値が保存し直される");
                check.Expect(round_trip.Find(ScriptNames::type_id) != nullptr,
                    "解決不能でも ScriptTypeID が保たれる");

                // ---- 再解決 ---------------------------------------------------------
                fixture.lua_backend->SetTypeResolvable(orphan_type, true);
                fixture.lua_backend->SetTypeFields(orphan_type,
                    MockScriptTypes::RotatingObjectFields());
                fixture.runtime->RequestSchemaReload(orphan_type);
                fixture.runtime->ApplyPendingSchemaSwaps(0.016f);

                check.Expect(orphan->ResolveSchema(), "再解決できる");
                check.Expect(orphan->DynamicProperties() != nullptr,
                    "再解決後に動的プロパティが戻る");
                check.Expect(orphan->ReadField("field.RotationSpeed").AsFloat(0.0f) == 123.5f,
                    "再解決後に Field 値が復元される");
                check.Expect(orphan->ReadField("field.LocalSpace").AsBool(true) == false,
                    "再解決後に bool の Field 値も復元される");
            }

            // 【状況 B】一度は読み込みに成功した型が、あとで読めなくなった場合。
            //
            // 指示書 8.4 / 9.6 の「最後に正常動作した版を維持する」がこれ。
            // Schema を捨ててしまうと Inspector から Field が消え、
            // Compile が通っていない間だけ設定が編集不能になる。
            {
                const ScriptTypeID rotating = MockScriptTypes::RotatingObjectTypeID();
                const ScriptFieldSchemaRef before = fixture.runtime->ResolveSchema(rotating);
                check.Expect(static_cast<bool>(before), "壊す前は Schema がある");

                fixture.lua_backend->SetTypeResolvable(rotating, false);
                fixture.runtime->RequestSchemaReload(rotating);
                fixture.runtime->ApplyPendingSchemaSwaps(0.016f);

                const ScriptFieldSchemaRef after = fixture.runtime->ResolveSchema(rotating);
                check.Expect(after.get() == before.get(),
                    "読み込みに失敗しても最後に成功した Schema を維持する");

                const ScriptTypeDescriptor* entry = fixture.runtime->Catalog().Find(rotating);
                check.Expect(entry != nullptr && entry->status == ScriptStatus::Error,
                    "失敗したことは状態として残る");
                check.Expect(entry != nullptr && !entry->last_error.empty(),
                    "失敗理由が残る");

                check.Expect(script->ReadField("field.RotationSpeed").AsFloat(0.0f) == 123.5f,
                    "読み込み失敗中も既存インスタンスの Field 値は編集できるまま");

                // 元へ戻す。以降の検査が影響を受けないようにする。
                fixture.lua_backend->SetTypeResolvable(rotating, true);
                fixture.runtime->RequestSchemaReload(rotating);
                fixture.runtime->ApplyPendingSchemaSwaps(0.016f);
            }
        }

        // ---- Schema 差し替え（Field 追加・型変更） ---------------------------------------

        {
            fixture.lua_backend->SetTypeFields(MockScriptTypes::RotatingObjectTypeID(),
                MockScriptTypes::RotatingObjectFieldsV2());
            fixture.runtime->RequestSchemaReload(MockScriptTypes::RotatingObjectTypeID());
            fixture.runtime->ApplyPendingSchemaSwaps(0.016f);

            script->BindSchema(fixture.runtime->ResolveSchema(script->ScriptType()));

            check.Expect(script->DynamicProperties() != nullptr &&
                script->DynamicProperties()->size() == 3,
                "差し替え後の Field は 3 個");
            check.Expect(script->ReadField("field.RotationSpeed").AsFloat(0.0f) == 123.5f,
                "据え置きの Field 値は保たれる");
            check.Expect(script->ReadField("field.LocalSpace").Type() ==
                Reflection::PropertyType::Int,
                "型を変えた Field は新しい型になる");
            check.Expect(script->ReadField("field.SpinAxis").AsVector3().y == 1.0f,
                "新しく増えた Field は既定値で埋まる");
        }

        // ---- Clone -------------------------------------------------------------------

        {
            // 元の Schema へ戻してから複製する。
            fixture.lua_backend->SetTypeFields(MockScriptTypes::RotatingObjectTypeID(),
                MockScriptTypes::RotatingObjectFields());
            fixture.runtime->RequestSchemaReload(MockScriptTypes::RotatingObjectTypeID());
            fixture.runtime->ApplyPendingSchemaSwaps(0.016f);
            script->BindSchema(fixture.runtime->ResolveSchema(script->ScriptType()));
            script->WriteField("field.RotationSpeed", PropertyValue::MakeFloat(77.25f));

            GameObject* copy = Serialization::DuplicateGameObject(fixture.world, *object, false);
            check.Expect(copy != nullptr, "GameObject を複製できる");

            if (copy != nullptr)
            {
                auto* copied_script = copy->GetComponent<ScriptComponent>();
                check.Expect(copied_script != nullptr, "複製先に ScriptComponent がある");
                if (copied_script != nullptr)
                {
                    check.Expect(copied_script->ScriptType() == script->ScriptType(),
                        "複製先の ScriptTypeID が同じ");
                    check.Expect(copied_script->DynamicProperties() ==
                        script->DynamicProperties(),
                        "複製先も同じ Schema を共有する");
                    check.Expect(
                        copied_script->ReadField("field.RotationSpeed").AsFloat(0.0f) == 77.25f,
                        "複製で Field 値が引き継がれる");
                }
            }
        }

        // ---- Scene 往復（SceneData 経由） -------------------------------------------------

        {
            Serialization::SceneData data;
            Serialization::CaptureScene(fixture.world, data);

            bool found_script_component = false;
            bool found_field = false;
            for (const Serialization::GameObjectData& object_data : data.objects)
            {
                for (const Serialization::ComponentData& component_data : object_data.components)
                {
                    if (component_data.type_name != "ScriptComponent") continue;
                    found_script_component = true;
                    if (component_data.properties.Find("field.RotationSpeed") != nullptr)
                    {
                        found_field = true;
                    }
                }
            }

            check.Expect(found_script_component, "ScriptComponent が SceneData へ保存される");
            check.Expect(found_field, "Field 値が SceneData へ保存される");
            check.Expect(data.version == Serialization::SceneData::current_version,
                "Scene のバージョンは現行のまま");
            check.Expect(Serialization::SceneData::current_version == 11,
                "Scene のバージョンは v11 のまま（上げない）");

            // Undo 相当。SceneData を戻すと値も戻ること。
            Scene::Scene restored_world("RestoredWorld");
            restored_world.Services().SetScripts(fixture.runtime.get());

            Serialization::SceneLoadReport report;
            check.Expect(Serialization::ApplySceneData(data, restored_world, report),
                "SceneData から Scene を復元できる");
            check.Expect(report.skipped_components == 0,
                "復元で読み飛ばされた Component が 0");
            check.Expect(report.missing_components == 0,
                "Missing Component にならない");

            GameObject* restored_target = restored_world.FindGameObjectByName("SaveTarget");
            check.Expect(restored_target != nullptr, "復元先の GameObject が見つかる");
            if (restored_target != nullptr)
            {
                auto* restored_script = restored_target->GetComponent<ScriptComponent>();
                check.Expect(restored_script != nullptr, "復元先に ScriptComponent がある");
                if (restored_script != nullptr)
                {
                    check.Expect(
                        restored_script->ReadField("field.RotationSpeed").AsFloat(0.0f) == 77.25f,
                        "Scene 往復で Field 値が保たれる");
                    check.Expect(restored_script->ScriptType() == script->ScriptType(),
                        "Scene 往復で ScriptTypeID が保たれる");
                }
            }
            restored_world.Clear();
        }

        return check.Report("script-serialization");
    }
}
