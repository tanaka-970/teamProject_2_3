#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

namespace ReplayEngine::Runtime
{
    // Runtime API の失敗理由。
    //
    // 方針:
    //   - 利用者のデータ不備 (壊れた参照・消えた Asset) で assert / クラッシュさせない。
    //     Script から呼ばれる API なので、間違った Handle を渡されるのは異常ではなく通常。
    //   - 例外を投げない。将来 C ABI 境界を作るときに例外を通せないため、
    //     最初から「戻り値で失敗を返す」ことに統一しておく。
    //   - 明示的な整数値を振る。C# 側の enum とそのまま突き合わせられるようにするため、
    //     一度決めた値は変更しない。追加は末尾へ足す。
    enum class RuntimeStatus : std::int32_t
    {
        Ok = 0,

        // ---- Handle ------------------------------------------------------
        InvalidHandle = 1,   // 空の Handle、または未初期化の Handle
        WrongWorld = 2,   // 別の World で作られた Handle
        ObjectDestroyed = 3,   // GameObject が破棄済み、または削除予約済み
        ComponentDestroyed = 4,   // Component が破棄済み、または削除予約済み
        ComponentNotFound = 5,   // 指定した型 / ID の Component が無い
        TypeMismatch = 6,   // 期待した型と実際の型が違う

        // ---- Asset -------------------------------------------------------
        AssetMissing = 7,
        InvalidAssetType = 8,

        // ---- Scene -------------------------------------------------------
        SceneMissing = 9,
        SceneLoadFailed = 10,
        TransitionInProgress = 11,

        // ---- 一般 --------------------------------------------------------
        UnsupportedOperation = 12,
        ServiceUnavailable = 13,   // 未実装 / 未接続の Service を呼んだ
        InvalidArgument = 14,
        DeferredOperationRejected = 15,

        // ---- Runtime Service / Component ---------------------------------
        SaveSlotNotFound = 16,
        SaveKeyNotFound = 17,
        SaveTypeMismatch = 18,
        SaveCorrupt = 19,
        SaveIOFailure = 20,
        ComponentDependencyMissing = 21,
        ComponentHasDependents = 22,
    };

    // ログ・Inspector 表示用。翻訳せず enum 名をそのまま返す（機械可読なまま保つ）。
    const char* ToString(RuntimeStatus status) noexcept;

    // 日本語の説明。Editor の表示用。
    const char* DescribeJapanese(RuntimeStatus status) noexcept;

    // 将来の C ABI 用。enum の値をそのまま整数として渡せることを明示するための関数。
    constexpr std::int32_t ToErrorCode(RuntimeStatus status) noexcept
    {
        return static_cast<std::int32_t>(status);
    }

    constexpr bool Succeeded(RuntimeStatus status) noexcept
    {
        return status == RuntimeStatus::Ok;
    }

    constexpr bool Failed(RuntimeStatus status) noexcept
    {
        return status != RuntimeStatus::Ok;
    }

    // 値を返す Runtime API の戻り値。
    //
    // これは C++ 側の呼び出しを短く書くための入れ物であり、ABI 境界へは出さない。
    // C ABI を足すときは「戻り値 = RuntimeStatus、結果 = 出力引数」の形へ落とす。
    // どちらの形でも同じ RuntimeStatus を使うので、意味がずれることはない。
    //
    // 失敗した RuntimeResult から Value() を読んでも未定義動作にはならない。
    // T の既定値が返るだけなので、Status() を確かめ忘れてもクラッシュしない。
    template<class T>
    class RuntimeResult final
    {
        static_assert(std::is_default_constructible_v<T>,
            "RuntimeResult<T> は失敗時に既定値を返すため、T は既定構築できる必要があります");

    public:
        RuntimeResult() noexcept = default;

        static RuntimeResult Success(T value)
        {
            RuntimeResult result;
            result.status_ = RuntimeStatus::Ok;
            result.value_ = std::move(value);
            return result;
        }

        static RuntimeResult Failure(RuntimeStatus status) noexcept
        {
            RuntimeResult result;
            // Ok を渡された場合でも「失敗」として扱わないよう、そのまま入れる。
            // 呼び出し側の書き間違いを黙って成功へ倒さない。
            result.status_ = status;
            return result;
        }

        RuntimeStatus Status() const noexcept { return status_; }
        bool Succeeded() const noexcept { return status_ == RuntimeStatus::Ok; }
        bool Failed() const noexcept { return status_ != RuntimeStatus::Ok; }

        explicit operator bool() const noexcept { return Succeeded(); }

        const T& Value() const noexcept { return value_; }
        T ValueOr(T fallback) const { return Succeeded() ? value_ : std::move(fallback); }

    private:
        // 既定は InvalidArgument。既定構築しただけの RuntimeResult を
        // 「成功」と読み間違えないようにするため、Ok を既定値にしない。
        RuntimeStatus status_ = RuntimeStatus::InvalidArgument;
        T value_{};
    };
}
