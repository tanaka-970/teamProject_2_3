#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace ReplayEngine::Core
{
    // Scene 内で GameObject を一意に識別する永続 ID。
    //
    // 設計方針:
    //  - 実行時ポインタや配列インデックスは Scene ファイルへ保存しない。永続参照は必ずこの ID を使う。
    //  - 保存済みIDとの互換性を保つため64bitを使用する。
    //  - 0 は「無効」を表す予約値。GameObject へ割り当てられることはない。
    //  - 値型なのでコピー・比較・ハッシュが安全に行える。生ポインタの代わりに気軽に持ち回してよい。
    class ObjectID final
    {
    public:
        using ValueType = std::uint64_t;

        static constexpr ValueType invalid_value = 0;

        constexpr ObjectID() noexcept = default;
        constexpr explicit ObjectID(ValueType value) noexcept : value_(value) {}

        constexpr ValueType Value() const noexcept { return value_; }
        constexpr bool Valid() const noexcept { return value_ != invalid_value; }
        constexpr explicit operator bool() const noexcept { return Valid(); }

        constexpr bool operator==(const ObjectID& other) const noexcept { return value_ == other.value_; }
        constexpr bool operator!=(const ObjectID& other) const noexcept { return value_ != other.value_; }
        constexpr bool operator<(const ObjectID& other) const noexcept { return value_ < other.value_; }

        // Scene ファイルとログ向けの文字列化。無効値は "0" になる。
        std::string ToString() const;

        // 文字列から復元する。解析できない場合は false を返し out は変更しない。
        static bool TryParse(const std::string& text, ObjectID& out) noexcept;

        static constexpr ObjectID Invalid() noexcept { return ObjectID{}; }

    private:
        ValueType value_ = invalid_value;
    };

    // ObjectID の採番器。Scene が 1 つ所有する。
    //
    // Scene 読み込み時は保存されていた ID をそのまま復元したうえで EnsureAbove() を呼び、
    // 次に採番される ID が既存 ID と衝突しないようにする。
    class ObjectIDGenerator final
    {
    public:
        ObjectID Next() noexcept
        {
            return ObjectID{ next_++ };
        }

        // 復元した ID より必ず大きい値が次に出るようにする。
        void EnsureAbove(ObjectID id) noexcept
        {
            if (id.Value() >= next_) next_ = id.Value() + 1;
        }

        void Reset() noexcept { next_ = 1; }

        ObjectID::ValueType Peek() const noexcept { return next_; }

    private:
        ObjectID::ValueType next_ = 1;
    };
}

namespace std
{
    template<>
    struct hash<ReplayEngine::Core::ObjectID>
    {
        std::size_t operator()(const ReplayEngine::Core::ObjectID& id) const noexcept
        {
            return std::hash<ReplayEngine::Core::ObjectID::ValueType>{}(id.Value());
        }
    };
}
