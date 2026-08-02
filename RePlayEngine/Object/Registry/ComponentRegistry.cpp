#include "ComponentRegistry.h"

#include "../Component/Component.h"
#include "../GameObject/GameObject.h"

#include <algorithm>

namespace ReplayEngine::Core
{
    namespace
    {
        // 関数ローカル static なので、翻訳単位をまたぐ静的初期化順序に依存しない。
        std::vector<ComponentTypeInfo>& Table() noexcept
        {
            static std::vector<ComponentTypeInfo> table;
            return table;
        }

        std::vector<std::string>& ConflictLog() noexcept
        {
            static std::vector<std::string> conflicts;
            return conflicts;
        }

        // この型が名乗る GUID（本体 + 別名）のどれかに一致するか。
        bool ClaimsGUID(const ComponentTypeInfo& info, Reflection::TypeGUID guid) noexcept
        {
            if (!guid.IsValid()) return false;
            if (info.type_guid == guid) return true;
            for (const Reflection::TypeGUID& alias : info.alias_guids)
            {
                if (alias == guid) return true;
            }
            return false;
        }
    }

    bool ComponentRegistry::RegisterInfo(ComponentTypeInfo info)
    {
        if (info.type_name.empty()) return false;
        if (info.type_id == invalid_component_type_id) return false;
        if (!info.factory) return false;

        auto& table = Table();

        // 同じ型 ID または同じ型名の二重登録は拒否する。
        // 上書きしないのは、後から入った登録が先の設定を静かに壊すのを防ぐため。
        const auto duplicated = std::find_if(table.begin(), table.end(),
            [&info](const ComponentTypeInfo& existing)
            {
                return existing.type_id == info.type_id || existing.type_name == info.type_name;
            });
        if (duplicated != table.end()) return false;

        // GUID の重複はもっと危険なので、記録したうえで登録自体を拒否する。
        //
        // 上書きしてしまうと、保存済み Scene の参照が別の型へ解決され、
        // 「開いたら中身が違う Component になっていた」という壊れ方をする。
        // 拒否すれば、その型は Missing Component として保持されるだけで済む。
        if (info.type_guid.IsValid())
        {
            const auto guid_conflict = std::find_if(table.begin(), table.end(),
                [&info](const ComponentTypeInfo& existing)
                {
                    return ClaimsGUID(existing, info.type_guid);
                });
            if (guid_conflict != table.end())
            {
                ConflictLog().push_back(
                    "Type GUID が重複しています: " + info.type_guid.ToString() +
                    " (" + info.type_name + " と " + guid_conflict->type_name + ")");
                return false;
            }
        }

        // 別名 GUID が既存の型と衝突する場合も同じ扱いにする。
        for (const Reflection::TypeGUID& alias : info.alias_guids)
        {
            if (!alias.IsValid()) continue;
            const auto alias_conflict = std::find_if(table.begin(), table.end(),
                [&alias](const ComponentTypeInfo& existing)
                {
                    return ClaimsGUID(existing, alias);
                });
            if (alias_conflict != table.end())
            {
                ConflictLog().push_back(
                    "別名 Type GUID が重複しています: " + alias.ToString() +
                    " (" + info.type_name + " と " + alias_conflict->type_name + ")");
                return false;
            }
        }

        table.push_back(std::move(info));
        return true;
    }

    void ComponentRegistry::Clear() noexcept
    {
        Table().clear();
        ConflictLog().clear();
    }

    const std::vector<std::string>& ComponentRegistry::TypeGUIDConflicts() noexcept
    {
        return ConflictLog();
    }

    const ComponentTypeInfo* ComponentRegistry::Find(ComponentTypeID type_id) noexcept
    {
        if (type_id == invalid_component_type_id) return nullptr;
        const auto& table = Table();
        for (const ComponentTypeInfo& info : table)
        {
            if (info.type_id == type_id) return &info;
        }
        return nullptr;
    }

    const ComponentTypeInfo* ComponentRegistry::Find(const std::string& type_name) noexcept
    {
        if (type_name.empty()) return nullptr;
        const auto& table = Table();
        for (const ComponentTypeInfo& info : table)
        {
            if (info.type_name == type_name) return &info;
        }
        return nullptr;
    }

    const ComponentTypeInfo* ComponentRegistry::Find(Reflection::TypeGUID type_guid) noexcept
    {
        if (!type_guid.IsValid()) return nullptr;

        const auto& table = Table();

        // 本体の GUID を先に総当たりする。別名より本体を優先するため、
        // 1 回のループでまとめて判定せずに 2 段構えにしてある。
        for (const ComponentTypeInfo& info : table)
        {
            if (info.type_guid == type_guid) return &info;
        }
        for (const ComponentTypeInfo& info : table)
        {
            if (ClaimsGUID(info, type_guid)) return &info;
        }
        return nullptr;
    }

    const ComponentTypeInfo* ComponentRegistry::Resolve(Reflection::TypeGUID type_guid,
        const std::string& type_name) noexcept
    {
        if (const ComponentTypeInfo* by_guid = Find(type_guid)) return by_guid;

        const ComponentTypeInfo* by_name = Find(type_name);
        if (by_name == nullptr) return nullptr;

        // 保存データに GUID があり、名前で当たった型が別の GUID を名乗っている場合は
        // 「別の型」と判断する。名前が同じでも中身が違う可能性があるため、
        // 黙って値を流し込まず Missing Component として保持させる。
        if (type_guid.IsValid() && by_name->type_guid.IsValid() &&
            by_name->type_guid != type_guid)
        {
            return nullptr;
        }
        return by_name;
    }

    const std::vector<ComponentTypeInfo>& ComponentRegistry::All() noexcept
    {
        return Table();
    }

    std::vector<std::string> ComponentRegistry::Categories()
    {
        std::vector<std::string> categories;
        for (const ComponentTypeInfo& info : Table())
        {
            if (!info.editor_visible) continue;
            const std::string& category = info.category.empty()
                ? std::string{ "Gameplay" } : info.category;
            if (std::find(categories.begin(), categories.end(), category) == categories.end())
            {
                categories.push_back(category);
            }
        }
        return categories;
    }

    std::unique_ptr<Component> ComponentRegistry::Instantiate(ComponentTypeID type_id)
    {
        const ComponentTypeInfo* info = Find(type_id);
        if (info == nullptr || !info->factory) return nullptr;
        return info->factory();
    }

    std::unique_ptr<Component> ComponentRegistry::Instantiate(const std::string& type_name)
    {
        const ComponentTypeInfo* info = Find(type_name);
        if (info == nullptr || !info->factory) return nullptr;
        return info->factory();
    }

    Component* ComponentRegistry::Create(ComponentTypeID type_id, GameObject& owner)
    {
        return CreateWithStableID(type_id, owner, invalid_component_stable_id);
    }

    Component* ComponentRegistry::CreateWithStableID(ComponentTypeID type_id, GameObject& owner,
        ComponentStableID stable_id)
    {
        // 重複禁止の型が既にある場合は、追加せず既存インスタンスを返す。
        // Editor から押されたときも Scene 読み込み時も、ここで一律に弾く。
        if (!AllowsMultiple(type_id))
        {
            if (Component* existing = owner.FindComponent(type_id)) return existing;
        }

        std::unique_ptr<Component> created = Instantiate(type_id);
        if (!created) return nullptr;

        Component* raw = created.get();
        return owner.AttachComponentWithStableID(std::move(created), stable_id) ? raw : nullptr;
    }

    Component* ComponentRegistry::Create(const std::string& type_name, GameObject& owner)
    {
        const ComponentTypeInfo* info = Find(type_name);
        if (info == nullptr) return nullptr;
        return Create(info->type_id, owner);
    }

    bool ComponentRegistry::AllowsMultiple(ComponentTypeID type_id) noexcept
    {
        const ComponentTypeInfo* info = Find(type_id);
        // 未登録なら「複数不可」に倒す。想定外の型が無限に増えるより安全。
        return info != nullptr && info->allow_multiple;
    }

    bool ComponentRegistry::IsRemovable(ComponentTypeID type_id) noexcept
    {
        const ComponentTypeInfo* info = Find(type_id);
        // 未登録なら削除できる方に倒す。読み込めなかった型を取り除けるようにするため。
        return info == nullptr || info->removable;
    }

    bool ComponentRegistry::IsSerializable(ComponentTypeID type_id) noexcept
    {
        const ComponentTypeInfo* info = Find(type_id);
        return info != nullptr && info->serializable;
    }

    bool ComponentRegistry::IsEditorVisible(ComponentTypeID type_id) noexcept
    {
        const ComponentTypeInfo* info = Find(type_id);
        return info != nullptr && info->editor_visible;
    }

    std::string ComponentRegistry::DisplayNameOf(ComponentTypeID type_id)
    {
        if (const ComponentTypeInfo* info = Find(type_id)) return info->DisplayName();
        return "(未登録: " + std::to_string(type_id) + ")";
    }
}
