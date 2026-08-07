#pragma once

#include "../../Core/ObjectID/ObjectID.h"
#include "../../Object/Component/Component.h"
#include "../../Scene/Runtime/Scene.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    // この GameObject がカメラの追従対象になれることを表す。
    //
    // 重要: このクラス自身はカメラを一切動かさない。
    //   カメラを動かすのはカメラ制御側（現状は SceneGame）。
    //   制御側は「CameraTargetComponent を持つ GameObject」を ObjectID で探し、
    //   その Transform と、ここに書かれたオフセット設定を読むだけ
    //   これにより、カメラ側が Player 具象型を知る必要がなくなる。
    //   追従対象を人型からメカやドローンへ変えても、
    //   この Component を付け替えるだけで済む。
    class CameraTargetComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(CameraTargetComponent)

    public:
        CameraTargetComponent() = default;

      
        DirectX::XMFLOAT3 target_offset{ 0.0f, 0.0f, 0.0f };

        // 注視点のオフセット。旧来のカメラ追従は position.y + 1.0f を見ていた。
        DirectX::XMFLOAT3 look_at_offset{ 0.0f, 1.0f, 0.0f };

        // カメラを置く距離と高さ。旧 SceneGame の follow_distance / follow_height。
        float follow_distance = 6.5f;
        float follow_height = 2.25f;

        // 追従の追いつき速さ。大きいほど機敏。
        float follow_lag = 12.0f;

        // 複数の対象がある場合に、値が大きいものを優先する。
        int priority = 0;

        // Runtime Camera の Projection 設定。
        //
        // Editor の Scene Camera とは共有しない。実行時にこの Target が選ばれた
        // ときだけ SceneGame 側の Runtime Camera へ反映される。
        float field_of_view_degrees = 50.0f;
        float near_clip = 0.1f;
        float far_clip = 10000.0f;
    };

    struct CameraTargetSelection final
    {
        const Core::GameObject* object = nullptr;
        const CameraTargetComponent* component = nullptr;
        bool used_controlled_object = false;

        bool Valid() const noexcept { return object != nullptr && component != nullptr; }
    };

    inline CameraTargetSelection ResolveCameraTargetSelection(
        const Scene::Scene& scene, Core::ObjectID controlled)
    {
        if (controlled.Valid())
        {
            const Core::GameObject* object = scene.FindGameObjectByID(controlled);
            const CameraTargetComponent* component = object != nullptr
                ? object->GetComponent<CameraTargetComponent>()
                : nullptr;
            if (component != nullptr && component->ActiveInHierarchy())
            {
                return { object, component, true };
            }
        }

        CameraTargetSelection best{};
        for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
        {
            const Core::GameObject* object = scene.GameObjectAt(index);
            if (object == nullptr || object->PendingDestroy()) continue;

            const CameraTargetComponent* component =
                object->GetComponent<CameraTargetComponent>();
            if (component == nullptr || !component->ActiveInHierarchy()) continue;

            if (!best.Valid() || component->priority > best.component->priority)
            {
                best = { object, component, false };
            }
        }

        return best;
    }
}
