#include "ScriptCoreValidationInternal.h"

namespace ReplayEngine::Scripting::Validation
{
    using namespace Detail;

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
