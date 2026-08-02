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

        // 読み込み時に「この型が知らない」として預かったぶんを書き戻す。
        //
        // 登録済みの名前と重なった場合は登録側を優先する。
        // 重なるのは「預かったあとで同名のプロパティが登録された」場合であり、
        // そのときは既に Apply 側で本来の場所へ復元されている。
        if (const PropertyBag* retained = component.UnknownProperties())
        {
            for (const PropertyBag::Entry& entry : retained->Entries())
            {
                if (output.Contains(entry.name)) continue;
                output.Set(entry.name, entry.value);
            }
        }
    }

    void PropertyRegistry::Apply(Core::Component& component, const PropertyBag& input,
        std::vector<std::string>* unknown_names)
    {
        const Core::ComponentTypeID type_id = component.TypeID();

        // この型が知らない名前をそのまま預かるための入れ物。
        // 名前だけでなく値ごと残すのが要点。名前しか残さないと、
        // 保存し直したときに値が空になってしまう。
        PropertyBag retained;

        for (const PropertyBag::Entry& entry : input.Entries())
        {
            const PropertyDesc* desc = Find(type_id, entry.name);
            if (desc == nullptr)
            {
                // 未知のプロパティ。次のような場面で発生する。
                //   - 新しいビルドで足したプロパティを古いビルドで開いた
                //   - C# Script が Compile できておらず型が読めていない
                // どちらも「値が要らなくなった」わけではないので、預かって書き戻す。
                retained.Set(entry.name, entry.value);
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
            //
            // 寄せられなかった場合でも預かりへは入れない。
            // 名前が登録済みである以上、保存時に登録側の値が同じ名前で書かれるため、
            // 預かってしまうと同じ名前が二重になり、どちらが勝つかが不定になる。
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

        // 預かり内容を差し替える。
        //
        // 以前預かっていた名前が今回登録済みになっていた場合、その名前は
        // retained へ入らないため、ここで自動的に預かりが解ける（Rehydrate）。
        // 値そのものは上のループで本来のプロパティへ復元済み。
        component.RetainUnknownProperties(retained);

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

        // 預かっている未知プロパティも複製先へ引き継ぐ。
        // これを写さないと、複製しただけで未知の値が消える。
        if (const PropertyBag* retained = source.UnknownProperties())
        {
            destination.RetainUnknownProperties(*retained);
        }
        else
        {
            destination.ClearUnknownProperties();
        }

        destination.OnPropertyChanged(nullptr);
    }
}
