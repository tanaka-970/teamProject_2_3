#pragma once

#include "../../Core/ObjectID/ObjectID.h"
#include "../../Core/ObjectID/RuntimeIdentity.h"

#include <string>
#include <utility>

namespace ReplayEngine::Reflection
{
    // Component / Behaviour が「他の何か」を指すための保存可能な参照。
    //
    // 4 種類を明確に分けている理由:
    //   指す先ごとに、保存する情報も壊れ方も違うため。
    //   1 つの汎用参照でまとめると、Missing の扱いと Prefab 配置時の
    //   付け替え規則がどちらも曖昧になる。
    //
    //   ObjectReference    … 同じ Scene 内の GameObject。Prefab 配置で付け替える。
    //   ComponentReference … 同じ Scene 内の Component。所有 ObjectID だけ付け替える。
    //   AssetReference     … Asset。AssetGUID なので Rename / Move で壊れない。
    //   SceneReference     … Scene Asset。Path も Scene 名も保存しない。
    //
    // 共通の約束:
    //   - 指す先が見つからなくても、保存されていた値は絶対に捨てない。
    //     捨ててしまうと「一時的に Asset を移動しただけ」で参照が消える。
    //   - 実行時の生ポインタは持たない。解決は HandleResolver / AssetDatabase が行う。
    //   - 値型。コピー・比較が安全。

    // 同じ Scene 内の GameObject を指す。
    struct ObjectReference final
    {
        Core::ObjectID object;

        bool IsAssigned() const noexcept { return object.Valid(); }
        void Clear() noexcept { object = Core::ObjectID::Invalid(); }

        bool operator==(const ObjectReference& other) const noexcept
        {
            return object == other.object;
        }
        bool operator!=(const ObjectReference& other) const noexcept
        {
            return !(*this == other);
        }
    };

    // 同じ Scene 内の Component を指す。
    //
    // owner + component の 2 段構えにしている理由:
    //   ComponentStableID は GameObject の中でだけ一意。
    //   Scene 全体で一意にすると、Prefab を配置するたびに Component 側も
    //   振り直さなければならなくなる。
    //   所有 GameObject を別に持てば、付け替えが必要なのは owner だけで済む。
    //
    // expected_type は検証用。型が違っていても参照自体は保持し、
    // Validation が警告を出せるようにする（黙って無効化しない）。
    struct ComponentReference final
    {
        Core::ObjectID owner;
        Core::ComponentStableID component = Core::invalid_component_stable_id;

        bool IsAssigned() const noexcept
        {
            return owner.Valid() && component != Core::invalid_component_stable_id;
        }
        void Clear() noexcept
        {
            owner = Core::ObjectID::Invalid();
            component = Core::invalid_component_stable_id;
        }

        bool operator==(const ComponentReference& other) const noexcept
        {
            return owner == other.owner && component == other.component;
        }
        bool operator!=(const ComponentReference& other) const noexcept
        {
            return !(*this == other);
        }
    };

    // Asset を AssetGUID で指す。
    //
    // Path を保存しない理由:
    //   Asset を移動・改名したときに参照が切れるため。
    //   表示名と Path は AssetDatabase から GUID で引き直す。
    //
    // GUID が解決できない場合でも guid は保持したままにする。
    // Editor は [Missing Asset] と表示し、Asset が戻れば自動的に復活する。
    struct AssetReference final
    {
        std::string guid;

        AssetReference() = default;
        explicit AssetReference(std::string value) : guid(std::move(value)) {}

        bool IsAssigned() const noexcept { return !guid.empty(); }
        void Clear() noexcept { guid.clear(); }

        bool operator==(const AssetReference& other) const noexcept
        {
            return guid == other.guid;
        }
        bool operator!=(const AssetReference& other) const noexcept
        {
            return !(*this == other);
        }
    };

    // Scene Asset を指す。
    //
    // AssetReference と型を分けている理由:
    //   Scene 遷移の要求先として使うため、Editor の Picker が
    //   Scene Asset だけを候補に出せる必要がある。
    //   型が同じだと「Texture を遷移先に設定できてしまう」ことになる。
    struct SceneReference final
    {
        std::string guid;

        SceneReference() = default;
        explicit SceneReference(std::string value) : guid(std::move(value)) {}

        bool IsAssigned() const noexcept { return !guid.empty(); }
        void Clear() noexcept { guid.clear(); }

        bool operator==(const SceneReference& other) const noexcept
        {
            return guid == other.guid;
        }
        bool operator!=(const SceneReference& other) const noexcept
        {
            return !(*this == other);
        }
    };
}
