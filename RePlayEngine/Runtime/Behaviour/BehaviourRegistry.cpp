#include "BehaviourRegistry.h"

#include "../../Object/Component/Component.h"

namespace ReplayEngine::Runtime
{
    namespace
    {
        std::vector<BehaviourRegistry::Entry>& Table() noexcept
        {
            static std::vector<BehaviourRegistry::Entry> table;
            return table;
        }
    }

    // ---- NativeBehaviourProvider -------------------------------------------

    bool NativeBehaviourProvider::CanInstantiate(Reflection::TypeGUID type_guid) const
    {
        const Core::ComponentTypeInfo* info = Core::ComponentRegistry::Find(type_guid);
        return info != nullptr && static_cast<bool>(info->factory);
    }

    std::unique_ptr<Core::Component> NativeBehaviourProvider::Instantiate(
        Reflection::TypeGUID type_guid)
    {
        const Core::ComponentTypeInfo* info = Core::ComponentRegistry::Find(type_guid);
        if (info == nullptr || !info->factory) return nullptr;
        return info->factory();
    }

    // ---- BehaviourRegistry --------------------------------------------------

    NativeBehaviourProvider& BehaviourRegistry::Native() noexcept
    {
        static NativeBehaviourProvider provider;
        return provider;
    }

    bool BehaviourRegistry::Register(Reflection::TypeGUID type_guid,
        IBehaviourProvider& provider)
    {
        // Behaviour には型 GUID を必須にする。
        // クラス名だけを永続 ID にすると、C# を載せたときに
        // Visual Studio でのクラス名変更で Scene の参照が切れる。
        if (!type_guid.IsValid()) return false;

        const Core::ComponentTypeInfo* info = Core::ComponentRegistry::Find(type_guid);
        if (info == nullptr) return false;   // 先に ComponentRegistry へ登録すること

        auto& table = Table();
        for (const Entry& existing : table)
        {
            if (existing.type_guid == type_guid) return false;   // 上書きしない
        }

        Entry entry;
        entry.type_guid = type_guid;
        entry.type_id = info->type_id;
        entry.type_name = info->type_name;
        entry.module_id = info->module_id;
        entry.provider = &provider;
        table.push_back(std::move(entry));
        return true;
    }

    void BehaviourRegistry::Clear() noexcept
    {
        Table().clear();
    }

    const std::vector<BehaviourRegistry::Entry>& BehaviourRegistry::All() noexcept
    {
        return Table();
    }

    const BehaviourRegistry::Entry* BehaviourRegistry::Find(
        Reflection::TypeGUID type_guid) noexcept
    {
        if (!type_guid.IsValid()) return nullptr;
        for (const Entry& entry : Table())
        {
            if (entry.type_guid == type_guid) return &entry;
        }
        return nullptr;
    }

    const BehaviourRegistry::Entry* BehaviourRegistry::Find(
        Core::ComponentTypeID type_id) noexcept
    {
        if (type_id == Core::invalid_component_type_id) return nullptr;
        for (const Entry& entry : Table())
        {
            if (entry.type_id == type_id) return &entry;
        }
        return nullptr;
    }

    bool BehaviourRegistry::CanInstantiate(Reflection::TypeGUID type_guid) noexcept
    {
        const Entry* entry = Find(type_guid);
        return entry != nullptr && entry->provider != nullptr &&
            entry->provider->CanInstantiate(type_guid);
    }
}
