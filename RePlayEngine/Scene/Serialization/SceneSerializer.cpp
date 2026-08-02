#include "SceneSerializer.h"

#include <fstream>
#include <iomanip>
#include <istream>
#include <limits>
#include <locale>
#include <ostream>
#include <sstream>
#include <string>

namespace ReplayEngine::Scene::Serialization
{
    using Reflection::PropertyBag;
    using Reflection::PropertyType;
    using Reflection::PropertyValue;

    namespace
    {
        constexpr const char* magic_token = "REPLAY_SCENE";

        // 壊れたファイルで巨大な確保が起きないようにする上限。
        constexpr std::size_t maximum_objects = 200000;
        constexpr std::size_t maximum_components_per_object = 512;
        constexpr std::size_t maximum_properties_per_component = 512;

        bool Expect(std::istream& stream, const char* expected, std::string& error)
        {
            std::string token;
            if (!(stream >> token) || token != expected)
            {
                error = std::string("Scene ファイルの形式が不正です。'") + expected +
                    "' が見つかりません。";
                return false;
            }
            return true;
        }

        void WriteFloat3(std::ostream& stream, const DirectX::XMFLOAT3& value)
        {
            stream << value.x << ' ' << value.y << ' ' << value.z;
        }

        bool ReadFloat3(std::istream& stream, DirectX::XMFLOAT3& value)
        {
            return static_cast<bool>(stream >> value.x >> value.y >> value.z);
        }

        bool WriteProperty(std::ostream& stream, const std::string& name,
            const PropertyValue& value)
        {
            stream << "    PROPERTY " << std::quoted(name) << ' '
                << Reflection::ToString(value.Type()) << ' ';

            switch (value.Type())
            {
            case PropertyType::Bool:
                stream << (value.AsBool() ? 1 : 0);
                break;
            case PropertyType::Int:
            case PropertyType::Enum:
            case PropertyType::CollisionLayer:
            case PropertyType::CollisionMask:
            case PropertyType::ColliderReference:
                // どれも内部表現は int。書式は同じで、型名だけが違う。
                stream << value.AsInt();
                break;
            case PropertyType::Float:
                stream << value.AsFloat();
                break;
            case PropertyType::Double:
                stream << value.AsDouble();
                break;
            case PropertyType::String:
            case PropertyType::AssetPath:
                stream << std::quoted(value.AsString());
                break;
            case PropertyType::Vector2:
            {
                const DirectX::XMFLOAT2 vector = value.AsVector2();
                stream << vector.x << ' ' << vector.y;
                break;
            }
            case PropertyType::Vector3:
                WriteFloat3(stream, value.AsVector3());
                break;
            case PropertyType::Vector4:
            case PropertyType::Quaternion:
            case PropertyType::Color:
            {
                const DirectX::XMFLOAT4 vector = value.AsVector4();
                stream << vector.x << ' ' << vector.y << ' ' << vector.z << ' ' << vector.w;
                break;
            }
            case PropertyType::ObjectReference:
                stream << value.AsObjectReference().Value();
                break;
            }
            stream << '\n';
            return static_cast<bool>(stream);
        }

        bool ReadProperty(std::istream& stream, PropertyBag& bag, std::string& error)
        {
            std::string name;
            std::string type_text;
            if (!(stream >> std::quoted(name) >> type_text))
            {
                error = "プロパティ名または型を読み取れません。";
                return false;
            }

            PropertyType type = PropertyType::Bool;
            if (!Reflection::TryParsePropertyType(type_text, type))
            {
                // 将来追加された型を古いビルドで読んだ場合。
                // 値の並びが分からないので、ここで打ち切る。
                error = "未知のプロパティ型です: " + type_text;
                return false;
            }

            switch (type)
            {
            case PropertyType::Bool:
            {
                int raw = 0;
                if (!(stream >> raw)) { error = "bool を読み取れません。"; return false; }
                bag.Set(name, PropertyValue::MakeBool(raw != 0));
                return true;
            }
            case PropertyType::Int:
            case PropertyType::Enum:
            case PropertyType::CollisionLayer:
            case PropertyType::CollisionMask:
            case PropertyType::ColliderReference:
            {
                int raw = 0;
                if (!(stream >> raw)) { error = "int を読み取れません。"; return false; }
                switch (type)
                {
                case PropertyType::Enum:
                    bag.Set(name, PropertyValue::MakeEnum(raw)); break;
                case PropertyType::CollisionLayer:
                    bag.Set(name, PropertyValue::MakeCollisionLayer(raw)); break;
                case PropertyType::CollisionMask:
                    bag.Set(name, PropertyValue::MakeCollisionMask(raw)); break;
                case PropertyType::ColliderReference:
                    bag.Set(name, PropertyValue::MakeColliderReference(raw)); break;
                default:
                    bag.Set(name, PropertyValue::MakeInt(raw)); break;
                }
                return true;
            }
            case PropertyType::Float:
            {
                float raw = 0.0f;
                if (!(stream >> raw)) { error = "float を読み取れません。"; return false; }
                bag.Set(name, PropertyValue::MakeFloat(raw));
                return true;
            }
            case PropertyType::Double:
            {
                double raw = 0.0;
                if (!(stream >> raw)) { error = "double を読み取れません。"; return false; }
                bag.Set(name, PropertyValue::MakeDouble(raw));
                return true;
            }
            case PropertyType::String:
            case PropertyType::AssetPath:
            {
                std::string raw;
                if (!(stream >> std::quoted(raw)))
                {
                    error = "文字列を読み取れません。";
                    return false;
                }
                bag.Set(name, type == PropertyType::AssetPath
                    ? PropertyValue::MakeAssetPath(std::move(raw))
                    : PropertyValue::MakeString(std::move(raw)));
                return true;
            }
            case PropertyType::Vector2:
            {
                DirectX::XMFLOAT2 raw{ 0.0f, 0.0f };
                if (!(stream >> raw.x >> raw.y)) { error = "vec2 を読み取れません。"; return false; }
                bag.Set(name, PropertyValue::MakeVector2(raw));
                return true;
            }
            case PropertyType::Vector3:
            {
                DirectX::XMFLOAT3 raw{ 0.0f, 0.0f, 0.0f };
                if (!ReadFloat3(stream, raw)) { error = "vec3 を読み取れません。"; return false; }
                bag.Set(name, PropertyValue::MakeVector3(raw));
                return true;
            }
            case PropertyType::Vector4:
            case PropertyType::Quaternion:
            case PropertyType::Color:
            {
                DirectX::XMFLOAT4 raw{ 0.0f, 0.0f, 0.0f, 1.0f };
                if (!(stream >> raw.x >> raw.y >> raw.z >> raw.w))
                {
                    error = "vec4 を読み取れません。";
                    return false;
                }
                if (type == PropertyType::Quaternion) bag.Set(name, PropertyValue::MakeQuaternion(raw));
                else if (type == PropertyType::Color)  bag.Set(name, PropertyValue::MakeColor(raw));
                else                                   bag.Set(name, PropertyValue::MakeVector4(raw));
                return true;
            }
            case PropertyType::ObjectReference:
            {
                Core::ObjectID::ValueType raw = 0;
                if (!(stream >> raw)) { error = "オブジェクト参照を読み取れません。"; return false; }
                bag.Set(name, PropertyValue::MakeObjectReference(Core::ObjectID(raw)));
                return true;
            }
            }

            error = "プロパティ型の処理に失敗しました。";
            return false;
        }
    }

    std::string SceneSerializer::UnsupportedVersionMessage(int version)
    {
        if (version > 0 && version < SceneData::current_version)
        {
            return "この Scene ファイルは旧形式 (v" + std::to_string(version) +
                ") です。GameObject / Component 基盤の刷新にともない非対応となりました。"
                "v" + std::to_string(SceneData::current_version) +
                " 形式で Scene を作り直してください。";
        }
        if (version > SceneData::current_version)
        {
            return "この Scene ファイルは新しい形式 (v" + std::to_string(version) +
                ") です。このビルドは v" + std::to_string(SceneData::current_version) +
                " までしか読み込めません。";
        }
        return "Scene ファイルのバージョンを判別できません。";
    }

    bool SceneSerializer::WriteText(const SceneData& data, std::ostream& stream,
        std::string& error)
    {
        stream.imbue(std::locale::classic());

        // float / double を往復させても値が変わらない桁数で書く。
        stream << std::setprecision(std::numeric_limits<double>::max_digits10);

        stream << magic_token << ' ' << SceneData::current_version << '\n';
        stream << "SCENE " << std::quoted(data.scene_name) << '\n';

        // v8 で追加。Scene 単位の状態。
        //
        // 操作対象を Component の有無ではなくここで決める。
        // 「操作対象」は ObjectID だけで表され、GameObject 名にも Prefab 名にも
        // 配列の並び順にも依存しない。
        //
        // 以前はこの行に旧 Player の移行状態が 2 項目並んでいたが、
        // 旧 Player 経路の撤去にともない書き出さなくなった。
        // 読み取り側は行単位で解析するので、古いファイルもそのまま読める。
        stream << "SCENE_STATE " << data.controlled_object.Value() << '\n';

        // v9/v10との構文互換用の予約行。BackendはScene Colliderへ統一済み。
        stream << "COLLISION_STATE 2 0\n";

        stream << "OBJECT_COUNT " << data.objects.size() << '\n';

        for (const GameObjectData& object : data.objects)
        {
            stream << "OBJECT\n";
            stream << "  ID " << object.id.Value() << '\n';
            stream << "  NAME " << std::quoted(object.name) << '\n';
            stream << "  ENABLED " << (object.enabled ? 1 : 0) << '\n';
            stream << "  PARENT " << object.parent_id.Value() << '\n';
            stream << "  TRANSFORM ";
            WriteFloat3(stream, object.position); stream << ' ';
            WriteFloat3(stream, object.rotation); stream << ' ';
            WriteFloat3(stream, object.scale);
            stream << '\n';

            // v10. Empty GUID is an ordinary GameObject. The raw identifiers live
            // only in the file/Advanced UI; normal authoring uses asset labels.
            stream << "  PREFAB " << std::quoted(object.prefab_source_guid) << ' '
                << object.prefab_local_id << ' ' << object.prefab_instance_root.Value() << '\n';

            stream << "  COMPONENT_COUNT " << object.components.size() << '\n';
            for (const ComponentData& component : object.components)
            {
                stream << "  COMPONENT " << std::quoted(component.type_name) << ' '
                    << (component.enabled ? 1 : 0) << '\n';
                stream << "    PROPERTY_COUNT " << component.properties.Size() << '\n';
                for (const PropertyBag::Entry& entry : component.properties.Entries())
                {
                    if (!WriteProperty(stream, entry.name, entry.value))
                    {
                        error = "プロパティの書き込みに失敗しました: " + entry.name;
                        return false;
                    }
                }
                stream << "  END_COMPONENT\n";
            }
            stream << "END_OBJECT\n";
        }

        if (!stream)
        {
            error = "Scene データの書き込みに失敗しました。";
            return false;
        }
        return true;
    }

    bool SceneSerializer::ReadText(SceneData& data, std::istream& stream, std::string& error)
    {
        stream.imbue(std::locale::classic());
        data.Clear();

        std::string magic;
        int version = 0;
        if (!(stream >> magic >> version) || magic != magic_token)
        {
            error = "Scene ファイルではありません。";
            return false;
        }

        // バージョン判定は必ずここを通す。将来 v8 を足す場合もこの分岐を拡張する。
        if (!IsSupportedVersion(version))
        {
            error = UnsupportedVersionMessage(version);
            return false;
        }
        data.version = version;

        if (!Expect(stream, "SCENE", error)) return false;
        if (!(stream >> std::quoted(data.scene_name)))
        {
            error = "Scene 名を読み取れません。";
            return false;
        }

        // v8 で追加した Scene 単位の状態。v7 には存在しないので読み飛ばす。
        //
        // 【行単位で読む理由】
        //   v8 / v9 の書き出しでは、この行に旧 Player の移行状態が 2 項目
        //   並んでいた。旧 Player 経路を撤去した今はもう書き出さないが、
        //   既に保存済みのファイルを読めなくするわけにはいかない。
        //   行の残りをまとめて読み捨てることで、項目が何個並んでいても
        //   「先頭が操作対象 ObjectID」という約束だけで読み進められる。
        if (version >= 8)
        {
            if (!Expect(stream, "SCENE_STATE", error)) return false;

            std::string state_line;
            if (!std::getline(stream, state_line))
            {
                error = "Scene 状態を読み取れません。";
                return false;
            }

            std::istringstream state_stream(state_line);
            state_stream.imbue(std::locale::classic());
            Core::ObjectID::ValueType controlled = 0;
            if (!(state_stream >> controlled))
            {
                error = "操作対象 ObjectID を読み取れません。";
                return false;
            }
            data.controlled_object = Core::ObjectID(controlled);
            // 行の残り（旧 Player の移行状態）は読み捨てる。
        }

        // v9/v10 の予約行。旧Backend値は構文互換のため読み飛ばす。
        if (version >= 9)
        {
            if (!Expect(stream, "COLLISION_STATE", error)) return false;
            int ignored_backend = 2;
            std::size_t source_count = 0;
            if (!(stream >> ignored_backend >> source_count) ||
                source_count > maximum_objects)
            {
                error = "衝突の設定を読み取れません。";
                return false;
            }
            for (std::size_t index = 0; index < source_count; ++index)
            {
                std::uint64_t source = 0;
                if (!(stream >> source))
                {
                    error = "移行済み Legacy Stage の一覧を読み取れません。";
                    return false;
                }
            }
        }

        if (!Expect(stream, "OBJECT_COUNT", error)) return false;
        std::size_t object_count = 0;
        if (!(stream >> object_count) || object_count > maximum_objects)
        {
            error = "GameObject の個数が不正です。";
            return false;
        }
        data.objects.reserve(object_count);

        for (std::size_t index = 0; index < object_count; ++index)
        {
            if (!Expect(stream, "OBJECT", error)) return false;

            GameObjectData object;

            if (!Expect(stream, "ID", error)) return false;
            Core::ObjectID::ValueType raw_id = 0;
            if (!(stream >> raw_id)) { error = "ObjectID を読み取れません。"; return false; }
            object.id = Core::ObjectID(raw_id);

            if (!Expect(stream, "NAME", error)) return false;
            if (!(stream >> std::quoted(object.name)))
            {
                error = "GameObject 名を読み取れません。";
                return false;
            }

            if (!Expect(stream, "ENABLED", error)) return false;
            int enabled_raw = 1;
            if (!(stream >> enabled_raw)) { error = "有効状態を読み取れません。"; return false; }
            object.enabled = enabled_raw != 0;

            if (!Expect(stream, "PARENT", error)) return false;
            Core::ObjectID::ValueType raw_parent = 0;
            if (!(stream >> raw_parent)) { error = "親 ID を読み取れません。"; return false; }
            object.parent_id = Core::ObjectID(raw_parent);

            if (!Expect(stream, "TRANSFORM", error)) return false;
            if (!ReadFloat3(stream, object.position) ||
                !ReadFloat3(stream, object.rotation) ||
                !ReadFloat3(stream, object.scale))
            {
                error = "Transform を読み取れません。";
                return false;
            }

            if (version >= 10)
            {
                if (!Expect(stream, "PREFAB", error)) return false;
                Core::ObjectID::ValueType raw_instance_root = 0;
                if (!(stream >> std::quoted(object.prefab_source_guid) >>
                    object.prefab_local_id >> raw_instance_root))
                {
                    error = "Prefab instance情報を読み取れません。";
                    return false;
                }
                object.prefab_instance_root = Core::ObjectID(raw_instance_root);
            }

            if (!Expect(stream, "COMPONENT_COUNT", error)) return false;
            std::size_t component_count = 0;
            if (!(stream >> component_count) || component_count > maximum_components_per_object)
            {
                error = "Component の個数が不正です。";
                return false;
            }
            object.components.reserve(component_count);

            for (std::size_t slot = 0; slot < component_count; ++slot)
            {
                if (!Expect(stream, "COMPONENT", error)) return false;

                ComponentData component;
                int component_enabled = 1;
                if (!(stream >> std::quoted(component.type_name) >> component_enabled))
                {
                    error = "Component の型名または有効状態を読み取れません。";
                    return false;
                }
                component.enabled = component_enabled != 0;
                component.type_id = Core::MakeComponentTypeID(component.type_name);

                if (!Expect(stream, "PROPERTY_COUNT", error)) return false;
                std::size_t property_count = 0;
                if (!(stream >> property_count) ||
                    property_count > maximum_properties_per_component)
                {
                    error = "プロパティの個数が不正です。";
                    return false;
                }

                for (std::size_t p = 0; p < property_count; ++p)
                {
                    if (!Expect(stream, "PROPERTY", error)) return false;
                    if (!ReadProperty(stream, component.properties, error)) return false;
                }

                if (!Expect(stream, "END_COMPONENT", error)) return false;
                object.components.push_back(std::move(component));
            }

            if (!Expect(stream, "END_OBJECT", error)) return false;
            data.objects.push_back(std::move(object));
        }

        return true;
    }

    bool SceneSerializer::SaveToFile(const SceneData& data,
        const std::filesystem::path& path, std::string& error)
    {
        std::error_code filesystem_error;
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path(), filesystem_error);
            if (filesystem_error)
            {
                error = "Scene の保存先フォルダーを作成できません。";
                return false;
            }
        }

        // まずメモリ上へ書き切る。
        // 途中で失敗しても既存のファイルを壊さないようにするため。
        std::ostringstream buffer;
        if (!WriteText(data, buffer, error)) return false;

        // 書き出す内容を同じ Reader でもう一度検証する。
        // Serializer の変更で読み戻せない形式を既存 Scene と差し替えない。
        SceneData validation;
        std::istringstream validation_stream(buffer.str());
        if (!ReadText(validation, validation_stream, error))
        {
            error = "保存前Validationに失敗しました: " + error;
            return false;
        }

        const std::filesystem::path temporary = path.string() + ".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                error = "Scene ファイルを作成できません。";
                return false;
            }
            const std::string text = buffer.str();
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            stream.flush();
            if (!stream)
            {
                error = "Scene ファイルへの書き込みに失敗しました。";
                return false;
            }
        }

        // Windows では既存Pathへの rename が失敗するため、先に既存Sceneを
        // .bak へ退避してから差し替える。途中で失敗した場合は元へ戻す。
        const std::filesystem::path backup = path.string() + ".bak";
        const bool had_existing = std::filesystem::exists(path, filesystem_error) && !filesystem_error;
        if (filesystem_error)
        {
            error = "既存Sceneの状態を確認できません。";
            return false;
        }

        if (had_existing)
        {
            std::error_code ignored;
            std::filesystem::remove(backup, ignored);
            std::filesystem::rename(path, backup, filesystem_error);
            if (filesystem_error)
            {
                error = "既存SceneをBackupへ退避できません。";
                return false;
            }
        }

        filesystem_error.clear();
        std::filesystem::rename(temporary, path, filesystem_error);
        if (filesystem_error)
        {
            if (had_existing)
            {
                std::error_code restore_error;
                std::filesystem::rename(backup, path, restore_error);
                if (restore_error)
                    error = "Scene差し替えとBackup復元に失敗しました。Backup: " + backup.string();
                else
                    error = "Sceneを差し替えられなかったため、元のSceneを復元しました。";
            }
            else
            {
                error = "Sceneファイルを配置できません。Temporary: " + temporary.string();
            }
            return false;
        }
        return true;
    }

    bool SceneSerializer::LoadFromFile(SceneData& data,
        const std::filesystem::path& path, std::string& error)
    {
        std::error_code filesystem_error;
        if (!std::filesystem::exists(path, filesystem_error) || filesystem_error)
        {
            error = "Scene ファイルが見つかりません: " + path.string();
            return false;
        }

        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            error = "Scene ファイルを開けません: " + path.string();
            return false;
        }
        return ReadText(data, stream, error);
    }

    int SceneSerializer::PeekVersion(const std::filesystem::path& path, std::string& error)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            error = "Scene ファイルを開けません: " + path.string();
            return 0;
        }
        stream.imbue(std::locale::classic());

        std::string magic;
        int version = 0;
        if (!(stream >> magic >> version) || magic != magic_token)
        {
            error = "Scene ファイルではありません。";
            return 0;
        }
        return version;
    }
}
