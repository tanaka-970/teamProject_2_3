#include "SceneSerializer.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <istream>
#include <limits>
#include <locale>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>
#include "SceneSerializerInternal.h"

namespace ReplayEngine::Scene::Serialization
{
    using Reflection::PropertyBag;
    using Reflection::PropertyType;
    using Reflection::PropertyValue;
    using namespace Detail;

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

                // v11 で追加した行。
                //
                // COMPONENT 行そのものは v10 と同じ形のまま残してある。
                // 追加情報を別行にしたのは、読み手の分岐を「行があるかどうか」の
                // 1 段だけにして、v10 以前の読み取り経路へ手を入れずに済ませるため。
                //
                // TYPE_GUID が空 (32 個の 0) の型は、これまで通り型名が主キーになる。
                // Engine 組み込み Component へ一斉に GUID を振る必要はない。
                stream << "    STABLE_ID " << component.stable_id << '\n';
                stream << "    TYPE_GUID " << std::quoted(component.type_guid.ToString()) << '\n';
                stream << "    TYPE_MODULE " << std::quoted(component.module_id) << '\n';
                stream << "    TYPE_VERSION " << component.type_version << '\n';

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
}
