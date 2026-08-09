// 終了時のリソース解放を確かめるための操作一式。
//
// ---------------------------------------------------------------------------
// 【何を確かめるものか】
//
//   ヘッドレスの Validation では D3D デバイスを作らないため、
//   GPU リソースの解放漏れは検出できない。
//   ここは実際にデバイスを持った状態で
//     Texture Cache / Dummy Texture / RenderStats Query /
//     Scene 切り替え / Play 開始・停止
//   をひととおり使い、そのうえで通常の終了処理を通す。
//
//   合否は終了後の ID3D11Debug::ReportLiveDeviceObjects で判断する。
//   期待値は「Query 0 / SRV 0 / 意図しない子リソース 0」。
//   Device 自体は Report を呼ぶために生きている必要があるので数に入れない。
//
// ---------------------------------------------------------------------------
// 【Debug 限定にしている理由】
//
//   確かめたいのは D3D Debug Layer の報告内容。
//   通常の Release 起動で Debug Layer や ReportLiveDeviceObjects を
//   強制的に有効化すると、製品版と動作も実行コストも変わってしまう。
//   Release ビルドでは何もしない。
//
// 依存方向:
//   framework -> RePlayEngine の一方向。

#include "framework.h"

#include "texture.h"

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

    // ---- 1) Texture Cache を使う ------------------------------------------
    //
    // 実際に読める Asset を 1 つ通す。読めなくても検査は続ける
    // （このシナリオの目的は「読めること」ではなく「解放されること」）。
    if (device)
    {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> cached_view;
        const HRESULT texture_result = load_texture_from_file(
            device.Get(), L".\\resources\\screenshot.jpg",
            cached_view.GetAddressOf(), nullptr);
        OutputDebugStringA(SUCCEEDED(texture_result)
            ? "[ShutdownRegression] Texture Cache 使用\n"
            : "[ShutdownRegression] Texture Cache 読み込み失敗（続行）\n");

        // ---- 2) Dummy Texture を作る --------------------------------------
        //
        // DirectXTK を通らない自前生成の SRV。命名経路が別なので分けて通す。
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> dummy_view;
        const HRESULT dummy_result = make_dummy_texture(
            device.Get(), dummy_view.GetAddressOf(), 0xFFFFFFFF, 16);
        OutputDebugStringA(SUCCEEDED(dummy_result)
            ? "[ShutdownRegression] Dummy Texture 生成\n"
            : "[ShutdownRegression] Dummy Texture 生成失敗（続行）\n");

        // ---- 3) RenderStats の Query を使う --------------------------------
        //
        // Initialize していなければここで作られる。
        // 作られた Query が終了処理で解放されることを確かめたい。
        ReplayEngine::Rendering::RenderStats& stats = ReplayEngine::Rendering::Stats();
        if (!stats.Initialized()) stats.Initialize(device.Get());
        if (immediate_context)
        {
            stats.BeginFrame(immediate_context.Get());
            stats.EndFrame(immediate_context.Get());
        }
        OutputDebugStringA(stats.Initialized()
            ? "[ShutdownRegression] RenderStats Query 使用\n"
            : "[ShutdownRegression] RenderStats 未初期化（続行）\n");
    }

    // ---- 4) Scene 切り替えを 100 回 ----------------------------------------
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

    // ---- 5) Play 開始 / 停止 ------------------------------------------------
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
