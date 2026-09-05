#include "SceneSerializer.h"
#include "../../Rendering/RenderStats.h"

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
                    error = "予約済みの衝突移行履歴を読み取れません。";
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

                // v11 で追加した行。v10 以前には存在しないので読まない。
                //
                // v10 以前を読んだ場合:
                //   stable_id = 0     -> 読み込み側が採番し直す
                //   type_guid = 無効  -> 型名で解決する（従来どおり）
                //   type_version = 0  -> 未記録
                // どれも「情報が無い」だけで、読み込みは通る。
                if (version >= 11)
                {
                    if (!Expect(stream, "STABLE_ID", error)) return false;
                    std::uint32_t raw_stable_id = 0;
                    if (!(stream >> raw_stable_id))
                    {
                        error = "Component の StableID を読み取れません。";
                        return false;
                    }
                    component.stable_id = raw_stable_id;

                    if (!Expect(stream, "TYPE_GUID", error)) return false;
                    std::string raw_type_guid;
                    if (!(stream >> std::quoted(raw_type_guid)))
                    {
                        error = "Component の Type GUID を読み取れません。";
                        return false;
                    }
                    // 解析できない文字列は無効 GUID として扱い、型名での解決へ落とす。
                    // 1 個の壊れた GUID で Scene 全体を読めなくしない。
                    Reflection::TypeGUID parsed;
                    if (Reflection::TypeGUID::TryParse(raw_type_guid, parsed))
                    {
                        component.type_guid = parsed;
                    }

                    if (!Expect(stream, "TYPE_MODULE", error)) return false;
                    if (!(stream >> std::quoted(component.module_id)))
                    {
                        error = "Component のモジュール名を読み取れません。";
                        return false;
                    }

                    if (!Expect(stream, "TYPE_VERSION", error)) return false;
                    if (!(stream >> component.type_version))
                    {
                        error = "Component の型バージョンを読み取れません。";
                        return false;
                    }
                }

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

    bool SceneSerializer::LoadFromFile(SceneData& data,
        const std::filesystem::path& path, std::string& error)
    {
        REPLAY_PROFILE_SCOPE("Asset/Scene");
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
