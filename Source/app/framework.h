#pragma once

#include <windows.h>
#include <tchar.h>
#include <sstream>
#include <filesystem>
#include <vector>
#include <algorithm>
#include "skinned_mesh.h"
#include "misc.h"
#include "high_resolution_timer.h"

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern ImWchar glyphRangesJapanese[];
#include <chrono>
#include <fstream>
#endif

#include <d3d11.h>
#include "sprite_batch.h"
#include <wrl.h>
#include "geometric_primitive.h"
#include "static_mesh.h"
#include "framebuffer.h"
#include "fullscreen_quad.h"
#include "UI.h"
#include "pbr_renderer.h"
#include "toon_renderer.h"
#include "csm_renderer.h"
#include "trail.h"
#include "particle_system.h"
#include "deferred_renderer.h"
#include "lights_manager.h"
#include "shading_model.h"
#include "../game/game_scene.h"
#include "../../RePlayEngine/Scene/SceneManager.h"
#include "../../RePlayEngine/Rendering/Passes/PostProcessPass.h"
#include "../../RePlayEngine/Rendering/Passes/BloomPass.h"
#include "../../RePlayEngine/Rendering/Passes/SsaoPass.h"
#include "../../RePlayEngine/Rendering/Passes/SsrPass.h"
#include "../../RePlayEngine/Rendering/Passes/TaaPass.h"
#include "../../RePlayEngine/Rendering/Deferred/TiledDeferredPass.h"
#include "../../RePlayEngine/Rendering/FrameConstants.h"
#include "../../RePlayEngine/Rendering/RenderStats.h"
#include "../../RePlayEngine/Rendering/Frustum.h"
#include "../render/motion_vector_context.h"
#include "../../RePlayEngine/Rendering/RenderGraph/RenderGraph.h"
#include "../../RePlayEngine/Rendering/ShaderStack/ShaderLayerStack.h"
#include "../../RePlayEngine/Rendering/Materials/CharacterMaterialProfile.h"
#include "../../RePlayEngine/Rendering/Materials/CharacterMaterialGpuData.h"
#include "../../RePlayEngine/Rendering/Materials/MaterialAsset.h"
#include "../../RePlayEngine/Assets/AssetDatabase.h"
#include "../../RePlayEngine/Assets/AsyncAssetManager.h"
#include "../../RePlayEngine/Assets/ConcurrentResourceCache.h"
#include "../../RePlayEngine/Project/ProjectSettings.h"
#include "../../RePlayEngine/Editor/Gizmo/TransformGizmo.h"
#include "../../RePlayEngine/Editor/Gizmo/ViewportPicker.h"
#include "../../RePlayEngine/Physics/MeshCollisionCooker.h"

// --- GameObject / Component 基盤 -------------------------------------------
// SceneManager とは責任が違う。
//   SceneManager  … 起動ロゴ / ロード画面 / ゲームという画面遷移
//   Scene (下記)  … GameObject の入れ物。今回の新基盤。
#include "../../RePlayEngine/Scene/Runtime/Scene.h"
#include "../../RePlayEngine/Editor/Core/EditorContext.h"
#include "../../RePlayEngine/Editor/Hierarchy/HierarchyPanel.h"
#include "../../RePlayEngine/Editor/Inspector/InspectorPanel.h"
#include "../../RePlayEngine/Rendering/Adapter/RenderItem.h"
#include "../../RePlayEngine/Scene/Services/PlayerControlSystem.h"
#include "../../RePlayEngine/Scene/Services/SceneCollisionWorld.h"
#include "../../RePlayEngine/Editor/Debug/ColliderDebugDraw.h"
#include "../../RePlayEngine/Editor/Validation/ValidationPanel.h"
#include "../../RePlayEngine/Editor/Viewport/EditorViewportCamera.h"
#include "../../RePlayEngine/Editor/Viewport/EditorCameraController.h"
#include "../../RePlayEngine/Editor/Viewport/EditorCameraStateStore.h"
#include "../game/legacy_camera_basis_bridge.h"

#include <unordered_map>
#include <unordered_set>

class gltf_model;

CONST LONG SCREEN_WIDTH{ 1600 };
CONST LONG SCREEN_HEIGHT{ 900 };
CONST BOOL FULLSCREEN{ FALSE };
CONST LPWSTR APPLICATION_NAME{ L"X3DGP_Upgraded" };

class framework
{
public:
    CONST HWND hwnd;

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_view;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_stencil_view;

    std::unique_ptr<sprite_batch> sprite_batches[8];
    std::unique_ptr<framebuffer> framebuffers[8];
    std::unique_ptr<fullscreen_quad> bit_block_transfer;

    enum class SAMPLER_STATE { POINT, LINEAR, ANISOTROPIC, ANISOTROPIC_CLAMP };
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_states[4];

    enum class DEPTH_STATE { ZT_ON_ZW_ON, ZT_ON_ZW_OFF, ZT_OFF_ZW_ON, ZT_OFF_ZW_OFF };
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_stencil_states[4];

    enum class BLEND_STATE { NONE, ALPHA, ADD, MULTIPLY };
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend_states[4];

    enum class RASTER_STATE { SOLID, WIREFRAME, CULL_NONE, WIREFRAME_CULL_NONE };
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_states[4];

    struct scene_constants
    {
        DirectX::XMFLOAT4X4 view_projection;
        DirectX::XMFLOAT4 light_direction;
        DirectX::XMFLOAT4 camera_position;
    };
    Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffers[8];

    DirectX::XMFLOAT4 camera_position{ 0.0f, 4.0f, -10.0f, 1.0f };
    DirectX::XMFLOAT4 light_direction{ 0.300f, 0.000f, 0.500f, 0.0f };

    DirectX::XMFLOAT3 translation{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 scaling{ 0.01f, 0.01f, 0.01f };
    DirectX::XMFLOAT3 rotation{ 81.0f, 8.5f, 180.0f };
    DirectX::XMFLOAT4 material_color{ 1, 1, 1, 1 };
    DirectX::XMFLOAT4 background_color{ 46.0f / 255.0f, 56.0f / 255.0f, 61.0f / 255.0f, 1.0f };
    bool draw_background_image{ false };
    bool use_pbr_skin{ true };
    bool enable_toon_shader{ true };
    bool enable_unlit_shader{ true };
    bool enable_outline_shader{ true };
    bool enable_pbr_shadow_shader{ true };
    bool enable_luminance_shader{ true };
    bool enable_final_pass_shader{ true };
    bool enable_bloom_shader{ true };
    bool enable_vignette_shader{ false };
    bool enable_fxaa_shader{ true };
    bool enable_static_meshes{ false };
    bool shader_stack_advanced_mode{ false };

    // static_meshes[0] … エディタのデバッグ用静的メッシュ (cube.obj)。既定は非表示。
    // skinned_meshes[1] … 取り込んだ旧ステージ素材。
    //
    // かつて skinned_meshes[0] は「旧 Player 専用のモデルスロット」で、
    // 起動時に固定の FBX を読み込み、Player の Transform で毎フレーム描いていた。
    // その経路は完全に撤去したので、index 0 は誰も使わない。
    // アニメーション付きモデルの描画は SkinnedMeshRendererComponent が提出する。
    //
    // shared_ptr なのは並列ロードとモデルキャッシュ (ConcurrentResourceCache /
    // gltf_model_cache) が同じ実体を共有するため。所有権はここだけにしない。
    std::shared_ptr<static_mesh> static_meshes[8];
    std::shared_ptr<skinned_mesh> skinned_meshes[8];
    std::shared_ptr<gltf_model> stage_gltf_model;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> static_mesh_unlit_ps;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> skinned_mesh_unlit_ps;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> static_mesh_gbuffer_ps;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> skinned_mesh_gbuffer_ps;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> object_pixelate_ps;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> skinned_stylized_character_ps;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> static_stylized_character_ps;

    struct material_override_constants
    {
        DirectX::XMFLOAT4 mat_params{ 0.0f, 0.55f, 1.0f, 0.0f };
        unsigned int shading_model{ 1 };
        float texture_contrast{ 1.0f };
        float pixelate_size{ 6.0f };
        float pixelate_strength{ 1.0f };
    };
    Microsoft::WRL::ComPtr<ID3D11Buffer> material_override_cb;

    Microsoft::WRL::ComPtr<ID3D11Buffer> shader_layer_cb;
    Microsoft::WRL::ComPtr<ID3D11Buffer> character_material_cb;

    // デバッグ用静的メッシュとステージのシェーディング設定 (static_meshes[i] と対応)
    // 0=FBX標準、1=PBR、2=トゥーン、3=アンリット、4=ピクセレーション
    //
    // skinned 側の配列は撤去した。旧 Player 専用スロットのためだけに存在しており、
    // GameObject の描画方式は SkinnedMeshRendererComponent /
    // MeshRendererComponent の shading_model プロパティが持つ。
    // 同じ値をここと Component の両方から変えられる状態は作らない。
    int shading_per_static [8] { 1, 1, 1, 1, 1, 1, 1, 1 };
    bool outline_per_static [8] { false, false, false, false, false, false, false, false };
    ReplayEngine::Rendering::ShaderLayerStack shader_layers_static[8];
    ReplayEngine::Rendering::CharacterMaterialProfile character_profiles_static[8];
    float pixelate_grid_per_static[8] { 6, 6, 6, 6, 6, 6, 6, 6 };
    float pixelate_strength_per_static[8] { 1, 1, 1, 1, 1, 1, 1, 1 };

    pbr_renderer pbr;

    toon_renderer    toon;
    csm_renderer     csm;
    trail            test_trail;
    particle_system  particles;
    deferred_renderer deferred;
    lights_manager   lights;
    ReplayEngine::Scene::SceneManager scene_manager;
    ReplayEngine::Rendering::RenderGraph render_graph;
    ReplayEngine::Rendering::PostProcessPass post_process;
    ReplayEngine::Rendering::BloomPass bloom_pass;
    ReplayEngine::Rendering::SsaoPass ssao_pass;
    ReplayEngine::Rendering::SsrPass ssr_pass;
    ReplayEngine::Rendering::TaaPass taa_pass;
    ReplayEngine::Rendering::TiledDeferredPass tiled_deferred;

    // SSAO/SSR/TAAが共有するフレーム定数。b9へ載せる。
    ReplayEngine::Rendering::FrameConstants frame_constants{};
    Microsoft::WRL::ComPtr<ID3D11Buffer> frame_constants_cb;
    // TAAの再投影に使う前フレームのビュー射影行列。初回は今フレームで埋める。
    DirectX::XMFLOAT4X4 previous_view_projection{};
    bool previous_view_projection_valid{ false };
    // 射影行列へ加算したTAAジッター(NDC)。モーションベクターで打ち消すのに使う。
    DirectX::XMFLOAT2 taa_jitter_ndc{ 0.0f, 0.0f };
    DirectX::XMFLOAT2 previous_taa_jitter_ndc{ 0.0f, 0.0f };
    unsigned int frame_index{ 0 };
    bool enable_ssao{ true };
    bool enable_ssr{ true };
    bool enable_taa{ true };
    // 深度プリパス。G-BufferのPS実行を最前面の1回に抑える。
    // 頂点処理が2回になるため、LODと併用する前提。
    bool enable_depth_prepass{ true };
    // 描画統計オーバーレイの表示。F4で切り替える。
    bool show_render_stats{ true };
    // 初回フレームだけ統計ウィンドウの位置を強制するためのフラグ。
    // imgui.ini に残った旧座標(インスペクタと重なる位置)に
    // 引っ張られるのを防ぐ。
    bool stats_window_placed_{ false };

    GameScene*       game_scene{ nullptr };
    UIManager        uiManager;
    bool             enable_scene_game{ true };
    bool             editor_mode{ false };

    // ステージの描画方式は skinned[0] と分離する。0はFBX標準で上書きしない。
    // 0=FBX標準、1=PBR、2=トゥーン、3=アンリット、4=ピクセレーション
    int              shading_per_stage{ 1 };
    bool             outline_per_stage{ false };
    ReplayEngine::Rendering::ShaderLayerStack stage_shader_layers;
    ReplayEngine::Rendering::CharacterMaterialProfile stage_character_profile;
    float            stage_pixelate_grid{ 6.0f };
    float            stage_pixelate_strength{ 1.0f };
    bool             enable_stage_shader{ true }; // false = always FBX default
    bool             stage_texture_wrap{ true };
    float            stage_texture_contrast{ 1.20f };
    std::string      selected_stage_asset_path;
    std::string      selected_stage_cache_path;
    std::string      stage_asset_status{ "未選択" };
    ReplayEngine::Assets::AssetDatabase asset_database;
    ReplayEngine::Assets::ConcurrentResourceCache<static_mesh> static_mesh_cache;
    ReplayEngine::Assets::ConcurrentResourceCache<skinned_mesh> skinned_mesh_cache;
    ReplayEngine::Assets::ConcurrentResourceCache<gltf_model> gltf_model_cache;
    ReplayEngine::Assets::AsyncAssetManager async_asset_manager;

    // プロジェクト設定。Default Controlled Character Prefab を持つ。
    // 参照は AssetGUID なので、Prefab の名前やパスを変えても壊れない。
    ReplayEngine::Project::ProjectSettings project_settings;
    std::string project_settings_status{ "プロジェクト設定 未読込" };

    // 直近で保存した Prefab の AssetGUID。
    // 「保存した Prefab をそのまま既定の操作キャラクターにする」ボタン用。
    std::string last_saved_prefab_guid;

    // 新規シーン作成ダイアログの入力欄。
    char new_object_scene_name[128]{ "NewScene" };

    ReplayEngine::Editor::TransformGizmo transform_gizmo;
    ReplayEngine::Physics::MeshCollisionCooker collision_cooker;
    std::string      selected_stage_asset_guid;
    std::string      shader_preset_status{ "プリセット未選択" };
    bool             async_stage_load_active{ false };

    // --- GameObject / Component 基盤 ---------------------------------------
    //
    // 所有関係:
    //   framework が編集用 Scene と実行用 Scene の 2 つを値で所有する。
    //   Scene が GameObject を、GameObject が Component を unique_ptr で所有する。
    //   EditorContext と各パネルは Scene を非所有参照するだけ。
    //
    // Play Mode:
    //   Play 開始時に編集 Scene を SceneData 経由で実行用 Scene へ複製する。
    //   Play 中の変更は実行用 Scene だけに入るため、編集内容が汚れない。
    ReplayEngine::Scene::Scene              object_scene;
    ReplayEngine::Scene::Scene              object_scene_runtime;
    ReplayEngine::Editor::EditorContext     object_editor_context;
    ReplayEngine::Editor::HierarchyPanel    object_hierarchy_panel;
    ReplayEngine::Editor::InspectorPanel    object_inspector_panel;
    ReplayEngine::Editor::ValidationPanel   object_validation_panel;
    ReplayEngine::Rendering::RenderItemList object_render_items;

    // Asset GUID -> メッシュ実体。
    // 読み込めた Asset だけを入れる。null や壊れたエントリは決して登録しない。
    std::unordered_map<std::string, std::unique_ptr<skinned_mesh>> object_mesh_cache;

    // 読み込みに失敗した Asset GUID。
    // 失敗をキャッシュ本体へ入れず別に持つことで、
    //   - キャッシュには常に有効なメッシュしか入らない
    //   - 毎フレーム同じ Asset を探し直さない
    //   - 同じ警告をログへ出し続けない
    // の 3 つを同時に満たす。
    std::unordered_set<std::string> object_mesh_failures;

    struct cached_material_asset
    {
        ReplayEngine::Rendering::MaterialAsset material;
        std::filesystem::file_time_type write_time{};
    };
    std::unordered_map<std::string, cached_material_asset> object_material_cache;
    std::unordered_set<std::string> object_material_failures;

    std::filesystem::path object_scene_path{ "resources/Scenes/TrainingStage.replayscene" };
    bool             object_scene_play_mode{ false };
    bool             object_scene_paused{ false };
    float            object_autosave_elapsed{ 0.0f };
    std::filesystem::path object_recovery_path;
    bool             object_recovery_available{ false };
    bool             object_recovery_prompt_opened{ false };
    std::string      object_autosave_status;

    // 操作対象を型ではなく ObjectID で持つ。
    // 人型からメカ・ドローンへ変えても GameObject のクラス型は変わらない。
    ReplayEngine::Scene::PlayerControlSystem player_control_system;

    // 【移行用】Gameplay Component から Camera / Stage 具象型を隠すための橋渡し。
    // 削除条件はそれぞれのヘッダーへ記載してある。
    LegacyCameraBasisBridge      object_camera_bridge;

    // --- 衝突 -------------------------------------------------------------
    //
    // 所有関係:
    //   framework が衝突世界と Cook キャッシュを値メンバとして 1 つずつ所有する。
    //   衝突世界は Scene を非所有参照し、Cook データの実体は
    //   MeshColliderComponent が shared_ptr で持つ（キャッシュは weak_ptr のみ）。
    //
    // Component からの経路:
    //   Component -> GetScene() -> Services().Physics() -> object_collision_world
    //   Motor は具象型を知らず、IPhysicsQueryService としてしか触らない。
    ReplayEngine::Scene::SceneCollisionWorld  object_collision_world;
    ReplayEngine::Physics::CookedMeshCollisionCache object_collision_cook_cache;

    // Editor の Scene View へ形を描くための線分。毎フレーム作り直す。
    std::vector<ReplayEngine::Editor::DebugLine> object_collider_debug_lines;
    bool             show_collider_debug_draw{ false };
    bool             show_collider_debug_bounds{ true };
    bool             show_collider_debug_wireframe{ false };
    bool             show_collision_diagnostics{ false };

    // Cook に失敗した Asset。同じ警告をログへ出し続けないための記録。
    std::unordered_set<std::string> object_collision_failures;

    // 固定時間更新（CharacterMotor の物理用）。
    float            object_fixed_time_step{ 1.0f / 60.0f };
    float            object_fixed_accumulator{ 0.0f };
    int              object_max_fixed_substeps{ 5 };

    // 操作対象が設定されていない Scene であることを Editor へ知らせる。
    // 「対象が居ないから何かを生成する」ことは決してしない。表示するだけ。
    bool             object_missing_controlled_target{ false };

    // --- Scene View の編集カメラ -------------------------------------------
    //
    // 所有関係:
    //   framework が値メンバとして 1 つ持つ。Scene にも GameObject にも属さない。
    //   Hierarchy へ出ないし、Scene ファイルにも Prefab にも保存されない。
    //
    // Runtime Camera との関係:
    //   一切値をやり取りしない。Play 開始時に Runtime Camera へ写すことも、
    //   Play 終了時に Runtime Camera から取り込むこともしない。
    //   Viewport が 1 つしかないため、描画に使う行列だけを
    //   「Edit Mode なら編集カメラ / Play・実行中なら Runtime Camera」と切り替える。
    ReplayEngine::Editor::EditorViewportCamera   editor_camera;
    ReplayEngine::Editor::EditorCameraController editor_camera_controller;

    // 編集カメラがマウス／キーを消費したフレーム。
    // Gizmo と選択処理を同時に走らせないためのフラグ。
    bool             editor_camera_consumed_input{ false };

    // 現在の Scene に対応する編集カメラ状態の保存キー。
    std::string      editor_camera_state_key;

    bool             enable_deferred { true };
    bool             enable_particles{ false };
    bool             enable_trail    { false };

    // ダミー法線テクスチャ (法線マップが無いマテリアル用フォールバック)
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> dummy_normal_srv;

    int toon_preset_index{ 0 };

    framework(HWND hwnd);
    ~framework();

    framework(const framework&) = delete;
    framework& operator=(const framework&) = delete;
    framework(framework&&) noexcept = delete;
    framework& operator=(framework&&) noexcept = delete;

    // 終了理由と主要な進行状況を Saved/engine_log.txt へ追記する。
    // 「なぜ落ちたか」が分からないと原因の切り分けができないため、
    // 例外や異常終了だけでなく正常終了の経路も残す。
    static void log_shutdown_reason(const char* reason)
    {
        std::error_code error;
        std::filesystem::create_directories("Saved", error);
        std::ofstream log("Saved/engine_log.txt", std::ios::app);
        if (!log) return;
        const auto now = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());
        tm local{};
        localtime_s(&local, &now);
        char stamp[32]{};
        strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);
        log << "[" << stamp << "] " << reason << '\n';
    }

    int run()
    {
        MSG msg{};
        log_shutdown_reason("=== 起動 ===");
        if (!initialize())
        {
            log_shutdown_reason("initialize() が false を返したため終了");
            return 0;
        }
        log_shutdown_reason("initialize() 完了");

#ifdef USE_IMGUI
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        if (!io.Fonts->AddFontFromFileTTF(
            "C:\\Windows\\Fonts\\meiryo.ttc", 15.0f, nullptr, glyphRangesJapanese))
        {
            io.Fonts->AddFontDefault();
        }
        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX11_Init(device.Get(), immediate_context.Get());
        ImGui::StyleColorsDark();
        configure_editor_style();
#endif

        while (WM_QUIT != msg.message)
        {
            if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            else
            {
                tictoc.tick();
                calculate_frame_stats();
                // 描画中の未捕捉例外で静かに落ちると原因が追えないため、
                // ここで捕まえて理由を残す。
                try
                {
                    update(tictoc.time_interval());
                    render(tictoc.time_interval());
                }
                catch (const std::exception& exception)
                {
                    log_shutdown_reason((std::string("例外: ") + exception.what()).c_str());
                    throw;
                }
                catch (...)
                {
                    log_shutdown_reason("不明な例外");
                    throw;
                }
            }
        }
        log_shutdown_reason("メッセージループを抜けた (WM_QUIT)");

#ifdef USE_IMGUI
        save_editor_session();
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
#endif

        if (is_fullscreen()) toggle_fullscreen();

        return uninitialize() ? static_cast<int>(msg.wParam) : 0;
    }

    LRESULT CALLBACK handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {
#ifdef USE_IMGUI
        const bool keyboard_message = msg == WM_KEYDOWN || msg == WM_KEYUP ||
            msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP || msg == WM_CHAR;
        if (editor_mode)
        {
            ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
            const bool control_down = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shortcut_pressed = msg == WM_KEYDOWN && control_down &&
                (lparam & 0x40000000) == 0 && !ImGui::GetIO().WantTextInput;
            if (shortcut_pressed && wparam == 'S')
            {
                const bool choose_path = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                // 標準保存は GameObject Scene だけを対象にする。
                // 旧 .replaystage を同時更新して二重の正本を作らない。
                save_object_scene(choose_path);
                return 0;
            }
            if (msg == WM_KEYDOWN && control_down && !ImGui::GetIO().WantTextInput &&
                (wparam == 'Z' || wparam == 'Y'))
            {
                if (wparam == 'Z') object_editor_context.Undo();
                else object_editor_context.Redo();
                return 0;
            }
            if (shortcut_pressed && wparam == 'D')
            {
                object_hierarchy_panel.DuplicateSelection(object_editor_context);
                return 0;
            }
            if (msg == WM_KEYDOWN && wparam == VK_DELETE &&
                !ImGui::GetIO().WantTextInput &&
                selected_editor_object == editor_selection::game_object)
            {
                object_hierarchy_panel.DestroySelection(object_editor_context);
                return 0;
            }
            if (msg == WM_KEYDOWN && wparam == 'F' && (GetKeyState(VK_CONTROL) & 0x8000))
            {
                focus_search_requested = true;
                set_edit_mode(true);
                return 0;
            }
            if (search_input_active && keyboard_message)
            {
                return 0;
            }
            if (msg == WM_KEYDOWN && edit_mode_active && !search_input_active)
            {
                if (wparam == 'W') transform_gizmo.SetOperation(ReplayEngine::Editor::GizmoOperation::Translate);
                if (wparam == 'E') transform_gizmo.SetOperation(ReplayEngine::Editor::GizmoOperation::Rotate);
                if (wparam == 'R') transform_gizmo.SetOperation(ReplayEngine::Editor::GizmoOperation::Scale);
            }
        }
        if (msg == WM_KEYDOWN && wparam == VK_F1)
        {
            editor_mode = !editor_mode;
            set_edit_mode(editor_mode);
            return 0;
        }
        if (msg == WM_KEYDOWN && wparam == VK_F3 && editor_mode)
        {
            set_edit_mode(!edit_mode_active);
            return 0;
        }
        if (msg == WM_KEYDOWN && wparam == VK_F4)
        {
            show_render_stats = !show_render_stats;
            return 0;
        }
        // F5 で GameObject シーンの実行 / 停止を切り替える。
        // 実行中は編集用 Scene を複製した実行用 Scene が動くため、
        // Play 中の位置や体力の変化が編集内容へ書き戻らない。
        if (msg == WM_KEYDOWN && wparam == VK_F5 && editor_mode)
        {
            if (object_scene_play_mode) exit_object_play_mode();
            else enter_object_play_mode();
            return 0;
        }
#endif
        if ((msg == WM_SYSKEYDOWN && wparam == VK_RETURN && (lparam & (1LL << 29))) ||
            (msg == WM_KEYDOWN && wparam == VK_F11))
        {
            toggle_fullscreen();
            return 0;
        }
        if (msg == WM_KEYDOWN && wparam == VK_F2)
        {
            render_graph.CycleOutput();
            if (render_graph.RequiresDeferred()) enable_deferred = true;
            return 0;
        }
        switch (msg)
        {
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps); break; }
        case WM_CLOSE: log_shutdown_reason("WM_CLOSE"); return DefWindowProc(hwnd, msg, wparam, lparam);
        case WM_DESTROY: log_shutdown_reason("WM_DESTROY"); PostQuitMessage(0); break;
        case WM_CREATE:  break;
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED)
            {
                const UINT width = LOWORD(lparam);
                const UINT height = HIWORD(lparam);
                if (width > 0 && height > 0)
                {
                    pending_client_width = width;
                    pending_client_height = height;
                    resize_pending = true;
                }
            }
            break;
        case WM_DPICHANGED:
#ifdef USE_IMGUI
            if (ImGui::GetCurrentContext() != nullptr)
            {
                configure_editor_style();
                editor_layout_dirty = true;
            }
#endif
            if (const RECT* suggested = reinterpret_cast<const RECT*>(lparam))
            {
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                    suggested->right - suggested->left, suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
            break;
        case WM_KEYDOWN:
            if (scene_manager.OnKeyDown(wparam))
            {
                break;
            }
            if (wparam == VK_ESCAPE)
            {
                if (editor_mode)
                {
                    // EditorではEscを操作解除へ使う。終了はFile > ExitまたはWM_CLOSE。
                    if (object_gizmo_dragging || editor_camera_controller.MouseCaptured() ||
                        ImGui::IsPopupOpen(static_cast<const char*>(nullptr),
                            ImGuiPopupFlags_AnyPopup)) return 0;
                    return 0;
                }
                log_shutdown_reason("ESCキー");
                PostMessage(hwnd, WM_CLOSE, 0, 0);
            }
            break;
        case WM_ENTERSIZEMOVE: tictoc.stop(); break;
        case WM_EXITSIZEMOVE:  tictoc.start(); break;
        default: return DefWindowProc(hwnd, msg, wparam, lparam);
        }
        return 0;
    }

private:
    enum class editor_workspace;
    bool initialize();
    void update(float elapsed_time);
    void render(float elapsed_time);
    bool uninitialize();
    // エディタのデバッグ用メッシュ (static_meshes[0]) を置くワールド行列。
    // かつてはここで旧 Player の Transform を返していたが、その分岐は撤去した。
    void store_debug_mesh_world(DirectX::XMFLOAT4X4& world) const;

    // SSAO/SSR/TAAが共有するフレーム定数を作ってb9へ載せる。
    void update_frame_constants(const DirectX::XMMATRIX& view,
        const DirectX::XMMATRIX& projection, float elapsed_time);
    ID3D11PixelShader* skinned_forward_shader(int shading) const;
    ID3D11PixelShader* static_forward_shader(int shading) const;
    unsigned int deferred_shading_model(int shading) const;
    void bind_gbuffer_material(unsigned int shading_model, bool stage_surface = false,
        float pixelate_size = 6.0f, float pixelate_strength = 1.0f,
        float metallic = 0.0f, float roughness = 0.55f,
        float ambient_occlusion = 1.0f, float emissive_strength = 0.0f);
    void apply_toon_preset(int preset);
    void reset_editor_values();
    void draw_editor();
    void draw_editor_main_menu();
    void draw_editor_toolbar();
    void draw_scene_view_panel();
    void draw_runtime_mode_banner();

    // 操作対象 GameObject の実行時診断。旧 Player の項目は持たない。
    void draw_controlled_character_diagnostics();
    void draw_search_results();
    void draw_scene_hierarchy();
    void draw_inspector();
    void draw_shader_adjustment_workspace();
    void draw_shader_stack(const char* id, int& base_shader, bool& outline_pass,
        ReplayEngine::Rendering::ShaderLayerStack& layers, float& pixel_grid,
        float& pixelate_strength);
    void draw_screen_effect_stack();
    // SSAO / SSR / TAA / CSM の有効化と調整項目。
    void draw_screen_space_settings();
    // ポリゴン数・ドローコール数などの描画統計オーバーレイ。
    void draw_render_stats_overlay();
    void draw_character_material_controls(const char* id, int& base_shader, bool& outline_pass,
        ReplayEngine::Rendering::ShaderLayerStack& layers,
        ReplayEngine::Rendering::CharacterMaterialProfile& profile, float& pixel_grid,
        float& pixelate_strength);
    bool browse_stage_asset();
    bool load_stage_asset(const std::wstring& filename);
    bool load_stage_asset_now(const std::wstring& filename);
    // ワーカースレッドから呼べるモデル先読み。キャッシュへ載せるだけで
    // frameworkの表示状態は触らないため、並列ロード中に安全に使える。
    bool prewarm_model_asset(const std::filesystem::path& path);
    bool place_asset_in_object_scene(const ReplayEngine::Assets::AssetRecord& asset,
        bool add_mesh_collider);
    void handle_viewport_selection();
    void draw_scene_grid_overlay();
    bool draw_object_transform_gizmo();
    void save_editor_session();
    void restore_editor_session();

    // 選択中の GameObject を Prefab として保存する。
    // choose_path が true ならファイル名を選ばせる（任意名で保存できる）。
    // 保存した Prefab は AssetDatabase へ登録され、AssetGUID で参照できるようになる。
    void save_selected_prefab(bool choose_path);
    void load_prefab();
    // --- GameObject / Component 基盤との接続 -------------------------------
    // 実装はすべて Source/app/Runtime/framework_gameobject_scene.cpp にある。
    void initialize_object_scene();
    void update_object_scene(float elapsed_time);
    bool save_object_scene(bool choose_path);
    bool load_object_scene(bool choose_path);
    bool autosave_object_scene();
    void check_object_scene_recovery();
    void draw_object_scene_recovery_prompt();
    void enter_object_play_mode();
    void exit_object_play_mode();
    ReplayEngine::Scene::Scene& active_object_scene() noexcept;
    const ReplayEngine::Scene::Scene& active_object_scene() const noexcept;
    skinned_mesh* resolve_object_mesh(const std::string& asset_guid);
    const ReplayEngine::Rendering::MaterialAsset* resolve_object_material(
        const std::string& asset_guid);
    ReplayEngine::Rendering::RenderItem resolve_render_item_material(
        const ReplayEngine::Rendering::RenderItem& item);
    // depth_only = true で深度プリパス用の描画になる。
    // 深度プリパスを使う構成では、GBuffer へ出すものを必ずここでも描くこと。
    // 描き漏らすと DepthFunc=EQUAL に落とされて画面から消える。
    void draw_object_scene_meshes(ID3D11PixelShader* override_pixel_shader,
        bool gbuffer_pass, bool depth_only = false);
    void clear_object_mesh_cache() noexcept;
    void clear_object_material_cache() noexcept;
    bool object_runtime_active() const noexcept;
    void update_object_fixed_step(float elapsed_time);
    void update_object_camera_follow(float elapsed_time);
    void refresh_object_scene_services();
    void sync_object_lights();

    // --- 新規 Scene 作成 ---------------------------------------------------
    //
    // Empty  … GameObject を 1 つも作らない。操作対象は未設定。
    // Default … Default Controlled Character Prefab を 1 体だけ配置し、
    //           そのルートを操作対象にする。
    //
    // どちらも「ユーザーが新規作成を選んだとき」しか呼ばれない。
    // 起動時や Scene 読み込み時に Prefab を配置することは決してない。
    bool create_object_scene(const std::string& name, bool place_default_character);

    // --- プロジェクト設定 --------------------------------------------------
    void load_project_settings();
    bool save_project_settings();
    void draw_project_settings_panel();

    // 「新しいシーンを作成」ボタンと Empty / Default の選択ダイアログ。
    void draw_new_object_scene_controls();

    // --- Scene View の編集カメラ (Source/app/Editor/framework_editor_camera.cpp) --
    //
    // 【描画・Picking・Gizmo・Collider Debug Draw の行列はここだけから取る】
    //   別々に行列を組み立てると、見えている位置・拾える位置・線の位置が
    //   互いにずれる。取得窓口を 1 か所にすることで構造的に防ぐ。
    //
    //   Edit Mode        -> 編集カメラ
    //   Play / 実行中     -> Runtime Camera (SceneGame が持つ Camera)
    bool using_editor_camera() const noexcept;
    DirectX::XMMATRIX viewport_view_matrix() const;
    DirectX::XMMATRIX viewport_projection_matrix() const;
    DirectX::XMFLOAT3 viewport_eye_position() const;

    // 画面座標からワールド空間の視線を作る。Picking はこれだけを使う。
    ReplayEngine::Editor::EditorViewportCamera::Ray viewport_picking_ray(
        float mouse_x, float mouse_y) const;

    // ImGui / Win32 から入力を読み、編集カメラへ渡す。
    void update_editor_camera(float elapsed_time);

    // F キーのフォーカス。選択対象の World Bounds を求めて収める。
    // Undo 履歴へは積まない（Scene のデータを変更していないため）。
    void focus_editor_camera_on_selection();

    void draw_editor_camera_settings();

    // 編集カメラ状態の保存・復元。Scene ファイルには一切書き込まない。
    void load_editor_camera_state();
    void save_editor_camera_state();
    std::string make_editor_camera_state_key() const;

    // Default Controlled Character Prefab の現在の解決結果。
    ReplayEngine::Project::PrefabReferenceStatus resolve_default_character_prefab() const;

    // --- 衝突 (Source/app/Runtime/framework_collision_world.cpp) ------------
    // Scene の切り替えと Play / Edit の切り替えに合わせて衝突世界をつなぎ替える。
    void initialize_collision_world();
    void attach_collision_world(ReplayEngine::Scene::Scene& scene);
    void detach_collision_world();
    void refresh_collision_world();
    void dispatch_collision_triggers();

    // --- 衝突メッシュの供給 (framework_collision_mesh_source.cpp) -----------
    // AssetGUID -> ローカル空間の三角形。Cook キャッシュから呼ばれる。
    bool load_collision_triangles(const ReplayEngine::Physics::CookKey& key,
        std::vector<ReplayEngine::Physics::Triangle>& out_local_triangles);
    std::string resolve_asset_revision(const std::string& asset_guid) const;

    // --- 衝突の可視化と診断 (Source/app/Editor/framework_collider_debug.cpp) -
    void draw_collider_debug_overlay();
    void draw_collision_diagnostics_panel();
    const skinned_mesh::animation::keyframe* resolve_object_keyframe(
        skinned_mesh& mesh, int clip_index, float animation_time) const;
    void draw_project_panel();
    void draw_console_panel();
    void execute_editor_command(const std::string& command);
    void draw_workspace_panel();
    void set_editor_workspace(editor_workspace workspace);
    void configure_editor_style();
    void set_edit_mode(bool enabled);
    bool resize_back_buffers(UINT width, UINT height);
    void apply_pending_resize();
    bool toggle_fullscreen();
    bool is_fullscreen() const;

private:
    high_resolution_timer tictoc;
    uint32_t frames{ 0 };
    float elapsed_time{ 0.0f };
    void calculate_frame_stats()
    {
        if (++frames, (tictoc.time_stamp() - elapsed_time) >= 1.0f)
        {
            float fps = static_cast<float>(frames);
            std::wostringstream outs;
            outs.precision(6);
            outs << APPLICATION_NAME << L" : FPS : " << fps << L" / " << L"Frame Time : " << 1000.0f / fps << L" (ms)";
            SetWindowTextW(hwnd, outs.str().c_str());
            frames = 0;
            elapsed_time += 1.0f;
        }
    }

private:
    float luminance_threshold{ 1.0f };
    UINT client_width{ SCREEN_WIDTH };
    UINT client_height{ SCREEN_HEIGHT };
    UINT pending_client_width{ SCREEN_WIDTH };
    UINT pending_client_height{ SCREEN_HEIGHT };
    bool resize_pending{ false };
    bool borderless_fullscreen{ false };
    DWORD windowed_style{ WS_OVERLAPPEDWINDOW | WS_VISIBLE };
    DWORD windowed_ex_style{ 0 };
    RECT windowed_rect{ 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };

    // 旧 Player 用の項目 (player) は撤去した。
    // 操作対象は Scene 内の通常 GameObject なので game_object で選択される。
    enum class editor_selection
    {
        world,
        camera,
        // Scene 内の GameObject / Component 基盤で選択された GameObject。
        game_object,
        directional_light,
        point_lights,
        rendering,
        post_process
    };
    enum class editor_workspace
    {
        general,
        placement,
        modeling,
        animation,
        rendering,
        shader_adjustment
    };
    enum class editor_view
    {
        scene,
        game
    };
    editor_selection selected_editor_object{ editor_selection::world };
    editor_workspace active_editor_workspace{ editor_workspace::general };
    editor_view active_editor_view{ editor_view::scene };
    bool editor_layout_checked{ false };
    bool editor_layout_dirty{ false };
    bool editor_hide_requested{ false };
    bool editor_session_active{ false };
    bool show_hierarchy_panel{ true };
    bool show_inspector_panel{ true };
    bool show_project_panel{ true };
    bool show_console_panel{ true };
    bool show_workspace_panel{ true };
    bool show_validation_panel{ true };
    bool show_scene_view{ true };
    bool scene_view_hovered{ false };
    bool scene_view_focused{ false };
    float scene_view_min_x{ 0.0f };
    float scene_view_min_y{ 0.0f };
    float scene_view_max_x{ 0.0f };
    float scene_view_max_y{ 0.0f };
    int scene_view_draw_mode{ 0 };
    char asset_search_text[192]{};
    int asset_type_filter{ 0 };
    std::string selected_asset_guid;
    bool asset_drop_add_collider{ false };
    char new_material_name[128]{ "NewMaterial" };
    ReplayEngine::Rendering::MaterialAsset material_editor_asset;
    std::string material_editor_guid;
    std::string material_editor_status;
    bool material_editor_loaded{ false };

    bool create_material_asset();
    bool load_material_editor(const ReplayEngine::Assets::AssetRecord& asset);
    bool save_material_editor();
    void draw_material_asset_editor();
    bool gizmo_local_space{ false };
    bool show_scene_grid{ true };
    float scene_grid_step{ 1.0f };
    struct ObjectGizmoState
    {
        ReplayEngine::Core::ObjectID id;
        DirectX::XMFLOAT3 world_position{};
        DirectX::XMFLOAT3 local_rotation{};
        DirectX::XMFLOAT3 local_scale{ 1.0f, 1.0f, 1.0f };
    };
    std::vector<ObjectGizmoState> object_gizmo_states;
    bool object_gizmo_dragging{ false };
    int object_gizmo_axis{ -1 };
    float object_gizmo_start_mouse_x{ 0.0f };
    float object_gizmo_start_mouse_y{ 0.0f };
    float object_gizmo_screen_axis_x{ 1.0f };
    float object_gizmo_screen_axis_y{ 0.0f };
    float object_gizmo_world_per_pixel{ 0.01f };
    DirectX::XMFLOAT3 object_gizmo_world_axis{ 1.0f, 0.0f, 0.0f };
    // このフレームでImGui::NewFrame()を通したか。
    // ロード完了フレームのようにupdate()が早期returnした直後にeditor_modeが
    // 立つ場合があり、NewFrame無しでRender()するとImGuiがassertするため、
    // NewFrameとRender/EndFrameの対をこのフラグで保証する。
    bool imgui_frame_active{ false };
    bool edit_mode_active{ false };
    bool search_input_active{ false };
    bool focus_search_requested{ false };
    char editor_search_text[256]{};
    char editor_command_text[256]{};
    std::string editor_command_result{ "help でコマンド一覧を表示" };
    bool viewport_drag_selecting{ false };
    POINT viewport_drag_start{};
};
