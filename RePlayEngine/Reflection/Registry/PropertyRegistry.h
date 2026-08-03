#pragma once

#include "../Property/PropertyBag.h"
#include "../Property/PropertyDesc.h"
#include "../../Object/Component/ComponentTypeID.h"

#include <string>
#include <vector>

namespace ReplayEngine::Reflection
{
    // Component 型ごとのプロパティ定義表。
    //
    // ComponentRegistry と同じ理由で静的な表として持つ。
    // 型のメタ情報はプロセス全体で 1 つしかなく、Scene ごとに変わらないため。
    //
    // 「Inspector に出す定義」と「Scene へ保存する定義」を分けないことが最重要の設計方針。
    // ここへ 1 回登録すれば、次のすべてが自動的に揃う。
    //   - Inspector の入力欄（型・範囲・刻み・表示名・ツールチップ・読み取り専用）
    //   - Scene ファイルへの保存と復元
    //   - GameObject / Component の複製
    class PropertyRegistry final
    {
    public:
        PropertyRegistry() = delete;

        // 型 T のプロパティを登録する。
        // 同じ名前が既にある場合は後勝ちで上書きせず、false を返す。
        template<class T>
        static bool Register(PropertyDesc desc)
        {
            static_assert(std::is_base_of_v<Core::Component, T>,
                "T must derive from ReplayEngine::Core::Component");
            return RegisterFor(T::StaticTypeID(), std::move(desc));
        }

        static bool RegisterFor(Core::ComponentTypeID type_id, PropertyDesc desc);

        static void Clear() noexcept;

        // 登録順のまま返す。Inspector の表示順と保存順がこれで決まる。
        static const std::vector<PropertyDesc>& PropertiesOf(Core::ComponentTypeID type_id) noexcept;

        static const PropertyDesc* Find(Core::ComponentTypeID type_id,
            const std::string& name) noexcept;

        static bool HasProperties(Core::ComponentTypeID type_id) noexcept
        {
            return !PropertiesOf(type_id).empty();
        }

        // ---- 一括操作 ------------------------------------------------------

        // Component の保存対象プロパティをすべて PropertyBag へ書き出す。
        // Component::OnSerialize による追加分もここで合流させる。
        static void Capture(const Core::Component& component, PropertyBag& output);

        // PropertyBag の値を Component へ反映する。
        //
        // 安全側の設計:
        //   - 登録されていない名前は無視し、unknown_names へ積んで呼び出し側が警告を出せるようにする
        //   - 保存データに存在しないプロパティは Component の初期値のままにする
        //   - 型が変わっている場合は PropertyValue 側の変換規則に従い、
        //     変換できなければ既定値を維持する
        //   - 読み取り専用プロパティへは書き込まない
        // いずれの場合も例外を投げず、Scene 全体の読み込みを止めない。
        static void Apply(Core::Component& component, const PropertyBag& input,
            std::vector<std::string>* unknown_names = nullptr);

        // 同じ型の Component 間で値を写す。GameObject / Component の複製に使う。
        static void CopyValues(const Core::Component& source, Core::Component& destination);
    };
}
