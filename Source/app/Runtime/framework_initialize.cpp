#include "framework.h"
#include "shader.h"
#include "texture.h"
#include "skinned_mesh.h"
#include "../../RePlayEngine/Scene/BootLogoScene.h"
#include "../../RePlayEngine/Scene/LoadingScene.h"

#include <filesystem>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
    bool IsFontFile(const std::filesystem::path& path)
    {
        std::string extension = path.extension().u8string();
        for (char& character : extension)
        {
            character = static_cast<char>(std::tolower(
                static_cast<unsigned char>(character)));
        }
        return extension == ".ttf" || extension == ".otf" || extension == ".ttc";
    }

    std::size_t RegisterProjectFonts(const std::filesystem::path& fonts_root,
        ReplayEngine::Assets::AssetDatabase& asset_database)
    {
        std::error_code error;
        if (!std::filesystem::is_directory(fonts_root, error) || error)
            return 0;

        std::size_t registered_count = 0;
        std::filesystem::recursive_directory_iterator iterator(fonts_root, error);
        const std::filesystem::recursive_directory_iterator end;
        while (iterator != end && !error)
        {
            const std::filesystem::directory_entry entry = *iterator;
            std::error_code entry_error;
            if (entry.is_regular_file(entry_error) && !entry_error &&
                IsFontFile(entry.path()))
            {
                const auto* record = asset_database.FindByPath(entry.path());
                if (record == nullptr || record->kind != ReplayEngine::Assets::AssetKind::Font)
                {
                    asset_database.Register(entry.path(),
                        ReplayEngine::Assets::AssetKind::Font);
                    ++registered_count;
                }
            }
            iterator.increment(error);
        }
        return registered_count;
    }

}

// 【削除済み】lower_copy / find_animation_clip
//   起動時に固定のプレイヤーモデルからクリップ名を探し、
//   旧 Player の clip_idle / clip_walk / clip_jump へ割り当てるための補助だった。
//   クリップの割り当ては AnimatorComponent のプロパティ
//   (idle_clip / walk_clip / jump_clip) が持ち、Scene へ保存される。

bool framework::initialize()
{
    // 製品起動ではDX12を唯一のRendererとする。呼び出し元の指定漏れや
    // 旧移行フラグの値に関係なく、初期化中にD3D11経路へ入らないよう固定する。
    dx12_framework_requested = true;
    dx12_framework_active = false;

    std::string asset_database_error;
    if (!asset_database.Load(asset_database_error))
        object_editor_context.SetStatus("AssetDatabase: " + asset_database_error);

    // resources/fonts へ TTF/OTF/TTC を置くだけで、Project Browser を開かなくても
    // UIText の Font picker へ出せるように起動時登録する。
    if (!standalone_game_mode)
    {
        const std::size_t registered_fonts = RegisterProjectFonts(
            content_path(std::filesystem::path("resources") / "fonts"), asset_database);
        if (registered_fonts > 0)
        {
            std::string save_error;
            if (!asset_database.Save(save_error))
            {
                object_editor_context.SetStatus(
                    "フォントAssetDatabase保存失敗: " + save_error);
            }
            else
            {
                push_editor_log("Info",
                    "resources/fonts からフォントを自動登録しました: " +
                    std::to_string(registered_fonts) + "件");
            }
        }
    }

    // Input Action Asset は ProjectSettings 読み込み後に initialize_object_scene() で適用する。
    // ここではまだ project_settings が未読込なので参照しない。

    if (!object_audio_system.Initialize())
    {
        push_editor_log("Warning", "Audio は silent mode で起動します");
    }


    {
        const bool ui_font_ok = ui_font_atlas.InitializeCpuOnly();
        if (!ui_font_ok)
            push_editor_log("Warning", "UI FontAtlas を初期化できません。UIText は描画されません");
        lights.data.light_counts = { 0, 0, 0, 0 };
    }

    auto loading_scene = std::make_unique<ReplayEngine::Scene::LoadingScene>();
    // 任意アセットの読み込みは「無ければスキップして続行」に統一する。
    // 実行に必須ではないファイルの不足で起動が止まらないようにするため。
    // 失敗は OutputDebugString へ理由付きで出す（Visual Studio の出力ウィンドウで読める）。
    // キャラクターモデルは SkinnedMeshRendererComponent の
    // mesh_asset (AssetGUID) が指し、resolve_object_mesh() が読み込む。
    // AssetDatabaseのモデルは1件ずつ独立したタスクにして、ロード画面の
    // ワーカー群へそのまま流す。セッション復元やステージ切り替えは
    // ConcurrentResourceCacheのヒットで待たされなくなる。
    for (const auto& record : asset_database.Records())
    {
        if (record.kind != ReplayEngine::Assets::AssetKind::Model) continue;
        const std::filesystem::path source = content_path(record.source_path);
        loading_scene->AddTask("Prewarm " + record.display_name, [this, source]
        {
            prewarm_model_asset(source);
            // 先読みは最適化なので、失敗してもロード全体は成功扱いにする。
            return true;
        });
    }
    // Game 起動ではロゴの裏でロードを進める。Editor 起動では固定長の
    // ロゴ待ちを省き、暗いロード画面から直接セッションを復元する。
    if (object_boot_from_startup_scene)
    {
        scene_manager.SetScene(std::make_unique<ReplayEngine::Scene::BootLogoScene>());
        scene_manager.QueueScene(std::move(loading_scene));
    }
    else
    {
        scene_manager.SetScene(std::move(loading_scene));
    }

    scene_manager.QueueSceneFactory([this]() -> std::unique_ptr<ReplayEngine::Scene::IScene>
    {
        // GameScene が持つのはカメラ操作だけ。
        // 操作キャラクターのモデルもアニメーションクリップも渡さない。
        // それらは Scene 内の GameObject が Component として持っている。
        auto next_scene = std::make_unique<GameScene>(
            static_cast<float>(client_width) / static_cast<float>(client_height));
        game_scene = next_scene.get();
        if (!standalone_game_mode && !object_boot_from_startup_scene)
            restore_editor_session();
        return next_scene;
    });

    // GameObject / Component 基盤の初期化。
    // Component 型の登録・編集用 Scene の準備・既定 Scene ファイルの読み込みを行う。
    // AssetDatabase の読み込み後に呼ぶ必要がある（Asset 参照を解決するため）。
    initialize_object_scene();

    bool enable_dx12_debug = false;
#if defined(_DEBUG) || defined(DEBUG)
    enable_dx12_debug = true;
#endif
    if (!dx12_device_context.Initialize(hwnd, client_width, client_height,
        enable_dx12_debug, false, false))
    {
        char error_message[256]{};
        std::snprintf(error_message, sizeof(error_message),
            "DX12 framework bootstrap の初期化に失敗しました: %s (hr=0x%08lx)",
            dx12_device_context.LastInitializationStage(),
            static_cast<unsigned long>(dx12_device_context.LastInitializationResult()));
        std::fprintf(stderr, "%s\n", error_message);
        push_editor_log("Error", error_message);
        return false;
    }
    dx12_framework_active = true;
    dx12_framework_render_error_reported = false;
    push_editor_log("Info", "DX12 framework bootstrap: DX12 が SwapChain / Present を所有します");

    return true;
}
