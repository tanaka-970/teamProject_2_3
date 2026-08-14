#include "RuntimeSceneService.h"

#include "WorldLifecycleListener.h"
#include "../API/RuntimeContext.h"
#include "../Events/CollisionEventDispatcher.h"
#include "../Events/EventBus.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Serialization/SceneSerializer.h"

#include <filesystem>
#include <utility>

namespace ReplayEngine::Runtime
{
    namespace Serialization = Scene::Serialization;

    const char* ToString(SceneLoadState state) noexcept
    {
        switch (state)
        {
        case SceneLoadState::Idle:        return "Idle";
        case SceneLoadState::Loading:     return "Loading";
        case SceneLoadState::ReadyToSwap: return "ReadyToSwap";
        case SceneLoadState::Swapping:    return "Swapping";
        case SceneLoadState::Completed:   return "Completed";
        case SceneLoadState::Failed:      return "Failed";
        }
        return "Unknown";
    }

    const char* ToString(SceneRequestResult result) noexcept
    {
        switch (result)
        {
        case SceneRequestResult::Accepted:       return "Accepted";
        case SceneRequestResult::Busy:           return "Busy";
        case SceneRequestResult::InvalidRequest: return "InvalidRequest";
        }
        return "Unknown";
    }

    RuntimeSceneService::RuntimeSceneService()
        : active_(std::make_unique<Scene::Scene>("Runtime World"))
    {
        // 初期状態でも空の World を 1 つ持つ。
        // ActiveWorld() が nullptr を返さないので、
        // 利用側が「まだ World が無い」場合の分岐を書かずに済む。
    }

    RuntimeSceneService::~RuntimeSceneService()
    {
        // World を壊す前に Runtime への参照を外す。
        //
        // このサービスと RuntimeContext のどちらが先に消えるかは
        // 利用側の宣言順で決まる。Context が先に消えていた場合、
        // World の破棄中に走る OnRuntimeDestroy が破棄済みの Context を触る。
        // 先に外しておけば、その順序に依存しなくなる。
        if (active_) active_->Services().SetRuntime(nullptr);
        if (active_) active_->Services().SetRuntimeScene(nullptr);
        if (active_) active_->Services().SetSceneFlow(nullptr);
        if (staging_) staging_->Services().SetRuntime(nullptr);
        if (staging_) staging_->Services().SetRuntimeScene(nullptr);
        if (staging_) staging_->Services().SetSceneFlow(nullptr);
        scene_flow_ = nullptr;
    }

    Core::WorldInstanceID RuntimeSceneService::ActiveWorldID() const noexcept
    {
        return active_->WorldInstanceID();
    }

    void RuntimeSceneService::PublishSceneNotification(Reflection::TypeGUID type,
        const char* type_name, const std::string& scene_guid,
        RuntimeStatus status) const
    {
        EventRecord record;
        record.type = type;
        record.type_name = type_name;
        record.payload.Set("scene_guid", Reflection::PropertyValue::MakeString(scene_guid));
        record.payload.Set("world_instance",
            Reflection::PropertyValue::MakeUInt64(active_->WorldInstanceID()));
        record.payload.Set("status",
            Reflection::PropertyValue::MakeInt(ToErrorCode(status)));
        if (runtime_ != nullptr) record.frame_index = runtime_->FrameIndex();

        EventBus::Global().Publish(std::move(record));
        ++published_notifications_;
    }

    void RuntimeSceneService::Fail(RuntimeStatus status, const char* stage,
        std::string detail)
    {
        // 失敗しても現在の World には一切触れない。
        // Clear もしないので、古い ObjectHandle は有効なまま残る。
        staging_.reset();
        staging_object_count_ = 0;
        pending_data_.Clear();

        state_ = SceneLoadState::Failed;
        last_status_ = status;
        last_failed_stage_ = stage;
        last_error_ = std::string("[") + stage + "] " + ToString(status) + ": " + detail;
        ++failure_count_;

        if (runtime_ != nullptr)
        {
            runtime_->LogError("Scene の読み込みに失敗しました (GUID " +
                pending_scene_guid_ + ") " + last_error_);
        }

        // 失敗したことも通知する。
        // 「読み込み中のまま黙って止まった」状態を購読側から見分けられるようにする。
        PublishSceneNotification(EngineEvents::SceneLoadFailed, "SceneLoadFailed",
            pending_scene_guid_, status);

        pending_scene_guid_.clear();
    }

    SceneRequestResult RuntimeSceneService::RequestLoad(const std::string& asset_guid)
    {
        if (asset_guid.empty()) return SceneRequestResult::InvalidRequest;

        // 進行中の要求は上書きしない。
        //
        // 上書きを許すと、Staging World を作りかけたまま別の World を
        // 作り始めることになり、暗黙に 2 つの World が同時に構築される。
        // どちらが入れ替わるかも不定になるので、明示的に Busy を返す。
        if (IsBusy()) return SceneRequestResult::Busy;

        pending_scene_guid_ = asset_guid;
        pending_source_ = PendingSource::AssetGuid;
        pending_data_.Clear();
        state_ = SceneLoadState::Loading;
        last_status_ = RuntimeStatus::Ok;
        last_error_.clear();
        last_failed_stage_.clear();

        PublishSceneNotification(EngineEvents::SceneLoadRequested, "SceneLoadRequested",
            pending_scene_guid_, RuntimeStatus::Ok);
        return SceneRequestResult::Accepted;
    }

    SceneRequestResult RuntimeSceneService::RequestReload()
    {
        if (current_scene_guid_.empty()) return SceneRequestResult::InvalidRequest;
        return RequestLoad(current_scene_guid_);
    }

    SceneRequestResult RuntimeSceneService::RequestAdopt(
        const Serialization::SceneData& data, const std::string& source_guid)
    {
        if (IsBusy()) return SceneRequestResult::Busy;

        // 要求を受けた時点で複製する。
        // 参照で持つと、Tick が走るまでの間に呼び出し側が中身を変えられる。
        pending_data_ = data;
        pending_source_ = PendingSource::InMemory;
        pending_scene_guid_ = source_guid;

        state_ = SceneLoadState::Loading;
        last_status_ = RuntimeStatus::Ok;
        last_error_.clear();
        last_failed_stage_.clear();

        PublishSceneNotification(EngineEvents::SceneLoadRequested, "SceneLoadRequested",
            pending_scene_guid_, RuntimeStatus::Ok);
        return SceneRequestResult::Accepted;
    }

    void RuntimeSceneService::ResetToEmptyWorld()
    {
        // 進行中の要求があれば先に捨てる。
        CancelPending();

        // 旧 World の後始末は入れ替えと同じ順序で行う。
        // ここだけ順序を変えると、Play 停止のときだけ購読や接触が残る。
        if (runtime_ != nullptr) runtime_->Events().Clear();
        if (collision_dispatcher_ != nullptr) collision_dispatcher_->Reset();

        // 入れ替えとまったく同じ 3 点で通知する。
        // ここだけ順序を変えると、Play 停止のときだけ後始末が漏れる。
        if (world_lifecycle_ != nullptr) world_lifecycle_->OnWorldUnloading(*active_);

        active_->Services().SetRuntime(nullptr);
        active_->Clear();

        if (world_lifecycle_ != nullptr) world_lifecycle_->OnWorldUnloaded(*active_);

        active_ = std::make_unique<Scene::Scene>("Runtime World");

        if (runtime_ != nullptr)
        {
            runtime_->Rebind(*active_);
            active_->Services().SetRuntime(runtime_);
        }
        active_->Services().SetRuntimeScene(this);
        active_->Services().SetSceneFlow(scene_flow_);

        // 空の World でも対称に呼ぶ。
        //
        // Play セッションの世代番号をここで進めておくと、
        // 停止中に残った古い Handle が次のセッションで再解決されない。
        if (world_lifecycle_ != nullptr) world_lifecycle_->OnWorldActivating(*active_);

        current_scene_guid_.clear();
        pending_scene_guid_.clear();
        pending_data_.Clear();
        pending_source_ = PendingSource::AssetGuid;
        last_report_ = Serialization::SceneLoadReport();
        staging_object_count_ = 0;
        state_ = SceneLoadState::Idle;
        last_status_ = RuntimeStatus::Ok;
        last_error_.clear();
        last_failed_stage_.clear();

        PublishSceneNotification(EngineEvents::WorldChanged, "WorldChanged",
            std::string(), RuntimeStatus::Ok);
        EventBus::Global().Dispatch(nullptr);
    }

    void RuntimeSceneService::CancelPending()
    {
        if (!IsBusy()) return;

        staging_.reset();
        staging_object_count_ = 0;
        pending_scene_guid_.clear();
        pending_data_.Clear();
        pending_source_ = PendingSource::AssetGuid;
        state_ = SceneLoadState::Idle;
    }

    void RuntimeSceneService::BuildStagingWorld()
    {
        PublishSceneNotification(EngineEvents::SceneLoadStarted, "SceneLoadStarted",
            pending_scene_guid_, RuntimeStatus::Ok);

        // 手元の SceneData から作る要求は、Asset 解決とファイル読み込みを飛ばす。
        // それ以降（Staging 構築・診断・入れ替え）は同じ経路を通る。
        if (pending_source_ == PendingSource::InMemory)
        {
            BuildStagingFromData(pending_data_);
            return;
        }

        // ---- 1) Asset 解決 -------------------------------------------------
        if (asset_resolver_ == nullptr)
        {
            Fail(RuntimeStatus::ServiceUnavailable, "ResolveAsset",
                "Scene Asset Resolver が接続されていません。");
            return;
        }

        std::string path;
        const RuntimeStatus resolve_status =
            asset_resolver_->ResolveScenePath(pending_scene_guid_, path);
        if (Failed(resolve_status))
        {
            Fail(resolve_status, "ResolveAsset",
                "AssetGUID から Scene を解決できません。");
            return;
        }

        // ---- 2) ファイル読み込み・Parse・Version Migration -------------------
        //
        // SceneSerializer が v7〜現行までの読み込みと、
        // 新しすぎる形式の拒否をまとめて担当する。
        Serialization::SceneData data;
        std::string error;
        if (!Serialization::SceneSerializer::LoadFromFile(data, path, error))
        {
            Fail(RuntimeStatus::SceneLoadFailed, "ParseScene", error);
            return;
        }

        BuildStagingFromData(data);
    }

    bool RuntimeSceneService::BuildStagingFromData(const Serialization::SceneData& data)
    {
        // ---- 3) 基本 Validation ---------------------------------------------
        if (data.objects.empty())
        {
            // 空の Scene 自体は不正ではない。読み込みは通す。
            // ただし診断へ残るよう記録しておく。
            if (runtime_ != nullptr)
            {
                runtime_->LogWarning("読み込んだ Scene に GameObject がありません: " +
                    pending_scene_guid_);
            }
        }

        // ---- 4) Staging World の構築 ----------------------------------------
        //
        // ここで GameObject / Component 生成、StableID 復元、Property 反映、
        // Missing Component / Unknown Property の保持、参照解決がすべて行われる。
        // ApplySceneData を通すので、Editor 読み込みと同じ規則が使われる。
        auto staging = std::make_unique<Scene::Scene>(data.scene_name);

        // Component を作る「前」に Services を張る。
        //
        // ApplySceneData は生成しながら OnDeserialize / OnPropertyChanged を
        // 流す。ScriptComponent はそこで Services()->Catalog() と照合して
        // Script 型を決めるため、ここで張らないと型が解決できず、
        // Play へ入っても Unresolved のままインスタンスが作られない。
        //
        // 編集 Scene 側は最初から Services が張ってあり、同じデータでも
        // そちらだけ Loaded になる。その食い違いがここで生まれていた。
        if (runtime_ != nullptr) staging->Services().SetRuntime(runtime_);
        staging->Services().SetRuntimeScene(this);
        staging->Services().SetSceneFlow(scene_flow_);
        if (world_lifecycle_ != nullptr) world_lifecycle_->OnWorldBuilding(*staging);

        Serialization::SceneLoadReport report;
        if (!Serialization::ApplySceneData(data, *staging, report))
        {
            Fail(RuntimeStatus::SceneLoadFailed, "BuildWorld",
                "Staging World の構築に失敗しました。");
            return false;
        }

        // Missing Component や Unknown Property は失敗にしない。
        // データは保持されており、型が戻れば復元されるため。
        // ただし診断へ必ず残す。
        if (runtime_ != nullptr && (report.missing_components != 0 ||
            report.unknown_properties != 0 || report.skipped_components != 0 ||
            report.unresolved_component_dependencies != 0))
        {
            runtime_->LogWarning("Scene 読み込み時の注意: Missing Component " +
                std::to_string(report.missing_components) + " 件, 未知プロパティ " +
                std::to_string(report.unknown_properties) + " 件, 生成失敗 " +
                std::to_string(report.skipped_components) + " 件, 依存自動補完 " +
                std::to_string(report.automatically_added_components) +
                " 件, 依存未解決 " +
                std::to_string(report.unresolved_component_dependencies) +
                " 件 (GUID " +
                pending_scene_guid_ + ")");
        }

        // 生成に失敗した Component がある場合だけ失敗扱いにする。
        // Missing は「保持できている」のに対し、こちらは復元できていない。
        if (report.skipped_components != 0)
        {
            Fail(RuntimeStatus::SceneLoadFailed, "BuildWorld",
                "復元できなかった Component が " +
                std::to_string(report.skipped_components) + " 件あります。");
            return false;
        }

        staging_ = std::move(staging);
        staging_object_count_ = staging_->GameObjectCount();
        last_report_ = report;
        state_ = SceneLoadState::ReadyToSwap;
        return true;
    }

    void RuntimeSceneService::SwapWorlds()
    {
        state_ = SceneLoadState::Swapping;

        // 旧 World がまだ生きているうちに知らせる。
        // 購読側はここで World 依存の状態を捨てられる。
        PublishSceneNotification(EngineEvents::BeforeSceneUnload, "BeforeSceneUnload",
            current_scene_guid_, RuntimeStatus::Ok);
        EventBus::Global().Dispatch(nullptr);

        // ---- 旧 World の後始末 ------------------------------------------------
        //
        // 順序が重要。先に購読と接触状態を捨ててから実体を解放する。
        // 逆にすると、解放済みの World を指したまま解除処理が走る。
        if (runtime_ != nullptr)
        {
            // 旧 World に紐づく購読と遅延要求を捨てる。
            // Scene をまたいで持ち越すと、消えた Object を指す購読が残る。
            runtime_->Events().Clear();
        }
        if (collision_dispatcher_ != nullptr)
        {
            // 接触状態を捨てる。
            // Dispatcher 自身も WorldInstanceID の変化で捨てるが、
            // 切り替えの瞬間に明示的に落としておく方が追いやすい。
            collision_dispatcher_->Reset();
        }

        // World 単位の付随状態へ「これから壊す」と伝える。
        // まだ全 Component が生きているので、購読の解除や
        // 新規要求の受付停止をここで行える。
        if (world_lifecycle_ != nullptr) world_lifecycle_->OnWorldUnloading(*active_);

        // Persistent ルートだけは World の寿命をまたいで引き取る。
        // それ以外は通常どおり Clear() で明示破棄する。
        std::vector<std::unique_ptr<Core::GameObject>> persistent_roots =
            active_->DetachPersistentRoots();

        // 旧 World から Runtime への参照を先に外す。
        //
        // 外さないと、このあとの Clear() で走る OnRuntimeDestroy が
        // Services().Runtime() を引き、入れ替え途中の Context を触る。
        // Behaviour 側は Runtime 未接続を元から扱えるので、
        // 破棄経路に分岐を増やさずに済む。
        //
        // Scripts() はここでは外さない。Clear() が流す OnRuntimeDestroy から
        // ユーザーの OnDestroy を呼び、インスタンスを解放する必要があるため。
        // 解放し終えたことは OnWorldUnloaded で確かめる。
        active_->Services().SetRuntime(nullptr);

        // 旧 World の Behaviour へ OnDisable / OnDestroy / OnDetach を通す。
        // Scene のデストラクタが Clear() を通して行うが、
        // 入れ替えの前に明示的に済ませておくことで、
        // 破棄処理の中から新 World が見えてしまう状態を作らない。
        active_->Clear();

        // OnRuntimeDestroy がすべて流れ終わった直後。
        // 付随状態の破棄と、解放漏れの検証はここで行う。
        // 実体はまだ生きているので、診断に World の情報を使える。
        if (world_lifecycle_ != nullptr) world_lifecycle_->OnWorldUnloaded(*active_);

        // ---- 入れ替え ---------------------------------------------------------
        //
        // unique_ptr の差し替え。旧 World の実体はここで解放される。
        // 新しい World は別の実体なので WorldInstanceID も別。
        // 旧 World の ObjectHandle / ComponentHandle はすべて WrongWorld になる。
        active_ = std::move(staging_);
        staging_.reset();

        active_->AdoptPersistentRoots(std::move(persistent_roots));

        // ---- 再接続 -----------------------------------------------------------
        if (runtime_ != nullptr)
        {
            runtime_->Rebind(*active_);
            active_->Services().SetRuntime(runtime_);
        }
        active_->Services().SetRuntimeScene(this);
        active_->Services().SetSceneFlow(scene_flow_);

        // ---- Runtime 開始 -----------------------------------------------------
        //
        // World 単位の付随状態を、Start() より「前」に用意する。
        //
        // ここが最後の安全点。この行を過ぎると Scene::Start() が
        // 全 Component の OnRuntimeAwake を流してしまい、
        // ユーザーのスクリプトが「実行基盤が無い状態」で走り出す。
        if (world_lifecycle_ != nullptr) world_lifecycle_->OnWorldActivating(*active_);

        // Scene::Start() が同期点を 1 回通し、
        // 全 Component へ OnRuntimeAwake -> OnEnable -> OnStart を
        // 既存の Lifecycle 設計どおりの順で流す。
        // Behaviour 用に別経路を作らないので、順序が食い違うことがない。
        active_->Start();

        current_scene_guid_ = pending_scene_guid_;
        pending_scene_guid_.clear();
        pending_data_.Clear();
        pending_source_ = PendingSource::AssetGuid;

        state_ = SceneLoadState::Completed;
        last_status_ = RuntimeStatus::Ok;
        ++swap_count_;
        ++load_count_;

        PublishSceneNotification(EngineEvents::WorldChanged, "WorldChanged",
            current_scene_guid_, RuntimeStatus::Ok);
        PublishSceneNotification(EngineEvents::SceneLoaded, "SceneLoaded",
            current_scene_guid_, RuntimeStatus::Ok);

        if (runtime_ != nullptr)
        {
            runtime_->LogInfo("Scene を読み込みました (GUID " + current_scene_guid_ +
                ", GameObject " + std::to_string(active_->GameObjectCount()) + " 体)");
        }
    }

    void RuntimeSceneService::Tick()
    {
        switch (state_)
        {
        case SceneLoadState::Loading:
            BuildStagingWorld();
            break;

        case SceneLoadState::ReadyToSwap:
            SwapWorlds();
            break;

        case SceneLoadState::Idle:
        case SceneLoadState::Swapping:
        case SceneLoadState::Completed:
        case SceneLoadState::Failed:
            // 何もしない。Completed / Failed は次の要求が来るまで保持する
            // （呼び出し側が結果を読み取れるようにするため）。
            break;
        }

        // 自分が積んだ通知を同じ同期点で配り切る。
        //
        // 配送を呼び出し側任せにすると、Global Bus を一度も Dispatch しない
        // 構成では待ち行列が伸び続ける。Tick() は「World を入れ替えてよい安全点」
        // として定義してあるので、ここで配るのが最も条件が揃っている。
        EventBus::Global().Dispatch(nullptr);
    }
}
