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

#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Scene/Serialization/SceneData.h"

#include <string>

void framework::run_shutdown_regression_scenario()
{
#if defined(_DEBUG) || defined(DEBUG)
    namespace Serialization = ReplayEngine::Scene::Serialization;

    OutputDebugStringA("[ShutdownRegression] 開始\n");

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
