#pragma once

#include "ICameraBasisProvider.h"
#include "IPhysicsDynamicsService.h"
#include "IPhysicsQueryService.h"
#include "IInputService.h"
#include "RespawnService.h"
#include "../../Core/ObjectID/ObjectID.h"

namespace ReplayEngine::Runtime
{
    class RuntimeContext;
    class RuntimeSceneService;
    class SceneFlowService;
}
namespace ReplayEngine::Scripting { class IScriptServices; }
namespace ReplayEngine::Audio { class IAudioPlaybackService; }
namespace ReplayEngine::Motion { class MotionMixer; }

namespace ReplayEngine::Scene
{
    class ILoadingProgressProvider
    {
    public:
        virtual ~ILoadingProgressProvider() = default;
        virtual float Progress() const noexcept = 0;
        virtual bool IsLoading() const noexcept = 0;
    };

    // Scene が Component へ提供する外部サービスの束。
    //
    // Singleton ではない。Scene が値メンバとして 1 つ持ち、
    // 中身はすべて「非所有の生ポインタ」。実体は framework 側が所有する。
    // 未設定なら nullptr のままで、Component は必ず null 確認をしてから使う。
    //
    // Component からの参照経路:
    //   Component -> GetScene() -> Services() -> Physics() / CameraBasis()
    //
    // これにより、Gameplay Component が Camera や Stage の具象型を
    // include しないまま、必要な情報だけを受け取れる。
    class SceneServices final
    {
    public:
        const ICameraBasisProvider* CameraBasis() const noexcept { return camera_basis_; }
        void SetCameraBasis(const ICameraBasisProvider* provider) noexcept
        {
            camera_basis_ = provider;
        }

        const IPhysicsQueryService* Physics() const noexcept { return physics_; }
        void SetPhysics(const IPhysicsQueryService* service) noexcept { physics_ = service; }

        const IPhysicsDynamicsService* PhysicsDynamics() const noexcept
        {
            return physics_dynamics_;
        }
        void SetPhysicsDynamics(const IPhysicsDynamicsService* service) noexcept
        {
            physics_dynamics_ = service;
        }

        const IInputService* Input() const noexcept { return input_; }
        void SetInput(const IInputService* service) noexcept { input_ = service; }

        Audio::IAudioPlaybackService* Audio() const noexcept { return audio_; }
        void SetAudio(Audio::IAudioPlaybackService* service) noexcept { audio_ = service; }

        const Motion::MotionMixer* MotionMixer() const noexcept { return motion_mixer_; }
        void SetMotionMixer(const Motion::MotionMixer* mixer) noexcept
        {
            motion_mixer_ = mixer;
        }

        // Runtime API への入口。
        //
        // 未接続 (nullptr) がありうるのは意図的:
        //   Editor で Scene を編集しているだけの状態では接続しない。
        //   Behaviour が「置いただけで Runtime API を叩き始める」ことを
        //   構造的に防ぐ。実体は framework が所有し、World の入れ替えのたびに
        //   接続し直す。ここは非所有参照だけを持つ。
        Runtime::RuntimeContext* Runtime() const noexcept { return runtime_; }
        void SetRuntime(Runtime::RuntimeContext* context) noexcept { runtime_ = context; }

        // Scene 遷移の実体へ触れる入口。SceneLoaderComponent が進捗を読むために使う。
        // 実体は framework / RuntimeSceneService が所有し、ここは非所有参照だけを持つ。
        Runtime::RuntimeSceneService* RuntimeScene() const noexcept
        {
            return runtime_scene_;
        }
        void SetRuntimeScene(Runtime::RuntimeSceneService* service) noexcept
        {
            runtime_scene_ = service;
        }

        // SceneFlow の状態を読む入口。遷移要求そのものは既存の SceneFlowService が受け持つ。
        Runtime::SceneFlowService* SceneFlow() const noexcept { return scene_flow_; }
        void SetSceneFlow(Runtime::SceneFlowService* service) noexcept
        {
            scene_flow_ = service;
        }

        const ILoadingProgressProvider* LoadingProgress() const noexcept
        {
            return loading_progress_;
        }
        void SetLoadingProgress(const ILoadingProgressProvider* provider) noexcept
        {
            loading_progress_ = provider;
        }

        // スクリプト機構への入口。Runtime と同じ扱いで、非所有参照だけを持つ。
        //
        // 未接続 (nullptr) がありうるのも同じ理由:
        //   Editor で Scene を編集しているだけの状態では接続しない。
        //   ScriptComponent が「置いただけで動き出す」ことを構造的に防ぐ。
        //
        // Component からの参照経路:
        //   ScriptComponent -> GetScene() -> Services() -> Scripts()
        Scripting::IScriptServices* Scripts() const noexcept { return scripts_; }
        void SetScripts(Scripting::IScriptServices* services) noexcept { scripts_ = services; }

        // 操作対象の GameObject。
        // 「プレイヤーかどうか」を型ではなく ObjectID で表す。
        // 人型からメカ・ドローンへ切り替えても、GameObject のクラス型は変わらない。
        //
        // この ID が無効なとき、Scene には操作対象が居ない。
        // 代わりに別の GameObject を探したり、何かを自動生成したりはしない。
        Core::ObjectID ControlledObject() const noexcept { return controlled_object_; }
        void SetControlledObject(Core::ObjectID id) noexcept { controlled_object_ = id; }

        // ---- Stage Gameplay -------------------------------------------------
        //
        // 復帰地点と出来事の記録。Scene が値で持つので Singleton にならない。
        // Play 用 Scene と編集 Scene がそれぞれ自分のぶんを持つ。
        RespawnService& Respawn() noexcept { return respawn_; }
        const RespawnService& Respawn() const noexcept { return respawn_; }

        GameplayEventLog& GameplayEvents() noexcept { return gameplay_events_; }
        const GameplayEventLog& GameplayEvents() const noexcept { return gameplay_events_; }

        // Play Mode 中かどうか。
        // 入力を受け付けてよいか、物理を進めてよいかの判断に使う。
        bool Playing() const noexcept { return playing_; }
        void SetPlaying(bool value) noexcept { playing_ = value; }

        void Reset() noexcept
        {
            camera_basis_ = nullptr;
            physics_ = nullptr;
            physics_dynamics_ = nullptr;
            input_ = nullptr;
            audio_ = nullptr;
            motion_mixer_ = nullptr;
            runtime_ = nullptr;
            runtime_scene_ = nullptr;
            scene_flow_ = nullptr;
            loading_progress_ = nullptr;
            scripts_ = nullptr;
            controlled_object_ = Core::ObjectID::Invalid();
            playing_ = false;
        }

    private:
        const ICameraBasisProvider* camera_basis_ = nullptr;
        const IPhysicsQueryService* physics_ = nullptr;
        const IPhysicsDynamicsService* physics_dynamics_ = nullptr;
        const IInputService* input_ = nullptr;
        Audio::IAudioPlaybackService* audio_ = nullptr;
        const Motion::MotionMixer* motion_mixer_ = nullptr;
        Runtime::RuntimeContext* runtime_ = nullptr;
        Runtime::RuntimeSceneService* runtime_scene_ = nullptr;
        Runtime::SceneFlowService* scene_flow_ = nullptr;
        const ILoadingProgressProvider* loading_progress_ = nullptr;
        Scripting::IScriptServices* scripts_ = nullptr;
        Core::ObjectID controlled_object_;
        RespawnService respawn_;
        GameplayEventLog gameplay_events_;
        bool playing_ = false;
    };
}
