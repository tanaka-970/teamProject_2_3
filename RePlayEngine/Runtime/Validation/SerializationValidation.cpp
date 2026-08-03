#include "SerializationValidation.h"

#include "../../Object/Component/MissingComponent.h"
#include "../../Object/Registry/BuiltInComponents.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Serialization/SceneData.h"
#include "../../Scene/Serialization/SceneSerializer.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ReplayEngine::Runtime::Validation
{
    namespace
    {
        using Core::MissingComponent;
        using Core::ObjectID;
        using Reflection::PropertyBag;
        using Reflection::PropertyType;
        using Reflection::PropertyValue;
        using Reflection::TypeGUID;
        namespace Serialization = Scene::Serialization;

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

            int Result() const noexcept { return first_failure_; }
            int Total() const noexcept { return total_; }
            int Failures() const noexcept { return failures_; }

            int Report(const char* title, int fallback_code) const
            {
                if (first_failure_ == 0)
                {
                    std::fprintf(stderr, "%s OK: %d checks passed\n", title, total_);
                    return 0;
                }
                std::fprintf(stderr, "%s FAILED: %d/%d checks failed (first=%d)\n",
                    title, failures_, total_, first_failure_);
                return first_failure_ != 0 ? first_failure_ : fallback_code;
            }

        private:
            int next_code_ = 0;
            int first_failure_ = 0;
            int total_ = 0;
            int failures_ = 0;
        };

        // SceneData を文字列へ書き、書いた文字列から読み戻す。
        // ファイルを触らないので、既存の Scene 原本を一切変更しない。
        bool RoundTrip(const Serialization::SceneData& source,
            Serialization::SceneData& restored, std::string& text, std::string& error)
        {
            std::ostringstream out;
            if (!Serialization::SceneSerializer::WriteText(source, out, error)) return false;
            text = out.str();

            std::istringstream in(text);
            return Serialization::SceneSerializer::ReadText(restored, in, error);
        }

        // 検証用に「あらゆる型のプロパティを 1 つずつ持つ」Component データを作る。
        // 型名はわざと登録されていないものにして、Missing 経路もまとめて通す。
        Serialization::ComponentData MakeAllTypesComponent()
        {
            Serialization::ComponentData component;
            component.type_name = "ValidationAllTypesComponent";
            component.type_id = Core::MakeComponentTypeID(component.type_name);
            component.type_guid =
                Reflection::MakeTypeGUID("11223344556677889900aabbccddeeff");
            component.module_id = "RePlayEngine.Validation";
            component.type_version = 3;
            component.stable_id = 7;
            component.enabled = false;

            PropertyBag& bag = component.properties;
            bag.Set("p_bool", PropertyValue::MakeBool(true));
            bag.Set("p_int", PropertyValue::MakeInt(-12345));
            bag.Set("p_int64", PropertyValue::MakeInt64(-9007199254740993LL));
            bag.Set("p_uint64", PropertyValue::MakeUInt64(18446744073709551615ULL));
            bag.Set("p_float", PropertyValue::MakeFloat(1.2345678f));
            bag.Set("p_double", PropertyValue::MakeDouble(3.141592653589793));
            bag.Set("p_string", PropertyValue::MakeString("日本語 と \"引用符\" を含む"));
            bag.Set("p_vec2", PropertyValue::MakeVector2(DirectX::XMFLOAT2{ 1.0f, 2.0f }));
            bag.Set("p_vec3", PropertyValue::MakeVector3(DirectX::XMFLOAT3{ 1.0f, 2.0f, 3.0f }));
            bag.Set("p_vec4", PropertyValue::MakeVector4(
                DirectX::XMFLOAT4{ 1.0f, 2.0f, 3.0f, 4.0f }));
            bag.Set("p_quat", PropertyValue::MakeQuaternion(
                DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 1.0f }));
            bag.Set("p_color", PropertyValue::MakeColor(
                DirectX::XMFLOAT4{ 0.25f, 0.5f, 0.75f, 1.0f }));
            bag.Set("p_enum", PropertyValue::MakeEnum(2));
            bag.Set("p_assetpath", PropertyValue::MakeAssetPath("resources/old/path.png"));
            bag.Set("p_layer", PropertyValue::MakeCollisionLayer(3));
            bag.Set("p_mask", PropertyValue::MakeCollisionMask(0x2A));
            bag.Set("p_colliderref", PropertyValue::MakeColliderReference(5));
            bag.Set("p_objref", PropertyValue::MakeObjectReference(ObjectID{ 42 }));
            bag.Set("p_assetref", PropertyValue::MakeAssetReference(
                "851a285f21a43904dfcc9ca70496e5d5"));
            bag.Set("p_sceneref", PropertyValue::MakeSceneReference(
                "8cd38b48d62df27150182f00e2b89a78"));

            Reflection::ComponentReference component_reference;
            component_reference.owner = ObjectID{ 42 };
            component_reference.component = 9;
            bag.Set("p_compref", PropertyValue::MakeComponentReference(component_reference));

            std::vector<PropertyValue> numbers;
            numbers.push_back(PropertyValue::MakeFloat(1.5f));
            numbers.push_back(PropertyValue::MakeFloat(-2.5f));
            numbers.push_back(PropertyValue::MakeFloat(0.0f));
            bag.Set("p_array_float", PropertyValue::MakeArray(PropertyType::Float,
                std::move(numbers)));

            std::vector<PropertyValue> references;
            references.push_back(PropertyValue::MakeObjectReference(ObjectID{ 42 }));
            references.push_back(PropertyValue::MakeObjectReference(ObjectID{ 43 }));
            bag.Set("p_array_objref", PropertyValue::MakeArray(PropertyType::ObjectReference,
                std::move(references)));

            std::vector<PropertyValue> names;
            names.push_back(PropertyValue::MakeString("空白 を含む 文字列"));
            names.push_back(PropertyValue::MakeString(""));
            bag.Set("p_array_string", PropertyValue::MakeArray(PropertyType::String,
                std::move(names)));

            return component;
        }

        // 2 つの PropertyBag が、名前も型も値も一致するか。
        bool BagsEqual(const PropertyBag& a, const PropertyBag& b)
        {
            if (a.Size() != b.Size()) return false;
            for (const PropertyBag::Entry& entry : a.Entries())
            {
                const PropertyValue* other = b.Find(entry.name);
                if (other == nullptr) return false;
                if (other->Type() != entry.value.Type()) return false;
                if (!Reflection::ValuesEqual(entry.value, *other)) return false;
            }
            return true;
        }
    }

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

    // =====================================================================
    // Scene Version
    // =====================================================================

    namespace
    {
        // 旧バージョンの Scene テキストを組み立てる。
        //
        // 実ファイルを用意せずコードで生成するのは、
        // 検証用のためだけに Scene 原本を増やさないため。
        // 書式は SceneSerializer の各バージョンの書き出しと同じ形にしてある。
        std::string MakeLegacyScene(int version)
        {
            std::ostringstream out;
            out.imbue(std::locale::classic());

            out << "REPLAY_SCENE " << version << '\n';
            out << "SCENE \"レガシー Scene\"\n";

            if (version >= 8)
            {
                // v8 / v9 は旧 Player の移行状態が後ろに並んでいた。
                // 行単位で読み飛ばせることも同時に確かめる。
                out << "SCENE_STATE 2";
                if (version <= 9) out << " 1 0";
                out << '\n';
            }
            if (version >= 9) out << "COLLISION_STATE 2 0\n";

            out << "OBJECT_COUNT 2\n";

            for (int index = 0; index < 2; ++index)
            {
                const int id = index + 1;
                out << "OBJECT\n";
                out << "  ID " << id << '\n';
                out << "  NAME \"レガシー" << id << "\"\n";
                out << "  ENABLED 1\n";
                out << "  PARENT " << (index == 0 ? 0 : 1) << '\n';
                out << "  TRANSFORM " << index << " 0 0 0 0 0 1 1 1\n";
                if (version >= 10) out << "  PREFAB \"\" 0 0\n";
                out << "  COMPONENT_COUNT 1\n";
                out << "  COMPONENT \"RotatorComponent\" 1\n";
                if (version >= 11)
                {
                    out << "    STABLE_ID 1\n";
                    out << "    TYPE_GUID \"00000000000000000000000000000000\"\n";
                    out << "    TYPE_MODULE \"\"\n";
                    out << "    TYPE_VERSION 1\n";
                }
                out << "    PROPERTY_COUNT 1\n";
                out << "    PROPERTY \"degrees_per_second\" float " << (30 * id) << '\n';
                out << "  END_COMPONENT\n";
                out << "END_OBJECT\n";
            }
            return out.str();
        }
    }

    int RunSceneVersionValidation()
    {
        Core::RegisterBuiltInComponents();
        Checker check(210);

        check.Expect(Serialization::SceneData::current_version == 11,
            "現行の Scene バージョンは 11");
        check.Expect(Serialization::SceneData::minimum_supported_version == 7,
            "最低対応バージョンは 7 のまま");

        // ---- v7 〜 v11 を読める --------------------------------------------

        for (int version = 7; version <= 11; ++version)
        {
            const std::string legacy = MakeLegacyScene(version);
            std::istringstream in(legacy);
            Serialization::SceneData data;
            std::string error;

            const bool ok = Serialization::SceneSerializer::ReadText(data, in, error);
            const std::string label = "v" + std::to_string(version);

            check.Expect(ok, ("旧形式 " + label + " を読み込める").c_str());
            if (!ok)
            {
                std::fprintf(stderr, "  %s read error: %s\n", label.c_str(), error.c_str());
                continue;
            }

            check.Expect(data.version == version,
                ("読み取ったバージョンが " + label + " と一致する").c_str());
            check.Expect(data.objects.size() == 2,
                (label + " の GameObject を 2 体読める").c_str());
            check.Expect(data.scene_name == "レガシー Scene",
                (label + " の日本語 Scene 名を読める").c_str());

            // v8 以降は操作対象が読める。v7 は無効のまま。
            const bool controlled_expected = version >= 8;
            check.Expect(data.controlled_object.Valid() == controlled_expected,
                (label + " の操作対象 ObjectID の有無が正しい").c_str());

            if (data.objects.size() == 2 && !data.objects[0].components.empty())
            {
                const Serialization::ComponentData& component = data.objects[0].components[0];
                check.Expect(component.type_name == "RotatorComponent",
                    (label + " の Component 型名を読める").c_str());
                check.Expect(component.properties.Contains("degrees_per_second"),
                    (label + " の Component プロパティを読める").c_str());

                // v11 未満は StableID が未記録 (0) であること。
                const bool has_stable_id = version >= 11;
                check.Expect((component.stable_id != 0) == has_stable_id,
                    (label + " の StableID の有無が正しい").c_str());
            }
        }

        // ---- 旧形式 -> 現行形式への移行と往復 --------------------------------

        for (int version = 7; version <= 10; ++version)
        {
            const std::string label = "v" + std::to_string(version);
            const std::string legacy = MakeLegacyScene(version);

            std::istringstream in(legacy);
            Serialization::SceneData loaded;
            std::string error;
            if (!Serialization::SceneSerializer::ReadText(loaded, in, error)) continue;

            // 実 Scene へ流し込んでから保存し直す。
            // Editor で「開いて保存した」ときと同じ経路を通す。
            ReplayEngine::Scene::Scene world("MigrationWorld");
            Serialization::SceneLoadReport report;
            check.Expect(Serialization::ApplySceneData(loaded, world, report),
                (label + " を実 Scene へ流し込める").c_str());
            check.Expect(report.missing_components == 0,
                (label + " の移行で Missing Component が発生しない").c_str());

            Serialization::SceneData migrated;
            Serialization::CaptureScene(world, migrated);

            Serialization::SceneData restored;
            std::string text;
            const bool ok = RoundTrip(migrated, restored, text, error);
            check.Expect(ok, (label + " を現行形式で保存して読み戻せる").c_str());
            if (!ok) continue;

            check.Expect(restored.version == Serialization::SceneData::current_version,
                (label + " から保存すると現行バージョンになる").c_str());
            check.Expect(restored.objects.size() == 2,
                (label + " の GameObject 数が移行後も保たれる").c_str());

            // 移行が冪等であること。もう一度通しても内容が変わらない。
            ReplayEngine::Scene::Scene second_world("MigrationWorld2");
            Serialization::SceneLoadReport second_report;
            Serialization::ApplySceneData(restored, second_world, second_report);
            Serialization::SceneData second_migrated;
            Serialization::CaptureScene(second_world, second_migrated);

            Serialization::SceneData ignored;
            std::string second_text;
            check.Expect(RoundTrip(second_migrated, ignored, second_text, error) &&
                second_text == text,
                (label + " の移行が冪等（2 回通しても同じ結果）").c_str());
        }

        // ---- 新しすぎるバージョンは拒否する ---------------------------------

        {
            const std::string too_new = MakeLegacyScene(Serialization::SceneData::current_version + 1);
            std::istringstream in(too_new);
            Serialization::SceneData data;
            std::string error;

            const bool ok = Serialization::SceneSerializer::ReadText(data, in, error);
            check.Expect(!ok, "現行より新しいバージョンは読み込みを拒否する");
            check.Expect(!error.empty(), "拒否時に理由のメッセージが入る");
            check.Expect(data.objects.empty(),
                "拒否時に途中まで読んだ内容を返さない（半端なデータで上書きしない）");
        }

        // ---- 対応範囲外の旧形式 --------------------------------------------

        {
            const std::string ancient = MakeLegacyScene(6);
            std::istringstream in(ancient);
            Serialization::SceneData data;
            std::string error;
            check.Expect(!Serialization::SceneSerializer::ReadText(data, in, error),
                "v6 以前は読み込みを拒否する");
        }

        // ---- Scene ファイルでないものを渡された場合 --------------------------

        {
            std::istringstream in("これは Scene ファイルではありません");
            Serialization::SceneData data;
            std::string error;
            check.Expect(!Serialization::SceneSerializer::ReadText(data, in, error),
                "Scene ファイルでない入力を拒否する（クラッシュしない）");
        }

        return check.Report("Scene version validation", 210);
    }
}
