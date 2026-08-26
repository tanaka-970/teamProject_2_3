// 終了時の DX12 リソース解放を確かめる回帰シナリオ。
// Motion、Scene 切り替え、Play 開始停止、DX12 キャッシュ解放を通し、終了時の D3D12 Live Object 報告へ渡す。
// 依存方向: framework -> RePlayEngine の一方向。
#include "framework.h"

#include "../../RePlayEngine/Components/UI/UIImageComponent.h"
#include "../../RePlayEngine/Motion/MotionAsset.h"
#include "../../RePlayEngine/Motion/MotionBindingResolver.h"
#include "../../RePlayEngine/Motion/MotionEvaluator.h"
#include "../../RePlayEngine/Motion/MotionMixer.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Reflection/Property/PropertyValue.h"
#include "../../RePlayEngine/Scene/Runtime/Scene.h"
#include "../../RePlayEngine/Scene/Serialization/SceneData.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    bool MotionClose(float lhs, float rhs) noexcept
    {
        return std::fabs(lhs - rhs) <= 0.0001f;
    }

    void WriteMotionValidationReport(bool ok, const std::string& message)
    {
        const std::filesystem::path report_path =
            std::filesystem::path("Saved") / "Validation" / "MotionRuntime.txt";

        std::error_code ec;
        std::filesystem::create_directories(report_path.parent_path(), ec);

        std::ofstream report(report_path);
        if (!report) return;

        report << "REPLAY_MOTION_RUNTIME_VALIDATION 1\n";
        report << "RESULT " << (ok ? "OK" : "FAIL") << "\n";
        if (!message.empty()) report << "DETAIL " << message << "\n";
    }

    bool RunMotionRuntimeValidation(std::string& error)
    {
        namespace Components = ReplayEngine::Components;
        namespace Motion = ReplayEngine::Motion;
        namespace Reflection = ReplayEngine::Reflection;
        namespace Scene = ReplayEngine::Scene;

        Scene::Scene scene("MotionRuntimeValidation");
        ReplayEngine::Core::GameObject* object = scene.CreateGameObject("ValidationImage");
        if (object == nullptr)
        {
            error = "GameObject を作成できません。";
            return false;
        }

        Components::UIImageComponent* image =
            object->AddComponent<Components::UIImageComponent>();
        if (image == nullptr)
        {
            error = "UIImageComponent を作成できません。";
            return false;
        }
        image->opacity = 0.0f;

        Motion::MotionAsset asset;
        asset.name = "ShutdownMotionOpacity";
        asset.duration = 1.0f;

        Motion::MotionTrack track;
        track.name = "ValidationImage.opacity";
        track.binding.object = object->ID();
        track.binding.component_type = Components::UIImageComponent::StaticTypeID();
        track.binding.component_index = 0;
        track.binding.property = "opacity";
        track.value_type = Reflection::PropertyType::Float;

        Motion::MotionKeyframe start;
        start.time = 0.0f;
        start.value = Reflection::PropertyValue::MakeFloat(0.0f);
        start.easing = Motion::MotionEasing::Linear;

        Motion::MotionKeyframe end;
        end.time = 1.0f;
        end.value = Reflection::PropertyValue::MakeFloat(1.0f);
        end.easing = Motion::MotionEasing::Linear;

        track.keys.push_back(start);
        track.keys.push_back(end);
        asset.tracks.push_back(track);
        asset.SortKeys();

        const std::filesystem::path motion_path =
            std::filesystem::path("Saved") / "Validation" / "ShutdownMotion.replaymotion";

        if (!Motion::MotionAsset::SaveToFile(motion_path, asset, error))
        {
            return false;
        }

        Motion::MotionAsset loaded;
        if (!Motion::MotionAsset::LoadFromFile(motion_path, loaded, error))
        {
            return false;
        }
        if (loaded.tracks.empty())
        {
            error = "読み込んだ Motion に Track がありません。";
            return false;
        }

        Reflection::PropertyValue value;
        if (!Motion::MotionEvaluator::EvaluateTrack(loaded.tracks.front(), 0.5f, value))
        {
            error = "MotionEvaluator が opacity Track を評価できません。";
            return false;
        }
        if (!MotionClose(value.AsFloat(-1.0f), 0.5f))
        {
            error = "MotionEvaluator の 0.5 秒地点が期待値と一致しません。";
            return false;
        }

        Motion::ResolvedMotionBinding binding =
            Motion::MotionBindingResolver::Resolve(scene, loaded.tracks.front().binding);
        if (!binding.Valid())
        {
            error = "MotionBindingResolver が UIImage.opacity を解決できません。";
            return false;
        }

        Motion::MotionMixer mixer;
        mixer.BeginFrame();
        mixer.Contribute(binding, value, 1.0f);
        mixer.Apply();

        if (!MotionClose(image->opacity, 0.5f))
        {
            error = "MotionMixer 適用後の UIImage.opacity が期待値と一致しません。";
            return false;
        }

        error = "UIImage.opacity 0->1 の 0.5 秒地点を評価・適用しました。";
        return true;
    }
}

void framework::run_shutdown_regression_scenario()
{
#if defined(_DEBUG) || defined(DEBUG)
    namespace Serialization = ReplayEngine::Scene::Serialization;

    OutputDebugStringA("[ShutdownRegression] 開始\n");

    std::string motion_error;
    const bool motion_ok = RunMotionRuntimeValidation(motion_error);
    WriteMotionValidationReport(motion_ok, motion_error);
    OutputDebugStringA(motion_ok
        ? "[ShutdownRegression] Motion Runtime 検証 OK\n"
        : ("[ShutdownRegression] Motion Runtime 検証失敗: " +
            motion_error + "\n").c_str());

    // DX12 の静的キャッシュ解放経路を明示的に通す。
    const bool cache_clear_ok = dx12_device_context.ClearStaticAssetCaches();
    OutputDebugStringA(cache_clear_ok
        ? "[ShutdownRegression] DX12 Static Asset Cache 解放\n"
        : "[ShutdownRegression] DX12 Static Asset Cache 解放失敗（続行）\n");

    ReplayEngine::Rendering::RenderStats& stats = ReplayEngine::Rendering::Stats();
    if (!stats.Initialized()) stats.Initialize();
    stats.BeginFrame();
    stats.EndFrame();
    OutputDebugStringA("[ShutdownRegression] RenderStats CPU/DX12 経路使用\n");

    // ---- Scene 切り替えを 100 回 ----------------------------------------
    //
    // 切り替えのたびに World の実体が入れ替わる。
    // 途中で作られた GameObject / Component が持つ GPU 参照が
    // 毎回きちんと切れているかを、最後の Live Object 数で見る。
    //
    // ファイルを増やさないよう、手元の SceneData から直接組み立てる
    // （RequestAdopt はファイルも AssetGUID も要らない）。
    Serialization::SceneData snapshot;
    Serialization::CaptureScene(object_scene, snapshot);

    int completed_switches = 0;
    for (int index = 0; index < 100; ++index)
    {
        if (object_runtime_scenes.RequestAdopt(snapshot, std::string()) !=
            ReplayEngine::Runtime::SceneRequestResult::Accepted)
        {
            break;
        }
        object_runtime_scenes.Tick();   // Staging World の構築
        object_runtime_scenes.Tick();   // 入れ替えと Scene::Start()
        if (object_runtime_scenes.State() !=
            ReplayEngine::Runtime::SceneLoadState::Completed)
        {
            break;
        }
        ++completed_switches;
    }
    OutputDebugStringA(("[ShutdownRegression] Scene 切り替え " +
        std::to_string(completed_switches) + " 回\n").c_str());

    // 使い終わった World は空へ戻す。
    object_runtime_scenes.ResetToEmptyWorld();

    // ---- Play 開始 / 停止 ------------------------------------------------
    //
    // Play 中は Runtime World が本番になり、衝突世界と EditorContext も
    // そちらへ張り替わる。停止でそれらが編集 Scene へ戻ることを通しておく。
    enter_object_play_mode();
    const bool entered = object_scene_play_mode;
    if (entered) exit_object_play_mode();
    OutputDebugStringA(entered
        ? "[ShutdownRegression] Play 開始／停止 実行\n"
        : "[ShutdownRegression] Play へ入れなかった（続行）\n");

    OutputDebugStringA("[ShutdownRegression] 完了。以降は通常の終了処理へ\n");
#endif
}
