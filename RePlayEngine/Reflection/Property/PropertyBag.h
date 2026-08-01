#pragma once

#include "PropertyValue.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ReplayEngine::Reflection
{
    // 名前付きプロパティ値の集まり。
    //
    // Component と Scene ファイルの間を行き来する箱。
    // 順序を保つため map ではなく vector を使う。保存の並びが毎回同じになり、
    // ファイルの差分が読みやすくなるため。件数は Component あたり数個なので線形探索で足りる。
    class PropertyBag final
    {
    public:
        struct Entry
        {
            std::string name;
            PropertyValue value;
        };

        void Set(std::string name, PropertyValue value);

        const PropertyValue* Find(const std::string& name) const noexcept;
        bool Contains(const std::string& name) const noexcept
        {
            return Find(name) != nullptr;
        }

        bool Remove(const std::string& name) noexcept;

        std::size_t Size() const noexcept { return entries_.size(); }
        bool Empty() const noexcept { return entries_.empty(); }
        void Clear() noexcept { entries_.clear(); }

        const std::vector<Entry>& Entries() const noexcept { return entries_; }

        std::vector<Entry>::const_iterator begin() const noexcept { return entries_.begin(); }
        std::vector<Entry>::const_iterator end() const noexcept { return entries_.end(); }

    private:
        std::vector<Entry> entries_;
    };
}
