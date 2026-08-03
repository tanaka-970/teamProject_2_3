#include "ProjectSettings.h"

#include "../Assets/AssetDatabase.h"

namespace ReplayEngine::Project
{
    std::string PrefabReferenceStatus::DisplayLabel() const
    {
        switch (state)
        {
        case State::Resolved:
            return display_name.empty() ? path.filename().generic_string() : display_name;
        case State::Missing:
            return "[ Missing Prefab ]";
        case State::Unset:
        default:
            return "（未設定）";
        }
    }

    PrefabReferenceStatus ProjectSettings::ResolveDefaultCharacterPrefab(
        const Assets::AssetDatabase& database) const
    {
        PrefabReferenceStatus status;
        status.guid = default_character_prefab_guid_;

        if (default_character_prefab_guid_.empty())
        {
            status.state = PrefabReferenceStatus::State::Unset;
            return status;
        }

        const Assets::AssetRecord* record =
            database.FindByGuid(default_character_prefab_guid_);
        if (record == nullptr)
        {
            // 登録が消えている。GUID は保持したままにする。
            // 同じ Asset を取り込み直せば同じ GUID で復活するため、
            // ここで設定を消してしまうとユーザーの指定が失われる。
            status.state = PrefabReferenceStatus::State::Missing;
            return status;
        }

        status.state = PrefabReferenceStatus::State::Resolved;
        status.display_name = record->display_name;
        status.path = record->source_path;
        return status;
    }

    AssetReferenceStatus ProjectSettings::ResolveStartupScene(
        const Assets::AssetDatabase& database) const
    {
        AssetReferenceStatus status;
        status.guid = startup_scene_guid_;

        if (startup_scene_guid_.empty())
        {
            status.state = AssetReferenceStatus::State::Unset;
            return status;
        }

        const Assets::AssetRecord* record = database.FindByGuid(startup_scene_guid_);
        if (record == nullptr)
        {
            // 登録が消えているだけ。GUID は保持したままにする。
            status.state = AssetReferenceStatus::State::Missing;
            return status;
        }
        if (record->kind != Assets::AssetKind::Scene)
        {
            // GUID はあるが Scene Asset ではない。
            // 起動先として使えないので Missing 扱いにし、値は残す。
            status.state = AssetReferenceStatus::State::Missing;
            status.display_name = record->display_name;
            status.path = record->source_path;
            return status;
        }

        status.state = AssetReferenceStatus::State::Resolved;
        status.display_name = record->display_name;
        status.path = record->source_path;
        return status;
    }
}
