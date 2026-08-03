#pragma once

#include "ScriptFieldSchema.h"
#include "ScriptTypeCatalog.h"
#include "ScriptTypes.h"

#include <cstddef>
#include <string>

namespace ReplayEngine::Scripting
{
    // 型の読み込み結果。
    //
    // 「読めなかった」を例外ではなく値で返すのは、
    // Compile 失敗や構文エラーが日常的に起きる前提だから。
    // 例外にすると呼び出し側が全部 try で囲むことになる。
    struct ScriptLoadResult final
    {
        bool succeeded = false;
        ScriptFieldSchemaRef schema;
        std::string error_message;
        std::string file;
        int line = 0;

        static ScriptLoadResult Success(ScriptFieldSchemaRef value)
        {
            ScriptLoadResult result;
            result.succeeded = true;
            result.schema = std::move(value);
            return result;
        }

        static ScriptLoadResult Failure(std::string message,
            std::string source_file = std::string(), int source_line = 0)
        {
            ScriptLoadResult result;
            result.succeeded = false;
            result.error_message = std::move(message);
            result.file = std::move(source_file);
            result.line = source_line;
            return result;
        }
    };

    // Lua / C# の実行 Backend。
    //
    // ---------------------------------------------------------------------
    // 【IBehaviourProvider とは役割が違う】
    //
    //   IBehaviourProvider … C++ / C# の Behaviour「型」の供給元。
    //                        Component の実体を作る。今回は一切変更しない。
    //   IScriptBackend     … スクリプトの「実行系」。
    //                        Component は作らない。作るのは Lua / C# 側の
    //                        インスタンスと、それを指す整数 Handle だけ。
    //
    //   GameObject が所有する実体は、どちらの言語でも ScriptComponent 1 種。
    //   Backend が Component を所有することは無い。
    //
    // ---------------------------------------------------------------------
    // 【外へ出してはいけないもの】
    //
    //   lua_State* / managed オブジェクト参照 / GCHandle / 関数ポインタ。
    //   このインターフェイスの引数と戻り値は、整数・文字列・PropertyValue だけ。
    //
    // ---------------------------------------------------------------------
    // 【例外について】
    //
    //   Lua のエラーも C# の例外も、この境界の内側で必ず止める。
    //   外へ投げると、Scene の走査中に巻き戻しが起きて
    //   Component コンテナが壊れた状態のまま残る。
    class IScriptBackend
    {
    public:
        virtual ~IScriptBackend() = default;

        virtual ScriptLanguage Language() const noexcept = 0;
        virtual const char* BackendName() const noexcept = 0;

        virtual bool Initialize() = 0;
        virtual void Shutdown() = 0;
        virtual bool Initialized() const noexcept = 0;

        // ---- 型 --------------------------------------------------------------

        // スクリプトを読み込み、構文検証して Schema を作る。インスタンスは作らない。
        //
        // 失敗しても既存の状態を壊さないこと。
        // 「新しいファイルにエラーがあったら、最後に正常動作した版を維持する」
        // という要求（指示書 8.4 / 9.6）は、Catalog 側が
        // 失敗時に schema を差し替えないことで満たす。
        virtual ScriptLoadResult LoadType(const ScriptTypeDescriptor& descriptor,
            std::uint32_t schema_revision) = 0;

        // その型を今インスタンス化できるか。
        // 「登録されている」と「今作れる」を分けるのは IBehaviourProvider と同じ理由。
        virtual bool CanInstantiate(ScriptTypeID type_id) const = 0;

        // ---- インスタンス -----------------------------------------------------

        // 作れなければ invalid_script_instance_handle を返す。例外は投げない。
        virtual ScriptInstanceHandle CreateInstance(const ScriptInstanceRequest& request) = 0;

        virtual void DestroyInstance(ScriptInstanceHandle instance) = 0;

        virtual ScriptInvokeResult Invoke(ScriptInstanceHandle instance,
            ScriptCallback callback, const ScriptArguments& arguments) = 0;

        virtual bool SetField(ScriptInstanceHandle instance,
            const std::string& saved_name, const ScriptValue& value) = 0;

        virtual bool GetField(ScriptInstanceHandle instance,
            const std::string& saved_name, ScriptValue& out) const = 0;

        // ---- 診断 -------------------------------------------------------------

        // 生存インスタンス数。ScriptWorld を捨てるときの漏れ検証に使う。
        // Lua Registry 参照や GCHandle が残っていれば、ここが 0 にならない。
        virtual std::size_t LiveInstanceCount() const noexcept = 0;

        // 直近の失敗内容。Invoke が RuntimeError を返した直後に読む。
        virtual const std::string& LastErrorMessage() const noexcept = 0;
        virtual const std::string& LastErrorFile() const noexcept = 0;
        virtual int LastErrorLine() const noexcept = 0;
    };
}
