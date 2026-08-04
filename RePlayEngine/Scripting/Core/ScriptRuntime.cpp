#include "ScriptRuntime.h"

#include "ScriptComponent.h"
#include "../../Scene/Runtime/Scene.h"

#include <algorithm>
#include <utility>

namespace ReplayEngine::Scripting
{
    ScriptRuntime::ScriptRuntime() = default;

    ScriptRuntime::~ScriptRuntime()
    {
        Shutdown();
    }

    // -----------------------------------------------------------------------
    // 起動・終了
    // -----------------------------------------------------------------------

    bool ScriptRuntime::Initialize()
    {
        if (initialized_) return true;

        bool all_ready = true;
        for (const std::unique_ptr<IScriptBackend>& backend : backends_)
        {
            if (backend && !backend->Initialize()) all_ready = false;
        }

        initialized_ = true;
        return all_ready;
    }

    void ScriptRuntime::Shutdown()
    {
        // World -> Backend -> Catalog の順で壊す。
        // Backend を先に壊すと、World に残ったインスタンスを解放できない。
        DestroyWorld();

        for (const std::unique_ptr<IScriptBackend>& backend : backends_)
        {
            if (backend) backend->Shutdown();
        }
        backends_.clear();

        catalog_.Clear();
        instance_owners_.clear();
        pending_reloads_.clear();
        errors_.Clear();
        pending_log_lines_.clear();

        initialized_ = false;
    }

    void ScriptRuntime::InstallBackend(std::unique_ptr<IScriptBackend> backend)
    {
        if (!backend) return;

        // 同じ言語の Backend が既にあれば差し替える。
        const ScriptLanguage language = backend->Language();
        for (std::unique_ptr<IScriptBackend>& existing : backends_)
        {
            if (!existing || existing->Language() != language) continue;
            existing->Shutdown();
            existing = std::move(backend);
            if (initialized_) existing->Initialize();
            return;
        }

        if (initialized_) backend->Initialize();
        backends_.push_back(std::move(backend));
    }

    IScriptBackend* ScriptRuntime::Backend(ScriptLanguage language) const noexcept
    {
        for (const std::unique_ptr<IScriptBackend>& backend : backends_)
        {
            if (backend && backend->Language() == language) return backend.get();
        }
        return nullptr;
    }

    IScriptBackend* ScriptRuntime::BackendOf(ScriptInstanceHandle instance) const noexcept
    {
        if (instance == invalid_script_instance_handle) return nullptr;
        for (const InstanceOwner& owner : instance_owners_)
        {
            if (owner.handle == instance) return owner.backend;
        }
        return nullptr;
    }

    // -----------------------------------------------------------------------
    // 目録
    // -----------------------------------------------------------------------

    bool ScriptRuntime::RegisterScriptType(ScriptTypeDescriptor descriptor)
    {
        if (!descriptor.type_id.IsValid()) return false;

        const ScriptLanguage language = descriptor.language;
        const ScriptTypeID type_id = descriptor.type_id;

        catalog_.Register(std::move(descriptor));

        IScriptBackend* backend = Backend(language);
        if (backend == nullptr)
        {
            catalog_.ReplaceSchema(type_id, ScriptFieldSchemaRef{}, ScriptStatus::Unresolved,
                "この言語の Backend が接続されていません。");
            return false;
        }

        const ScriptTypeDescriptor* entry = catalog_.Find(type_id);
        if (entry == nullptr) return false;

        const ScriptLoadResult result = backend->LoadType(*entry, catalog_.NextRevision());
        if (!result.succeeded)
        {
            // 失敗しても既存 Schema は捨てない。
            // 捨てると Inspector から Field が消え、値の対応付けが切れる。
            catalog_.ReplaceSchema(type_id, ScriptFieldSchemaRef{}, ScriptStatus::Error,
                result.error_message);

            ScriptErrorRecord record;
            record.kind = ScriptErrorKind::Load;
            record.language = language;
            record.script_type = type_id;
            record.script_name = entry->script_name;
            record.asset_guid = entry->asset_guid;
            record.class_name = entry->class_name;
            record.message = result.error_message;
            record.file = result.file;
            record.line = result.line;
            ReportError(record);
            return false;
        }

        catalog_.ReplaceSchema(type_id, result.schema, ScriptStatus::Loaded, std::string());
        return true;
    }

    void ScriptRuntime::RequestSchemaReload(ScriptTypeID type_id)
    {
        if (!type_id.IsValid()) return;

        const auto found = std::find(pending_reloads_.begin(), pending_reloads_.end(), type_id);
        if (found != pending_reloads_.end()) return;

        pending_reloads_.push_back(type_id);
    }

    // -----------------------------------------------------------------------
    // 同期点
    // -----------------------------------------------------------------------

    void ScriptRuntime::ApplyPendingSchemaSwaps(float delta_time)
    {
        if (delta_time > 0.0f) clock_seconds_ += static_cast<double>(delta_time);

        if (pending_reloads_.empty()) return;

        // 走査中に RequestSchemaReload が呼ばれても壊れないよう、先に取り出す。
        std::vector<ScriptTypeID> reloads;
        reloads.swap(pending_reloads_);

        for (const ScriptTypeID type_id : reloads)
        {
            const ScriptTypeDescriptor* entry = catalog_.Find(type_id);
            if (entry == nullptr) continue;

            IScriptBackend* backend = Backend(entry->language);
            if (backend == nullptr) continue;

            const ScriptLoadResult result = backend->LoadType(*entry, catalog_.NextRevision());
            if (!result.succeeded)
            {
                catalog_.ReplaceSchema(type_id, ScriptFieldSchemaRef{}, ScriptStatus::Error,
                    result.error_message);

                ScriptErrorRecord record;
                record.kind = ScriptErrorKind::Load;
                record.language = entry->language;
                record.script_type = type_id;
                record.script_name = entry->script_name;
                record.asset_guid = entry->asset_guid;
                record.class_name = entry->class_name;
                record.message = result.error_message;
                record.file = result.file;
                record.line = result.line;
                ReportError(record);

                // 最後に成功した Schema のまま動かし続ける。差し替えない。
                continue;
            }

            catalog_.ReplaceSchema(type_id, result.schema, ScriptStatus::Loaded, std::string());
        }

        // 差し替わった Schema を、使っている ScriptComponent へ配る。
        //
        // ここで Callback は 1 つも呼ばない。値の移送だけを行う。
        //
        // 配布先は「Play セッションに登録済みの Component」だけ。
        // 編集 Scene の Component は world_ に居ないため、ここでは届かない。
        // 編集側の再解決は Editor が bound_scene_ 経由で行う
        // (ResolveSchemasInScene)。ここで Scene を触らないのは、
        // Play 中の更新経路へ編集 Scene を混ぜないため。
        if (!world_) return;

        // BindSchema の中から Unregister されることは無いが、
        // 念のため複製してから回す。
        const std::vector<ScriptComponent*> snapshot = world_->Components();
        for (ScriptComponent* component : snapshot)
        {
            if (component == nullptr) continue;

            const ScriptTypeID type_id = component->ScriptType();
            const bool affected = std::find(reloads.begin(), reloads.end(), type_id) != reloads.end();
            if (!affected) continue;

            ScriptFieldSchemaRef schema = catalog_.FindSchema(type_id);
            if (!schema) continue;
            if (component->Schema().get() == schema.get()) continue;

            component->BindSchema(std::move(schema));
        }
    }

    // -----------------------------------------------------------------------
    // World ライフサイクル
    // -----------------------------------------------------------------------

    void ScriptRuntime::OnWorldUnloading(Scene::Scene& /*world*/)
    {
        // Scene::Clear() が OnRuntimeDestroy を流す直前。
        // 新しいインスタンス生成だけを止める。既存の後始末はこれから走る。
        if (world_) world_->BeginClosing();
        for (const std::unique_ptr<IScriptBackend>& backend : backends_)
        {
            if (backend) backend->BindRuntimeContext(nullptr);
        }
    }

    void ScriptRuntime::OnWorldUnloaded(Scene::Scene& world)
    {
        // Scene::Clear() が終わった直後。
        // 正しく後始末できていれば、ここで登録は 0 件になっている。
        DestroyWorld();

        // 参照を外すのはここ。Clear() より前に外すと、
        // OnRuntimeDestroy から Services().Scripts() が引けなくなり、
        // ユーザーの OnDestroy もインスタンスの解放も走らない。
        world.Services().SetScripts(nullptr);
    }

    void ScriptRuntime::OnWorldBuilding(Scene::Scene& world)
    {
        // ApplySceneData が Component を作る「前」。
        //
        // ここで自分を World へ繋いでおかないと、復元中の
        // ScriptComponent::RefreshScriptType() が Services() を引けず、
        // Catalog と照合できないまま Script 型が決まってしまう。
        // その状態で Play へ入ると Schema が解決できず Unresolved で止まる。
        //
        // Play セッション（world_）はまだ作らない。作るのは
        // OnWorldActivating。ここで作るとインスタンスが
        // Scene::Start() より前に生まれてしまう。
        world.Services().SetScripts(this);
    }

    void ScriptRuntime::OnWorldActivating(Scene::Scene& world)
    {
        // Scene::Start() の直前。
        // ここで用意したものは、直後の OnRuntimeAwake から必ず見える。
        //
        // 自分自身を World へ接続するのもここで行う。
        // framework の毎フレーム処理へ任せると、Start() に間に合わない。
        world.Services().SetScripts(this);
        for (const std::unique_ptr<IScriptBackend>& backend : backends_)
        {
            if (backend) backend->BindRuntimeContext(world.Services().Runtime());
        }

        if (world_)
        {
            // 前のセッションが閉じられていない。設計上ここへは来ない。
            pending_log_lines_.push_back(
                "[Script] 前の Play セッションが閉じられていません。強制的に破棄します。");
            DestroyWorld();
        }

        ++session_generation_;
        world_ = std::make_unique<ScriptWorld>(session_generation_);
        errors_.Clear();
    }

    void ScriptRuntime::DestroyWorld()
    {
        if (!world_) return;

        // 残っているインスタンスを数える。
        //
        // ここが 0 でなければ、Lua Registry 参照か GCHandle が
        // 解放されずに残っている。エンジンは止めず、件数を記録して
        // Validation と診断表示から見えるようにする。
        std::size_t leaked = world_->Count();

        for (const std::unique_ptr<IScriptBackend>& backend : backends_)
        {
            if (backend) leaked += backend->LiveInstanceCount();
        }
        last_leaked_instances_ = leaked;

        if (leaked != 0)
        {
            pending_log_lines_.push_back(
                "[Script] Play セッション終了時にスクリプトインスタンスが " +
                std::to_string(leaked) + " 件残っています。");
        }

        // 締めのエラー集計を出す。
        for (std::string& line : errors_.BuildSummary())
        {
            pending_log_lines_.push_back("[Script] " + std::move(line));
        }

        world_.reset();
        instance_owners_.clear();
    }

    // -----------------------------------------------------------------------
    // IScriptServices
    // -----------------------------------------------------------------------

    ScriptFieldSchemaRef ScriptRuntime::ResolveSchema(ScriptTypeID type) const
    {
        return catalog_.FindSchema(type);
    }

    std::string ScriptRuntime::ResolveDisplayName(ScriptTypeID type) const
    {
        return catalog_.FindDisplayName(type);
    }

    bool ScriptRuntime::PlaySessionActive() const noexcept
    {
        return static_cast<bool>(world_) && !world_->Closing();
    }

    void ScriptRuntime::RegisterComponent(ScriptComponent& component)
    {
        if (!world_) return;
        world_->Register(component);
    }

    void ScriptRuntime::UnregisterComponent(ScriptComponent& component)
    {
        if (!world_) return;
        world_->Unregister(component);
    }

    ScriptInstanceHandle ScriptRuntime::CreateInstance(const ScriptInstanceRequest& request)
    {
        if (!PlaySessionActive()) return invalid_script_instance_handle;

        const ScriptTypeDescriptor* entry = catalog_.Find(request.type_id);
        if (entry == nullptr) return invalid_script_instance_handle;

        IScriptBackend* backend = Backend(entry->language);
        if (backend == nullptr) return invalid_script_instance_handle;
        if (!backend->CanInstantiate(request.type_id)) return invalid_script_instance_handle;

        const ScriptInstanceHandle handle = backend->CreateInstance(request);
        if (handle == invalid_script_instance_handle) return handle;

        InstanceOwner owner;
        owner.handle = handle;
        owner.backend = backend;
        instance_owners_.push_back(owner);
        return handle;
    }

    void ScriptRuntime::DestroyInstance(ScriptInstanceHandle instance)
    {
        if (instance == invalid_script_instance_handle) return;

        for (std::size_t index = 0; index < instance_owners_.size(); ++index)
        {
            if (instance_owners_[index].handle != instance) continue;

            if (instance_owners_[index].backend != nullptr)
            {
                instance_owners_[index].backend->DestroyInstance(instance);
            }
            instance_owners_[index] = instance_owners_.back();
            instance_owners_.pop_back();
            return;
        }
    }

    ScriptInvokeResult ScriptRuntime::Invoke(ScriptInstanceHandle instance,
        ScriptCallback callback, const ScriptArguments& arguments)
    {
        IScriptBackend* backend = BackendOf(instance);
        if (backend == nullptr) return ScriptInvokeResult::NoInstance;

        const ScriptInvokeResult result = backend->Invoke(instance, callback, arguments);
        if (result != ScriptInvokeResult::RuntimeError) return result;

        ScriptErrorRecord record;
        record.kind = ScriptErrorKind::Runtime;
        record.language = backend->Language();
        record.callback = ToString(callback);
        record.message = backend->LastErrorMessage();
        record.file = backend->LastErrorFile();
        record.line = backend->LastErrorLine();
        ReportError(record);

        return result;
    }

    bool ScriptRuntime::PushField(ScriptInstanceHandle instance,
        const std::string& saved_name, const ScriptValue& value)
    {
        IScriptBackend* backend = BackendOf(instance);
        if (backend == nullptr) return false;
        return backend->SetField(instance, saved_name, value);
    }

    bool ScriptRuntime::PullField(ScriptInstanceHandle instance,
        const std::string& saved_name, ScriptValue& out) const
    {
        IScriptBackend* backend = BackendOf(instance);
        if (backend == nullptr) return false;
        return backend->GetField(instance, saved_name, out);
    }

    void ScriptRuntime::ReportError(const ScriptErrorRecord& record)
    {
        // 同じエラーが毎フレーム出ても、ログを埋めないよう集約する。
        if (!errors_.Record(record, clock_seconds_)) return;
        pending_log_lines_.push_back("[Script] " + record.Describe());
    }

    std::vector<std::string> ScriptRuntime::DrainPendingLogLines()
    {
        std::vector<std::string> drained;
        drained.swap(pending_log_lines_);
        return drained;
    }
}
