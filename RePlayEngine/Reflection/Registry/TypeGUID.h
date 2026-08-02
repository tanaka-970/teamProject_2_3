#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>

namespace ReplayEngine::Reflection
{
    // Component / Behaviour の型を永続的に識別する 128bit の値。
    //
    // ComponentTypeID との違い:
    //   ComponentTypeID … 型名から計算する 32bit ハッシュ。実行時の検索用。
    //                     型名を変えると値が変わるので、永続 ID には使えない。
    //   TypeGUID        … 型ごとに人が 1 回だけ決める固定値。実行時の計算に依存しない。
    //                     クラス名・名前空間・ファイル名・ファイル位置を変えても不変。
    //
    // なぜ C++ のクラス名を永続 ID にしないか:
    //   将来 C# Script を載せたとき、Visual Studio でのクラス名変更・
    //   名前空間変更・ファイル移動が日常操作になる。
    //   クラス名を主キーにしていると、そのたびに Scene / Prefab の参照が切れる。
    //   GUID を主キーにしておけば、名前は表示用の属性でしかなくなる。
    //
    // 保存形式:
    //   32 文字の小文字 16 進文字列（ハイフンなし）。
    //   AssetGUID と同じ表記に揃えてあるので、Editor の表示処理を使い回せる。
    //
    // 型の性質:
    //   trivially copyable な POD。将来 C ABI へそのまま渡せる。
    struct TypeGUID final
    {
        std::uint64_t high = 0;
        std::uint64_t low = 0;

        constexpr bool IsValid() const noexcept { return high != 0 || low != 0; }

        constexpr bool operator==(const TypeGUID& other) const noexcept
        {
            return high == other.high && low == other.low;
        }
        constexpr bool operator!=(const TypeGUID& other) const noexcept
        {
            return !(*this == other);
        }
        // 並べ替え可能にしておく。Validation の重複検出で使う。
        constexpr bool operator<(const TypeGUID& other) const noexcept
        {
            return high != other.high ? high < other.high : low < other.low;
        }

        static constexpr TypeGUID Invalid() noexcept { return TypeGUID{}; }

        // 32 文字の小文字 16 進。無効値は 32 個の '0' を返す。
        std::string ToString() const;

        // 32 文字 16 進からの復元。大文字・ハイフン入りも受け付ける。
        // 解析できない場合は false を返し、out は変更しない。
        static bool TryParse(std::string_view text, TypeGUID& out) noexcept;
    };

    static_assert(std::is_trivially_copyable_v<TypeGUID>,
        "TypeGUID は将来 C ABI へ出すため trivially copyable を保つこと");
    static_assert(std::is_standard_layout_v<TypeGUID>,
        "TypeGUID は将来 C ABI へ出すため standard layout を保つこと");

    namespace Detail
    {
        // constexpr な 16 進 1 文字の変換。不正文字は 0xFF を返す。
        constexpr std::uint8_t HexDigitValue(char character) noexcept
        {
            if (character >= '0' && character <= '9')
                return static_cast<std::uint8_t>(character - '0');
            if (character >= 'a' && character <= 'f')
                return static_cast<std::uint8_t>(character - 'a' + 10);
            if (character >= 'A' && character <= 'F')
                return static_cast<std::uint8_t>(character - 'A' + 10);
            return 0xFFu;
        }
    }

    // ソースへ直接書く用。32 文字の 16 進からコンパイル時に TypeGUID を作る。
    //
    //   static constexpr Reflection::TypeGUID StaticTypeGUID() noexcept
    //   {
    //       return Reflection::MakeTypeGUID("6f1c0a4b9d2e47c8a10f3b5d8e2c7194");
    //   }
    //
    // 不正な文字列を渡した場合は無効値 (0,0) になる。
    // 無効な GUID は ComponentRegistry の登録時に弾かれるので、
    // 書き間違いが黙って通ることはない。
    constexpr TypeGUID MakeTypeGUID(std::string_view hex) noexcept
    {
        if (hex.size() != 32) return TypeGUID{};

        std::uint64_t high = 0;
        std::uint64_t low = 0;
        for (std::size_t index = 0; index < 32; ++index)
        {
            const std::uint8_t digit = Detail::HexDigitValue(hex[index]);
            if (digit == 0xFFu) return TypeGUID{};

            if (index < 16) high = (high << 4) | digit;
            else            low = (low << 4) | digit;
        }

        TypeGUID result;
        result.high = high;
        result.low = low;
        return result;
    }
}

namespace std
{
    template<>
    struct hash<ReplayEngine::Reflection::TypeGUID>
    {
        std::size_t operator()(const ReplayEngine::Reflection::TypeGUID& guid) const noexcept
        {
            std::size_t seed = std::hash<std::uint64_t>{}(guid.high);
            seed ^= std::hash<std::uint64_t>{}(guid.low) +
                0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
}
