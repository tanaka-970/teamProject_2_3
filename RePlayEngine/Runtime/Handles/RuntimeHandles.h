#pragma once

#include "../../Core/ObjectID/ObjectID.h"
#include "../../Core/ObjectID/RuntimeIdentity.h"
#include "../../Object/Component/ComponentTypeID.h"

#include <cstddef>
#include <functional>
#include <string>
#include <type_traits>

namespace ReplayEngine::Runtime
{
    // Script から GameObject / Component を指すための安全な参照。
    //
    // 生ポインタを長期参照として公開しない理由:
    //   Scene を切り替えると World ごと作り直される。Object を消せば実体も消える。
    //   生ポインタを Script 側が持ち続けると、そのどちらでも解放済みメモリを指す。
    //   Handle は「番号の組」でしかないので、指す先が消えても Handle 自体は壊れない。
    //   解決 (Resolve) のたびに World / Object / Component の生存を確かめる。
    //
    // 使い方の約束:
    //   - Handle は好きなだけ保持してよい。
    //   - Resolve して得た生ポインタはフレームをまたいで保持しない。
    //     その場で使い切り、次に必要になったらもう一度 Resolve する。
    //
    // 型の性質:
    //   trivially copyable かつ standard layout。
    //   将来 C ABI へそのまま出せるよう、仮想関数も不変条件も持たせない。
    //   下の static_assert がその性質を壊す変更を検出する。

    struct ObjectHandle final
    {
        // どの World のものか。Scene の切り替え・再読み込みで必ず変わる。
        Core::WorldInstanceID world = Core::invalid_world_instance_id;

        // World 内での GameObject の ID。
        Core::ObjectID object{};

        // 同じ ObjectID が作り直された場合に古い Handle を弾くための世代番号。
        Core::ObjectGeneration generation = Core::invalid_object_generation;

        // 「何も指していない」状態か。
        // これが true でも Resolve は失敗を返すだけで、assert もクラッシュもしない。
        constexpr bool IsEmpty() const noexcept
        {
            return world == Core::invalid_world_instance_id ||
                !object.Valid() ||
                generation == Core::invalid_object_generation;
        }

        static constexpr ObjectHandle None() noexcept { return ObjectHandle{}; }

        constexpr bool operator==(const ObjectHandle& other) const noexcept
        {
            return world == other.world &&
                object == other.object &&
                generation == other.generation;
        }
        constexpr bool operator!=(const ObjectHandle& other) const noexcept
        {
            return !(*this == other);
        }

        // ログ・Diagnostics 用。"world:object:generation" 形式。
        std::string ToString() const;
    };

    struct ComponentHandle final
    {
        // 所有 GameObject。Component だけが生き残ることはないので、必ず一緒に持つ。
        ObjectHandle owner{};

        // World 内で一度だけ振られる通し番号。再利用しないので世代番号を兼ねる。
        Core::ComponentInstanceID instance = Core::invalid_component_instance_id;

        // 期待する型。Resolve せずに型違いを弾けるようにするために持つ。
        // Phase 2 で導入する Type GUID は、この実行時 ID とは別に Registry 側が持つ。
        // Handle の並びは変えずに済むため、ここは実行時の型 ID のまま据え置く。
        Core::ComponentTypeID type_id = Core::invalid_component_type_id;

        constexpr bool IsEmpty() const noexcept
        {
            return owner.IsEmpty() || instance == Core::invalid_component_instance_id;
        }

        static constexpr ComponentHandle None() noexcept { return ComponentHandle{}; }

        constexpr bool operator==(const ComponentHandle& other) const noexcept
        {
            return owner == other.owner &&
                instance == other.instance &&
                type_id == other.type_id;
        }
        constexpr bool operator!=(const ComponentHandle& other) const noexcept
        {
            return !(*this == other);
        }

        std::string ToString() const;
    };

    // C ABI / C# へ渡せる形であり続けることを保証する。
    // 仮想関数やコンストラクタを足すとここで落ちる。
    static_assert(std::is_trivially_copyable_v<ObjectHandle>,
        "ObjectHandle は将来 C ABI へ出すため trivially copyable を保つこと");
    static_assert(std::is_standard_layout_v<ObjectHandle>,
        "ObjectHandle は将来 C ABI へ出すため standard layout を保つこと");
    static_assert(std::is_trivially_copyable_v<ComponentHandle>,
        "ComponentHandle は将来 C ABI へ出すため trivially copyable を保つこと");
    static_assert(std::is_standard_layout_v<ComponentHandle>,
        "ComponentHandle は将来 C ABI へ出すため standard layout を保つこと");
}

namespace std
{
    template<>
    struct hash<ReplayEngine::Runtime::ObjectHandle>
    {
        std::size_t operator()(const ReplayEngine::Runtime::ObjectHandle& handle) const noexcept
        {
            // 64bit 前提の単純な混ぜ方。等値比較の前段の絞り込みにしか使わない。
            std::size_t seed = std::hash<std::uint64_t>{}(handle.world);
            seed ^= std::hash<ReplayEngine::Core::ObjectID>{}(handle.object) +
                0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
            seed ^= std::hash<std::uint32_t>{}(handle.generation) +
                0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    template<>
    struct hash<ReplayEngine::Runtime::ComponentHandle>
    {
        std::size_t operator()(const ReplayEngine::Runtime::ComponentHandle& handle) const noexcept
        {
            std::size_t seed = std::hash<ReplayEngine::Runtime::ObjectHandle>{}(handle.owner);
            seed ^= std::hash<std::uint64_t>{}(handle.instance) +
                0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
}
