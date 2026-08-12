#include "SerializationValidationInternal.h"

namespace ReplayEngine::Runtime::Validation::Detail::SerializationValidation
{
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
