#include "PropertyRegistry.h"

#include <algorithm>
#include <unordered_map>

namespace ReplayEngine::Reflection
{
    namespace
    {
        using Table = std::unordered_map<Core::ComponentTypeID, std::vector<PropertyDesc>>;

        Table& PropertyTable() noexcept
        {
            static Table table;
            return table;
        }

        const std::vector<PropertyDesc>& EmptyList() noexcept
        {
            static const std::vector<PropertyDesc> empty;
            return empty;
        }
    }

    bool PropertyRegistry::RegisterFor(Core::ComponentTypeID type_id, PropertyDesc desc)
    {
        if (type_id == Core::invalid_component_type_id) return false;
        if (!desc.Valid()) return false;

        std::vector<PropertyDesc>& list = PropertyTable()[type_id];
        const auto duplicated = std::find_if(list.begin(), list.end(),
            [&desc](const PropertyDesc& existing) { return existing.name == desc.name; });
        if (duplicated != list.end()) return false;

        list.push_back(std::move(desc));
        return true;
    }

    void PropertyRegistry::Clear() noexcept
    {
        PropertyTable().clear();
    }

    const std::vector<PropertyDesc>& PropertyRegistry::PropertiesOf(
        Core::ComponentTypeID type_id) noexcept
    {
        const Table& table = PropertyTable();
        const auto found = table.find(type_id);
        return found == table.end() ? EmptyList() : found->second;
    }

    const PropertyDesc* PropertyRegistry::Find(Core::ComponentTypeID type_id,
        const std::string& name) noexcept
    {
        for (const PropertyDesc& desc : PropertiesOf(type_id))
        {
            if (desc.name == name) return &desc;
        }
        return nullptr;
    }

    void PropertyRegistry::Capture(const Core::Component& component, PropertyBag& output)
    {
        for (const PropertyDesc& desc : PropertiesOf(component.TypeID()))
        {
            if (!desc.serializable) continue;
            output.Set(desc.name, desc.Capture(component));
        }

        // PropertyRegistry では表現しきれない値の追加分。
        // 通常の数値・文字列プロパティはここではなく登録側で扱う。
        component.OnSerialize(output);
    }

    void PropertyRegistry::Apply(Core::Component& component, const PropertyBag& input,
        std::vector<std::string>* unknown_names)
    {
        const Core::ComponentTypeID type_id = component.TypeID();

        for (const PropertyBag::Entry& entry : input.Entries())
        {
            const PropertyDesc* desc = Find(type_id, entry.name);
            if (desc == nullptr)
            {
                // 未知のプロパティ。Component の実装が変わって削除された場合など。
                // 読み込み自体は続行し、呼び出し側が警告を出せるように名前だけ残す。
                if (unknown_names != nullptr) unknown_names->push_back(entry.name);
                continue;
            }
            if (!desc->serializable || desc->read_only) continue;

            if (entry.value.Type() == desc->type)
            {
                desc->Apply(component, entry.value);
                continue;
            }

            // 保存時と型が変わっている場合。寄せられるなら寄せ、無理なら初期値を維持する。
            PropertyValue converted;
            if (entry.value.ConvertTo(desc->type, converted))
            {
                desc->Apply(component, converted);
            }
            else if (unknown_names != nullptr)
            {
                unknown_names->push_back(entry.name);
            }
        }

        component.OnDeserialize(input);
        component.OnPropertyChanged(nullptr);
    }

    void PropertyRegistry::CopyValues(const Core::Component& source, Core::Component& destination)
    {
        if (source.TypeID() != destination.TypeID()) return;

        for (const PropertyDesc& desc : PropertiesOf(source.TypeID()))
        {
            if (desc.read_only) continue;
            desc.Apply(destination, desc.Capture(source));
        }

        // 追加保存分も同じ経路で写す。
        PropertyBag extra;
        source.OnSerialize(extra);
        if (!extra.Empty()) destination.OnDeserialize(extra);

        destination.OnPropertyChanged(nullptr);
    }
}
