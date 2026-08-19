// Script 基盤検証のうち、Schema・Catalog・ScriptTypeID の判定を持つ。
//
//   ScriptCoreValidation.cpp              … Script Core（このファイル）
//   ScriptCoreLifecycleValidation.cpp     … Lifecycle
//   ScriptCoreSerializationValidation.cpp … 保存・復元・Clone
//   ScriptCoreValidationSupport.cpp       … Registry の共有準備
//   ScriptCoreValidationInternal.h        … 分割内部の Checker と Fixture

#include "ScriptCoreValidationInternal.h"

namespace ReplayEngine::Scripting::Validation
{
    using namespace Detail;

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
}
