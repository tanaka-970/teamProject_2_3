#include "AnimationUndoValidation.h"

#include "../Commands/SceneEditHistory.h"
#include "../Commands/MotionEditHistory.h"
#include "../Core/EditorContext.h"
#include "../../Object/Component/ComponentTypeID.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/BuiltInComponents.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Serialization/SceneData.h"

#include <cstdio>
#include <string>

namespace ReplayEngine::Editor::Validation
{
    namespace
    {
        using Core::GameObject;
        using Reflection::MakeProperty;
        using Reflection::PropertyRegistry;

        class Checker final
        {
        public:
            explicit Checker(int first_code) : next_code_(first_code) {}

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

        // 「毎フレーム進む Component」の代役。
        //
        // 本物の AnimatorComponent を使わないのは、そちらが
        // Animation Clip / SkinnedMesh / GPU リソースを必要とし、
        // ヘッドレスの検証に載らないため。
        //
        // 確かめたいのは Animator 固有の処理ではなく
        // 「Undo のあとも Scene が Component を更新し続けるか」なので、
        // 更新回数を数えるだけの Component で十分かつ確実。
        class TickProbeComponent final : public Core::Component
        {
            REPLAY_COMPONENT_BODY(TickProbeComponent)

        public:
            // 保存対象。Undo で値が戻ることの確認にも使う。
            float speed = 1.0f;

            // 実行時専用。Scene が更新を回しているかを表す。
            float elapsed = 0.0f;
            int update_count = 0;
            int awake_count = 0;
            int start_count = 0;

        private:
            void OnRuntimeAwake() override { ++awake_count; }
            void OnStart() override { ++start_count; }
            void OnUpdate(float delta_time) override
            {
                ++update_count;
                elapsed += delta_time * speed;
            }
        };

        void EnsureProbeRegistered()
        {
            static bool registered = false;
            if (registered) return;
            registered = true;

            Core::ComponentRegistry::Register<TickProbeComponent>(
                Core::ComponentTypeInfo::Describe("Tick Probe", "Validation")
                    .HiddenInEditor());

            PropertyRegistry::Register<TickProbeComponent>(
                MakeProperty("speed", &TickProbeComponent::speed));
        }

        // Scene 上の最初の TickProbe を引き直す。
        //
        // Undo は Scene を作り直すので、前に持っていたポインタは無効になる。
        // 毎回引き直すこと自体が「生ポインタを跨いで持たない」規約の確認になる。
        TickProbeComponent* FindProbe(Scene::Scene& scene)
        {
            for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
            {
                GameObject* object = scene.GameObjectAt(index);
                if (object == nullptr) continue;
                if (auto* probe = object->GetComponent<TickProbeComponent>()) return probe;
            }
            return nullptr;
        }
    }

    int RunAnimationUndoValidation()
    {
        Core::RegisterBuiltInComponents();
        EnsureProbeRegistered();

        Checker check(800);

        Scene::Scene world("AnimationUndoWorld");
        EditorContext context;
        context.AttachScene(&world);

        GameObject* object = world.CreateGameObject("Animated");
        check.Expect(object != nullptr, "検証用 GameObject を作れる");
        if (object == nullptr) return check.Report("animation-undo");

        auto* probe = object->AddComponent<TickProbeComponent>();
        check.Expect(probe != nullptr, "更新を数える Component を追加できる");
        if (probe == nullptr) return check.Report("animation-undo");

        // ---- 実行開始 ---------------------------------------------------------

        world.Start();
        check.Expect(world.Started(), "Scene を開始できる");

        world.Update(0.016f);
        world.Update(0.016f);
        check.Expect(FindProbe(world) != nullptr && FindProbe(world)->update_count == 2,
            "開始後は毎フレーム更新される");

        // ---- 編集を 1 件積む ---------------------------------------------------

        context.BeginEdit("速度を変更");
        FindProbe(world)->speed = 2.0f;
        context.CommitEdit();

        world.Update(0.016f);
        check.Expect(world.Started(), "編集を確定しても実行状態は保たれる");

        const int before_undo = FindProbe(world)->update_count;
        check.Expect(before_undo == 3, "編集後も更新が続いている");

        // ---- ここが本題。Undo のあとも更新が続くか ------------------------------

        check.Expect(context.Undo(), "Undo できる");

        check.Expect(world.Started(),
            "Undo のあとも Scene の実行状態が保たれる（started_ が false のまま残らない）");

        TickProbeComponent* after_undo = FindProbe(world);
        check.Expect(after_undo != nullptr, "Undo 後も Component が存在する");
        if (after_undo == nullptr) return check.Report("animation-undo");

        check.Expect(after_undo->speed == 1.0f, "Undo で保存値が戻る");
        check.Expect(after_undo->awake_count == 1,
            "作り直された Component へ Awake が届く");

        const int base = after_undo->update_count;
        world.Update(0.016f);
        world.Update(0.016f);
        world.Update(0.016f);

        TickProbeComponent* ticking = FindProbe(world);
        check.Expect(ticking != nullptr && ticking->update_count == base + 3,
            "Undo のあとも毎フレーム更新され続ける（これが止まると Animation が凍る）");
        check.Expect(ticking != nullptr && ticking->elapsed > 0.0f,
            "Undo のあとも時間が進む");

        // ---- Redo でも同じ -----------------------------------------------------

        check.Expect(context.Redo(), "Redo できる");
        check.Expect(world.Started(), "Redo のあとも実行状態が保たれる");

        TickProbeComponent* redone = FindProbe(world);
        check.Expect(redone != nullptr && redone->speed == 2.0f, "Redo で値が戻る");

        const int redo_base = redone->update_count;
        world.Update(0.016f);
        check.Expect(FindProbe(world)->update_count == redo_base + 1,
            "Redo のあとも更新され続ける");

        // ---- 最初の状態まで戻してから、もう一度 Undo ----------------------------
        //
        // 報告された再現手順のひとつ。履歴の先頭で余分に Undo を押しても
        // 状態が壊れないことを確かめる。

        check.Expect(context.Undo(), "履歴の先頭まで戻せる");
        check.Expect(!context.Undo(), "先頭でさらに Undo しても失敗として返る");
        check.Expect(world.Started(), "空振りの Undo で実行状態が壊れない");

        const int tail_base = FindProbe(world)->update_count;
        world.Update(0.016f);
        world.Update(0.016f);
        check.Expect(FindProbe(world)->update_count == tail_base + 2,
            "空振りの Undo のあとも更新され続ける");

        // ---- Undo / Redo を 20 往復 ---------------------------------------------

        for (int index = 0; index < 20; ++index)
        {
            context.Redo();
            world.Update(0.016f);
            context.Undo();
            world.Update(0.016f);
        }
        check.Expect(world.Started(), "20 往復しても実行状態が保たれる");

        const int loop_base = FindProbe(world)->update_count;
        world.Update(0.016f);
        check.Expect(FindProbe(world)->update_count == loop_base + 1,
            "20 往復のあとも更新され続ける");

        // ---- まだ開始していない Scene では動き出さないこと ------------------------
        //
        // 無条件に Start() を呼ぶ実装だと、ここで動き出してしまう。

        {
            Scene::Scene idle("IdleWorld");
            EditorContext idle_context;
            idle_context.AttachScene(&idle);

            GameObject* idle_object = idle.CreateGameObject("Idle");
            check.Expect(idle_object != nullptr, "未開始 Scene に GameObject を作れる");
            if (idle_object != nullptr)
            {
                idle_object->AddComponent<TickProbeComponent>();

                idle_context.BeginEdit("未開始 Scene の編集");
                FindProbe(idle)->speed = 3.0f;
                idle_context.CommitEdit();

                check.Expect(!idle.Started(), "編集しただけでは開始しない");
                check.Expect(idle_context.Undo(), "未開始 Scene でも Undo できる");
                check.Expect(!idle.Started(),
                    "未開始の Scene は Undo しても開始しない（勝手に動き出さない）");

                idle.Update(0.016f);
                TickProbeComponent* idle_probe = FindProbe(idle);
                check.Expect(idle_probe != nullptr && idle_probe->update_count == 0,
                    "未開始のままなので更新もされない");
            }
        }

        {
            Motion::MotionAsset asset;
            Motion::MotionTrack track;
            Motion::MotionKeyframe key;
            key.value = Reflection::PropertyValue::MakeFloat(1.0f);
            track.keys.push_back(key);
            asset.tracks.push_back(track);
            MotionEditHistory history;
            std::string label;

            auto reset = [&]()
            {
                asset = Motion::MotionAsset{};
                Motion::MotionTrack fresh_track;
                Motion::MotionKeyframe fresh_key;
                fresh_key.value = Reflection::PropertyValue::MakeFloat(1.0f);
                fresh_track.keys.push_back(fresh_key);
                asset.tracks.push_back(fresh_track);
                history.Clear();
                label.clear();
            };

            history.Begin(asset, u8"Wiggle有効を変更");
            asset.tracks[0].wiggle.enabled = true;
            history.Commit(asset);
            check.Expect(history.Undo(asset, label), u8"Wiggle enabled だけの変更を Undo できる");
            check.Expect(!asset.tracks[0].wiggle.enabled, u8"Wiggle enabled が Undo で元へ戻る");

            reset();
            history.Begin(asset, u8"Wiggle振幅を変更");
            asset.tracks[0].wiggle.amplitude = 2.5f;
            history.Commit(asset);
            check.Expect(history.Undo(asset, label), u8"Wiggle amplitude だけの変更を Undo できる");
            check.Expect(asset.tracks[0].wiggle.amplitude == 0.0f, u8"Wiggle amplitude が Undo で元へ戻る");

            reset();
            history.Begin(asset, u8"Wiggle周波数を変更");
            asset.tracks[0].wiggle.frequency = 7.0f;
            history.Commit(asset);
            check.Expect(history.Undo(asset, label), u8"Wiggle frequency だけの変更を Undo できる");
            check.Expect(asset.tracks[0].wiggle.frequency == 2.0f, u8"Wiggle frequency が Undo で元へ戻る");

            reset();
            history.Begin(asset, u8"Wiggleシードを変更");
            asset.tracks[0].wiggle.seed = 91;
            history.Commit(asset);
            check.Expect(history.Undo(asset, label), u8"Wiggle seed だけの変更を Undo できる");
            check.Expect(asset.tracks[0].wiggle.seed == 0, u8"Wiggle seed が Undo で元へ戻る");

            reset();
            history.Begin(asset, u8"Wiggleオクターブを変更");
            asset.tracks[0].wiggle.octaves = 4;
            history.Commit(asset);
            check.Expect(history.Undo(asset, label), u8"Wiggle octaves だけの変更を Undo できる");
            check.Expect(asset.tracks[0].wiggle.octaves == 1, u8"Wiggle octaves が Undo で元へ戻る");

            reset();
            history.Begin(asset, u8"トラックループを変更");
            asset.tracks[0].loop = Motion::MotionTrackLoop::Repeat;
            history.Commit(asset);
            check.Expect(history.Undo(asset, label), u8"loop だけの変更を Undo できる");
            check.Expect(asset.tracks[0].loop == Motion::MotionTrackLoop::None, u8"loop が Undo で元へ戻る");

            reset();
            history.Begin(asset, u8"カーブ参照を変更");
            asset.tracks[0].keys[0].easing_curve.guid = "undo-easing-curve-guid";
            history.Commit(asset);
            check.Expect(history.Undo(asset, label), u8"easing_curve だけの変更を Undo できる");
            check.Expect(asset.tracks[0].keys[0].easing_curve.guid.empty(), u8"easing_curve が Undo で元へ戻る");

            reset();
            history.Begin(asset, u8"ブレンドモードを変更");
            asset.tracks[0].blend_mode = Motion::MotionBlendMode::Additive;
            history.Commit(asset);
            check.Expect(history.Undo(asset, label), u8"blend_mode だけの変更を Undo できる");
            check.Expect(asset.tracks[0].blend_mode == Motion::MotionBlendMode::Override, u8"blend_mode が Undo で元へ戻る");

            reset();
            history.Begin(asset, u8"時間リマップを変更");
            asset.time_remap.guid = "undo-time-remap-guid";
            history.Commit(asset);
            check.Expect(history.Undo(asset, label), u8"time_remap だけの変更を Undo できる");
            check.Expect(asset.time_remap.guid.empty(), u8"time_remap が Undo で元へ戻る");

            reset();
            history.Begin(asset, u8"変更なし");
            history.Commit(asset);
            check.Expect(!history.CanUndo(), u8"変更なしの Begin/Commit は履歴を増やさない");
        }

        // ---- Play 中は Undo / Redo を実行しないこと -------------------------------

        {
            context.SetPlayMode(true);
            const int play_base = FindProbe(world)->update_count;

            check.Expect(!context.Undo(), "実行中は Undo を実行しない");
            check.Expect(!context.Redo(), "実行中は Redo を実行しない");
            check.Expect(!context.Status().empty(),
                "実行中に押した理由が Status へ出る（黙って無視しない）");
            check.Expect(world.Started(), "実行中の Undo 要求で実行状態が壊れない");

            world.Update(0.016f);
            check.Expect(FindProbe(world)->update_count == play_base + 1,
                "実行中の Undo 要求のあとも更新され続ける");

            context.SetPlayMode(false);
        }

        world.Clear();
        return check.Report("animation-undo");
    }
}
