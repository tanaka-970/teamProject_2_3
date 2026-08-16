#include "ScriptComponent.h"

#include "ScriptError.h"
#include "ScriptServices.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Rendering/RenderStats.h"

#include <utility>

namespace ReplayEngine::Scripting
{
    using Reflection::PropertyBag;

    // -----------------------------------------------------------------------
    // ライフサイクル
    // -----------------------------------------------------------------------

    void ScriptComponent::OnAttach()
    {
        // ここではまだプロパティが入っていない。
        // Schema の解決は OnDeserialize / OnPropertyChanged / OnRuntimeAwake で行う。
        status_ = script_type_.IsValid() ? ScriptStatus::Unresolved : ScriptStatus::Unassigned;
    }

    void ScriptComponent::OnRuntimeAwake()
    {
        // DeferAwakeUntilObjectActive() が true なので、
        // ここへ来た時点で所有 GameObject の階層は有効。
        ResolveSchema();
        CreateInstanceAndAwake();
    }

    void ScriptComponent::OnEnable()
    {
        InvokeCallback(ScriptCallback::OnEnable, ScriptArguments::None());
    }

    void ScriptComponent::OnStart()
    {
        InvokeCallback(ScriptCallback::Start, ScriptArguments::None());
    }

    void ScriptComponent::OnFixedUpdate(float fixed_delta_time)
    {
        REPLAY_PROFILE_SCOPE("Script/FixedUpdate");
        InvokeCallback(ScriptCallback::FixedUpdate,
            ScriptArguments::DeltaTime(fixed_delta_time));
    }

    void ScriptComponent::OnUpdate(float delta_time)
    {
        REPLAY_PROFILE_SCOPE("Script/Update");
        InvokeCallback(ScriptCallback::Update, ScriptArguments::DeltaTime(delta_time));
    }

    void ScriptComponent::OnLateUpdate(float delta_time)
    {
        REPLAY_PROFILE_SCOPE("Script/LateUpdate");
        InvokeCallback(ScriptCallback::LateUpdate, ScriptArguments::DeltaTime(delta_time));
    }

    void ScriptComponent::OnDisable()
    {
        InvokeCallback(ScriptCallback::OnDisable, ScriptArguments::None());
    }

    void ScriptComponent::OnRuntimeDestroy()
    {
        // ユーザーの OnDestroy は「インスタンスがある場合だけ」呼ぶ。
        //
        // Inactive な GameObject へ置かれたまま Scene が終わった場合、
        // Awake が一度も走っていない。そこへ OnDestroy だけ届くと、
        // 初期化していない状態で後始末を書くことになり、
        // スクリプト側で null 参照を踏む原因になる。
        DestroyInstanceIfAny();
    }

    void ScriptComponent::OnDetach()
    {
        // 通常は OnRuntimeDestroy が先に走って片付いている。
        // 取りこぼしがあってもここで必ず閉じる。
        DestroyInstanceIfAny();
    }

    // -----------------------------------------------------------------------
    // インスタンス
    // -----------------------------------------------------------------------

    void ScriptComponent::CreateInstanceAndAwake()
    {
        if (HasInstance()) return;

        // どの条件で作られなかったのかを last_error_ へ必ず残す。
        //
        // 以前はどの return も理由を残さず、Inspector からは
        // 「状態 Loaded・インスタンス なし」としか見えなかった。
        // その状態からは Services が無いのか Play セッションが無いのかを
        // 区別できず、原因の切り分けができなかった。
        IScriptServices* services = Services();
        if (services == nullptr)
        {
            last_error_ = "Scene に ScriptServices が接続されていません"
                "（World 構築時の SetScripts 漏れ）";
            return;
        }

        // Edit Mode では作らない。「置いただけで動き出す」ことを防ぐ。
        if (!services->PlaySessionActive())
        {
            last_error_ = "Play セッションがありません"
                "（Edit 中は正常。Play 中に出るなら OnWorldActivating 未通過）";
            return;
        }

        if (!script_type_.IsValid())
        {
            last_error_ = "Script 型が未指定です";
            status_ = ScriptStatus::Unassigned;
            return;
        }

        if (!schema_)
        {
            // Schema が無くても Field 値は預かったまま。エンジンは止めない。
            last_error_ = "Schema を解決できていません"
                "（Catalog に型が無い / Assembly 未ロード）";
            status_ = ScriptStatus::Unresolved;
            return;
        }

        if (!registered_)
        {
            services->RegisterComponent(*this);
            registered_ = true;
        }

        ScriptInstanceRequest request;
        request.type_id = script_type_;
        request.owner_object = Owner() != nullptr ? Owner()->ID() : Core::ObjectID::Invalid();
        request.owner_component = StableID();
        if (Owner() != nullptr && GetScene() != nullptr)
        {
            request.owner_handle.world = GetScene()->WorldInstanceID();
            request.owner_handle.object = Owner()->ID();
            request.owner_handle.generation = Owner()->Generation();

            request.component_handle.owner = request.owner_handle;
            request.component_handle.instance = InstanceID();
            request.component_handle.type_id = TypeID();
        }

        instance_ = services->CreateInstance(request);
        if (!HasInstance())
        {
            status_ = ScriptStatus::Error;
            if (last_error_.empty()) last_error_ = "スクリプトインスタンスを生成できませんでした。";
            return;
        }

        status_ = ScriptStatus::Running;
        last_error_.clear();

        // Inspector と Scene に保存された値を、Awake より前に流し込む。
        // Awake の中で自分の Field を読めるようにするため。
        PushAllFields();

        InvokeCallback(ScriptCallback::Awake, ScriptArguments::None());
    }

    void ScriptComponent::DestroyInstanceIfAny()
    {
        IScriptServices* services = Services();

        if (HasInstance())
        {
            if (services != nullptr)
            {
                InvokeCallback(ScriptCallback::OnDestroy, ScriptArguments::None());
                services->DestroyInstance(instance_);
            }
            instance_ = invalid_script_instance_handle;
        }

        if (registered_)
        {
            if (services != nullptr) services->UnregisterComponent(*this);
            registered_ = false;
        }

        if (status_ == ScriptStatus::Running)
        {
            status_ = schema_ ? ScriptStatus::Loaded : ScriptStatus::Unresolved;
        }
    }

    void ScriptComponent::InvokeCallback(ScriptCallback callback,
        const ScriptArguments& arguments)
    {
        if (!HasInstance()) return;

        IScriptServices* services = Services();
        if (services == nullptr) return;

        const ScriptInvokeResult result = services->Invoke(instance_, callback, arguments);
        if (ScriptInvokeSucceeded(result)) return;

        // 失敗しても他の Component へ波及させない。
        // ここで例外を投げず、状態とエラー文字列を残すだけにする。
        status_ = ScriptStatus::Error;
        last_error_ = std::string(ToString(callback)) + " の実行に失敗しました (" +
            ToString(result) + ")";
    }

    void ScriptComponent::PushAllFields()
    {
        if (!HasInstance()) return;

        IScriptServices* services = Services();
        if (services == nullptr) return;

        for (const PropertyBag::Entry& entry : field_values_.Entries())
        {
            services->PushField(instance_, entry.name, entry.value);
        }
    }
}
