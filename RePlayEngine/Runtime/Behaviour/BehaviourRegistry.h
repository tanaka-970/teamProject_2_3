#pragma once

#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Reflection/Registry/TypeGUID.h"

#include <memory>
#include <string>
#include <vector>

namespace ReplayEngine::Core { class Component; }

namespace ReplayEngine::Runtime
{
    // Behaviour 型を「どこが供給しているか」を表す境界。
    //
    // ---------------------------------------------------------------------
    // 【なぜインターフェイスを挟むのか】
    //
    //   今回の Behaviour はすべて C++ で書かれた Native 型で、
    //   ComponentRegistry の factory（std::function<unique_ptr<Component>()>）
    //   だけで生成できる。ここだけを見ればインターフェイスは要らない。
    //
    //   問題は次の Phase で C# を載せたときに起きる。
    //   Managed な Behaviour は次の点で Native と性質が違う。
    //     - 生成に AssemblyLoadContext と型解決が要る
    //     - Assembly を入れ替えると、同じ TypeGUID のまま実体の作り方が変わる
    //     - Compile 失敗中は「型はあるが作れない」状態になる
    //   これらを std::function 1 本で表そうとすると、Registry の外側に
    //   C# 用の分岐が散らばる。
    //
    //   供給元を Provider として分けておけば、
    //   ManagedBehaviourProvider を 1 つ足すだけで済み、
    //   ComponentRegistry / Scene / Serializer には手を入れずに済む。
    //
    //   今回 ManagedBehaviourProvider は作らない。
    //   ただし「Native の factory しか置けない Registry」には固定しない。
    // ---------------------------------------------------------------------
    class IBehaviourProvider
    {
    public:
        virtual ~IBehaviourProvider() = default;

        // "Native" / "Managed" など。Diagnostics 表示と、
        // Assembly Reload の対象を絞り込むために使う。
        virtual const char* ProviderName() const noexcept = 0;

        // この Provider が今その型を作れるか。
        //
        // 「登録されている」と「今作れる」を分ける理由:
        //   C# の Compile が失敗している間は、型の情報は残っているが
        //   実体は作れない。その状態を false で表せるようにしておく。
        //   作れないときは Missing Behaviour として保持される。
        virtual bool CanInstantiate(Reflection::TypeGUID type_guid) const = 0;

        // 実体を作る。作れなければ nullptr。例外は投げない。
        virtual std::unique_ptr<Core::Component> Instantiate(
            Reflection::TypeGUID type_guid) = 0;
    };

    // C++ で書かれた Behaviour の供給元。
    //
    // 実体の生成は ComponentRegistry の factory をそのまま使う。
    // Native 型については、この Provider は薄い覆いでしかない。
    class NativeBehaviourProvider final : public IBehaviourProvider
    {
    public:
        const char* ProviderName() const noexcept override { return "Native"; }
        bool CanInstantiate(Reflection::TypeGUID type_guid) const override;
        std::unique_ptr<Core::Component> Instantiate(Reflection::TypeGUID type_guid) override;
    };

    // Behaviour 型の登録簿。
    //
    // ComponentRegistry と役割が重ならないようにしてある。
    //   ComponentRegistry … 型の生成・保存・Inspector 表示の唯一の定義点。
    //                       Behaviour も普通の Component として必ずここへ登録する。
    //   BehaviourRegistry … その上に「誰が供給しているか」と
    //                       「Behaviour として扱う型はどれか」を重ねるだけ。
    //
    // Behaviour 専用の生成経路・保存経路・更新経路は作らない。
    // 作ると、Component とのあいだで挙動が食い違う場所ができる。
    class BehaviourRegistry final
    {
    public:
        BehaviourRegistry() = delete;

        struct Entry
        {
            Reflection::TypeGUID type_guid;
            Core::ComponentTypeID type_id = Core::invalid_component_type_id;
            std::string type_name;
            std::string module_id;
            IBehaviourProvider* provider = nullptr;
        };

        // Behaviour 型として登録する。
        //
        // 前提: 同じ型が既に ComponentRegistry へ登録済みであること。
        // 型 GUID が無効、または ComponentRegistry に無い場合は false。
        static bool Register(Reflection::TypeGUID type_guid, IBehaviourProvider& provider);

        static void Clear() noexcept;

        static const std::vector<Entry>& All() noexcept;
        static const Entry* Find(Reflection::TypeGUID type_guid) noexcept;
        static const Entry* Find(Core::ComponentTypeID type_id) noexcept;

        static bool IsBehaviour(Core::ComponentTypeID type_id) noexcept
        {
            return Find(type_id) != nullptr;
        }

        // 今その型を作れるか。Provider へ委ねる。
        static bool CanInstantiate(Reflection::TypeGUID type_guid) noexcept;

        // プロセス共有の Native Provider。
        static NativeBehaviourProvider& Native() noexcept;
    };
}
