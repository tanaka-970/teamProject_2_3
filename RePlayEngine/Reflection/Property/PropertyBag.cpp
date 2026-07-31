#include "PropertyBag.h"

#include <algorithm>

namespace ReplayEngine::Reflection
{
    void PropertyBag::Set(std::string name, PropertyValue value)
    {
        if (name.empty()) return;

        for (Entry& entry : entries_)
        {
            if (entry.name == name)
            {
                entry.value = std::move(value);
                return;
            }
        }
        entries_.push_back(Entry{ std::move(name), std::move(value) });
    }

    const PropertyValue* PropertyBag::Find(const std::string& name) const noexcept
    {
        for (const Entry& entry : entries_)
        {
            if (entry.name == name) return &entry.value;
        }
        return nullptr;
    }

    bool PropertyBag::Remove(const std::string& name) noexcept
    {
        const auto found = std::find_if(entries_.begin(), entries_.end(),
            [&name](const Entry& entry) { return entry.name == name; });
        if (found == entries_.end()) return false;
        entries_.erase(found);
        return true;
    }
}
