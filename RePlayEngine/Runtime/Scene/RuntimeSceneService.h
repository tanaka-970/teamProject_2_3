#pragma once

#include "../Core/RuntimeResult.h"
#include "../Handles/RuntimeHandles.h"
#include "../../Scene/Serialization/SceneData.h"

#include <cstdint>
#include <memory>
#include <string>

namespace ReplayEngine::Scene { class Scene; }

namespace ReplayEngine::Runtime
{
    class RuntimeContext;
    class SceneFlowService;
    class EventBus;
    class CollisionEventDispatcher;
    class IWorldLifecycleListener;

    // Scene Asset の解決窓口。
    //
    // Runtime 層が AssetDatabase を直接 include しないための境界。
    // 実装は framework が用意し、非所有参照として渡す。
    // 未接続なら ServiceUnavailable。パスを推測して読みに行くことはしない。
    class ISceneAssetResolver
    {
    public:
        virtual ~ISceneAssetResolver() = default;

        // AssetGUID から Scene ファイルの絶対／相対パスを引く。
        //
        // 戻り値:
        //   Ok              … out_path が有効
        //   AssetMissing    … GUID が Asset Database に無い
        //   InvalidAssetType… GUID はあるが Scene Asset ではない
        virtual RuntimeStatus ResolveScenePath(const std::string& asset_guid,
            std::string& out_path) const = 0;
    };

    // Runtime Scene の読み込み状態。
    enum class SceneLoadState : std::int32_t
    {
        Idle = 0,
        Loading = 1,       // ファイル読み込み〜Staging World 構築中
        ReadyToSwap = 2,   // Staging World が完成し、同期点を待っている
        Swapping = 3,      // 入れ替え実行中
        Completed = 4,     // 直近の読み込みが成功した
        Failed = 5,        // 直近の読み込みが失敗した（現在の World は無傷）
    };

    const char* ToString(SceneLoadState state) noexcept;

    // 読み込み要求の受理結果。
    //
    // RuntimeStatus と分けている理由:
    //   「要求そのものを受け取れたか」と「読み込みが成功したか」は別の話。
    //   受理された要求が後で失敗することもある。呼び出し側が
    //   再送すべきか待つべきかを判断できるよう、受理段階を独立させる。
    enum class SceneRequestResult : std::int32_t
    {
        Accepted = 0,       // 受理した。以降 State を見て進行を追う
        Busy = 1,           // 読み込み中。今の要求は受け付けない
        InvalidRequest = 2, // GUID が空など、要求そのものが不正
    };

    const char* ToString(SceneRequestResult result) noexcept;

    // Runtime 中に .replayscene を切り替える。
    //
    // ---------------------------------------------------------------------
    // 【Editor の Scene 読み込みを流用しない理由】
    //
    //   Editor の読み込みは Editor Session・Undo 履歴・Selection・Dirty フラグ・
    //   Recent Scenes を同時に更新する。Runtime の遷移でそれらを触ると、
    //   「ゲーム中に Scene が切り替わっただけで編集中の Scene が差し替わる」
    //   ことになる。責任が違うので経路ごと分ける。
    //
    // ---------------------------------------------------------------------
    // 【Staging World を挟む理由】
    //
    //   現在の World を先に消してから読み込むと、途中で失敗したときに
    //   戻る先が無い。Asset が壊れていただけでゲームが空の World になる。
    //
    //   先に別の World を丸ごと組み立て、Object も Component も Property も
    //   参照解決もすべて成功したことを確かめてから入れ替える。
    //   失敗した場合、現在の World は 1 バイトも触れていない。
    //   古い ObjectHandle も有効なまま残る。
    //
    // ---------------------------------------------------------------------
    // 所有関係:
    //   このサービスが Runtime World を unique_ptr で所有する。
    //   入れ替えは unique_ptr の差し替えなので、通常の旧 World 実体は
    //   その場で破棄される。ただし Scene 直下の PersistentComponent ルートだけは、
    //   差し替え前に一時退避し、新 World が同じ実体を引き取る。
    class RuntimeSceneService final
    {
    public:
        RuntimeSceneService();
        ~RuntimeSceneService();

        RuntimeSceneService(const RuntimeSceneService&) = delete;
        RuntimeSceneService& operator=(const RuntimeSceneService&) = delete;

        // ---- 接続 --------------------------------------------------------

        void SetAssetResolver(const ISceneAssetResolver* resolver) noexcept
        {
            asset_resolver_ = resolver;
        }

        // World が入れ替わったときに再接続する相手。どれも非所有。
        void SetRuntimeContext(RuntimeContext* context) noexcept { runtime_ = context; }
        void SetSceneFlowService(SceneFlowService* service) noexcept
        {
            scene_flow_ = service;
        }
        void SetCollisionDispatcher(CollisionEventDispatcher* dispatcher) noexcept
        {
            collision_dispatcher_ = dispatcher;
        }

        // World 単位の付随状態を作り直す相手。非所有。
        //
        // 何のためにあるか:
        //   SwapWorlds() は新しい World へ差し替えたあと Scene::Start() を呼び、
        //   全 Component の OnRuntimeAwake -> OnEnable -> OnStart を一気に流す。
        //   World 単位の付随状態は、その前に用意されていなければならない。
        //
        //   Play Mode の開始処理へ書くと、SceneFlowService 経由の
        //   ゲーム中 Scene 遷移で同じ準備が漏れる。Editor の Play では動くのに
        //   遷移した瞬間だけ動かなくなる、という追いにくい壊れ方をする。
        //
        // 詳細は WorldLifecycleListener.h を参照。
        void SetWorldLifecycleListener(IWorldLifecycleListener* listener) noexcept
        {
            world_lifecycle_ = listener;
        }

        // ---- World --------------------------------------------------------

        // 現在の Runtime World。初期状態でも空の World が 1 つある（nullptr にならない）。
        //
        // 【生ポインタを持ち越さないこと】
        //   入れ替えのたびに World の実体が変わる。参照を保存すると、
        //   次の切り替えで解放済みの Scene を指す。
        //   使うたびにここから取り直し、跨いで持つのは ObjectHandle だけにする。
        Scene::Scene& ActiveWorld() noexcept { return *active_; }
        const Scene::Scene& ActiveWorld() const noexcept { return *active_; }

        // World の実体が常にあるかの確認。設計上ここは必ず true になるが、
        // 利用側が「World がある前提」を明示できるよう用意してある。
        bool HasActiveWorld() const noexcept { return static_cast<bool>(active_); }

        Core::WorldInstanceID ActiveWorldID() const noexcept;

        // 現在読み込まれている Scene の AssetGUID。未読み込みなら空。
        const std::string& CurrentSceneGUID() const noexcept { return current_scene_guid_; }
        const std::string& ActiveSceneGuid() const noexcept { return current_scene_guid_; }
        const std::string& PendingSceneGUID() const noexcept { return pending_scene_guid_; }

        // ---- 要求 ----------------------------------------------------------

        // AssetGUID の Scene を読み込む。
        //
        // 同じ GUID を指定しても読み直す（Reload と同じ扱い）。
        // 読み込みが進行中の場合は Busy を返し、現在の要求を壊さない。
        SceneRequestResult RequestLoad(const std::string& asset_guid);

        // 現在の Scene をもう一度読み込む。現在の GUID が空なら InvalidRequest。
        SceneRequestResult RequestReload();

        // 手元にある SceneData から Runtime World を組み立てる。
        //
        // 【何のためにあるか】
        //   Editor の Play 開始は「編集中の Scene をそのまま実行する」ので、
        //   ファイルにも AssetGUID にも対応する実体が無い（未保存の場合もある）。
        //   ここが無いと、framework が自分で Scene を組み立てて所有することになり、
        //   Runtime World の所有者が 2 つに割れる。
        //
        //   読み込み経路が違うだけで、Staging 構築・入れ替え・通知・診断は
        //   ファイル経由とまったく同じ流れを通る。
        //
        //   source_guid は診断表示と Reload のためのラベル。空でもよい。
        //   空の場合、この World に対する RequestReload() は InvalidRequest になる
        //   （読み直す元が存在しないため、黙って別の Scene を読むことはしない）。
        SceneRequestResult RequestAdopt(const Scene::Serialization::SceneData& data,
            const std::string& source_guid);

        // Runtime World を空へ戻す。Play 停止で使う。
        //
        // 所有者は変わらない。空の World を作り直して差し替えるだけなので、
        // 「Play 中の World を framework が持ち帰る」経路は存在しない。
        void ResetToEmptyWorld();

        // 進行中の要求を取り消す。Staging World は解放され、現在の World は無傷。
        void CancelPending();

        // ---- 進行 ------------------------------------------------------------

        // 読み込みを 1 段進める。フレームの安全な同期点で呼ぶ。
        //
        // 同期読み込みなので、実際には
        //   1 回目 … Loading -> ReadyToSwap（またはFailed）
        //   2 回目 … ReadyToSwap -> Completed
        // の 2 段で進む。段を分けてあるのは、
        // 「構築」と「入れ替え」の間にフレーム境界を挟めるようにするため。
        // 将来ファイル読み込みをワーカーへ逃がすときも、この形のまま拡張できる。
        //
        // Update の最中に呼ばないこと。World の実体が入れ替わる。
        void Tick();

        // ---- 状態 --------------------------------------------------------------

        SceneLoadState State() const noexcept { return state_; }
        // SceneLoaderComponent が UI へ出すための 0..1 の進捗。
        // 現在の実装は「Staging 構築」と「World 入れ替え」の 2 段を公開する。
        float Progress() const noexcept
        {
            switch (state_)
            {
            case SceneLoadState::Loading:     return 0.5f;
            case SceneLoadState::ReadyToSwap: return 0.75f;
            case SceneLoadState::Swapping:    return 0.9f;
            case SceneLoadState::Completed:   return 1.0f;
            case SceneLoadState::Failed:      return 0.0f;
            case SceneLoadState::Idle:        return 1.0f;
            }
            return 0.0f;
        }
        bool IsBusy() const noexcept
        {
            return state_ == SceneLoadState::Loading ||
                state_ == SceneLoadState::ReadyToSwap ||
                state_ == SceneLoadState::Swapping;
        }

        RuntimeStatus LastStatus() const noexcept { return last_status_; }
        const std::string& LastError() const noexcept { return last_error_; }

        // 失敗した処理段階。診断表示用。
        const std::string& LastFailedStage() const noexcept { return last_failed_stage_; }

        // 直近の読み込みの統計。
        std::size_t StagingObjectCount() const noexcept { return staging_object_count_; }
        std::uint64_t LoadCount() const noexcept { return load_count_; }
        std::uint64_t FailureCount() const noexcept { return failure_count_; }
        std::uint64_t SwapCount() const noexcept { return swap_count_; }

        const Scene::Serialization::SceneLoadReport& LastLoadReport() const noexcept
        {
            return last_report_;
        }

        // 発行した Scene 遷移通知の件数。診断表示用。
        std::uint64_t PublishedNotificationCount() const noexcept
        {
            return published_notifications_;
        }

    private:
        // 失敗を記録する。現在の World には一切触れない。
        void Fail(RuntimeStatus status, const char* stage, std::string detail);

        // Scene 遷移の通知を Global Event Bus へ発行する。
        //
        // なぜ Scene 単位ではなく Global なのか:
        //   この通知の受け手は「World が消えること」を知りたい側であり、
        //   World と一緒に消えてしまっては意味が無い。
        //   Scene 単位の Bus は入れ替えで捨てられるので、ここでは使わない。
        //
        // 将来 C# を載せたときの用途:
        //   Assembly 側が持っている World 依存の状態を捨てる同期点として使う。
        //   Engine 側に C# 固有の呼び出しを足さずに済むよう、
        //   最初から「購読できる通知」として出しておく。
        void PublishSceneNotification(Reflection::TypeGUID type, const char* type_name,
            const std::string& scene_guid, RuntimeStatus status) const;

        // 進行中の要求の入手先。
        enum class PendingSource : std::int32_t
        {
            AssetGuid = 0,   // Resolver でパスを引き、ファイルから読む
            InMemory = 1,    // 既に手元にある SceneData を使う
        };

        // Staging World を組み立てる。成功したら ReadyToSwap へ。
        void BuildStagingWorld();

        // SceneData から Staging World を作る。読み込み経路の共通部分。
        bool BuildStagingFromData(const Scene::Serialization::SceneData& data);

        // 安全点での入れ替え。
        void SwapWorlds();

        std::unique_ptr<Scene::Scene> active_;
        std::unique_ptr<Scene::Scene> staging_;

        const ISceneAssetResolver* asset_resolver_ = nullptr;
        RuntimeContext* runtime_ = nullptr;
        SceneFlowService* scene_flow_ = nullptr;
        CollisionEventDispatcher* collision_dispatcher_ = nullptr;
        IWorldLifecycleListener* world_lifecycle_ = nullptr;

        SceneLoadState state_ = SceneLoadState::Idle;
        RuntimeStatus last_status_ = RuntimeStatus::Ok;
        std::string last_error_;
        std::string last_failed_stage_;

        std::string current_scene_guid_;
        std::string pending_scene_guid_;

        PendingSource pending_source_ = PendingSource::AssetGuid;

        // InMemory 要求の内容。要求を受理した時点で複製しておく。
        // 参照だけ持つと、要求から Tick までの間に呼び出し側が
        // SceneData を書き換えたり破棄したりできてしまう。
        Scene::Serialization::SceneData pending_data_;

        Scene::Serialization::SceneLoadReport last_report_;
        std::size_t staging_object_count_ = 0;
        std::uint64_t load_count_ = 0;
        std::uint64_t failure_count_ = 0;
        std::uint64_t swap_count_ = 0;
        mutable std::uint64_t published_notifications_ = 0;
    };
}
