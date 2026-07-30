#include "SceneDocument.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <utility>

namespace ReplayEngine::Scene
{
    namespace
    {
        std::string NormalizeIdentifier(const std::string& source)
        {
            // UTF-8文字は保持し、ASCIIの区切り文字だけを単一の下線へ畳み込む。
            std::string result;
            bool pending_separator = false;
            for (const unsigned char character : source)
            {
                if (character >= 0x80)
                {
                    if (pending_separator && !result.empty()) result.push_back('_');
                    pending_separator = false;
                    result.push_back(static_cast<char>(character));
                    continue;
                }
                if (std::isalnum(character))
                {
                    if (pending_separator && !result.empty()) result.push_back('_');
                    pending_separator = false;
                    result.push_back(static_cast<char>(std::tolower(character)));
                }
                else if (character == '_' || character == '-' || std::isspace(character))
                {
                    pending_separator = !result.empty();
                }
            }
            while (!result.empty() && result.back() == '_') result.pop_back();
            return result.empty() ? "entity" : result;
        }

        std::string NumberedIdentifier(const std::string& base, std::uint32_t number)
        {
            std::ostringstream stream;
            stream << base << '_' << std::setfill('0') << std::setw(3) << number;
            return stream.str();
        }
    }

    SceneEntity& SceneDocument::CreateEntity(std::string name)
    {
        SceneEntity entity{};
        entity.id = next_id_++;
        entity.name = std::move(name);
        entity.identifier = MakeUniqueIdentifier(entity.name);
        entity.transform.emplace();
        entities_.push_back(std::move(entity));
        return entities_.back();
    }

    SceneEntity& SceneDocument::ImportEntity(const SceneEntity& source)
    {
        // 複製元の内容は保ちつつ、文書内IDと識別子だけを新しく割り当てる。
        SceneEntity entity = source;
        entity.id = next_id_++;
        entity.identifier = MakeUniqueIdentifier(source.name + "_copy");
        entities_.push_back(std::move(entity));
        return entities_.back();
    }

    bool SceneDocument::DestroyEntity(EntityId id)
    {
        const auto found = std::remove_if(entities_.begin(), entities_.end(),
            [id](const SceneEntity& entity) { return entity.id == id; });
        if (found == entities_.end()) return false;
        entities_.erase(found, entities_.end());
        return true;
    }

    SceneEntity* SceneDocument::Find(EntityId id) noexcept
    {
        const auto found = std::find_if(entities_.begin(), entities_.end(),
            [id](const SceneEntity& entity) { return entity.id == id; });
        return found == entities_.end() ? nullptr : &*found;
    }

    const SceneEntity* SceneDocument::Find(EntityId id) const noexcept
    {
        const auto found = std::find_if(entities_.begin(), entities_.end(),
            [id](const SceneEntity& entity) { return entity.id == id; });
        return found == entities_.end() ? nullptr : &*found;
    }

    std::string SceneDocument::MakeUniqueIdentifier(const std::string& seed, EntityId except_id) const
    {
        const std::string base = NormalizeIdentifier(seed);
        for (std::uint32_t number = 1; number != 0; ++number)
        {
            const std::string candidate = NumberedIdentifier(base, number);
            const bool used = std::any_of(entities_.begin(), entities_.end(),
                [&candidate, except_id](const SceneEntity& entity)
                {
                    return entity.id != except_id && entity.identifier == candidate;
                });
            if (!used) return candidate;
        }
        return base;
    }

    bool SceneDocument::SetIdentifier(EntityId id, const std::string& desired)
    {
        SceneEntity* entity = Find(id);
        if (!entity) return false;
        entity->identifier = MakeUniqueIdentifier(desired, id);
        return true;
    }

    void SceneDocument::SetSceneName(std::string name)
    {
        scene_name_ = name.empty() ? "Scene" : std::move(name);
        scene_identifier_ = NormalizeIdentifier(scene_name_);
    }

    void SceneDocument::Clear() noexcept
    {
        entities_.clear();
        next_id_ = 1;
        scene_name_ = "Main";
        scene_identifier_ = "main";
    }

    void SceneDocument::RebuildNextId()
    {
        // 外部ファイル由来の重複識別子を修復しながら最大IDの次を求める。
        next_id_ = 1;
        std::vector<std::string> used_identifiers;
        for (SceneEntity& entity : entities_)
        {
            next_id_ = (std::max)(next_id_, entity.id + 1);
            const std::string requested = entity.identifier.empty() ? entity.name : entity.identifier;
            const std::string normalized = NormalizeIdentifier(requested);
            std::string repaired = normalized;
            if (std::find(used_identifiers.begin(), used_identifiers.end(), repaired) != used_identifiers.end() ||
                entity.identifier.empty())
            {
                for (std::uint32_t number = 1; number != 0; ++number)
                {
                    repaired = NumberedIdentifier(normalized, number);
                    if (std::find(used_identifiers.begin(), used_identifiers.end(), repaired) ==
                        used_identifiers.end()) break;
                }
            }
            entity.identifier = std::move(repaired);
            used_identifiers.push_back(entity.identifier);
        }
    }
}
