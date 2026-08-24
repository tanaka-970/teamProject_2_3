#include "RuntimeContext.h"

#include "../Events/EventBus.h"
#include "../../Components/Motion/MotionPlayerComponent.h"
#include "../../Object/Component/Component.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Scene/Runtime/Scene.h"

namespace ReplayEngine::Runtime
{
    using Core::Component;
    using Core::GameObject;
    using Core::ObjectID;
    // ---- Scene 遷移 ----------------------------------------------------------
    //
    // どれも「要求を渡すだけ」。ここで World を触らない。
    // 未接続なら ServiceUnavailable。遷移したふりはしない。

    RuntimeStatus RuntimeContext::LoadScene(const std::string& asset_guid)
    {
        if (scene_flow_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (asset_guid.empty()) return RuntimeStatus::InvalidArgument;
        return scene_flow_->RequestSceneLoad(asset_guid);
    }

    RuntimeStatus RuntimeContext::ReloadCurrentScene()
    {
        if (scene_flow_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        return scene_flow_->RequestSceneReload();
    }

    RuntimeStatus RuntimeContext::ReturnToPreviousScene()
    {
        if (scene_flow_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        return scene_flow_->RequestReturnToPreviousScene();
    }

    RuntimeStatus RuntimeContext::TriggerSceneFlow(const std::string& event_name)
    {
        if (scene_flow_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (event_name.empty()) return RuntimeStatus::InvalidArgument;
        return scene_flow_->RequestSceneFlowTrigger(event_name);
    }

    RuntimeStatus RuntimeContext::SetSceneFlowBool(const std::string& key, bool value)
    {
        if (scene_flow_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (key.empty()) return RuntimeStatus::InvalidArgument;
        return scene_flow_->SetSceneFlowBool(key, value);
    }

    RuntimeStatus RuntimeContext::SetSceneFlowInt(const std::string& key, std::int64_t value)
    {
        if (scene_flow_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (key.empty()) return RuntimeStatus::InvalidArgument;
        return scene_flow_->SetSceneFlowInt(key, value);
    }

    RuntimeStatus RuntimeContext::SetSceneFlowFloat(const std::string& key, double value)
    {
        if (scene_flow_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (key.empty()) return RuntimeStatus::InvalidArgument;
        return scene_flow_->SetSceneFlowFloat(key, value);
    }

    RuntimeStatus RuntimeContext::QuitApplication(const std::string& reason)
    {
        if (scene_flow_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        return scene_flow_->RequestQuitApplication(reason);
    }

    bool RuntimeContext::SceneTransitionInProgress() const noexcept
    {
        return scene_flow_ != nullptr && scene_flow_->SceneTransitionInProgress();
    }

    float RuntimeContext::SceneTransitionProgress() const noexcept
    {
        if (scene_flow_ == nullptr) return 0.0f;
        const float value = scene_flow_->SceneTransitionProgress();
        if (value < 0.0f) return 0.0f;
        if (value > 1.0f) return 1.0f;
        return value;
    }

    RuntimeStatus RuntimeContext::SceneTransitionStatus() const noexcept
    {
        if (scene_flow_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (scene_flow_->SceneTransitionInProgress())
            return RuntimeStatus::TransitionInProgress;
        return scene_flow_->LastSceneTransitionStatus();
    }

    const std::string& RuntimeContext::CurrentSceneGuid() const noexcept
    {
        // 未接続でも参照を返せるようにするための空文字列。
        static const std::string empty;
        return scene_flow_ != nullptr ? scene_flow_->CurrentSceneGuid() : empty;
    }

    // ---- Prefab -------------------------------------------------------------

    RuntimeStatus RuntimeContext::InstantiatePrefab(const std::string& asset_guid,
        const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation_euler,
        const DirectX::XMFLOAT3& scale, const ObjectHandle& parent, ObjectHandle& out)
    {
        if (asset_guid.empty()) return RuntimeStatus::InvalidArgument;

        // 未接続なら「できません」と返す。何もせず成功を返さない。
        if (prefab_instantiator_ == nullptr) return RuntimeStatus::ServiceUnavailable;

        ObjectID parent_id = ObjectID::Invalid();
        if (!parent.IsEmpty())
        {
            RuntimeStatus status = RuntimeStatus::Ok;
            const GameObject* parent_object = ResolveObject(parent, status);
            if (parent_object == nullptr) return status;
            parent_id = parent_object->ID();
        }

        ObjectID created = ObjectID::Invalid();
        const RuntimeStatus status = prefab_instantiator_->InstantiatePrefab(
            asset_guid, *world_, position, rotation_euler, scale, parent_id, created);
        if (Failed(status)) return status;

        out = resolver_.FindByObjectID(created);
        return out.IsEmpty() ? RuntimeStatus::SceneLoadFailed : RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InstantiatePrefabDeferred(const std::string& asset_guid,
        const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation_euler,
        const DirectX::XMFLOAT3& scale, const ObjectHandle& parent)
    {
        if (asset_guid.empty()) return RuntimeStatus::InvalidArgument;
        if (prefab_instantiator_ == nullptr) return RuntimeStatus::ServiceUnavailable;

        PendingInstantiation pending;
        pending.asset_guid = asset_guid;
        pending.position = position;
        pending.rotation = rotation_euler;
        pending.scale = scale;

        // 親は ObjectID で覚える。Handle のまま持つと、
        // 実行までの間に World が入れ替わった場合の判定が二重になる。
        // World が変われば ObjectID の解決自体が失敗するので、これで足りる。
        if (!parent.IsEmpty())
        {
            RuntimeStatus status = RuntimeStatus::Ok;
            const GameObject* parent_object = ResolveObject(parent, status);
            if (parent_object == nullptr) return status;
            pending.parent = parent_object->ID();
        }

        pending_instantiations_.push_back(std::move(pending));
        return RuntimeStatus::Ok;
    }

    void RuntimeContext::FlushDeferredOperations()
    {
        if (pending_instantiations_.empty()) return;
        if (prefab_instantiator_ == nullptr)
        {
            // 実行できないまま溜め続けない。捨てたことはログへ残す。
            LogWarning("Prefab の生成要求を破棄しました（Instantiator が未接続）。");
            pending_instantiations_.clear();
            return;
        }

        // 引き取ってから実行する。実行中に新しい要求が積まれても、
        // それは次の同期点で処理される（同じフレームで無限に増えない）。
        std::vector<PendingInstantiation> batch;
        batch.swap(pending_instantiations_);

        for (const PendingInstantiation& pending : batch)
        {
            ObjectID created = ObjectID::Invalid();
            const RuntimeStatus status = prefab_instantiator_->InstantiatePrefab(
                pending.asset_guid, *world_, pending.position, pending.rotation,
                pending.scale, pending.parent, created);
            if (Failed(status))
            {
                LogWarning(std::string("Prefab の生成に失敗しました: ") +
                    ToString(status) + " (" + pending.asset_guid + ")");
            }

            // 番号付きで積まれた要求だけ、結果を引き取れるように残す。
            if (pending.request == 0) continue;
            if (spawn_results_.size() >= maximum_spawn_results)
            {
                spawn_results_.erase(spawn_results_.begin());
                LogWarning("引き取られない Spawn 結果が上限を超えたため古いものを捨てました。");
            }
            SpawnResult result;
            result.request = pending.request;
            result.created = created;
            result.status = status;
            spawn_results_.push_back(result);
        }
    }

    RuntimeStatus RuntimeContext::InstantiatePrefabDeferredTracked(
        const std::string& asset_guid, const DirectX::XMFLOAT3& position,
        const DirectX::XMFLOAT3& rotation_euler, const DirectX::XMFLOAT3& scale,
        const ObjectHandle& parent, SpawnRequestID& out_request)
    {
        out_request = 0;
        const RuntimeStatus status = InstantiatePrefabDeferred(
            asset_guid, position, rotation_euler, scale, parent);
        if (Failed(status)) return status;

        // 直前に積んだ要求へ番号を付ける。積めていれば必ず末尾にある。
        if (pending_instantiations_.empty()) return RuntimeStatus::UnsupportedOperation;
        out_request = next_spawn_request_++;
        pending_instantiations_.back().request = out_request;
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::TryTakeSpawnResult(SpawnRequestID request,
        ObjectHandle& out)
    {
        out = ObjectHandle::None();
        if (request == 0) return RuntimeStatus::InvalidArgument;

        for (std::size_t index = 0; index < spawn_results_.size(); ++index)
        {
            if (spawn_results_[index].request != request) continue;

            const SpawnResult result = spawn_results_[index];
            spawn_results_.erase(spawn_results_.begin() +
                static_cast<std::ptrdiff_t>(index));
            if (Failed(result.status)) return result.status;

            GameObject* created = world_->FindGameObjectByID(result.created);
            if (created == nullptr) return RuntimeStatus::ObjectDestroyed;
            out = resolver_.MakeHandle(created);
            return RuntimeStatus::Ok;
        }

        // まだ Flush されていないか、既に引き取り済み。
        for (const PendingInstantiation& pending : pending_instantiations_)
        {
            if (pending.request == request) return RuntimeStatus::TransitionInProgress;
        }
        return RuntimeStatus::ComponentNotFound;
    }
}
