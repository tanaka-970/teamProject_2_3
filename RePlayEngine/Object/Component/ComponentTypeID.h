#pragma once

#include <cstdint>
#include <string_view>

namespace ReplayEngine::Core
{
    // Component 型の識別子。型名から計算する 32bit ハッシュ。
    //
    //  - コンパイル時に決まるので switch や配列添字に使える。
    //  - 実行ごとに値が変わらないため、そのまま保存しても壊れない。
    //  - ただし Scene ファイルへは「読める型名」を主キーとして書き出す。
    //    型 ID はランタイムの検索を速くするための副次的なキーに留める。
    //    こうしておくと、後から型名を変えたときに移行しやすい。
    using ComponentTypeID = std::uint32_t;

    inline constexpr ComponentTypeID invalid_component_type_id = 0;

    // FNV-1a 32bit。constexpr で評価できる範囲に収めてある。
    constexpr ComponentTypeID MakeComponentTypeID(std::string_view name) noexcept
    {
        std::uint32_t hash = 2166136261u;
        for (const char character : name)
        {
            hash ^= static_cast<std::uint32_t>(static_cast<unsigned char>(character));
            hash *= 16777619u;
        }
        // 0 は「無効」の予約値なので衝突したらずらす。
        return hash == invalid_component_type_id ? 1u : hash;
    }
}

// Component の派生クラスへ型情報を埋め込むマクロ。
//
// クラス本体の先頭（開き波括弧の直後）へ置くこと。展開後のアクセス指定は public のまま残るため、
// 続けて public: / private: を明示的に書くこと。
//
//   class HealthComponent final : public ReplayEngine::Core::Component
//   {
//       REPLAY_COMPONENT_BODY(HealthComponent)
//   public:
//       ...
//   };
#define REPLAY_COMPONENT_BODY(TypeName_)                                              \
public:                                                                               \
    static constexpr const char* StaticTypeName() noexcept { return #TypeName_; }     \
    static constexpr ::ReplayEngine::Core::ComponentTypeID StaticTypeID() noexcept    \
    {                                                                                 \
        return ::ReplayEngine::Core::MakeComponentTypeID(#TypeName_);                 \
    }                                                                                 \
    ::ReplayEngine::Core::ComponentTypeID TypeID() const noexcept override            \
    {                                                                                 \
        return StaticTypeID();                                                        \
    }                                                                                 \
    const char* TypeName() const noexcept override                                    \
    {                                                                                 \
        return StaticTypeName();                                                      \
    }
