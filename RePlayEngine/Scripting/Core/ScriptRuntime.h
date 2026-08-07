#pragma once

#include "ScriptBackend.h"
#include "ScriptError.h"
#include "ScriptServices.h"
#include "ScriptTypeCatalog.h"
#include "ScriptTypes.h"
#include "ScriptWorld.h"
#include "../../Runtime/Scene/WorldLifecycleListener.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ReplayEngine::Scripting
{
    // Lua / C# のスクリプト機構を束ねる。framework が 1 つ所有する。
    //
    // ---------------------------------------------------------------------
    // 【Update / FixedUpdate / LateUpdate を持たない】
    //
    //   持つと「Scene が回す Component」と「Runtime が回すスクリプト」の
    //   2 経路ができる。どちらが先か、削除予約はどちらが見るか、
    //   Play 停止でどちらが止まるか、が食い違ってそのままバグになる。
    //   （BehaviourComponent.h が同じ理由で専用更新経路を禁じている。）
    //
    //   更新は Scene::Update -> ScriptComponent::OnUpdate -> Backend::Invoke の
    //   1 本だけ。ScriptRuntime はその途中に入らない。
    //
    // ---------------------------------------------------------------------
    // 【責務】
    //
    //   - IScriptBackend の所有（Lua / C#）
    //   - ScriptTypeCatalog の所有（Play をまたいで生存する Schema キャッシュ）
    //   - ScriptWorld の所有（Play セッション単位）
    //   - IWorldLifecycleListener の実装（Scene::Start の前に Play Session を用意する）
    //   - Schema 差し替えの同期点
    //   - エラーの集約と抑制
    //
    // ---------------------------------------------------------------------
    // 【破棄順】
    //
    //   ScriptWorld -> Backend -> Catalog の順で壊す。
    //   framework 側では object_runtime_scenes（World の所有者）より
    //   「後」に壊すこと。World の破棄中に OnRuntimeDestroy が走り、
    //   そこから Backend を触るため。
    class ScriptRuntime final
        : public IScriptServices
        , public Runtime::IWorldLifecycleListener
    {
    public:
        ScriptRuntime();
        ~ScriptRuntime() override;

        ScriptRuntime(const ScriptRuntime&) = delete;
        ScriptRuntime& operator=(const ScriptRuntime&) = delete;

        // ---- 起動・終了 -------------------------------------------------------

        bool Initialize();
        void Shutdown();
        bool Initialized() const noexcept { return initialized_; }

        // ---- Backend ----------------------------------------------------------
        //
        // Phase 1 では Mock だけを挿す。Phase 2 以降で Lua / C# が入る。
        // 所有権はここへ移る。
        void InstallBackend(std::unique_ptr<IScriptBackend> backend);

        IScriptBackend* Backend(ScriptLanguage language) const noexcept;

        // ---- 目録 -------------------------------------------------------------

        ScriptTypeCatalog& Catalog() noexcept { return catalog_; }
        const ScriptTypeCatalog& Catalog() const noexcept override { return catalog_; }

        // 型を目録へ入れ、対応する Backend で読み込んで Schema を作る。
        //
        // 失敗しても目録の既存 Schema は捨てない（最後に成功した版を維持する）。
        // 戻り値は読み込みの成否。失敗理由は Descriptor の last_error に残る。
        bool RegisterScriptType(ScriptTypeDescriptor descriptor);

        // 目録にある型の Schema を作り直す要求を積む。
        // 実際の差し替えは ApplyPendingSchemaSwaps() まで起きない。
        void RequestSchemaReload(ScriptTypeID type_id);

        // ---- 同期点 -----------------------------------------------------------
        //
        // フレーム先頭（World 入れ替えの安全点）で 1 回だけ呼ぶ。
        //
        // ここでやること:
        //   1. 積まれた Schema 差し替え要求を処理する
        //   2. 差し替わった型を使っている ScriptComponent へ新しい Schema を配る
        //      （値の移送は ScriptComponent::BindSchema が行う）
        //   3. 診断用の時計を進める
        //
        // ライフサイクル Callback は 1 つも呼ばない。
        // Component の有効・無効も削除予約も見ない。
        // したがって「第二の更新経路」にはあたらない。
        //
        // Inspector 描画中や Serialize 中には絶対に呼ばないこと。
        // DynamicProperties() が返した配列の足元が崩れる。
        void ApplyPendingSchemaSwaps(float delta_time);

        // ---- Play セッション ---------------------------------------------------

        ScriptWorld* World() noexcept { return world_.get(); }
        const ScriptWorld* World() const noexcept { return world_.get(); }

        // IWorldLifecycleListener
        void OnWorldBuilding(Scene::Scene& world) override;
        void OnWorldUnloading(Scene::Scene& world) override;
        void OnWorldUnloaded(Scene::Scene& world) override;
        void OnWorldActivating(Scene::Scene& world) override;

        // ---- IScriptServices ---------------------------------------------------

        ScriptFieldSchemaRef ResolveSchema(ScriptTypeID type) const override;
        std::string ResolveDisplayName(ScriptTypeID type) const override;

        bool PlaySessionActive() const noexcept override;

        void RegisterComponent(ScriptComponent& component) override;
        void UnregisterComponent(ScriptComponent& component) override;

        ScriptInstanceHandle CreateInstance(const ScriptInstanceRequest& request) override;
        void DestroyInstance(ScriptInstanceHandle instance) override;

        ScriptInvokeResult Invoke(ScriptInstanceHandle instance,
            ScriptCallback callback, const ScriptArguments& arguments) override;

        bool PushField(ScriptInstanceHandle instance,
            const std::string& saved_name, const ScriptValue& value) override;

        bool PullField(ScriptInstanceHandle instance,
            const std::string& saved_name, ScriptValue& out) const override;

        void ReportError(const ScriptErrorRecord& record) override;

        // ---- 診断 --------------------------------------------------------------

        const ScriptErrorLog& Errors() const noexcept { return errors_; }
        ScriptErrorLog& Errors() noexcept { return errors_; }

        // 出力すべきエラー文字列を取り出して空にする。
        // framework がログへ流す。ここでは std::printf も imgui も呼ばない。
        std::vector<std::string> DrainPendingLogLines();

        // Play セッションが終わったときに残っていたインスタンス数。
        // 0 でなければ解放漏れ。Validation が見る。
        std::size_t LastLeakedInstanceCount() const noexcept { return last_leaked_instances_; }

        std::uint64_t SessionGeneration() const noexcept { return session_generation_; }

    private:
        // Handle からどの Backend の物かを引く。
        //
        // Handle 自体に言語を埋め込む方式は採らない。
        // 埋め込むとビット幅の割り当てが将来の制約になるうえ、
        // Handle を「ただの整数」として扱えなくなる。
        IScriptBackend* BackendOf(ScriptInstanceHandle instance) const noexcept;

        void DestroyWorld();

        std::vector<std::unique_ptr<IScriptBackend>> backends_;
        ScriptTypeCatalog catalog_;
        std::unique_ptr<ScriptWorld> world_;

        // 差し替え待ちの型。ApplyPendingSchemaSwaps が空にする。
        std::vector<ScriptTypeID> pending_reloads_;

        // Handle -> Backend の対応。DestroyInstance / Invoke の振り分けに使う。
        struct InstanceOwner
        {
            ScriptInstanceHandle handle = invalid_script_instance_handle;
            IScriptBackend* backend = nullptr;
        };
        std::vector<InstanceOwner> instance_owners_;

        ScriptErrorLog errors_;
        std::vector<std::string> pending_log_lines_;

        double clock_seconds_ = 0.0;
        std::uint64_t session_generation_ = 0;
        std::size_t last_leaked_instances_ = 0;

        bool initialized_ = false;
    };
}
