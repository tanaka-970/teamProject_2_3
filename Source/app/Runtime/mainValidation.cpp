// Runtime main のうち「検証共通処理・結果出力・検証ディスパッチ」だけを持つ。
//
// Scene / Runtime 系の検証本体は責務ごとの兄弟ファイルへ移している。
// 検証の呼び出し順と各関数の本体は元のままにする。
#include "framework.h"
#include "mainInternal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../../../RePlayEngine/Components/Motion/MotionPlayerComponent.h"
#include "../../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../../RePlayEngine/Editor/Validation/AnimationUndoValidation.h"
#include "../../../RePlayEngine/Editor/Validation/EditorIntegrationValidation.h"
#include "../../../RePlayEngine/Runtime/Validation/BehaviourValidation.h"
#include "../../../RePlayEngine/Runtime/Validation/RuntimeSceneValidation.h"
#include "../../../RePlayEngine/Runtime/Validation/SceneFlowValidation.h"
#include "../../../RePlayEngine/Runtime/Validation/SerializationValidation.h"
#include "../../../RePlayEngine/Runtime/Validation/StressValidation.h"
#include "../../../RePlayEngine/Scripting/Validation/CSharpScriptValidation.h"
#include "../../../RePlayEngine/Scripting/Validation/ScriptCoreValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderAssetValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderBuiltInValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderMaterialValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderRenderValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderTextureValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderEditorValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderLightingValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderLayerValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderPassValidation.h"
#include "../../../RePlayEngine/Rendering/ShaderComposer/ShaderComposerValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderCompileValidation.h"
#include "../../game/Behaviours/ValidationBehaviours.h"
#include "../../game/game_input.h"

namespace ReplayEngine::Runtime::Detail
{
    std::filesystem::path ValidationFolder()
    {
        return framework::shutdown_log_folder() / "Validation";
    }

    void WriteValidationResultFile(const char* file_name, const char* header,
        bool ok, const std::vector<std::string>& lines)
    {
        const std::filesystem::path folder = ValidationFolder();
        std::error_code directory_error;
        std::filesystem::create_directories(folder, directory_error);

        std::ofstream report(folder / file_name, std::ios::binary | std::ios::trunc);
        if (!report) return;

        report << header << " 1\n";
        report << "RESULT " << (ok ? "OK" : "NG") << '\n';
        for (const std::string& line : lines) report << line << '\n';
    }
}

namespace
{
    long long CountInclusive(long long first, long long last) noexcept
    {
        return last >= first ? (last - first + 1) : 0;
    }

    long long CountMotionEventTriggers(int wrap_mode, double duration,
        double before, double delta_time, double speed,
        int direction_before, double event_time) noexcept
    {
        using ReplayEngine::Components::MotionPlayerComponent;

        if (duration <= 0.0 || delta_time <= 0.0 || speed == 0.0 ||
            event_time < 0.0 || event_time > duration)
        {
            return 0;
        }

        constexpr double epsilon = 1.0e-9;
        if (wrap_mode == MotionPlayerComponent::Loop)
        {
            const double travel = delta_time * speed;
            const double to = before + travel;
            if (travel > 0.0)
            {
                const long long first = static_cast<long long>(std::floor(
                    (before - event_time) / duration)) + 1;
                const long long last = static_cast<long long>(std::floor(
                    (to - event_time + epsilon) / duration));
                return CountInclusive(first, last);
            }
            if (travel < 0.0)
            {
                const long long first = static_cast<long long>(std::ceil(
                    (to - event_time - epsilon) / duration));
                const long long last = static_cast<long long>(std::ceil(
                    (before - event_time) / duration)) - 1;
                return CountInclusive(first, last);
            }
            return 0;
        }

        if (wrap_mode == MotionPlayerComponent::PingPong)
        {
            if (direction_before == 0) return 0;
            const double period = duration * 2.0;
            const double phase_from = direction_before > 0
                ? before : period - before;
            const double phase_to = phase_from + delta_time * std::fabs(speed);

            const auto count_phase_series = [phase_from, phase_to, period](double base)
            {
                const long long first = static_cast<long long>(std::floor(
                    (phase_from - base) / period)) + 1;
                const long long last = static_cast<long long>(std::floor(
                    (phase_to - base + 1.0e-9) / period));
                return CountInclusive(first, last);
            };

            long long count = count_phase_series(event_time);
            // 0 と duration は往路/復路の位相が同じなので、片側だけ数える。
            if (event_time > 0.0 && event_time < duration)
                count += count_phase_series(period - event_time);
            return count;
        }

        const double travel = delta_time * speed;
        double after = before + travel;
        after = (std::max)(0.0, (std::min)(duration, after));
        if (travel > 0.0)
            return event_time > before && event_time <= after + epsilon ? 1 : 0;
        if (travel < 0.0)
            return event_time < before && event_time + epsilon >= after ? 1 : 0;
        return 0;
    }
}

namespace ReplayEngine::Runtime::Detail
{
    int RunHeadlessInputValidation()
    {
        std::string error;
        const bool ok = GameInput::InputState::ValidateDeterministicQueries(error);

        std::vector<std::string> lines;
        lines.push_back("CHECKS pressed held released idle");
        if (!error.empty()) lines.push_back("ERROR " + error);
        WriteValidationResultFile("InputState.txt",
            "REPLAY_INPUT_STATE_VALIDATION", ok, lines);

        std::fprintf(stderr, "input validation: RESULT %s\n", ok ? "OK" : "NG");
        if (!ok && !error.empty())
            std::fprintf(stderr, "input validation error: %s\n", error.c_str());
        return ok ? 0 : 1410;
    }

    int RunHeadlessMotionEventsValidation()
    {
        using ReplayEngine::Components::MotionPlayerComponent;

        struct Case
        {
            const char* name;
            int wrap_mode;
            double duration;
            double before;
            double delta_time;
            double speed;
            int direction_before;
            double event_time;
            long long expected;
        };

        const std::vector<Case> cases = {
            { "loop_start_zero_small_delta", MotionPlayerComponent::Loop,
                1.0, 0.0, 0.1, 1.0, 1, 0.0, 0 },
            { "loop_start_zero_ten_seconds", MotionPlayerComponent::Loop,
                1.0, 0.0, 10.0, 1.0, 1, 0.0, 10 },
            { "loop_quarter_ten_seconds", MotionPlayerComponent::Loop,
                1.0, 0.0, 10.0, 1.0, 1, 0.25, 10 },
            { "reverse_loop_cross_zero", MotionPlayerComponent::Loop,
                1.0, 0.1, 0.2, -1.0, -1, 0.0, 1 },
            { "reverse_loop_start_at_duration", MotionPlayerComponent::Loop,
                1.0, 1.0, 10.0, -1.0, -1, 1.0, 10 },
            { "pingpong_duration_endpoint_once", MotionPlayerComponent::PingPong,
                1.0, 0.9, 0.2, 1.0, 1, 1.0, 1 },
            { "pingpong_zero_endpoint_once", MotionPlayerComponent::PingPong,
                1.0, 0.1, 0.2, 1.0, -1, 0.0, 1 },
        };

        bool ok = true;
        std::vector<std::string> lines;
        lines.reserve(cases.size() + 1);
        for (const Case& test : cases)
        {
            const long long actual = CountMotionEventTriggers(test.wrap_mode,
                test.duration, test.before, test.delta_time, test.speed,
                test.direction_before, test.event_time);
            const bool passed = actual == test.expected;
            ok = ok && passed;

            std::ostringstream row;
            row << "CASE " << test.name
                << " EXPECTED " << test.expected
                << " ACTUAL " << actual
                << " " << (passed ? "OK" : "NG");
            lines.push_back(row.str());
        }

        WriteValidationResultFile("MotionEvents.txt",
            "REPLAY_MOTION_EVENTS_VALIDATION", ok, lines);

        std::fprintf(stderr, "motion-events validation: RESULT %s (%zu cases)\n",
            ok ? "OK" : "NG", cases.size());
        return ok ? 0 : 1420;
    }

    // Phase 2 (Serialization Foundation) の検証。
    // どれもファイルを触らず、メモリ上の文字列で往復を確かめる。
    // 既存の Scene / Prefab 原本は一切変更しない。
    int RunHeadlessSerializationValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command)) return -1;

        namespace Validation = ReplayEngine::Runtime::Validation;
        if (command == "--validate-serialization")
        {
            return Validation::RunSerializationValidation();
        }
        if (command == "--validate-missing-component")
        {
            return Validation::RunMissingComponentValidation();
        }
        if (command == "--validate-scene-version")
        {
            return Validation::RunSceneVersionValidation();
        }
        if (command == "--validate-input")
        {
            return RunHeadlessInputValidation();
        }
        if (command == "--validate-motion-events")
        {
            return RunHeadlessMotionEventsValidation();
        }
        if (command == "--validate-physics")
        {
            return RunHeadlessPhysicsValidation(command_line);
        }
        if (command == "--validate-motion-trigger")
        {
            return RunHeadlessMotionTriggerValidation(command_line);
        }
        if (command == "--validate-property-link")
        {
            return RunHeadlessPropertyLinkValidation(command_line);
        }
        if (command == "--validate-scene-persistence")
        {
            return RunHeadlessScenePersistenceValidation(command_line);
        }

        // Phase 3-5。Behaviour を使う検証は、先にゲーム側の登録を通しておく。
        if (command == "--validate-behaviour")
        {
            ReplayEngine::Core::RegisterBuiltInComponents();
            Game::RegisterGameBehaviours();
            return Validation::RunBehaviourValidation();
        }
        if (command == "--validate-events")
        {
            ReplayEngine::Core::RegisterBuiltInComponents();
            Game::RegisterGameBehaviours();
            return Validation::RunEventValidation();
        }
        if (command == "--validate-runtime-api")
        {
            ReplayEngine::Core::RegisterBuiltInComponents();
            Game::RegisterGameBehaviours();
            return Validation::RunRuntimeApiValidation();
        }
        if (command == "--validate-collision")
        {
            ReplayEngine::Core::RegisterBuiltInComponents();
            Game::RegisterGameBehaviours();
            return Validation::RunCollisionValidation();
        }

        // Phase 6。Runtime Scene の読み込みと入れ替え。
        // 検証用の Scene ファイルは Saved/Validation/RuntimeScene/ へ
        // その場で書き出すため、既存の Scene 原本には触れない。
        if (command == "--validate-runtime-scene")
        {
            ReplayEngine::Core::RegisterBuiltInComponents();
            Game::RegisterGameBehaviours();
            return Validation::RunRuntimeSceneValidation();
        }

        // Phase 7。Scene Flow / Startup Scene / SceneTransitionBehaviour。
        //
        // 2 段に分けている理由:
        //   前半 (460-507) は Engine 側だけで完結する検証。
        //   後半 (508-519) は Game Module の SceneTransitionBehaviour を触る。
        //   Engine 側の Validation から Game の型を参照すると
        //   「Engine が特定のゲームを知っている」依存ができるため、
        //   呼び分けをここで行う。
        if (command == "--validate-scene-flow")
        {
            ReplayEngine::Core::RegisterBuiltInComponents();
            Game::RegisterGameBehaviours();
            const int engine_result = Validation::RunSceneFlowValidation();
            if (engine_result != 0) return engine_result;
            return Game::RunSceneTransitionValidation(508);
        }

        // Phase 8。Editor 統合。
        //
        // ImGui の操作そのものは自動化していない。
        // UI が呼ぶのと同じ内部 API を叩き、データが壊れないことを確かめる。
        if (command == "--validate-editor-integration")
        {
            ReplayEngine::Core::RegisterBuiltInComponents();
            Game::RegisterGameBehaviours();
            return ReplayEngine::Editor::Validation::RunEditorIntegrationValidation();
        }

        // Phase 9。反復と大量データの耐久検査。
        if (command == "--validate-stress")
        {
            ReplayEngine::Core::RegisterBuiltInComponents();
            Game::RegisterGameBehaviours();
            return Validation::RunStressValidation();
        }

        // Pre-Scripting Stabilization / Phase A-1。
        //
        // Undo / Redo のあとも Scene の実行状態が保たれることを、
        // Component の更新回数で直接確かめる。
        // 終了コード帯は 800-859。
        if (command == "--validate-animation-undo")
        {
            ReplayEngine::Core::RegisterBuiltInComponents();
            return ReplayEngine::Editor::Validation::RunAnimationUndoValidation();
        }

        // Script Phase 1。共通スクリプト基盤。
        //
        // Lua も .NET も使わない。MockScriptBackend の 2 種類のスクリプト型で、
        // Schema の共有・ライフサイクル順序・保存・複製・値の保護を確かめる。
        // 終了コード帯は 620-799。
        {
            namespace ScriptValidation = ReplayEngine::Scripting::Validation;

            if (command == "--validate-script-core")
            {
                ReplayEngine::Core::RegisterBuiltInComponents();
                return ScriptValidation::RunScriptCoreValidation();
            }
            if (command == "--validate-script-lifecycle")
            {
                ReplayEngine::Core::RegisterBuiltInComponents();
                return ScriptValidation::RunScriptLifecycleValidation();
            }
            if (command == "--validate-script-serialization")
            {
                ReplayEngine::Core::RegisterBuiltInComponents();
                return ScriptValidation::RunScriptSerializationValidation();
            }
            if (command == "--validate-csharp-scripting")
            {
                ReplayEngine::Core::RegisterBuiltInComponents();
                return ScriptValidation::RunCSharpScriptValidation();
            }
        }

        // シェーダ基盤。フェーズ 1（実行時コンパイル）。
        //
        // D3D デバイスを作らずに走る。D3DCompile はデバイス非依存なので、
        // ヘッドレスで検証できる。終了コード帯は 900-949。
        if (command == "--validate-shader-compile")
        {
            return ReplayEngine::Rendering::Validation::RunShaderCompileValidation();
        }

        // シェーダ基盤。フェーズ 2（pragma 解析 / GUID 採番 / Catalog）。
        // 終了コード帯は 950-999。
        if (command == "--validate-shader-asset")
        {
            return ReplayEngine::Rendering::Validation::RunShaderAssetValidation();
        }

        // シェーダ基盤。フェーズ 4（組み込み 5 種の移植）。
        //
        // 実プロジェクトの Shader/ を走査するので、
        // カレントディレクトリがプロジェクト直下であること。
        // 終了コードは 1200 から連番。
        if (command == "--validate-shader-builtin")
        {
            return ReplayEngine::Rendering::Validation::RunShaderBuiltInValidation();
        }

        // シェーダ基盤。フェーズ 5（MaterialAsset v3 / 旧版移行）。
        if (command == "--validate-shader-material")
        {
            return ReplayEngine::Rendering::Validation::RunShaderMaterialValidation();
        }

        // シェーダ基盤。フェーズ 11（照明モデルをShader Asset宣言へ分離）。
        if (command == "--validate-shader-lighting")
        {
            return ReplayEngine::Rendering::Validation::RunShaderLightingValidation();
        }

        // シェーダ基盤。フェーズ 6（Material -> Catalog -> Render binding）。
        if (command == "--validate-shader-render")
        {
            return ReplayEngine::Rendering::Validation::RunShaderRenderValidation();
        }

        // シェーダ基盤。フェーズ 12（Texture AssetGUID / t40+ / default）。
        if (command == "--validate-shader-texture")
        {
            return ReplayEngine::Rendering::Validation::RunShaderTextureValidation();
        }

        // シェーダ基盤。フェーズ 7（Shader Picker / Schema Inspector / 保存保持）。
        if (command == "--validate-shader-editor")
        {
            return ReplayEngine::Rendering::Validation::RunShaderEditorValidation();
        }

        // シェーダ基盤。フェーズ 10（Layer Shader Asset / GUID Stack）。
        if (command == "--validate-shader-layer")
        {
            return ReplayEngine::Rendering::Validation::RunShaderLayerValidation();
        }

        // シェーダ基盤。フェーズ 16（Material Layer / Shader-owned Pass 分離）。
        if (command == "--validate-shader-pass")
        {
            return ReplayEngine::Rendering::Validation::RunShaderPassValidation();
        }

        // Shader Composer v1 (graph save/load -> HLSL -> normal ShaderAsset compile).
        if (command == "--validate-shader-composer")
        {
            return ReplayEngine::Rendering::Validation::RunShaderComposerValidation();
        }

        return -1;
    }
}
