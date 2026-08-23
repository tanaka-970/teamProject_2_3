#pragma once

#include "../Core/RuntimeResult.h"
#include "../Handles/HandleResolver.h"
#include "../Handles/RuntimeHandles.h"
#include "../../Scene/Services/IPhysicsQueryService.h"
#include "../../Scene/Services/IInputService.h"
#include "../../Audio/AudioService.h"
#include "RuntimeSaveGameService.h"

#include <DirectXMath.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ReplayEngine::Core { class Component; class GameObject; }
namespace ReplayEngine::Scene { class Scene; }
namespace ReplayEngine::Reflection { class PropertyValue; }

namespace ReplayEngine::Runtime
{
    class EventBus;
    struct EventRecord;

    // 時間の情報。framework が毎フレーム更新する。
    struct RuntimeTime final
    {
        float delta_time = 0.0f;
        float fixed_delta_time = 1.0f / 60.0f;

        // タイムスケールの影響を受けない経過時間。Editor/UI/ロード演出はこれを使う。
        float unscaled_delta_time = 0.0f;

        // Scene の共通時間倍率。0 でゲーム時間だけを完全停止する。
        float time_scale = 1.0f;

        std::uint64_t frame_index = 0;
    };

    // ログの重要度。
    enum class LogLevel : std::int32_t { Info = 0, Warning = 1, Error = 2 };

    enum class PhysicsQueryKind : std::int32_t
    {
        RaycastAll = 0,
        OverlapSphere = 1,
        OverlapBox = 2,
        OverlapCapsule = 3,
        SphereCast = 4,
        BoxCast = 5,
        CapsuleCast = 6,
    };

    enum class ComponentCommand : std::int32_t
    {
        AnimatorPlayState = 0,
        AnimatorPause = 1,
        AnimatorResume = 2,
        AnimatorStop = 3,
        AnimatorSetBool = 4,
        AnimatorSetFloat = 5,
        AnimatorSetTrigger = 6,
        AnimatorResetTrigger = 7,
        AudioPlay = 8,
        AudioStop = 9,
        ParticlePlay = 10,
        ParticleStop = 11,
        ParticleEmit = 12,
        ParticleClear = 13,
    };

    struct PhysicsQueryRequest final
    {
        PhysicsQueryKind kind = PhysicsQueryKind::RaycastAll;
        DirectX::XMFLOAT3 point_a{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 point_b{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 direction{ 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT3 half_extents{ 0.5f, 0.5f, 0.5f };
        float radius = 0.5f;
        float max_distance = 0.0f;
        int layer = 0;
        int mask = -1;
        ObjectHandle ignore;
    };

    // Prefab を Runtime から生成するための窓口。
    //
    // なぜインターフェイスで挟むか:
    //   Prefab の実体化には AssetDatabase（GUID から Path を引く）と
    //   PrefabSerializer が要る。Runtime 層がそれらを直接 include すると、
    //   Runtime が Asset 管理へ依存してしまい、将来 C# 側へ切り出しにくくなる。
    //   実装は framework が用意し、非所有参照として渡す。
    //
    //   接続されていなければ ServiceUnavailable を返す。
    //   「成功したように見えて何も起きない」偽の実装は置かない。
    class IPrefabInstantiator
    {
    public:
        virtual ~IPrefabInstantiator() = default;

        // AssetGUID から Prefab を配置する。
        // 失敗理由は AssetMissing / InvalidAssetType / SceneLoadFailed など。
        virtual RuntimeStatus InstantiatePrefab(const std::string& asset_guid,
            Scene::Scene& world, const DirectX::XMFLOAT3& position,
            const DirectX::XMFLOAT3& rotation_euler, const DirectX::XMFLOAT3& scale,
            Core::ObjectID parent, Core::ObjectID& created_root) = 0;
    };

    // Scene 遷移要求の窓口。
    //
    // なぜインターフェイスで挟むか:
    //   実体は SceneFlowService だが、RuntimeContext がそれを直接知ると
    //   「Runtime API -> Scene Flow -> Runtime Scene Service」という
    //   下向きの依存が生まれる。Behaviour から見えるのは要求の口だけでよい。
    //
    //   接続されていなければ ServiceUnavailable を返す。
    //   遷移したふりをして何も起きない実装は置かない。
    //
    // 【即時に切り替わらないことは仕様】
    //   ここで受け付けるのは「要求」だけ。実際の World 入れ替えは
    //   RuntimeSceneService::Tick() の安全点まで起きない。
    //   OnTrigger の最中に呼んでも、走査中の World が足元で消えることはない。
    class ISceneFlow
    {
    public:
        virtual ~ISceneFlow() = default;

        virtual RuntimeStatus RequestSceneLoad(const std::string& asset_guid) = 0;
        virtual RuntimeStatus RequestSceneReload() = 0;
        virtual RuntimeStatus RequestReturnToPreviousScene() = 0;

        // Scene Flow Asset を使う上位のイベント駆動遷移。
        // 既存のテスト用 ISceneFlow 実装を壊さないため、追加 API は既定で未対応。
        virtual RuntimeStatus RequestSceneFlowTrigger(const std::string& /*event_name*/)
        {
            return RuntimeStatus::UnsupportedOperation;
        }
        virtual RuntimeStatus SetSceneFlowBool(const std::string& /*key*/, bool /*value*/)
        {
            return RuntimeStatus::UnsupportedOperation;
        }
        virtual RuntimeStatus SetSceneFlowInt(const std::string& /*key*/, std::int64_t /*value*/)
        {
            return RuntimeStatus::UnsupportedOperation;
        }
        virtual RuntimeStatus SetSceneFlowFloat(const std::string& /*key*/, double /*value*/)
        {
            return RuntimeStatus::UnsupportedOperation;
        }

        // プロセスを落とさない。要求として記録するだけ。
        virtual RuntimeStatus RequestQuitApplication(const std::string& reason) = 0;

        virtual bool SceneTransitionInProgress() const = 0;
        virtual const std::string& CurrentSceneGuid() const = 0;
    };

    // ログの出力先。framework / Editor が実装する。
    class IRuntimeLogSink
    {
    public:
        virtual ~IRuntimeLogSink() = default;
        virtual void Write(LogLevel level, const std::string& message,
            const ObjectHandle& source) = 0;
    };

    // Behaviour から Engine を触るための唯一の窓口。
    //
    // ---------------------------------------------------------------------
    // 【設計方針】
    //
    //   1. 生ポインタを長期参照として返さない。
    //      返すのは Handle か、その場限りの値だけ。
    //
    //   2. 失敗で assert しない。必ず RuntimeStatus を返す。
    //      壊れた Handle を渡されるのは Script API では通常の入力。
    //
    //   3. 未接続の Service は ServiceUnavailable を返す。
    //      Audio / Input / SaveGame は実装が無いので、
    //      それらしい API を置いて 0 を返すようなことはしない。
    //
    //   4. テンプレートと Engine 内部ポインタに依存した形にしない。
    //      将来 C ABI を被せられるよう、引数と戻り値は
    //      Handle・整数・文字列・POD 構造体に収めてある。
    //
    // ---------------------------------------------------------------------
    // 所有関係:
    //   RuntimeContext は Scene も Service も所有しない。すべて非所有参照。
    //   実体は framework が持ち、World の入れ替えのたびに接続し直す。
    class RuntimeContext final
    {
    public:
        explicit RuntimeContext(Scene::Scene& world) noexcept;
        ~RuntimeContext();

        RuntimeContext(const RuntimeContext&) = delete;
        RuntimeContext& operator=(const RuntimeContext&) = delete;

        // ---- 接続 (framework が設定する) -------------------------------------

        void SetPrefabInstantiator(IPrefabInstantiator* instantiator) noexcept
        {
            prefab_instantiator_ = instantiator;
        }
        void SetLogSink(IRuntimeLogSink* sink) noexcept { log_sink_ = sink; }
        void SetTime(const RuntimeTime& time) noexcept { time_ = time; }

        // frameworkが所有する既存Serviceを非所有参照で接続する。
        void SetInputService(const Scene::IInputService* service) noexcept
        {
            input_service_ = service;
        }
        void SetAudioService(Audio::IAudioPlaybackService* service) noexcept
        {
            audio_service_ = service;
        }
        void SetSaveGameService(ISaveGameService* service) noexcept
        {
            save_game_service_ = service;
        }

        // World の入れ替えでは切らない。framework が持つ接続なので Rebind でも残す。
        void SetSceneFlow(ISceneFlow* flow) noexcept { scene_flow_ = flow; }

        Scene::Scene& World() const noexcept { return *world_; }
        Core::WorldInstanceID CurrentWorldID() const noexcept;

        // World が入れ替わったときに接続し直す。RuntimeSceneService が呼ぶ。
        //
        // 何を捨てるか:
        //   Event 購読 … 旧 World の Object を指す購読が残ると、
        //                 配送のたびに解決に失敗し続ける。
        //   遅延要求   … 旧 World の親 ObjectID を持つ生成要求は意味を失う。
        //   診断カウンタ… World ごとの統計として読めるよう 0 から数え直す。
        //
        // 何を残すか:
        //   Prefab Instantiator / Log Sink / Time は framework が持つ接続なので残す。
        void Rebind(Scene::Scene& world);

        EventBus& Events() noexcept { return *events_; }
        const EventBus& Events() const noexcept { return *events_; }

        const HandleResolver& Resolver() const noexcept { return resolver_; }
        const HandleDiagnostics& Diagnostics() const noexcept { return diagnostics_; }

        // ---- Time -------------------------------------------------------------

        const RuntimeTime& Time() const noexcept { return time_; }
        float DeltaTime() const noexcept { return time_.delta_time; }
        float FixedDeltaTime() const noexcept { return time_.fixed_delta_time; }
        float UnscaledDeltaTime() const noexcept { return time_.unscaled_delta_time; }
        float TimeScale() const noexcept { return time_.time_scale; }
        std::uint64_t FrameIndex() const noexcept { return time_.frame_index; }

        // ---- Object -----------------------------------------------------------

        bool IsValid(const ObjectHandle& handle) const noexcept;
        ObjectHandle FindByObjectID(Core::ObjectID id) const noexcept;

        // 名前で探す。見つからなければ空 Handle。
        //
        // 同じ名前が複数あるときは Scene の並び順で最初のものを返す
        // （Scene::FindGameObjectByName の挙動をそのまま使う）。
        // 破棄予定のものは飛ばす。
        // 名前を一意にするのは呼び出し側の責任。
        ObjectHandle FindByName(const std::string& name) const noexcept;

        ObjectHandle ControlledObject() const noexcept;

        RuntimeStatus GetName(const ObjectHandle& handle, std::string& out) const;
        RuntimeStatus SetName(const ObjectHandle& handle, const std::string& name);

        RuntimeStatus SetEnabled(const ObjectHandle& handle, bool enabled);
        RuntimeStatus IsEnabled(const ObjectHandle& handle, bool& out) const;

        RuntimeStatus GetParent(const ObjectHandle& handle, ObjectHandle& out) const;
        RuntimeStatus GetChildren(const ObjectHandle& handle,
            std::vector<ObjectHandle>& out) const;
        RuntimeStatus SetParent(const ObjectHandle& child, const ObjectHandle& parent,
            bool preserve_world_transform);

        // ---- Transform ---------------------------------------------------------

        RuntimeStatus GetLocalPosition(const ObjectHandle& handle,
            DirectX::XMFLOAT3& out) const;
        RuntimeStatus SetLocalPosition(const ObjectHandle& handle,
            const DirectX::XMFLOAT3& value);
        RuntimeStatus GetLocalRotationEuler(const ObjectHandle& handle,
            DirectX::XMFLOAT3& out) const;
        RuntimeStatus SetLocalRotationEuler(const ObjectHandle& handle,
            const DirectX::XMFLOAT3& value);
        RuntimeStatus GetLocalScale(const ObjectHandle& handle,
            DirectX::XMFLOAT3& out) const;
        RuntimeStatus SetLocalScale(const ObjectHandle& handle,
            const DirectX::XMFLOAT3& value);
        RuntimeStatus GetWorldPosition(const ObjectHandle& handle,
            DirectX::XMFLOAT3& out) const;
        RuntimeStatus SetWorldPosition(const ObjectHandle& handle,
            const DirectX::XMFLOAT3& value);
        RuntimeStatus GetWorldRotationQuaternion(const ObjectHandle& handle,
            DirectX::XMFLOAT4& out) const;
        RuntimeStatus SetWorldRotationQuaternion(const ObjectHandle& handle,
            const DirectX::XMFLOAT4& value);
        RuntimeStatus GetWorldScale(const ObjectHandle& handle,
            DirectX::XMFLOAT3& out) const;
        RuntimeStatus SetWorldScale(const ObjectHandle& handle,
            const DirectX::XMFLOAT3& value);

        // 前後左右上のワールド方向。回転から作るので Transform の正本は増えない。
        RuntimeStatus GetWorldAxes(const ObjectHandle& handle,
            DirectX::XMFLOAT3& forward, DirectX::XMFLOAT3& right,
            DirectX::XMFLOAT3& up) const;

        // ローカル前方をワールドの target 方向へ向ける。up が縮退なら失敗を返す。
        RuntimeStatus LookAt(const ObjectHandle& handle,
            const DirectX::XMFLOAT3& target, const DirectX::XMFLOAT3& world_up);

        // ---- Component ---------------------------------------------------------

        bool HasComponent(const ObjectHandle& handle,
            Core::ComponentTypeID type_id) const noexcept;
        ComponentHandle GetComponent(const ObjectHandle& handle,
            Core::ComponentTypeID type_id) const noexcept;
        std::vector<ComponentHandle> GetComponents(const ObjectHandle& handle,
            Core::ComponentTypeID type_id) const;

        RuntimeStatus AddComponent(const ObjectHandle& handle,
            Core::ComponentTypeID type_id, ComponentHandle& out);
        RuntimeStatus SetComponentEnabled(const ComponentHandle& handle, bool enabled);
        RuntimeStatus IsComponentEnabled(const ComponentHandle& handle, bool& out) const;
        RuntimeStatus GetScriptField(const ComponentHandle& handle,
            const std::string& field_name, Reflection::PropertyValue& out) const;
        RuntimeStatus SetScriptField(const ComponentHandle& handle,
            const std::string& field_name, const Reflection::PropertyValue& value);

        // ---- Component の型とプロパティ ------------------------------------------
        //
        // Inspector と同じ PropertyRegistry を読む。Component 型ごとの専用 API を
        // 増やさずに、登録済みのプロパティを名前で読み書きするための唯一の入口。

        // 型名（"CameraComponent" など）から ComponentTypeID を引く。
        // 未登録なら Core::invalid_component_type_id を返す。
        Core::ComponentTypeID FindComponentTypeId(
            const std::string& type_name) const noexcept;
        RuntimeStatus GetComponentTypeName(const ComponentHandle& handle,
            std::string& out) const;
        RuntimeStatus GetComponentProperty(const ComponentHandle& handle,
            const std::string& property_name, Reflection::PropertyValue& out) const;
        RuntimeStatus SetComponentProperty(const ComponentHandle& handle,
            const std::string& property_name, const Reflection::PropertyValue& value);
        RuntimeStatus InvokeComponentCommand(const ComponentHandle& handle,
            ComponentCommand command, const std::string& text = std::string(),
            float scalar = 0.0f, float secondary_scalar = 0.0f, int integer = 0);

        // ---- Rigidbody -----------------------------------------------------------
        //
        // 力は必ず Component の公開 API を通す。
        // PhysicsDynamicsWorld の内部 Body や Solver はスクリプトへ出さない。

        RuntimeStatus RigidbodyAddForce(const ComponentHandle& handle,
            const DirectX::XMFLOAT3& force);
        RuntimeStatus RigidbodyAddTorque(const ComponentHandle& handle,
            const DirectX::XMFLOAT3& torque);
        RuntimeStatus RigidbodyClearForces(const ComponentHandle& handle);
        RuntimeStatus RigidbodyTeleport(const ComponentHandle& handle,
            const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation_euler);

        // 速度は Inspector 上は読み取り専用なので、汎用プロパティ API では書けない。
        // Script から積む入口だけをここへ出す。
        RuntimeStatus RigidbodyGetLinearVelocity(const ComponentHandle& handle,
            DirectX::XMFLOAT3& out) const;
        RuntimeStatus RigidbodySetLinearVelocity(const ComponentHandle& handle,
            const DirectX::XMFLOAT3& value);
        RuntimeStatus RigidbodyGetAngularVelocity(const ComponentHandle& handle,
            DirectX::XMFLOAT3& out) const;
        RuntimeStatus RigidbodySetAngularVelocity(const ComponentHandle& handle,
            const DirectX::XMFLOAT3& value);

        // ---- Motion Player -----------------------------------------------------

        RuntimeStatus FindMotionPlayer(const ObjectHandle& owner,
            const std::string& key, ComponentHandle& out) const;
        RuntimeStatus MotionPlay(const ComponentHandle& player);
        RuntimeStatus MotionPlayFrom(const ComponentHandle& player, float seconds);
        RuntimeStatus MotionPause(const ComponentHandle& player);
        RuntimeStatus MotionResume(const ComponentHandle& player);
        RuntimeStatus MotionStop(const ComponentHandle& player);
        RuntimeStatus MotionReverse(const ComponentHandle& player);
        RuntimeStatus SetMotionTime(const ComponentHandle& player, float seconds);
        RuntimeStatus SetMotionSpeed(const ComponentHandle& player, float speed);
        RuntimeStatus SetMotionWeight(const ComponentHandle& player, float weight);
        RuntimeStatus IsMotionPlaying(const ComponentHandle& player, bool& out) const;
        RuntimeStatus GetMotionTime(const ComponentHandle& player, float& out) const;
        RuntimeStatus GetMotionDuration(const ComponentHandle& player, float& out) const;

        // ---- 生成・破棄 ---------------------------------------------------------

        // 空の GameObject を作る。生成は即時で、Update 中でも安全
        // （Scene の配列は末尾追加しかしないため、走査中の添字が壊れない）。
        RuntimeStatus CreateGameObject(const std::string& name, ObjectHandle& out);

        // 破棄はすべて遅延。即時破棄の API は Behaviour へ公開しない。
        //
        // 即時破棄を出さない理由:
        //   Update / Trigger コールバックの中から自分や相手を消せてしまうと、
        //   呼び出し元の走査が壊れた実体へ触れる。
        //   予約だけ立てて Scene の同期点でまとめて片付ければ、
        //   その事故が構造的に起きない。
        RuntimeStatus DestroyGameObject(const ObjectHandle& handle);
        RuntimeStatus DestroyComponent(const ComponentHandle& handle);

        // ---- Prefab -------------------------------------------------------------

        // AssetGUID から Prefab を生成する。
        // Instantiator が未接続なら ServiceUnavailable。
        RuntimeStatus InstantiatePrefab(const std::string& asset_guid,
            const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation_euler,
            const DirectX::XMFLOAT3& scale, const ObjectHandle& parent,
            ObjectHandle& out);

        // Update / Trigger の最中に呼んでも安全なように、
        // 生成要求だけ積んで次の同期点で実行する。
        // 遅延生成の 1 件を識別する番号。0 は無効。
        using SpawnRequestID = std::uint64_t;

        // 遅延生成を積み、その要求番号を返す。
        // Flush 後に TryTakeSpawnResult() で出来た GameObject を引き取れる。
        RuntimeStatus InstantiatePrefabDeferredTracked(const std::string& asset_guid,
            const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation_euler,
            const DirectX::XMFLOAT3& scale, const ObjectHandle& parent,
            SpawnRequestID& out_request);

        // 完了した遅延生成の結果を 1 件引き取る。引き取ると表から消える。
        // まだ Flush されていなければ TransitionInProgress を返す。
        RuntimeStatus TryTakeSpawnResult(SpawnRequestID request, ObjectHandle& out);

        // 引き取られないまま溜まった結果の件数。診断用。
        std::size_t PendingSpawnResultCount() const noexcept
        {
            return spawn_results_.size();
        }

        RuntimeStatus InstantiatePrefabDeferred(const std::string& asset_guid,
            const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation_euler,
            const DirectX::XMFLOAT3& scale, const ObjectHandle& parent);

        // 積まれた遅延要求を実行する。Scene の同期点で framework が呼ぶ。
        void FlushDeferredOperations();

        std::size_t PendingDeferredOperationCount() const noexcept
        {
            return pending_instantiations_.size();
        }

        // ---- Scene 遷移 ----------------------------------------------------------
        //
        // どれも要求を出すだけ。戻り値が Ok でも、その場で World は変わらない。
        // 実際の入れ替えは次の安全点で起きる。

        bool SceneFlowAvailable() const noexcept { return scene_flow_ != nullptr; }

        RuntimeStatus LoadScene(const std::string& asset_guid);
        RuntimeStatus ReloadCurrentScene();
        RuntimeStatus ReturnToPreviousScene();

        // Scene Flow Asset のイベントを発火し、条件に一致した遷移を要求する。
        RuntimeStatus TriggerSceneFlow(const std::string& event_name);
        RuntimeStatus SetSceneFlowBool(const std::string& key, bool value);
        RuntimeStatus SetSceneFlowInt(const std::string& key, std::int64_t value);
        RuntimeStatus SetSceneFlowFloat(const std::string& key, double value);

        // アプリケーションの終了要求。ここではプロセスを終了しない。
        RuntimeStatus QuitApplication(const std::string& reason = std::string());

        // 遷移中か。要求の二重発行を避けたい Behaviour が見る。
        bool SceneTransitionInProgress() const noexcept;

        // 現在の Runtime Scene の AssetGUID。未接続なら空。
        const std::string& CurrentSceneGuid() const noexcept;

        // ---- Physics ------------------------------------------------------------
        //
        // 既存の安全な問い合わせだけを通す。
        // Collider Component のポインタは公開しない。

        bool PhysicsAvailable() const noexcept;
        RuntimeStatus QueryGround(const DirectX::XMFLOAT3& origin, float radius,
            float up_offset, float down_distance, float walkable_normal_y,
            const ObjectHandle& ignore, Scene::GroundHit& out) const;
        RuntimeStatus SweepSphere(const DirectX::XMFLOAT3& start,
            const DirectX::XMFLOAT3& end, float radius, float maximum_normal_y,
            const ObjectHandle& ignore, Scene::SphereSweepHit& out) const;

        RuntimeStatus Raycast(const DirectX::XMFLOAT3& origin,
            const DirectX::XMFLOAT3& direction, float max_distance,
            int layer, int mask, const ObjectHandle& ignore,
            Scene::RaycastHit& out) const;
        RuntimeStatus PhysicsQuery(const PhysicsQueryRequest& request,
            std::vector<Scene::PhysicsQueryHit>& out) const;

        // ---- Log ----------------------------------------------------------------

        void Log(LogLevel level, const std::string& message,
            const ObjectHandle& source = ObjectHandle::None()) const;
        void LogInfo(const std::string& message,
            const ObjectHandle& source = ObjectHandle::None()) const;
        void LogWarning(const std::string& message,
            const ObjectHandle& source = ObjectHandle::None()) const;
        void LogError(const std::string& message,
            const ObjectHandle& source = ObjectHandle::None()) const;

        // ---- Input Action -------------------------------------------------------

        bool InputActionAvailable() const noexcept;
        RuntimeStatus InputHeld(const std::string& action, int player_slot, bool& out) const;
        RuntimeStatus InputPressed(const std::string& action, int player_slot, bool& out) const;
        RuntimeStatus InputReleased(const std::string& action, int player_slot, bool& out) const;
        RuntimeStatus InputAxis(const std::string& axis, int player_slot, float& out) const;
        RuntimeStatus InputPointerDeltaX(float& out) const;
        RuntimeStatus InputPointerDeltaY(float& out) const;

        // ---- 生デバイス入力 -------------------------------------------------
        //
        // Action / Axis で足りるならそちらを使う。ここはキーコンフィグ画面や
        // 一時的な入力のための窓口。key は仮想キーコード。

        RuntimeStatus InputKeyHeld(int key, bool& out) const;
        RuntimeStatus InputKeyPressed(int key, bool& out) const;
        RuntimeStatus InputKeyReleased(int key, bool& out) const;
        RuntimeStatus InputMouseButtonHeld(int button, bool& out) const;
        RuntimeStatus InputMouseButtonPressed(int button, bool& out) const;
        RuntimeStatus InputMouseButtonReleased(int button, bool& out) const;
        RuntimeStatus InputPointerPosition(float& out_x, float& out_y) const;
        RuntimeStatus InputWheelDelta(float& out) const;
        RuntimeStatus InputGamepadConnected(int player_slot, bool& out) const;
        RuntimeStatus InputGamepadButtonHeld(int player_slot, int button, bool& out) const;
        RuntimeStatus InputGamepadButtonPressed(int player_slot, int button, bool& out) const;
        RuntimeStatus InputGamepadButtonReleased(int player_slot, int button, bool& out) const;
        RuntimeStatus InputGamepadAxis(int player_slot, int axis, float& out) const;
        RuntimeStatus InputSetGamepadVibration(int player_slot, float low, float high);

        // ---- Audio --------------------------------------------------------------

        bool AudioAvailable() const noexcept;
        RuntimeStatus PlayAudio(const std::string& clip_path, bool loop, float volume,
            float pitch, int spatial_mode, const DirectX::XMFLOAT3& position,
            float min_distance, float max_distance, std::uint64_t& out) const;
        RuntimeStatus StopAudio(std::uint64_t voice) const;
        RuntimeStatus UpdateAudio(std::uint64_t voice, const std::string& clip_path,
            bool loop, float volume, float pitch, int spatial_mode,
            const DirectX::XMFLOAT3& position, float min_distance,
            float max_distance) const;

        // ---- SaveGame -----------------------------------------------------------

        bool SaveGameAvailable() const noexcept;
        RuntimeStatus SaveBool(const std::string& slot, const std::string& key, bool value) const;
        RuntimeStatus SaveInt(const std::string& slot, const std::string& key,
            std::int64_t value) const;
        RuntimeStatus SaveDouble(const std::string& slot, const std::string& key,
            double value) const;
        RuntimeStatus SaveString(const std::string& slot, const std::string& key,
            const std::string& value) const;
        RuntimeStatus LoadBool(const std::string& slot, const std::string& key,
            bool& out) const;
        RuntimeStatus LoadInt(const std::string& slot, const std::string& key,
            std::int64_t& out) const;
        RuntimeStatus LoadDouble(const std::string& slot, const std::string& key,
            double& out) const;
        RuntimeStatus LoadString(const std::string& slot, const std::string& key,
            std::string& out) const;
        RuntimeStatus HasSaveKey(const std::string& slot, const std::string& key,
            bool& out) const;
        RuntimeStatus DeleteSaveKey(const std::string& slot, const std::string& key) const;
        RuntimeStatus SaveGame(const std::string& slot) const;
        RuntimeStatus LoadGame(const std::string& slot) const;
        RuntimeStatus DeleteSave(const std::string& slot) const;

        // ---- Runtime UI ---------------------------------------------------------

        bool RuntimeUIAvailable() const noexcept;
        RuntimeStatus CreateUIElement(const std::string& name, const ObjectHandle& parent,
            ObjectHandle& out);
        RuntimeStatus SetUIText(const ObjectHandle& object, const std::string& text);
        RuntimeStatus GetUIText(const ObjectHandle& object, std::string& out) const;
        RuntimeStatus SetUIImageColor(const ObjectHandle& object,
            const DirectX::XMFLOAT4& color);
        RuntimeStatus SetUIRect(const ObjectHandle& object,
            const DirectX::XMFLOAT2& position, const DirectX::XMFLOAT2& size,
            const DirectX::XMFLOAT2& scale, float rotation, int sort_order);
        RuntimeStatus SetUIButtonInteractable(const ObjectHandle& object, bool interactable);
        RuntimeStatus GetUIFocus(ObjectHandle& out);
        RuntimeStatus SetUIFocus(const ObjectHandle& object);
        RuntimeStatus FindUIFocusInDirection(const ObjectHandle& from, int direction,
            ObjectHandle& out);

        // ---- Event --------------------------------------------------------------

        RuntimeStatus PublishEvent(EventRecord record);

    private:
        struct PendingInstantiation
        {
            std::string asset_guid;
            DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 rotation{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
            Core::ObjectID parent;
            SpawnRequestID request = 0;
        };

        struct SpawnResult
        {
            SpawnRequestID request = 0;
            Core::ObjectID created;
            RuntimeStatus status = RuntimeStatus::Ok;
        };

        // 引き取られない結果が無限に溜まらないよう上限を設ける。
        // 超えたら古いものから捨てる。捨てた件数はログへ残す。
        static constexpr std::size_t maximum_spawn_results = 256;
        std::vector<SpawnResult> spawn_results_;
        SpawnRequestID next_spawn_request_ = 1;

        Core::GameObject* ResolveObject(const ObjectHandle& handle,
            RuntimeStatus& status) const;

        Scene::Scene* world_ = nullptr;
        HandleDiagnostics diagnostics_;
        HandleResolver resolver_;

        // EventBus は unique_ptr で持つ。ヘッダを軽く保つため前方宣言のみ。
        std::unique_ptr<EventBus> events_;

        IPrefabInstantiator* prefab_instantiator_ = nullptr;
        IRuntimeLogSink* log_sink_ = nullptr;
        ISceneFlow* scene_flow_ = nullptr;
        const Scene::IInputService* input_service_ = nullptr;
        Audio::IAudioPlaybackService* audio_service_ = nullptr;
        ISaveGameService* save_game_service_ = nullptr;

        RuntimeTime time_;
        std::vector<PendingInstantiation> pending_instantiations_;
    };
}
