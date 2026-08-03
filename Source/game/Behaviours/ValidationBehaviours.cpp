#include "ValidationBehaviours.h"

#include "SceneTransitionBehaviour.h"
#include "../../../RePlayEngine/Components/Physics/SphereColliderComponent.h"
#include "../../../RePlayEngine/Object/Component/TriggerContact.h"
#include "../../../RePlayEngine/Runtime/Scene/RuntimeSceneService.h"
#include "../../../RePlayEngine/Runtime/Scene/SceneFlowService.h"
#include "../../../RePlayEngine/Scene/Runtime/Scene.h"

#include "../../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../../RePlayEngine/Object/Registry/ComponentRegistry.h"
#include "../../../RePlayEngine/Reflection/Registry/PropertyRegistry.h"
#include "../../../RePlayEngine/Runtime/API/RuntimeContext.h"
#include "../../../RePlayEngine/Runtime/Behaviour/BehaviourRegistry.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Game::Behaviours
{
    using ReplayEngine::Core::ComponentRegistry;
    using ReplayEngine::Core::ComponentTypeInfo;
    using ReplayEngine::Reflection::MakeProperty;
    using ReplayEngine::Reflection::PropertyRegistry;

    // ---- RotatorBehaviour ---------------------------------------------------

    void RotatorBehaviour::OnAwake()
    {
        // Awake の時点で Property は反映済み。ここで初期状態を整えてよい。
        accumulated_degrees = 0.0f;
    }

    void RotatorBehaviour::OnUpdate(float delta_time)
    {
        ReplayEngine::Core::GameObject* owner = Owner();
        if (owner == nullptr) return;

        float step = delta_time;
        if (use_unscaled_time)
        {
            if (const Runtime::RuntimeContext* runtime = Runtime())
            {
                step = runtime->UnscaledDeltaTime();
            }
        }

        const float degrees = degrees_per_second * step;
        accumulated_degrees += degrees;

        const float radians = degrees * 0.01745329252f;

        DirectX::XMFLOAT3 rotation = owner->GetTransform().LocalRotationEuler();
        rotation.x += axis.x * radians;
        rotation.y += axis.y * radians;
        rotation.z += axis.z * radians;
        owner->GetTransform().SetLocalRotationEuler(rotation);
    }

    // ---- TriggerCounterBehaviour --------------------------------------------

    bool TriggerCounterBehaviour::Accepts(const Runtime::TriggerEvent& event) const noexcept
    {
        if (count_trigger_side_only && !event.self_is_trigger) return false;

        // -1 は「Layer を問わない」。相手の Layer が不明 (-1) の場合も通す。
        // 判別できないことを理由に取りこぼすより、数えて診断へ残す方がよい。
        if (accepted_layer >= 0 && event.other_layer >= 0 &&
            event.other_layer != accepted_layer)
        {
            return false;
        }
        return true;
    }

    void TriggerCounterBehaviour::OnTriggerEnter(const Runtime::TriggerEvent& event)
    {
        if (!Accepts(event)) return;
        ++enter_count;
        last_other.object = event.other.object;
    }

    void TriggerCounterBehaviour::OnTriggerStay(const Runtime::TriggerEvent& event)
    {
        if (!Accepts(event)) return;
        ++stay_count;
    }

    void TriggerCounterBehaviour::OnTriggerExit(const Runtime::TriggerEvent& event)
    {
        if (!Accepts(event)) return;
        ++exit_count;
    }
}

namespace Game
{
    void RegisterGameBehaviours()
    {
        using namespace Game::Behaviours;
        using ReplayEngine::Runtime::BehaviourRegistry;

        // ---- RotatorBehaviour ------------------------------------------------

        ComponentRegistry::Register<RotatorBehaviour>(
            ComponentTypeInfo::Describe("Rotator Behaviour", "Behaviours")
                .WithTooltip("GameObject を回し続ける。Behaviour 基盤の動作確認用。")
                .WithTypeGUID(RotatorBehaviour::StaticTypeGUID())
                .InModule("Game.Behaviours"));

        PropertyRegistry::Register<RotatorBehaviour>(
            MakeProperty("axis", &RotatorBehaviour::axis)
                .Display("回転軸").Step(0.01));
        PropertyRegistry::Register<RotatorBehaviour>(
            MakeProperty("degrees_per_second", &RotatorBehaviour::degrees_per_second)
                .Display("回転速度").Unit("度/秒").Range(-1440.0, 1440.0).Step(1.0));
        PropertyRegistry::Register<RotatorBehaviour>(
            MakeProperty("use_unscaled_time", &RotatorBehaviour::use_unscaled_time)
                .Display("タイムスケールを無視").Advanced());
        PropertyRegistry::Register<RotatorBehaviour>(
            MakeProperty("accumulated_degrees", &RotatorBehaviour::accumulated_degrees)
                .Display("累積回転量").Unit("度").ReadOnly().RuntimeOnly().NotSerializable());

        BehaviourRegistry::Register(RotatorBehaviour::StaticTypeGUID(),
            BehaviourRegistry::Native());

        // ---- TriggerCounterBehaviour ------------------------------------------

        ComponentRegistry::Register<TriggerCounterBehaviour>(
            ComponentTypeInfo::Describe("Trigger Counter Behaviour", "Behaviours")
                .WithTooltip("Trigger の接触回数を数える。Trigger 配送の動作確認用。")
                .WithTypeGUID(TriggerCounterBehaviour::StaticTypeGUID())
                .InModule("Game.Behaviours"));

        PropertyRegistry::Register<TriggerCounterBehaviour>(
            MakeProperty("accepted_layer", &TriggerCounterBehaviour::accepted_layer)
                .Display("対象 Layer").AsCollisionLayer());
        PropertyRegistry::Register<TriggerCounterBehaviour>(
            MakeProperty("count_trigger_side_only",
                &TriggerCounterBehaviour::count_trigger_side_only)
                .Display("Trigger 側だけ数える").Advanced());
        PropertyRegistry::Register<TriggerCounterBehaviour>(
            MakeProperty("enter_count", &TriggerCounterBehaviour::enter_count)
                .Display("Enter 回数").ReadOnly().RuntimeOnly().NotSerializable());
        PropertyRegistry::Register<TriggerCounterBehaviour>(
            MakeProperty("stay_count", &TriggerCounterBehaviour::stay_count)
                .Display("Stay 回数").ReadOnly().RuntimeOnly().NotSerializable());
        PropertyRegistry::Register<TriggerCounterBehaviour>(
            MakeProperty("exit_count", &TriggerCounterBehaviour::exit_count)
                .Display("Exit 回数").ReadOnly().RuntimeOnly().NotSerializable());
        PropertyRegistry::Register<TriggerCounterBehaviour>(
            MakeProperty("last_other", &TriggerCounterBehaviour::last_other)
                .Display("最後の接触相手").ReadOnly().RuntimeOnly().NotSerializable());

        BehaviourRegistry::Register(TriggerCounterBehaviour::StaticTypeGUID(),
            BehaviourRegistry::Native());

        // ---- SceneTransitionBehaviour -------------------------------------------
        //
        // 検証用ではなく実際に使う Behaviour。
        // Title / Game / Result のような画面ごとの C++ クラスを作らず、
        // Scene 上の Trigger と遷移先の設定だけでゲームの流れを組めるようにする。

        ComponentRegistry::Register<SceneTransitionBehaviour>(
            ComponentTypeInfo::Describe("Scene Transition", "Behaviours")
                .WithTooltip("Trigger に入ったら Scene 遷移を要求する。"
                    "実際の切り替えは安全な同期点で行われる。")
                .WithTypeGUID(SceneTransitionBehaviour::StaticTypeGUID())
                .InModule("Game.Behaviours"));

        PropertyRegistry::Register<SceneTransitionBehaviour>(
            MakeProperty("destination_scene",
                &SceneTransitionBehaviour::destination_scene)
                .Display("遷移先 Scene")
                .Tooltip("Scene Asset だけを指定できる。パスではなく AssetGUID で保存される。"));
        PropertyRegistry::Register<SceneTransitionBehaviour>(
            MakeProperty("transition_mode", &SceneTransitionBehaviour::transition_mode)
                .Display("遷移の種類")
                .AsEnum({ "指定の Scene へ", "現在の Scene を再読み込み",
                    "1 つ前の Scene へ戻る", "アプリケーション終了要求" }));
        PropertyRegistry::Register<SceneTransitionBehaviour>(
            MakeProperty("trigger_once", &SceneTransitionBehaviour::trigger_once)
                .Display("一度だけ発火"));
        PropertyRegistry::Register<SceneTransitionBehaviour>(
            MakeProperty("accepted_layer", &SceneTransitionBehaviour::accepted_layer)
                .Display("対象 Layer").AsCollisionLayer());
        PropertyRegistry::Register<SceneTransitionBehaviour>(
            MakeProperty("require_trigger_side",
                &SceneTransitionBehaviour::require_trigger_side)
                .Display("Trigger 側のときだけ反応").Advanced());
        PropertyRegistry::Register<SceneTransitionBehaviour>(
            MakeProperty("request_count", &SceneTransitionBehaviour::request_count)
                .Display("要求回数").ReadOnly().RuntimeOnly().NotSerializable());
        PropertyRegistry::Register<SceneTransitionBehaviour>(
            MakeProperty("rejected_count", &SceneTransitionBehaviour::rejected_count)
                .Display("拒否回数").ReadOnly().RuntimeOnly().NotSerializable());
        PropertyRegistry::Register<SceneTransitionBehaviour>(
            MakeProperty("last_diagnostic", &SceneTransitionBehaviour::last_diagnostic)
                .Display("直近の結果").ReadOnly().RuntimeOnly().NotSerializable());

        BehaviourRegistry::Register(SceneTransitionBehaviour::StaticTypeGUID(),
            BehaviourRegistry::Native());
    }

    // =====================================================================
    // SceneTransitionBehaviour の検証
    //
    // Engine 側の --validate-scene-flow の後半として main.cpp から呼ばれる。
    // Scene ファイルは Engine 側の検証が Saved/Validation/SceneFlow/ へ
    // 書き出したものを使い回す。既存の Scene 原本には触れない。
    // =====================================================================

    namespace
    {
        namespace REngine = ReplayEngine;
        namespace RRuntime = ReplayEngine::Runtime;

        // 定数は名前空間スコープへ置く（関数ローカルの constexpr を
        // キャプチャ無しラムダから参照すると MSVC が C3493 で落ちるため）。
        constexpr const char* transition_guid_title = "f10w0000000000000000000000000001";
        constexpr const char* transition_guid_game = "f10w0000000000000000000000000002";
        constexpr const char* transition_guid_corrupt = "f10w0000000000000000000000000004";

        constexpr int visitor_layer = 3;
        constexpr int unmatched_layer = 7;

        class TransitionChecker final
        {
        public:
            explicit TransitionChecker(int first_code) : next_code_(first_code) {}

            void Expect(bool condition, const char* what)
            {
                const int code = next_code_++;
                ++total_;
                if (condition) return;
                ++failures_;
                if (first_failure_ == 0) first_failure_ = code;
                std::fprintf(stderr, "  [FAIL %d] %s\n", code, what);
            }

            int Report(const char* title) const
            {
                if (first_failure_ == 0)
                {
                    std::fprintf(stderr, "%s OK: %d checks passed\n", title, total_);
                    return 0;
                }
                std::fprintf(stderr, "%s FAILED: %d/%d checks failed (first=%d)\n",
                    title, failures_, total_, first_failure_);
                return first_failure_;
            }

        private:
            int next_code_ = 0;
            int first_failure_ = 0;
            int total_ = 0;
            int failures_ = 0;
        };

        class FileSceneResolver final : public RRuntime::ISceneAssetResolver
        {
        public:
            void Map(std::string guid, const std::filesystem::path& path)
            {
                Entry entry;
                entry.guid = std::move(guid);
                entry.path = path.string();
                entries_.push_back(std::move(entry));
            }

            RRuntime::RuntimeStatus ResolveScenePath(const std::string& asset_guid,
                std::string& out_path) const override
            {
                for (const Entry& entry : entries_)
                {
                    if (entry.guid != asset_guid) continue;
                    out_path = entry.path;
                    return RRuntime::RuntimeStatus::Ok;
                }
                return RRuntime::RuntimeStatus::AssetMissing;
            }

        private:
            struct Entry
            {
                std::string guid;
                std::string path;
            };
            std::vector<Entry> entries_;
        };

        // Trigger の接触を「本物と同じ入口」から流し込む。
        //
        // Core::Component::OnTriggerEnter(TriggerContact) は public なので、
        // 基底クラスの参照から呼べば SceneCollisionWorld と同じ経路を通る。
        // BehaviourComponent が TriggerEvent へ組み直す処理も含めて検証できる。
        void DeliverTriggerEnter(REngine::Core::Component& component,
            REngine::Core::ObjectID trigger_object, REngine::Core::ObjectID other_object)
        {
            REngine::Core::TriggerContact contact;
            contact.trigger_object = trigger_object;
            contact.trigger_collider = 1;
            contact.other_object = other_object;
            contact.other_collider = 1;
            component.OnTriggerEnter(contact);
        }
    }

    int RunSceneTransitionValidation(int first_code)
    {
        using Game::Behaviours::SceneTransitionBehaviour;
        using Game::Behaviours::SceneTransitionMode;
        using REngine::Core::GameObject;
        using RRuntime::RuntimeStatus;

        TransitionChecker check(first_code);

        const std::filesystem::path folder =
            std::filesystem::path("Saved") / "Validation" / "SceneFlow";

        FileSceneResolver resolver;
        resolver.Map(transition_guid_title, folder / "Title.replayscene");
        resolver.Map(transition_guid_game, folder / "Game.replayscene");
        resolver.Map(transition_guid_corrupt, folder / "Corrupt.replayscene");

        RRuntime::RuntimeSceneService scenes;
        RRuntime::RuntimeContext runtime(scenes.ActiveWorld());
        scenes.ActiveWorld().Services().SetRuntime(&runtime);
        scenes.SetRuntimeContext(&runtime);
        scenes.SetAssetResolver(&resolver);

        RRuntime::SceneFlowService flow(scenes);

        // ---- Runtime 未接続 --------------------------------------------------
        //
        // Editor で置いただけの Behaviour は Runtime を持たない。
        // その状態で発火しても「できません」と答えるだけで、落ちも成功もしない。
        {
            REngine::Scene::Scene editor_world("EditorWorld");
            GameObject* editor_object = editor_world.CreateGameObject("Door");
            auto* offline = editor_object != nullptr
                ? editor_object->AddComponent<SceneTransitionBehaviour>() : nullptr;
            check.Expect(offline != nullptr &&
                offline->RequestTransition() == RuntimeStatus::ServiceUnavailable,
                "Runtime 未接続の Behaviour は ServiceUnavailable を返す");
        }

        // ---- 起動 -------------------------------------------------------------

        runtime.SetSceneFlow(&flow);
        flow.BeginStartupScene(transition_guid_title);
        flow.Tick();
        flow.Tick();
        check.Expect(flow.StartupState() == RRuntime::StartupSceneState::Ready,
            "Behaviour 検証用の起動 Scene を読み込める");

        GameObject* door = scenes.ActiveWorld().CreateGameObject("Door");
        GameObject* other_door = scenes.ActiveWorld().CreateGameObject("OtherDoor");
        GameObject* visitor = scenes.ActiveWorld().CreateGameObject("Visitor");

        auto* behaviour = door != nullptr
            ? door->AddComponent<SceneTransitionBehaviour>() : nullptr;
        auto* other_behaviour = other_door != nullptr
            ? other_door->AddComponent<SceneTransitionBehaviour>() : nullptr;
        auto* visitor_collider = visitor != nullptr
            ? visitor->AddComponent<REngine::Components::SphereColliderComponent>()
            : nullptr;
        check.Expect(behaviour != nullptr && other_behaviour != nullptr &&
            visitor_collider != nullptr,
            "検証用の Trigger 側 GameObject と相手の Collider を用意できる");
        if (behaviour == nullptr || other_behaviour == nullptr ||
            visitor_collider == nullptr || door == nullptr || other_door == nullptr ||
            visitor == nullptr)
        {
            return check.Report("Scene transition behaviour validation");
        }

        // 相手の Layer を確定させる。-1（不明）のままだと Layer 判定を検証できない。
        visitor_collider->collision_layer = visitor_layer;

        // 同期点を 1 回通して OnAwake を届ける。
        scenes.ActiveWorld().Update(0.0f);

        // ---- 遷移先未設定 -------------------------------------------------------

        DeliverTriggerEnter(*behaviour, door->ID(), visitor->ID());
        check.Expect(behaviour->request_count == 0 && behaviour->rejected_count == 1 &&
            !behaviour->Fired() && !behaviour->last_diagnostic.empty(),
            "遷移先が未設定なら要求を出さず、診断だけを残す");

        // ---- Trigger からの遷移要求 ----------------------------------------------

        behaviour->destination_scene = REngine::Reflection::SceneReference(
            std::string(transition_guid_game));
        other_behaviour->destination_scene = REngine::Reflection::SceneReference(
            std::string(transition_guid_title));

        DeliverTriggerEnter(*behaviour, door->ID(), visitor->ID());
        check.Expect(behaviour->request_count == 1 && behaviour->Fired() &&
            flow.TransitionInProgress() &&
            scenes.ActiveWorld().Name() == "FlowTitle",
            "Trigger から遷移要求が出るが、その場では World が入れ替わらない");

        // ---- 同一フレームの 2 件目 ------------------------------------------------

        DeliverTriggerEnter(*other_behaviour, other_door->ID(), visitor->ID());
        check.Expect(other_behaviour->request_count == 0 &&
            other_behaviour->rejected_count == 1 && !other_behaviour->Fired(),
            "同じフレームの 2 件目の Trigger は遷移中として拒否される");

        // ---- 再発火の抑止 ---------------------------------------------------------

        scenes.CancelPending();
        flow.Tick();
        DeliverTriggerEnter(*behaviour, door->ID(), visitor->ID());
        check.Expect(behaviour->request_count == 1 && behaviour->rejected_count == 2,
            "一度だけの設定なら、遷移が取り消されても再発火しない");

        behaviour->ResetFired();
        behaviour->trigger_once = false;
        behaviour->accepted_layer = unmatched_layer;
        DeliverTriggerEnter(*behaviour, door->ID(), visitor->ID());
        check.Expect(behaviour->request_count == 1,
            "対象 Layer が一致しない Trigger では発火しない");

        behaviour->accepted_layer = -1;
        behaviour->require_trigger_side = true;
        DeliverTriggerEnter(*behaviour, visitor->ID(), door->ID());
        check.Expect(behaviour->request_count == 1,
            "Trigger 側限定の設定なら、入った側として受けた接触では発火しない");

        // ---- 失敗しても Behaviour は壊れない ------------------------------------------

        behaviour->require_trigger_side = false;
        behaviour->destination_scene = REngine::Reflection::SceneReference(
            std::string(transition_guid_corrupt));
        DeliverTriggerEnter(*behaviour, door->ID(), visitor->ID());
        flow.Tick();
        check.Expect(
            flow.CurrentTransitionState() == RRuntime::SceneTransitionState::Failed &&
            behaviour->request_count == 2 &&
            door->GetComponent<SceneTransitionBehaviour>() == behaviour &&
            !behaviour->PendingDestroy(),
            "遷移に失敗しても Behaviour と所有 GameObject は生き残る");

        // ---- 終了要求モード ---------------------------------------------------------

        behaviour->transition_mode = static_cast<int>(SceneTransitionMode::QuitApplication);
        DeliverTriggerEnter(*behaviour, door->ID(), visitor->ID());
        check.Expect(flow.QuitRequested() && flow.QuitRequestCount() == 1,
            "終了要求モードの Trigger でアプリケーション終了要求が出る");
        flow.ClearQuitRequest();

        // ---- Trigger 経由の遷移が完了する ------------------------------------------

        behaviour->transition_mode = static_cast<int>(SceneTransitionMode::LoadScene);
        behaviour->destination_scene = REngine::Reflection::SceneReference(
            std::string(transition_guid_game));
        DeliverTriggerEnter(*behaviour, door->ID(), visitor->ID());
        flow.Tick();
        flow.Tick();
        check.Expect(flow.CurrentSceneGUID() == transition_guid_game &&
            scenes.ActiveWorld().Name() == "FlowGame",
            "Trigger から要求した遷移が安全点で完了する");

        return check.Report("Scene transition behaviour validation");
    }
}
