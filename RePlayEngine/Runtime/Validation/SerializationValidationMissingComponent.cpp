#include "SerializationValidationInternal.h"

namespace ReplayEngine::Runtime::Validation
{
    using namespace Detail::SerializationValidation;

    // =====================================================================
    // Missing Component / Unknown Property
    // =====================================================================

    int RunMissingComponentValidation()
    {
        Core::RegisterBuiltInComponents();
        Checker check(180);

        // ---- 未登録の型を含む Scene ---------------------------------------

        Serialization::SceneData source;
        source.scene_name = "MissingComponentValidation";

        Serialization::GameObjectData object;
        object.id = ObjectID{ 1 };
        object.name = "MissingHolder";
        object.components.push_back(MakeAllTypesComponent());

        // 登録済みの型に、その型が知らないプロパティを混ぜたものも並べる。
        Serialization::ComponentData known;
        known.type_name = "RotatorComponent";
        known.type_id = Core::MakeComponentTypeID(known.type_name);
        known.stable_id = 20;
        known.properties.Set("degrees_per_second", PropertyValue::MakeFloat(90.0f));
        known.properties.Set("future_field_added_later", PropertyValue::MakeInt(1234));
        known.properties.Set("future_array",
            PropertyValue::MakeArray(PropertyType::String,
                std::vector<PropertyValue>{ PropertyValue::MakeString("残る") }));
        object.components.push_back(known);

        source.objects.push_back(object);

        ReplayEngine::Scene::Scene world("MissingWorld");
        Serialization::SceneLoadReport report;
        check.Expect(Serialization::ApplySceneData(source, world, report),
            "未登録の型を含む Scene でも読み込みが成功する");
        check.Expect(report.missing_components == 1,
            "未登録の型が Missing Component として 1 件記録される");
        check.Expect(report.skipped_components == 0,
            "未登録の型を読み飛ばしていない");
        check.Expect(report.unknown_properties == 2,
            "登録済みの型に混ざった未知プロパティ 2 件が記録される");

        Core::GameObject* holder = world.FindGameObjectByID(ObjectID{ 1 });
        check.Expect(holder != nullptr, "GameObject が復元される");

        Core::Component* missing =
            holder != nullptr ? holder->FindComponentByStableID(7) : nullptr;
        check.Expect(missing != nullptr, "Missing Component が StableID を保って作られる");
        check.Expect(missing != nullptr &&
            missing->TypeID() == MissingComponent::StaticTypeID(),
            "作られたのは MissingComponent である");

        if (missing != nullptr && missing->TypeID() == MissingComponent::StaticTypeID())
        {
            const MissingComponent::Record& record =
                static_cast<MissingComponent*>(missing)->Original();
            check.Expect(record.type_name == "ValidationAllTypesComponent",
                "元の型名を保持している");
            check.Expect(record.type_guid ==
                Reflection::MakeTypeGUID("11223344556677889900aabbccddeeff"),
                "元の Type GUID を保持している");
            check.Expect(record.module_id == "RePlayEngine.Validation",
                "元のモジュール名を保持している");
            check.Expect(record.type_version == 3, "元の型バージョンを保持している");
            check.Expect(record.properties.Size() ==
                MakeAllTypesComponent().properties.Size(),
                "元のプロパティを 1 件も失っていない");
            check.Expect(!missing->Enabled(), "元の有効状態を保持している");
        }

        // ---- 登録済みの型に混ざった未知プロパティ ---------------------------

        Core::Component* rotator =
            holder != nullptr ? holder->FindComponentByStableID(20) : nullptr;
        check.Expect(rotator != nullptr, "登録済みの型は通常どおり生成される");
        check.Expect(rotator != nullptr && rotator->UnknownProperties() != nullptr,
            "未知プロパティが Component へ預けられる");

        if (rotator != nullptr && rotator->UnknownProperties() != nullptr)
        {
            const PropertyBag& retained = *rotator->UnknownProperties();
            check.Expect(retained.Size() == 2, "未知プロパティが 2 件預けられている");
            const PropertyValue* future = retained.Find("future_field_added_later");
            check.Expect(future != nullptr && future->AsInt() == 1234,
                "未知プロパティの値が保持されている（名前だけでなく値も）");
            const PropertyValue* future_array = retained.Find("future_array");
            check.Expect(future_array != nullptr && future_array->IsArray() &&
                future_array->ArrayElements().size() == 1,
                "未知の配列プロパティも保持されている");
        }

        // ---- 保存し直しても失われない --------------------------------------

        Serialization::SceneData captured;
        Serialization::CaptureScene(world, captured);
        check.Expect(captured.objects.size() == 1, "保存し直せる");

        if (captured.objects.size() == 1)
        {
            const std::vector<Serialization::ComponentData>& components =
                captured.objects[0].components;
            check.Expect(components.size() == 2, "Component が 2 件とも書き出される");

            const Serialization::ComponentData* rewritten = nullptr;
            const Serialization::ComponentData* rewritten_known = nullptr;
            for (const Serialization::ComponentData& component : components)
            {
                if (component.type_name == "ValidationAllTypesComponent") rewritten = &component;
                if (component.type_name == "RotatorComponent") rewritten_known = &component;
            }

            check.Expect(rewritten != nullptr,
                "Missing Component は元の型名で書き戻される（MissingComponent とは書かない）");
            if (rewritten != nullptr)
            {
                check.Expect(rewritten->type_guid ==
                    Reflection::MakeTypeGUID("11223344556677889900aabbccddeeff"),
                    "書き戻しでも元の Type GUID が入る");
                check.Expect(rewritten->stable_id == 7, "書き戻しでも StableID が入る");
                check.Expect(rewritten->type_version == 3,
                    "書き戻しでも元の型バージョンが入る");
                check.Expect(BagsEqual(MakeAllTypesComponent().properties,
                    rewritten->properties),
                    "Missing Component のプロパティが 1 件も欠けずに書き戻される");
            }

            check.Expect(rewritten_known != nullptr, "登録済みの型も書き戻される");
            if (rewritten_known != nullptr)
            {
                check.Expect(rewritten_known->properties.Contains("future_field_added_later"),
                    "未知プロパティが保存へ合流する");
                check.Expect(rewritten_known->properties.Contains("future_array"),
                    "未知の配列プロパティも保存へ合流する");
                const PropertyValue* known_value =
                    rewritten_known->properties.Find("degrees_per_second");
                check.Expect(known_value != nullptr &&
                    std::fabs(known_value->AsFloat() - 90.0f) < 0.001f,
                    "登録済みプロパティの値も正しく保存される");
            }
        }

        // ---- ファイルへ往復しても内容が変わらない ---------------------------

        Serialization::SceneData reloaded;
        std::string text;
        std::string error;
        check.Expect(RoundTrip(captured, reloaded, text, error),
            "Missing Component を含む Scene をファイル形式で往復できる");

        ReplayEngine::Scene::Scene second_world("MissingWorld2");
        Serialization::SceneLoadReport second_report;
        check.Expect(Serialization::ApplySceneData(reloaded, second_world, second_report),
            "往復後の Scene をもう一度読み込める");
        check.Expect(second_report.missing_components == 1,
            "往復後も Missing Component は 1 件のまま（増えも減りもしない）");

        Serialization::SceneData second_captured;
        Serialization::CaptureScene(second_world, second_captured);
        std::string second_text;
        Serialization::SceneData ignored;
        check.Expect(RoundTrip(second_captured, ignored, second_text, error) &&
            second_text == text,
            "Missing Component を含んだまま保存を繰り返しても内容が変化しない");

        // ---- 型が使えるようになれば復元される ------------------------------

        {
            // RotatorComponent は登録済みなので、
            // 「未知だったプロパティ名が既知になった」状況を模して確かめる。
            Serialization::SceneData rehydrate;
            rehydrate.scene_name = "Rehydrate";
            Serialization::GameObjectData target;
            target.id = ObjectID{ 1 };
            target.name = "Rehydrate";

            Serialization::ComponentData data;
            data.type_name = "RotatorComponent";
            data.type_id = Core::MakeComponentTypeID(data.type_name);
            // 1 は組み込み Transform が取るので使わない。
            data.stable_id = 5;
            // 未知の名前で保存されていた値。
            data.properties.Set("unknown_then", PropertyValue::MakeFloat(45.0f));
            target.components.push_back(data);
            rehydrate.objects.push_back(target);

            ReplayEngine::Scene::Scene rehydrate_world("RehydrateWorld");
            Serialization::SceneLoadReport rehydrate_report;
            Serialization::ApplySceneData(rehydrate, rehydrate_world, rehydrate_report);

            Core::GameObject* rehydrate_object =
                rehydrate_world.FindGameObjectByID(ObjectID{ 1 });
            Core::Component* rehydrate_component = rehydrate_object != nullptr
                ? rehydrate_object->FindComponentByStableID(5) : nullptr;

            check.Expect(rehydrate_component != nullptr &&
                rehydrate_component->UnknownProperties() != nullptr &&
                rehydrate_component->UnknownProperties()->Contains("unknown_then"),
                "未知の名前は預かられる");

            // 既知の名前だけを与え直すと、預かりが解ける。
            if (rehydrate_component != nullptr)
            {
                PropertyBag known_only;
                known_only.Set("degrees_per_second", PropertyValue::MakeFloat(45.0f));
                Reflection::PropertyRegistry::Apply(*rehydrate_component, known_only);

                check.Expect(rehydrate_component->UnknownProperties() == nullptr,
                    "すべて既知になった時点で預かりが解ける（Rehydrate）");
            }
        }

        return check.Report("Missing Component validation", 180);
    }
}
