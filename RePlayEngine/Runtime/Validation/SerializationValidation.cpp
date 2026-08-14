// Serialization 検証のうち、型付き値・参照・StableID・Prefab の判定を持つ。
//
//   SerializationValidation.cpp                 … Serialization 判定（このファイル）
//   SerializationValidationSupport.cpp          … 往復・検証データ生成ヘルパ
//   SerializationValidationMissingComponent.cpp … Missing / Unknown Property 判定
//   SerializationValidationSceneVersion.cpp     … Scene Version 移行判定
//   SerializationValidationInternal.h           … 分割内部の Checker と共有宣言

#include "SerializationValidationInternal.h"

namespace ReplayEngine::Runtime::Validation
{
    using namespace Detail::SerializationValidation;

    // =====================================================================
    // Serialization
    // =====================================================================

    int RunSerializationValidation()
    {
        Core::RegisterBuiltInComponents();
        Checker check(140);

        // ---- TypeGUID の文字列往復 ---------------------------------------

        const TypeGUID guid = Reflection::MakeTypeGUID("0123456789abcdeffedcba9876543210");
        check.Expect(guid.IsValid(), "32 文字 16 進から TypeGUID を作れる");
        check.Expect(guid.ToString() == "0123456789abcdeffedcba9876543210",
            "TypeGUID を同じ文字列へ戻せる");

        TypeGUID parsed;
        check.Expect(TypeGUID::TryParse("0123456789ABCDEFFEDCBA9876543210", parsed) &&
            parsed == guid, "大文字表記も同じ TypeGUID として解析できる");
        check.Expect(TypeGUID::TryParse("01234567-89ab-cdef-fedc-ba9876543210", parsed) &&
            parsed == guid, "ハイフン入り表記も解析できる");
        check.Expect(!TypeGUID::TryParse("short", parsed), "桁数不足は解析に失敗する");
        check.Expect(!TypeGUID::TryParse("0123456789abcdeffedcba987654321z", parsed),
            "16 進以外の文字は解析に失敗する");
        check.Expect(!Reflection::MakeTypeGUID("not-32-hex-characters").IsValid(),
            "不正な文字列からは無効 GUID になる");
        check.Expect(!TypeGUID::Invalid().IsValid(), "無効 GUID は IsValid が false");

        // ---- 全プロパティ型のファイル往復 ---------------------------------

        Serialization::SceneData source;
        source.scene_name = "SerializationValidation";
        source.controlled_object = ObjectID{ 42 };

        Serialization::GameObjectData object;
        object.id = ObjectID{ 42 };
        object.name = "検証オブジェクト";
        object.enabled = false;
        object.position = DirectX::XMFLOAT3{ 1.0f, 2.0f, 3.0f };
        object.rotation = DirectX::XMFLOAT3{ 0.1f, 0.2f, 0.3f };
        object.scale = DirectX::XMFLOAT3{ 2.0f, 2.0f, 2.0f };
        object.components.push_back(MakeAllTypesComponent());
        source.objects.push_back(object);

        Serialization::SceneData restored;
        std::string text;
        std::string error;
        const bool round_trip_ok = RoundTrip(source, restored, text, error);
        check.Expect(round_trip_ok, "全型を含む Scene を書いて読み戻せる");

        if (!round_trip_ok)
        {
            std::fprintf(stderr, "  round-trip error: %s\n", error.c_str());
            return check.Report("Serialization validation", 140);
        }

        check.Expect(restored.version == Serialization::SceneData::current_version,
            "書き出したファイルは現行バージョンになる");
        check.Expect(restored.scene_name == source.scene_name, "Scene 名が往復する");
        check.Expect(restored.controlled_object == source.controlled_object,
            "操作対象 ObjectID が往復する");
        check.Expect(restored.objects.size() == 1, "GameObject 数が往復する");

        if (restored.objects.size() == 1 && restored.objects[0].components.size() == 1)
        {
            const Serialization::ComponentData& original = source.objects[0].components[0];
            const Serialization::ComponentData& result = restored.objects[0].components[0];

            check.Expect(result.type_name == original.type_name, "型名が往復する");
            check.Expect(result.type_guid == original.type_guid, "Type GUID が往復する");
            check.Expect(result.module_id == original.module_id, "モジュール名が往復する");
            check.Expect(result.type_version == original.type_version,
                "型バージョンが往復する");
            check.Expect(result.stable_id == original.stable_id,
                "Component StableID が往復する");
            check.Expect(result.enabled == original.enabled, "Component の有効状態が往復する");
            check.Expect(BagsEqual(original.properties, result.properties),
                "全プロパティ型が型も値も保ったまま往復する");

            // 個別に効いていることを確かめる（まとめて比較すると
            // 1 つの型が抜けても気づきにくいため）。
            const auto* int64_value = result.properties.Find("p_int64");
            check.Expect(int64_value != nullptr &&
                int64_value->Type() == PropertyType::Int64 &&
                int64_value->AsInt64() == -9007199254740993LL,
                "int64 が桁落ちせず往復する");

            const auto* uint64_value = result.properties.Find("p_uint64");
            check.Expect(uint64_value != nullptr &&
                uint64_value->Type() == PropertyType::UInt64 &&
                uint64_value->AsUInt64() == 18446744073709551615ULL,
                "uint64 の最大値が往復する");

            const auto* asset_value = result.properties.Find("p_assetref");
            check.Expect(asset_value != nullptr &&
                asset_value->Type() == PropertyType::AssetReference &&
                asset_value->AsString() == "851a285f21a43904dfcc9ca70496e5d5",
                "AssetReference が AssetGUID のまま往復する");

            const auto* scene_value = result.properties.Find("p_sceneref");
            check.Expect(scene_value != nullptr &&
                scene_value->Type() == PropertyType::SceneReference,
                "SceneReference が AssetReference と区別されたまま往復する");

            const auto* comp_value = result.properties.Find("p_compref");
            check.Expect(comp_value != nullptr &&
                comp_value->Type() == PropertyType::ComponentReference &&
                comp_value->AsComponentReference().owner == ObjectID{ 42 } &&
                comp_value->AsComponentReference().component == 9,
                "ComponentReference が所有 ID と Component ID の両方を保って往復する");

            const auto* array_value = result.properties.Find("p_array_string");
            check.Expect(array_value != nullptr && array_value->IsArray() &&
                array_value->ArrayElementType() == PropertyType::String &&
                array_value->ArrayElements().size() == 2 &&
                array_value->ArrayElements()[0].AsString() == "空白 を含む 文字列" &&
                array_value->ArrayElements()[1].AsString().empty(),
                "空白や空文字を含む文字列配列が往復する");

            const auto* string_value = result.properties.Find("p_string");
            check.Expect(string_value != nullptr &&
                string_value->AsString() == "日本語 と \"引用符\" を含む",
                "日本語と引用符を含む文字列が往復する");
        }
        else
        {
            check.Expect(false, "Component が 1 件読み戻せる");
        }

        // ---- 2 回目の往復が 1 回目と完全一致する（冪等） -------------------

        Serialization::SceneData second;
        std::string second_text;
        const bool second_ok = RoundTrip(restored, second, second_text, error);
        check.Expect(second_ok, "読み戻した SceneData をもう一度書き出せる");
        check.Expect(second_ok && second_text == text,
            "書き出しが冪等（2 回目の出力が 1 回目と完全一致）");

        // ---- NaN / Infinity の扱い ----------------------------------------

        {
            Serialization::SceneData broken = source;
            broken.objects[0].components[0].properties.Set("p_float",
                PropertyValue::MakeFloat(std::nanf("")));
            broken.objects[0].position.x = std::numeric_limits<float>::infinity();

            Serialization::SceneData recovered;
            std::string broken_text;
            const bool broken_ok = RoundTrip(broken, recovered, broken_text, error);
            check.Expect(broken_ok, "NaN / Infinity を含んでいても Scene を書いて読み戻せる");

            if (broken_ok && !recovered.objects.empty() &&
                !recovered.objects[0].components.empty())
            {
                const auto* value = recovered.objects[0].components[0].properties.Find("p_float");
                check.Expect(value != nullptr && std::isfinite(value->AsFloat()),
                    "NaN は有限値へ丸められて保存される");
                check.Expect(std::isfinite(recovered.objects[0].position.x),
                    "Infinity の Transform も有限値へ丸められる");
            }
            else
            {
                check.Expect(false, "NaN を含む Scene の読み戻し結果を確認できる");
            }
        }

        // ---- 値の等価判定 --------------------------------------------------

        check.Expect(Reflection::ValuesEqual(PropertyValue::MakeFloat(1.0f),
            PropertyValue::MakeFloat(1.000001f)), "float は許容差で等しいと判定する");
        check.Expect(!Reflection::ValuesEqual(PropertyValue::MakeFloat(1.0f),
            PropertyValue::MakeFloat(1.1f)), "float の明確な差は不一致と判定する");
        check.Expect(!Reflection::ValuesEqual(PropertyValue::MakeInt(1),
            PropertyValue::MakeInt64(1)), "型が違えば等しくない");

        {
            std::vector<PropertyValue> a;
            a.push_back(PropertyValue::MakeInt(1));
            std::vector<PropertyValue> b;
            b.push_back(PropertyValue::MakeInt(1));
            std::vector<PropertyValue> c;
            c.push_back(PropertyValue::MakeInt(2));
            check.Expect(Reflection::ValuesEqual(
                PropertyValue::MakeArray(PropertyType::Int, std::move(a)),
                PropertyValue::MakeArray(PropertyType::Int, std::move(b))),
                "同じ内容の配列は等しいと判定する");
            std::vector<PropertyValue> a2;
            a2.push_back(PropertyValue::MakeInt(1));
            check.Expect(!Reflection::ValuesEqual(
                PropertyValue::MakeArray(PropertyType::Int, std::move(a2)),
                PropertyValue::MakeArray(PropertyType::Int, std::move(c))),
                "内容が違う配列は不一致と判定する");
        }

        // ---- StableID と参照付け替え（実 Scene 経由） ----------------------

        {
            ReplayEngine::Scene::Scene world("ValidationWorld");
            Serialization::SceneLoadReport report;
            check.Expect(Serialization::ApplySceneData(restored, world, report),
                "検証用 Scene を実 Scene へ流し込める");
            check.Expect(world.GameObjectCount() == 1, "GameObject が 1 体できる");

            Core::GameObject* loaded = world.FindGameObjectByID(ObjectID{ 42 });
            check.Expect(loaded != nullptr, "保存 ObjectID のまま復元される");

            if (loaded != nullptr)
            {
                Core::Component* placeholder = loaded->FindComponentByStableID(7);
                check.Expect(placeholder != nullptr,
                    "保存された StableID のまま Component を引ける");
                check.Expect(placeholder != nullptr && placeholder->StableID() == 7,
                    "Component の StableID が保存値と一致する");
                check.Expect(placeholder != nullptr && !placeholder->Enabled(),
                    "Component の無効状態が復元される");

                // 保存し直しても StableID と型情報が変わらないこと。
                Serialization::SceneData captured;
                Serialization::CaptureScene(world, captured);
                check.Expect(!captured.objects.empty() &&
                    captured.objects[0].components.size() == 1 &&
                    captured.objects[0].components[0].stable_id == 7,
                    "保存し直しても StableID が維持される");
                check.Expect(!captured.objects.empty() &&
                    captured.objects[0].components.size() == 1 &&
                    captured.objects[0].components[0].type_guid ==
                        Reflection::MakeTypeGUID("11223344556677889900aabbccddeeff"),
                    "保存し直しても Type GUID が維持される");
            }
        }

        // ---- StableID が衝突した場合の救済 ---------------------------------
        //
        // GameObject を作った時点で組み込みの TransformComponent が自動で付き、
        // StableID 1 を取る。保存データ側が 1 を主張してきた場合、
        // そのまま受け入れると「同じ番号の Component が 2 つ」になり、
        // ComponentReference の解決先が不定になる。
        // 採番し直したうえで報告する経路が働くことを確かめる。
        {
            Serialization::SceneData colliding;
            colliding.scene_name = "StableIDCollision";

            Serialization::GameObjectData target;
            target.id = ObjectID{ 1 };
            target.name = "Collision";

            Serialization::ComponentData claims_one;
            claims_one.type_name = "ValidationCollidingComponent";
            claims_one.type_id = Core::MakeComponentTypeID(claims_one.type_name);
            claims_one.stable_id = 1;   // 組み込み Transform と衝突する
            target.components.push_back(claims_one);
            colliding.objects.push_back(target);

            ReplayEngine::Scene::Scene world("CollisionWorld");
            Serialization::SceneLoadReport report;
            Serialization::ApplySceneData(colliding, world, report);

            check.Expect(report.repaired_component_ids == 1,
                "StableID の衝突が 1 件として報告される");

            Core::GameObject* object = world.FindGameObjectByID(ObjectID{ 1 });
            check.Expect(object != nullptr, "衝突があっても GameObject は復元される");

            if (object != nullptr)
            {
                Core::Component* holder = object->FindComponentByStableID(1);
                check.Expect(holder != nullptr &&
                    holder->TypeID() != MissingComponent::StaticTypeID(),
                    "StableID 1 は組み込み Transform のまま（奪われない）");

                int missing_count = 0;
                Core::ComponentStableID assigned = Core::invalid_component_stable_id;
                for (std::size_t slot = 0; slot < object->ComponentCount(); ++slot)
                {
                    Core::Component* component = object->ComponentAt(slot);
                    if (component == nullptr) continue;
                    if (component->TypeID() != MissingComponent::StaticTypeID()) continue;
                    ++missing_count;
                    assigned = component->StableID();
                }
                check.Expect(missing_count == 1, "衝突した Component も破棄されず復元される");
                check.Expect(assigned != Core::invalid_component_stable_id && assigned != 1,
                    "衝突した Component には別の StableID が振り直される");
            }
        }

        // ---- Prefab 配置での参照付け替え -----------------------------------

        {
            ReplayEngine::Scene::Scene world("PrefabRemapWorld");
            Serialization::SceneLoadReport report;

            // 2 体構成の部分木。片方がもう片方を ObjectReference と
            // ComponentReference で指している。
            Serialization::SceneData prefab;
            prefab.scene_name = "Prefab";

            Serialization::GameObjectData root;
            root.id = ObjectID{ 1 };
            root.name = "Root";
            Serialization::ComponentData pointer;
            pointer.type_name = "ValidationPointerComponent";
            pointer.type_id = Core::MakeComponentTypeID(pointer.type_name);
            // StableID 1 は使わない。
            // GameObject を作った時点で組み込みの TransformComponent が自動で付き、
            // それが 1 を取る。実際の保存データでも、保存対象の Component は
            // 必ず 2 以降になる。ここも同じ前提に合わせる。
            pointer.stable_id = 5;
            pointer.properties.Set("target", PropertyValue::MakeObjectReference(ObjectID{ 2 }));
            Reflection::ComponentReference inner;
            inner.owner = ObjectID{ 2 };
            inner.component = 6;
            pointer.properties.Set("target_component",
                PropertyValue::MakeComponentReference(inner));
            pointer.properties.Set("outside",
                PropertyValue::MakeObjectReference(ObjectID{ 9999 }));
            root.components.push_back(pointer);
            prefab.objects.push_back(root);

            Serialization::GameObjectData child;
            child.id = ObjectID{ 2 };
            child.parent_id = ObjectID{ 1 };
            child.name = "Child";
            Serialization::ComponentData marker;
            marker.type_name = "ValidationMarkerComponent";
            marker.type_id = Core::MakeComponentTypeID(marker.type_name);
            marker.stable_id = 6;
            child.components.push_back(marker);
            prefab.objects.push_back(child);

            Core::GameObject* first =
                Serialization::InstantiateSceneData(prefab, world, report, "prefab-guid");
            Core::GameObject* second =
                Serialization::InstantiateSceneData(prefab, world, report, "prefab-guid");

            check.Expect(first != nullptr && second != nullptr,
                "同じ Prefab データを 2 回配置できる");
            check.Expect(first != second && (first == nullptr || second == nullptr ||
                first->ID() != second->ID()), "2 回の配置で別々の ObjectID になる");

            const auto read_reference = [](Core::GameObject* instance_root,
                const char* property_name) -> ObjectID
            {
                if (instance_root == nullptr) return ObjectID::Invalid();
                Core::Component* component = instance_root->FindComponentByStableID(5);
                if (component == nullptr ||
                    component->TypeID() != MissingComponent::StaticTypeID())
                {
                    return ObjectID::Invalid();
                }
                const PropertyValue* value =
                    static_cast<MissingComponent*>(component)->Original()
                        .properties.Find(property_name);
                return value != nullptr ? value->AsObjectReference() : ObjectID::Invalid();
            };

            const ObjectID first_target = read_reference(first, "target");
            const ObjectID second_target = read_reference(second, "target");

            check.Expect(first_target.Valid() && second_target.Valid(),
                "Prefab 内の ObjectReference が配置先の ID へ付け替わる");
            check.Expect(first_target != second_target,
                "2 つの Instance が互いに別の Object を指す");
            check.Expect(first != nullptr && !first->Children().empty() &&
                first_target == first->Children()[0]->ID(),
                "1 つ目の Instance の参照が、その Instance の子を指している");
            check.Expect(second != nullptr && !second->Children().empty() &&
                second_target == second->Children()[0]->ID(),
                "2 つ目の Instance の参照が、その Instance の子を指している");

            check.Expect(!read_reference(first, "outside").Valid(),
                "Prefab の外を指す参照は配置時に切られる（無関係な Object を指さない）");

            // ComponentReference は所有 ObjectID だけが付け替わり、
            // ComponentStableID はそのまま残ること。
            const auto read_component_reference = [](Core::GameObject* instance_root)
            {
                Reflection::ComponentReference result;
                if (instance_root == nullptr) return result;
                Core::Component* component = instance_root->FindComponentByStableID(5);
                if (component == nullptr ||
                    component->TypeID() != MissingComponent::StaticTypeID())
                {
                    return result;
                }
                const PropertyValue* value =
                    static_cast<MissingComponent*>(component)->Original()
                        .properties.Find("target_component");
                return value != nullptr ? value->AsComponentReference() : result;
            };

            const Reflection::ComponentReference first_component =
                read_component_reference(first);
            check.Expect(first_component.component == 6,
                "ComponentReference の ComponentStableID は付け替えずに維持される");
            check.Expect(first != nullptr && !first->Children().empty() &&
                first_component.owner == first->Children()[0]->ID(),
                "ComponentReference の所有 ObjectID だけが配置先へ付け替わる");
        }

        // ---- 複製での参照付け替え ------------------------------------------

        {
            ReplayEngine::Scene::Scene world("DuplicateWorld");
            Core::GameObject* parent = world.CreateGameObject("Parent");
            Core::GameObject* child = world.CreateGameObject("Child");
            check.Expect(parent != nullptr && child != nullptr, "複製元を作れる");

            if (parent != nullptr && child != nullptr)
            {
                child->SetParent(parent, false);

                Core::Component* pointer = Core::ComponentRegistry::CreateWithStableID(
                    MissingComponent::StaticTypeID(), *parent, 3);
                check.Expect(pointer != nullptr, "複製元へ参照保持用 Component を付けられる");

                if (pointer != nullptr)
                {
                    MissingComponent::Record record;
                    record.type_name = "ValidationPointerComponent";
                    record.properties.Set("target",
                        PropertyValue::MakeObjectReference(child->ID()));
                    static_cast<MissingComponent*>(pointer)->SetOriginal(std::move(record));

                    Core::GameObject* clone =
                        Serialization::DuplicateGameObject(world, *parent, true);
                    check.Expect(clone != nullptr, "子を含めて複製できる");

                    if (clone != nullptr)
                    {
                        check.Expect(clone->Children().size() == 1, "子も複製される");
                        Core::Component* copied = clone->FindComponentByStableID(3);
                        check.Expect(copied != nullptr,
                            "複製先でも StableID が維持される");

                        if (copied != nullptr &&
                            copied->TypeID() == MissingComponent::StaticTypeID() &&
                            clone->Children().size() == 1)
                        {
                            const PropertyValue* value =
                                static_cast<MissingComponent*>(copied)->Original()
                                    .properties.Find("target");
                            check.Expect(value != nullptr &&
                                value->AsObjectReference() == clone->Children()[0]->ID(),
                                "複製先の参照が複製先の子を指す（複製元を指したままにしない）");
                        }
                        else
                        {
                            check.Expect(false, "複製先の参照内容を確認できる");
                        }
                    }
                }
            }
        }

        return check.Report("Serialization validation", 140);
    }
}
