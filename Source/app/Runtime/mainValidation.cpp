// Runtime main のうち「検証共通処理・結果出力・検証ディスパッチ」だけを持つ。
//
// Scene / Runtime 系の検証本体は責務ごとの兄弟ファイルへ移している。
// 検証の呼び出し順と各関数の本体は元のままにする。
#include "framework.h"
#include "mainInternal.h"

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "../../../RePlayEngine/Components/Motion/MotionPlayerComponent.h"
#include "../../../RePlayEngine/Components/Core/SceneLoaderComponent.h"
#include "../../../RePlayEngine/Components/UI/RectTransformComponent.h"
#include "../../../RePlayEngine/Components/UI/UIImageComponent.h"
#include "../../../RePlayEngine/Components/UI/UISpriteAnimatorComponent.h"
#include "../../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../../RePlayEngine/Editor/Validation/AnimationUndoValidation.h"
#include "../../../RePlayEngine/Editor/Validation/EditorIntegrationValidation.h"
#include "../../../RePlayEngine/Editor/Validation/EditorCameraValidation.h"
#include "../../../RePlayEngine/Runtime/Validation/BehaviourValidation.h"
#include "../../../RePlayEngine/Runtime/Validation/RuntimeSceneValidation.h"
#include "../../../RePlayEngine/Runtime/Validation/SceneFlowValidation.h"
#include "../../../RePlayEngine/Runtime/Validation/SerializationValidation.h"
#include "../../../RePlayEngine/Runtime/Validation/StressValidation.h"
#include "../../../RePlayEngine/Scene/LoadingScene.h"
#include "../../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../../RePlayEngine/Scene/Serialization/SceneSerializer.h"
#include "../../../RePlayEngine/Motion/MotionAsset.h"
#include "../../../RePlayEngine/Motion/MotionBindingResolver.h"
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
#include "../../mesh/skinned_mesh.h"

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

    int RunHeadlessEditorHelpValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command) || command != "--validate-editor-help") return -1;

        std::string report;
        const bool ok = ReplayEngine::Editor::EditorHelp::ValidateRoundTrip(report);
        WriteValidationResultFile("EditorHelp.txt", "REPLAY_EDITOR_HELP_VALIDATION", ok,
            { report });
        std::fprintf(stderr, "EditorHelp validation: RESULT %s (%s)\n",
            ok ? "OK" : "NG", report.c_str());
        return ok ? 0 : 1800;
    }

    int RunHeadlessLoadingBridgeValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command) || command != "--validate-loading-bridge") return -1;

        int checks = 0;
        int first_failure = 0;
        const auto Check = [&checks, &first_failure](bool condition, const char* label)
        {
            ++checks;
            if (condition) return;
            if (first_failure == 0) first_failure = checks;
            std::fprintf(stderr, "  [FAIL %d] %s\n", checks, label);
        };

        ReplayEngine::Core::RegisterBuiltInComponents();
        framework host(nullptr);
        auto active_runtime = std::make_unique<ReplayEngine::Runtime::RuntimeContext>(
            host.object_runtime_scenes.ActiveWorld());
        host.object_runtime_scenes.SetRuntimeContext(active_runtime.get());
        host.object_runtime_scenes.ActiveWorld().Services().SetRuntime(active_runtime.get());
        const auto gate = std::make_shared<std::atomic<bool>>(false);
        auto loading_scene = std::make_unique<ReplayEngine::Scene::LoadingScene>();
        loading_scene->AddTask("LoadingBridge", [gate]()
        {
            while (!gate->load(std::memory_order_acquire)) Sleep(1);
            return true;
        });

        Check(host.scene_manager.SetScene(std::move(loading_scene)) &&
            host.scene_manager.IsExclusive(),
            "検証用 LoadingScene が Exclusive として設定される");
        Check(host.exclusive_scene_for_render() == nullptr,
            "専用 Scene 未読込時は既存の SceneManager 経路へ落ちる");

        const std::filesystem::path path = ValidationFolder() /
            "LoadingBridge.replayscene";
        std::error_code directory_error;
        std::filesystem::create_directories(path.parent_path(), directory_error);
        std::string error;
        ReplayEngine::Motion::MotionAsset motion_asset;
        motion_asset.name = "LoadingBridgeMotion";
        motion_asset.duration = 1.0f;
        ReplayEngine::Motion::MotionTrack motion_track;
        motion_track.name = "MoveRect";
        motion_track.binding.origin = static_cast<int>(
            ReplayEngine::Motion::MotionBindingOrigin::Self);
        motion_track.binding.component_type =
            ReplayEngine::Components::RectTransformComponent::StaticTypeID();
        motion_track.binding.property = "anchored_position";
        motion_track.value_type = ReplayEngine::Reflection::PropertyType::Vector2;
        motion_track.keys.push_back({ 0.0f,
            ReplayEngine::Reflection::PropertyValue::MakeVector2({ 0.0f, 0.0f }) });
        motion_track.keys.push_back({ 1.0f,
            ReplayEngine::Reflection::PropertyValue::MakeVector2({ 100.0f, 0.0f }) });
        motion_asset.tracks.push_back(std::move(motion_track));
        ReplayEngine::Motion::MotionEventTrack event_track;
        event_track.events.push_back({ 0.25f, "LoadingMotionEvent", "validation" });
        motion_asset.event_tracks.push_back(std::move(event_track));
        const std::filesystem::path motion_path = ValidationFolder() /
            "LoadingBridgeMotion.replaymotion";
        std::string motion_error;
        const bool motion_written = !directory_error &&
            ReplayEngine::Motion::MotionAsset::SaveToFile(
                motion_path, motion_asset, motion_error);
        Check(motion_written, "専用 Motion Asset の検証ファイルを書き出せる");
        const std::string motion_guid = motion_written
            ? host.asset_database.Register(motion_path,
                ReplayEngine::Assets::AssetKind::Motion).guid : std::string();

        ReplayEngine::Scene::Scene source_scene("LoadingBridge");
        ReplayEngine::Core::GameObject* source_loader =
            source_scene.CreateGameObject("Loader");
        ReplayEngine::Core::GameObject* source_animated =
            source_scene.CreateGameObject("Animated");
        ReplayEngine::Components::SceneLoaderComponent* source_loader_component =
            source_loader != nullptr
            ? source_loader->AddComponent<
                ReplayEngine::Components::SceneLoaderComponent>() : nullptr;
        ReplayEngine::Components::UISpriteAnimatorComponent* source_animator =
            source_animated != nullptr
            ? source_animated->AddComponent<
                ReplayEngine::Components::UISpriteAnimatorComponent>() : nullptr;
        ReplayEngine::Components::MotionPlayerComponent* source_motion =
            source_animated != nullptr
            ? source_animated->AddComponent<
                ReplayEngine::Components::MotionPlayerComponent>() : nullptr;
        ReplayEngine::Components::RectTransformComponent* source_rect =
            source_animated != nullptr
            ? source_animated->GetComponent<
                ReplayEngine::Components::RectTransformComponent>() : nullptr;
        const bool fixture_created = source_loader_component != nullptr &&
            source_animated != nullptr && source_animator != nullptr &&
            source_motion != nullptr && source_rect != nullptr;
        if (source_animator != nullptr)
        {
            source_animator->columns = 2;
            source_animator->rows = 2;
            source_animator->start_frame = 0;
            source_animator->end_frame = 3;
            source_animator->frames_per_second = 2.0f;
            source_animator->play_mode =
                ReplayEngine::Components::UISpriteAnimatorComponent::Loop;
            source_animator->playing = true;
            source_animator->frame = 0.0f;
        }
        if (source_motion != nullptr)
        {
            source_motion->motion.guid = motion_guid;
            source_motion->play_on_start = true;
            source_motion->trigger =
                ReplayEngine::Components::MotionPlayerComponent::TriggerStart;
            source_motion->speed = 1.0f;
        }
        ReplayEngine::Scene::Serialization::SceneData data;
        if (fixture_created)
            ReplayEngine::Scene::Serialization::CaptureScene(source_scene, data);
        const bool written = fixture_created && !directory_error &&
            ReplayEngine::Scene::Serialization::SceneSerializer::SaveToFile(
                data, path, error);
        Check(written, "専用 Scene の検証ファイルを書き出せる");

        ReplayEngine::Core::GameObject* runtime_probe =
            host.object_runtime_scenes.ActiveWorld().CreateGameObject("RuntimeProbe");
        ReplayEngine::Components::UIImageComponent* runtime_probe_image =
            runtime_probe != nullptr
            ? runtime_probe->AddComponent<ReplayEngine::Components::UIImageComponent>()
            : nullptr;
        ReplayEngine::Motion::MotionBinding runtime_probe_binding;
        runtime_probe_binding.origin = static_cast<int>(
            ReplayEngine::Motion::MotionBindingOrigin::Self);
        runtime_probe_binding.component_type =
            ReplayEngine::Components::UIImageComponent::StaticTypeID();
        runtime_probe_binding.property = "fill_color_2";
        const ReplayEngine::Motion::ResolvedMotionBinding resolved_probe_binding =
            runtime_probe != nullptr
            ? ReplayEngine::Motion::MotionBindingResolver::Resolve(
                host.object_runtime_scenes.ActiveWorld(), runtime_probe_binding,
                runtime_probe) : ReplayEngine::Motion::ResolvedMotionBinding{};
        host.motion_mixer.BeginFrame();
        host.motion_mixer.Contribute(resolved_probe_binding,
            ReplayEngine::Reflection::PropertyValue::MakeVector4(
                { 0.5f, 0.5f, 0.5f, 1.0f }), 1.0f);
        const bool runtime_mixer_before = runtime_probe_image != nullptr &&
            host.motion_mixer.WasDriven(*runtime_probe_image, "fill_color_2");

        const bool loaded = host.load_exclusive_scene_from_path(path);
        Check(loaded && host.exclusive_scene_for_render() != nullptr,
            "専用 Scene 読込後は専用 Scene 経路が選ばれる");

        ReplayEngine::Scene::Scene* exclusive_scene = host.exclusive_scene_for_render();
        ReplayEngine::Components::SceneLoaderComponent* loader = nullptr;
        if (exclusive_scene != nullptr && exclusive_scene->GameObjectCount() > 0)
        {
            ReplayEngine::Core::GameObject* owner = exclusive_scene->GameObjectAt(0);
            if (owner != nullptr)
            {
                loader = owner->GetComponent<
                    ReplayEngine::Components::SceneLoaderComponent>();
            }
        }
        Check(loader != nullptr, "専用 Scene の SceneLoaderComponent を取得できる");

        ReplayEngine::Runtime::RuntimeContext* exclusive_runtime =
            exclusive_scene != nullptr ? exclusive_scene->Services().Runtime() : nullptr;
        Check(exclusive_runtime != nullptr && exclusive_runtime != active_runtime.get(),
            "専用 Scene が active World と分離した RuntimeContext を持つ");

        const ReplayEngine::Scene::ILoadingProgressProvider* provider =
            exclusive_scene != nullptr
            ? exclusive_scene->Services().LoadingProgress() : nullptr;
        Check(provider != nullptr && provider->Progress() == 0.0f &&
            provider->IsLoading(),
            "LoadingScene の Progress を provider 経由で取得できる");

        const std::size_t active_pending_before =
            active_runtime->Events().PendingEventCount();
        const std::uint64_t active_dispatched_before =
            active_runtime->Events().DispatchedEventCount();
        const std::uint64_t active_dropped_before =
            active_runtime->Events().DroppedEventCount();
        const std::uint64_t exclusive_dispatched_before = exclusive_runtime != nullptr
            ? exclusive_runtime->Events().DispatchedEventCount() : 0;
        host.update_exclusive_scene(0.5f);
        Check(loader != nullptr && loader->progress == 0.0f && loader->is_loading &&
            loader->state == static_cast<int>(ReplayEngine::Runtime::SceneLoadState::Loading),
            "Exclusive 中の Component 更新が LoadingScene provider の値を反映する");

        ReplayEngine::Core::GameObject* loaded_animated = exclusive_scene != nullptr
            ? exclusive_scene->FindGameObjectByName("Animated") : nullptr;
        ReplayEngine::Components::RectTransformComponent* loaded_rect =
            loaded_animated != nullptr
            ? loaded_animated->GetComponent<
                ReplayEngine::Components::RectTransformComponent>() : nullptr;
        ReplayEngine::Components::UISpriteAnimatorComponent* loaded_animator =
            loaded_animated != nullptr
            ? loaded_animated->GetComponent<
                ReplayEngine::Components::UISpriteAnimatorComponent>() : nullptr;
        ReplayEngine::Components::UIImageComponent* loaded_image =
            loaded_animated != nullptr
            ? loaded_animated->GetComponent<
                ReplayEngine::Components::UIImageComponent>() : nullptr;
        Check(loaded_rect != nullptr && loaded_rect->anchored_position.x > 1.0f,
            "Exclusive 中に専用 Scene の Motion が評価される");
        Check(loaded_animator != nullptr && loaded_image != nullptr &&
            loaded_animator->frame > 0.5f && loaded_image->uv_offset.x > 0.25f,
            "Exclusive 中に専用 Scene の UI sprite animation が進む");
        Check(exclusive_runtime != nullptr &&
            exclusive_runtime->Events().DispatchedEventCount() > exclusive_dispatched_before,
            "専用 Scene の MotionEvent が専用 EventBus で処理される");
        Check(runtime_mixer_before && runtime_probe_image != nullptr &&
            host.motion_mixer.WasDriven(*runtime_probe_image, "fill_color_2"),
            "専用 Scene の更新前後で Runtime World の MotionMixer が不変である");
        Check(active_runtime->Events().PendingEventCount() == active_pending_before &&
            active_runtime->Events().DispatchedEventCount() == active_dispatched_before &&
            active_runtime->Events().DroppedEventCount() == active_dropped_before,
            "専用 Scene の更新前後で Runtime World の MotionEvent 状態が不変である");

        gate->store(true, std::memory_order_release);
        host.scene_manager.Clear();
        host.object_runtime_scenes.ActiveWorld().Services().SetRuntime(nullptr);
        host.object_runtime_scenes.SetRuntimeContext(nullptr);
        active_runtime.reset();
        std::fprintf(stderr, "loading-bridge validation: %s (%d checks)\n",
            first_failure == 0 ? "OK" : "FAILED", checks);
        return first_failure;
    }
}

namespace
{
    int RunHeadlessGltfImportValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command, root_text;
        if (!(arguments >> command) || command != "--validate-gltf-import") return -1;
        if (!(arguments >> std::quoted(root_text)) || root_text.empty())
        {
            std::fprintf(stderr, "--validate-gltf-import requires a directory\n");
            return 1700;
        }
        // 検証対象パスだけはGetCommandLineWから取り直す。WinMainのLPSTRを経由すると
        // 日本語を含むフォルダ名が実行環境のANSI変換に依存してしまうため。
        int wide_argument_count = 0;
        LPWSTR* wide_arguments = CommandLineToArgvW(GetCommandLineW(), &wide_argument_count);
        if (wide_arguments == nullptr || wide_argument_count < 3)
        {
            if (wide_arguments != nullptr) LocalFree(wide_arguments);
            std::fprintf(stderr, "glTF import validation wide path missing\n");
            return 1701;
        }
        const std::filesystem::path root(wide_arguments[2]);
        LocalFree(wide_arguments);
        std::error_code error;
        if (!std::filesystem::is_directory(root, error) || error)
        {
            std::fprintf(stderr, "glTF import validation directory not found\n");
            return 1701;
        }

        std::vector<std::filesystem::path> files;
        for (std::filesystem::recursive_directory_iterator current(root, error), end;
            current != end && !error; current.increment(error))
        {
            if (!current->is_regular_file(error)) continue;
            std::string extension = current->path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            if (extension == ".glb" || extension == ".gltf") files.push_back(current->path());
        }
        std::sort(files.begin(), files.end());

        std::vector<std::string> lines;
        int passed = 0;
        int failed = 0;
        std::size_t total_meshes = 0;
        std::size_t total_materials = 0;
        std::size_t total_animations = 0;
        for (const auto& file : files)
        {
            try
            {
                // 全件調査はCPU import・DDS cache生成を対象にする。GPU生成は通常ビルドと
                // 実機smokeで別に確認し、58体分を一度にVRAMへ積まない。
                skinned_mesh model(file, false, 0.0f);
                const bool ok = model.IsGltf() && !model.meshes.empty() && !model.materials.empty();
                std::ostringstream row;
                row << (ok ? "OK " : "NG ") << file.generic_string()
                    << " MESHES " << model.meshes.size()
                    << " MATERIALS " << model.materials.size()
                    << " ANIMATIONS " << model.animation_clips.size();
                lines.push_back(row.str());
                if (ok)
                {
                    ++passed;
                    total_meshes += model.meshes.size();
                    total_materials += model.materials.size();
                    total_animations += model.animation_clips.size();
                }
                else ++failed;
            }
            catch (const std::exception& exception)
            {
                ++failed;
                lines.push_back("NG " + file.generic_string() + " ERROR " + exception.what());
            }
            catch (...)
            {
                ++failed;
                lines.push_back("NG " + file.generic_string() + " ERROR unknown");
            }

            // 58体の全Textureを検証中ずっと保持するとVRAMを圧迫する。
            // 各モデルの判定後に共有Texture Cacheも明示解放する。
        }
        std::ostringstream summary;
        summary << "FILES " << files.size() << " PASSED " << passed << " FAILED " << failed
            << " MESHES " << total_meshes << " MATERIALS " << total_materials
            << " ANIMATIONS " << total_animations;
        lines.insert(lines.begin(), summary.str());
        const bool ok = !files.empty() && failed == 0;
        ReplayEngine::Runtime::Detail::WriteValidationResultFile("GltfImport.txt",
            "REPLAY_GLTF_IMPORT_VALIDATION", ok, lines);
        std::fprintf(stderr, "glTF import validation: RESULT %s (%d/%zu)\n",
            ok ? "OK" : "NG", passed, files.size());
        return ok ? 0 : 1703;
    }

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

        if (command == "--validate-gltf-import")
            return RunHeadlessGltfImportValidation(command_line);

        if (command == "--validate-loading-bridge")
            return RunHeadlessLoadingBridgeValidation(command_line);

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
        if (command == "--validate-component-api")
        {
            ReplayEngine::Core::RegisterBuiltInComponents();
            return Validation::RunComponentApiValidation();
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

        // Editor Camera の開始関門・操作継続・Preset 永続化。
        // ImGui 自体は回さず、UI が渡すのと同じ純粋入力/API を直接検証する。
        // 終了コード帯は 1900-1999。
        if (command == "--validate-editor-camera")
        {
            return ReplayEngine::Editor::Validation::RunEditorCameraValidation();
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
        // D3D デバイスを作らずに走る。DXC はデバイス非依存なので、
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
