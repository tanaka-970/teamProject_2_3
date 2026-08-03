#pragma once

#include "ScriptLanguage.h"
#include "../../Core/ObjectID/ObjectID.h"
#include "../../Core/ObjectID/RuntimeIdentity.h"
#include "../../Reflection/Registry/TypeGUID.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace ReplayEngine::Scripting
{
    // ---- ScriptTypeID ------------------------------------------------------
    //
    // ユーザーが書いた 1 つのスクリプト型を永続的に識別する値。
    //
    // 【なぜ Reflection::TypeGUID をそのまま使うか】
    //   第二の ID 体系を作らないため。TypeGUID は既に次を満たしている。
    //     - 128bit の POD（trivially copyable / standard layout を static_assert 済み）
    //     - std::hash の特殊化がある
    //     - ToString() が AssetGUID とまったく同じ 32 桁小文字 16 進
    //     - TryParse() がハイフン入り・大文字も受け付ける
    //   新しい構造体を起こしても、これらを全部書き直すだけになる。
    //
    // 【ObjectID / ComponentTypeID との違い】
    //   ObjectID        … Scene 内の GameObject 実体を指す。ScriptTypeID は何も指さない
    //   ComponentTypeID … C++ の Component 型。ScriptComponent は 1 つだけ持つ
    //   ScriptTypeID    … スクリプトアセット（+ C# ならクラス）の識別子
    //
    //   ScriptTypeID を ComponentRegistry へ登録することはしない。
    //   GameObject が所有する実体は常に ScriptComponent 1 種のままにする。
    //
    // 【用途】
    //   Add Component の表示 / Field Schema の対応付け / Reload 対象の特定 /
    //   エラー情報の識別。これ以外に使わない。
    using ScriptTypeID = Reflection::TypeGUID;

    inline constexpr ScriptTypeID InvalidScriptTypeID() noexcept
    {
        return Reflection::TypeGUID::Invalid();
    }

    // Lua 用の導出。Script Asset の GUID をそのまま ScriptTypeID として読む。
    //
    // ハッシュを噛ませない理由:
    //   AssetGUID は既に 32 桁 16 進で、TypeGUID とビット幅も表記も同じ。
    //   そのまま読めば可逆で、衝突も起こらず、ログを見た人が
    //   「どの Asset のことか」を目で追える。
    //
    // Lua は 1 ファイル 1 モジュールなので、Asset だけで一意になる。
    // 解析できない GUID 文字列を渡された場合は無効値を返す。
    ScriptTypeID MakeLuaScriptTypeID(std::string_view asset_guid) noexcept;

    // C# 用の導出。Script Asset の GUID と完全修飾クラス名から作る。
    //
    // Asset だけで足りない理由:
    //   1 つの .cs へ複数の ScriptBehaviour 派生クラスを書けるため。
    //   Asset だけを鍵にすると、同じファイル内の 2 つのクラスが同じ ID になる。
    //
    // 導出は "<asset_guid>#<Namespace.Class>" の FNV-1a 128bit。
    // 実行ごとに値が変わらないので、そのまま Scene へ保存できる。
    // どちらかが空なら無効値を返す。
    ScriptTypeID MakeCSharpScriptTypeID(std::string_view asset_guid,
        std::string_view full_class_name) noexcept;

    // 言語で振り分ける入口。呼び出し側に if を散らさないために置く。
    ScriptTypeID MakeScriptTypeID(ScriptLanguage language,
        std::string_view asset_guid, std::string_view full_class_name) noexcept;

    // ---- ScriptInstanceHandle ----------------------------------------------
    //
    // Backend が管理するスクリプトインスタンスへの参照。
    //
    // 整数の別名にとどめているのは、C++ / Lua / C# の境界をまたぐため。
    // Lua State ポインタも managed オブジェクト参照も、この外側へは出さない。
    //
    // 0 は「未生成」を表す予約値。再利用しないので、
    // 破棄したインスタンスの Handle が後から別の実体を掴むことはない。
    using ScriptInstanceHandle = std::uint64_t;

    inline constexpr ScriptInstanceHandle invalid_script_instance_handle = 0;

    // ---- ライフサイクル Callback -------------------------------------------
    //
    // ユーザーが書く関数の種類。名前は Lua / C# で共通。
    //
    // OnCreate は採用しない。既存 Component の OnRuntimeAwake が
    // 「プロパティ反映後・参照解決後の最初の同期点」を表しており、
    // それがそのまま Awake の意味になるため、2 つに分ける理由が無い。
    enum class ScriptCallback : std::int32_t
    {
        Awake = 0,
        OnEnable,
        Start,
        FixedUpdate,
        Update,
        LateUpdate,
        OnDisable,
        OnDestroy,
    };

    inline constexpr int script_callback_count = 8;

    // Lua の関数名 / C# のメソッド名。そのまま検索に使う。
    const char* ToString(ScriptCallback callback) noexcept;

    // 引数に delta time を取る Callback かどうか。
    // Backend が引数を積むかどうかの判断に使う。
    bool ScriptCallbackTakesDeltaTime(ScriptCallback callback) noexcept;

    // ---- 実行結果 ----------------------------------------------------------

    enum class ScriptStatus : std::int32_t
    {
        // まだ何もしていない。Script Asset 未設定の初期状態。
        Unassigned = 0,

        // Script Type は指定されているが、Schema がまだ解決できていない。
        // Lua ファイルが無い / C# が Compile 前 / Edit Mode で Backend 未起動、など。
        // このとき Field 値は預かったまま失われない。
        Unresolved,

        // Schema の解決に成功した。まだインスタンスは無い（Edit Mode）。
        Loaded,

        // インスタンスが生成され、Callback を受け取れる状態。
        Running,

        // 読み込みまたは実行でエラーが起きた。エンジンは止めない。
        Error,
    };

    const char* ToString(ScriptStatus status) noexcept;

    // Callback 呼び出しの結果。
    //
    // 例外や Lua エラーをここで受け止め、C++ の境界の外へ漏らさない。
    enum class ScriptInvokeResult : std::int32_t
    {
        // 正常に呼べた。
        Ok = 0,

        // その Callback をスクリプトが定義していない。エラーではない。
        NotImplemented,

        // インスタンスが無い（未生成 / 破棄済み）。
        NoInstance,

        // Backend が停止中（Play セッション外 / Shutdown 済み）。
        BackendUnavailable,

        // スクリプト内で実行時エラーが起きた。詳細は ScriptError が持つ。
        RuntimeError,
    };

    const char* ToString(ScriptInvokeResult result) noexcept;

    inline bool ScriptInvokeSucceeded(ScriptInvokeResult result) noexcept
    {
        // NotImplemented は失敗ではない。
        // 空の Callback を書かなくてよいのが Unity 型の前提であり、
        // 「定義されていない」ことをエラー扱いすると毎フレーム警告が出る。
        return result == ScriptInvokeResult::Ok ||
            result == ScriptInvokeResult::NotImplemented;
    }

    // ---- Callback の引数 ----------------------------------------------------
    //
    // 現時点で必要なのは delta time だけ。
    // 構造体にしてあるのは、将来 Trigger / Collision を通すときに
    // IScriptBackend::Invoke のシグネチャを変えずに済ませるため。
    struct ScriptArguments final
    {
        float delta_time = 0.0f;

        static ScriptArguments None() noexcept { return ScriptArguments{}; }

        static ScriptArguments DeltaTime(float value) noexcept
        {
            ScriptArguments arguments;
            arguments.delta_time = value;
            return arguments;
        }
    };

    // ---- インスタンス生成の要求 ---------------------------------------------
    //
    // Backend へ生ポインタを渡さないための入れ物。
    // ObjectID + ComponentStableID があれば、Backend 側は
    // Native API 経由で所有 GameObject を引き直せる。
    struct ScriptInstanceRequest final
    {
        ScriptTypeID type_id;
        Core::ObjectID owner_object;
        Core::ComponentStableID owner_component = Core::invalid_component_stable_id;
    };
}
