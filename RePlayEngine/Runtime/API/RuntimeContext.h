#pragma once

#include "../Core/RuntimeResult.h"
#include "../Handles/HandleResolver.h"
#include "../Handles/RuntimeHandles.h"
#include "../../Scene/Services/IPhysicsQueryService.h"

#include <DirectXMath.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ReplayEngine::Core { class Component; class GameObject; }
namespace ReplayEngine::Scene { class Scene; }

namespace ReplayEngine::Runtime
{
    class EventBus;

    // 時間の情報。framework が毎フレーム更新する。
    struct RuntimeTime final
    {
        float delta_time = 0.0f;
        float fixed_delta_time = 1.0f / 60.0f;

        // タイムスケールの影響を受けない経過時間。
        // まだタイムスケール機能が無いため delta_time と同じ値が入る。
        // 別フィールドにしてあるのは、後から入れたときに
        // 利用側のコードを書き換えずに済ませるため。
        float unscaled_delta_time = 0.0f;

        std::uint64_t frame_index = 0;
    };

    // ログの重要度。
    enum class LogLevel : std::int32_t { Info = 0, Warning = 1, Error = 2 };

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

        Scene::Scene& World() const noexcept { return *world_; }
        Core::WorldInstanceID CurrentWorldID() const noexcept;

        EventBus& Events() noexcept { return *events_; }
        const EventBus& Events() const noexcept { return *events_; }

        const HandleResolver& Resolver() const noexcept { return resolver_; }
        const HandleDiagnostics& Diagnostics() const noexcept { return diagnostics_; }

        // ---- Time -------------------------------------------------------------

        const RuntimeTime& Time() const noexcept { return time_; }
        float DeltaTime() const noexcept { return time_.delta_time; }
        float FixedDeltaTime() const noexcept { return time_.fixed_delta_time; }
        float UnscaledDeltaTime() const noexcept { return time_.unscaled_delta_time; }
        std::uint64_t FrameIndex() const noexcept { return time_.frame_index; }

        // ---- Object -----------------------------------------------------------

        bool IsValid(const ObjectHandle& handle) const noexcept;
        ObjectHandle FindByObjectID(Core::ObjectID id) const noexcept;
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
        RuntimeStatus InstantiatePrefabDeferred(const std::string& asset_guid,
            const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation_euler,
            const DirectX::XMFLOAT3& scale, const ObjectHandle& parent);

        // 積まれた遅延要求を実行する。Scene の同期点で framework が呼ぶ。
        void FlushDeferredOperations();

        std::size_t PendingDeferredOperationCount() const noexcept
        {
            return pending_instantiations_.size();
        }

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

        // ---- Log ----------------------------------------------------------------

        void Log(LogLevel level, const std::string& message,
            const ObjectHandle& source = ObjectHandle::None()) const;
        void LogInfo(const std::string& message,
            const ObjectHandle& source = ObjectHandle::None()) const;
        void LogWarning(const std::string& message,
            const ObjectHandle& source = ObjectHandle::None()) const;
        void LogError(const std::string& message,
            const ObjectHandle& source = ObjectHandle::None()) const;

        // ---- 未実装 Service ------------------------------------------------------
        //
        // Audio / Input Action / SaveGame / Runtime UI はまだ無い。
        // 呼べる API を用意して 0 や true を返す「動いているように見える実装」は置かない。
        // 存在を問い合わせる手段だけを提供し、答えは常に false。
        // 実装が入った時点でここを差し替える。
        bool AudioAvailable() const noexcept { return false; }
        bool InputActionAvailable() const noexcept { return false; }
        bool SaveGameAvailable() const noexcept { return false; }
        bool RuntimeUIAvailable() const noexcept { return false; }

    private:
        struct PendingInstantiation
        {
            std::string asset_guid;
            DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 rotation{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
            Core::ObjectID parent;
        };

        Core::GameObject* ResolveObject(const ObjectHandle& handle,
            RuntimeStatus& status) const;

        Scene::Scene* world_ = nullptr;
        HandleDiagnostics diagnostics_;
        HandleResolver resolver_;

        // EventBus は unique_ptr で持つ。ヘッダを軽く保つため前方宣言のみ。
        std::unique_ptr<EventBus> events_;

        IPrefabInstantiator* prefab_instantiator_ = nullptr;
        IRuntimeLogSink* log_sink_ = nullptr;

        RuntimeTime time_;
        std::vector<PendingInstantiation> pending_instantiations_;
    };
}
