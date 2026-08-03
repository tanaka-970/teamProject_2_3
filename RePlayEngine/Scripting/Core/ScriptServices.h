#pragma once

#include "ScriptFieldSchema.h"
#include "ScriptTypes.h"

#include <string>

namespace ReplayEngine::Scripting
{
    class ScriptComponent;
    struct ScriptErrorRecord;

    // ScriptComponent が ScriptRuntime を触るための細い口。
    //
    // ---------------------------------------------------------------------
    // 【なぜ Singleton にしないか】
    //
    //   実体は framework が unique_ptr で 1 つ持つ。
    //   Component からの参照経路は、既存の Runtime API とまったく同じにする。
    //
    //     Component -> GetScene() -> Services() -> Scripts()
    //
    //   未接続 (nullptr) がありうるのは意図的で、Editor で Scene を
    //   編集しているだけの状態では接続しない。
    //   「置いただけでスクリプトが動き出す」ことを構造的に防ぐ。
    //
    // ---------------------------------------------------------------------
    // 【ここに置かないもの】
    //
    //   Lua State / managed オブジェクト / Backend の具象型は 1 つも出さない。
    //   やり取りするのは ScriptTypeID・整数 Handle・PropertyValue だけ。
    class IScriptServices
    {
    public:
        virtual ~IScriptServices() = default;

        // ---- 型と Schema ---------------------------------------------------

        // Catalog から Schema を引く。未登録・未解決なら空の shared_ptr。
        // 呼び出し側は「まだ Schema が無い」を正常状態として扱うこと。
        virtual ScriptFieldSchemaRef ResolveSchema(ScriptTypeID type) const = 0;

        // Inspector ヘッダーに出す名前（"Rotating Object"）。未登録なら空。
        virtual std::string ResolveDisplayName(ScriptTypeID type) const = 0;

        // ---- Play セッション ------------------------------------------------

        // インスタンスを作ってよい状態か。Edit Mode では false。
        virtual bool PlaySessionActive() const noexcept = 0;

        // ScriptWorld への出入り。ScriptComponent が自分で申告する。
        // Runtime 側から World を走査して集めることはしない。
        virtual void RegisterComponent(ScriptComponent& component) = 0;
        virtual void UnregisterComponent(ScriptComponent& component) = 0;

        // ---- インスタンス ---------------------------------------------------

        virtual ScriptInstanceHandle CreateInstance(const ScriptInstanceRequest& request) = 0;
        virtual void DestroyInstance(ScriptInstanceHandle instance) = 0;

        virtual ScriptInvokeResult Invoke(ScriptInstanceHandle instance,
            ScriptCallback callback, const ScriptArguments& arguments) = 0;

        // Inspector / 保存値をインスタンスへ流し込む。名前は保存名（"field.X"）。
        virtual bool PushField(ScriptInstanceHandle instance,
            const std::string& saved_name, const ScriptValue& value) = 0;

        // インスタンス側で変わった値を読み戻す。実行中の値を Inspector へ出すのに使う。
        virtual bool PullField(ScriptInstanceHandle instance,
            const std::string& saved_name, ScriptValue& out) const = 0;

        // ---- 診断 ------------------------------------------------------------

        virtual void ReportError(const ScriptErrorRecord& record) = 0;
    };
}
