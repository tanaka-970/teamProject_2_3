#include "ScriptTypeCatalog.h"

#include "ScriptValue.h"

#include <algorithm>
#include <utility>

namespace ReplayEngine::Scripting
{
    std::string ScriptTypeDescriptor::ResolvedCategory() const
    {
        if (!category.empty()) return category;
        return ScriptCategoryName(language);
    }

    ScriptTypeDescriptor* ScriptTypeCatalog::FindMutable(ScriptTypeID type_id) noexcept
    {
        for (ScriptTypeDescriptor& entry : entries_)
        {
            if (entry.type_id == type_id) return &entry;
        }
        return nullptr;
    }

    const ScriptTypeDescriptor* ScriptTypeCatalog::Find(ScriptTypeID type_id) const noexcept
    {
        for (const ScriptTypeDescriptor& entry : entries_)
        {
            if (entry.type_id == type_id) return &entry;
        }
        return nullptr;
    }

    void ScriptTypeCatalog::Register(ScriptTypeDescriptor descriptor)
    {
        if (!descriptor.type_id.IsValid()) return;

        if (descriptor.display_name.empty() && !descriptor.script_name.empty())
        {
            descriptor.display_name = HumanizeFieldName(descriptor.script_name);
        }
        if (descriptor.category.empty())
        {
            descriptor.category = ScriptCategoryName(descriptor.language);
        }

        if (ScriptTypeDescriptor* existing = FindMutable(descriptor.type_id))
        {
            *existing = std::move(descriptor);
            return;
        }
        entries_.push_back(std::move(descriptor));
    }

    bool ScriptTypeCatalog::ReplaceSchema(ScriptTypeID type_id, ScriptFieldSchemaRef schema,
        ScriptStatus status, std::string last_error)
    {
        ScriptTypeDescriptor* existing = FindMutable(type_id);
        if (existing == nullptr) return false;

        // 読み込みに失敗した場合は、最後に成功した Schema を捨てない。
        // 捨てると Inspector から Field が消え、Undo でも戻せなくなる。
        if (schema) existing->schema = std::move(schema);

        existing->status = status;
        existing->last_error = std::move(last_error);
        return true;
    }

    bool ScriptTypeCatalog::Remove(ScriptTypeID type_id) noexcept
    {
        for (std::size_t index = 0; index < entries_.size(); ++index)
        {
            if (entries_[index].type_id != type_id) continue;
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
            return true;
        }
        return false;
    }

    std::size_t ScriptTypeCatalog::RemoveLanguage(ScriptLanguage language) noexcept
    {
        const std::size_t before = entries_.size();
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
            [language](const ScriptTypeDescriptor& entry)
            {
                return entry.language == language;
            }), entries_.end());
        return before - entries_.size();
    }

    void ScriptTypeCatalog::Clear() noexcept
    {
        entries_.clear();
    }

    ScriptFieldSchemaRef ScriptTypeCatalog::FindSchema(ScriptTypeID type_id) const
    {
        const ScriptTypeDescriptor* entry = Find(type_id);
        if (entry == nullptr) return ScriptFieldSchemaRef{};
        return entry->schema;
    }

    std::string ScriptTypeCatalog::FindDisplayName(ScriptTypeID type_id) const
    {
        const ScriptTypeDescriptor* entry = Find(type_id);
        if (entry == nullptr) return std::string();
        return entry->DisplayName();
    }
}
