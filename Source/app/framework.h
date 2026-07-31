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
#include "../../RePlayEngine/Assets/AssetDatabase.h"
#include "../../RePlayEngine/Assets/AsyncAssetManager.h"
#include "../../RePlayEngine/Assets/ConcurrentResourceCache.h"
#include "../../RePlayEngine/Editor/Commands/UndoStack.h"
#include "../../RePlayEngine/Editor/Gizmo/TransformGizmo.h"
#include "../../RePlayEngine/Editor/Gizmo/ViewportPicker.h"
#include "../../RePlayEngine/Scene/SceneDocument.h"
#include "../../RePlayEngine/Physics/MeshCollisionCooker.h"

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
    bool animate_model{ true };
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
    int animation_clip_index{ 0 };
    float animation_tick{ 0.0f };
    float animation_speed{ 1.0f };
    bool animation_loop{ true };

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

    // ƒ‚ƒfƒ‹–ˆ‚ÌƒVƒF[ƒfƒBƒ“ƒOİ’è (skinned_meshes[i] ‚Æ‘Î‰)
    // 0=FBX•W€A1=PBRA2=ƒgƒD[ƒ“A3=ƒAƒ“ƒŠƒbƒgA4=ƒsƒNƒZƒŒ[ƒVƒ‡ƒ“
    int shading_per_skinned[8] { 1, 1, 1, 1, 1, 1, 1, 1 };
    int shading_per_static [8] { 1, 1, 1, 1, 1, 1, 1, 1 };
    bool outline_per_skinned[8] { false, false, false, false, false, false, false, false };
    bool outline_per_static [8] { false, false, false, false, false, false, false, false };
    ReplayEngine::Rendering::ShaderLayerStack shader_layers_skinned[8];
    ReplayEngine::Rendering::ShaderLayerStack shader_layers_static[8];
    ReplayEngine::Rendering::CharacterMaterialProfile character_profiles_skinned[8];
    ReplayEngine::Rendering::CharacterMaterialProfile character_profiles_static[8];
    float pixelate_grid_per_skinned[8] { 6, 6, 6, 6, 6, 6, 6, 6 };
    float pixelate_strength_per_skinned[8] { 1, 1, 1, 1, 1, 1, 1, 1 };
    float pixelate_grid_per_static[8] { 6, 6, 6, 6, 6, 6, 6, 6 };
    float pixelate_strength_per_static[8] { 1, 1, 1, 1, 1, 1, 1, 1 };

    // UI‚©‚ç‘S‘Ì‚Ö“K—p‚·‚é•`‰æ•û®Bshading_per_skinned[0] ‚Æ“¯Šú‚·‚éB
    int shading_model_override { 1 };

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

    // SSAO/SSR/TAA‚ª‹¤—L‚·‚éƒtƒŒ[ƒ€’è”Bb9‚ÖÚ‚¹‚éB
    ReplayEngine::Rendering::FrameConstants frame_constants{};
    Microsoft::WRL::ComPtr<ID3D11Buffer> frame_constants_cb;
    // TAA‚ÌÄ“Š‰e‚Ég‚¤‘OƒtƒŒ[ƒ€‚Ìƒrƒ…[Ë‰es—ñB‰‰ñ‚Í¡ƒtƒŒ[ƒ€‚Å–„‚ß‚éB
    DirectX::XMFLOAT4X4 previous_view_projection{};
    bool previous_view_projection_valid{ false };
    // Ë‰es—ñ‚Ö‰ÁZ‚µ‚½TAAƒWƒbƒ^[(NDC)Bƒ‚[ƒVƒ‡ƒ“ƒxƒNƒ^[‚Å‘Å‚¿Á‚·‚Ì‚Ég‚¤B
    DirectX::XMFLOAT2 taa_jitter_ndc{ 0.0f, 0.0f };
    DirectX::XMFLOAT2 previous_taa_jitter_ndc{ 0.0f, 0.0f };
    unsigned int frame_index{ 0 };
    bool enable_ssao{ true };
    bool enable_ssr{ true };
    bool enable_taa{ true };
    // [“xƒvƒŠƒpƒXBG-Buffer‚ÌPSÀs‚ğÅ‘O–Ê‚Ì1‰ñ‚É—}‚¦‚éB
    // ’¸“_ˆ—‚ª2‰ñ‚É‚È‚é‚½‚ßALOD‚Æ•¹—p‚·‚é‘O’ñB
    bool enable_depth_prepass{ true };
    // •`‰æ“ŒvƒI[ƒo[ƒŒƒC‚Ì•\¦BF4‚ÅØ‚è‘Ö‚¦‚éB
    bool show_render_stats{ true };
    // åˆå›ãƒ•ãƒ¬ãƒ¼ãƒ ã ã‘çµ±è¨ˆã‚¦ã‚£ãƒ³ãƒ‰ã‚¦ã®ä½ç½®ã‚’å¼·åˆ¶ã™ã‚‹ãŸã‚ã®ãƒ•ãƒ©ã‚°ã€‚
    // imgui.ini ã«æ®‹ã£ãŸæ—§åº§æ¨™(ã‚¤ãƒ³ã‚¹ãƒšã‚¯ã‚¿ã¨é‡ãªã‚‹ä½ç½®)ã«
    // å¼•ã£å¼µã‚‰ã‚Œã‚‹ã®ã‚’é˜²ãã€‚
    bool stats_window_placed_{ false };

    GameScene*       game_scene{ nullptr };
    UIManager        uiManager;
    bool             enable_scene_game{ true };
    bool             editor_mode{ false };

    // ƒXƒe[ƒW‚Ì•`‰æ•û®‚Í skinned[0] ‚Æ•ª—£‚·‚éB0‚ÍFBX•W€‚Åã‘‚«‚µ‚È‚¢B
    // 0=FBX•W€A1=PBRA2=ƒgƒD[ƒ“A3=ƒAƒ“ƒŠƒbƒgA4=ƒsƒNƒZƒŒ[ƒVƒ‡ƒ“
    int              shading_per_stage{ 1 };
    bool             outline_per_stage{ false };
    ReplayEngine::Rendering::ShaderLayerStack stage_shader_layers;
    ReplayEngine::Rendering::CharacterMaterialProfile stage_character_profile;
    float            stage_pixelate_grid{ 6.0f };
    float            stage_pixelate_strength{ 1.0f };
    bool             enable_stage_shader{ true }; // false = always FBX default
    bool             enable_stage_render{ false };
    bool             stage_texture_wrap{ true };
    float            stage_texture_contrast{ 1.20f };
    bool             stage_asset_placed{ false };
    std::string      selected_stage_asset_path;
    std::string      selected_stage_cache_path;
    std::string      stage_asset_status{ "–¢‘I‘ğ" };
    ReplayEngine::Assets::AssetDatabase asset_database;
    ReplayEngine::Assets::ConcurrentResourceCache<static_mesh> static_mesh_cache;
    ReplayEngine::Assets::ConcurrentResourceCache<skinned_mesh> skinned_mesh_cache;
    ReplayEngine::Assets::ConcurrentResourceCache<gltf_model> gltf_model_cache;
    ReplayEngine::Assets::AsyncAssetManager async_asset_manager;
    ReplayEngine::Scene::SceneDocument editor_scene_document;
    ReplayEngine::Scene::SceneDocument runtime_scene_document;
    ReplayEngine::Editor::UndoStack scene_undo_stack;
    ReplayEngine::Editor::TransformGizmo transform_gizmo;
    ReplayEngine::Physics::MeshCollisionCooker collision_cooker;
    ReplayEngine::Scene::EntityId active_stage_placement_id{ 0 };
    ReplayEngine::Scene::EntityId selected_scene_entity_id{ 0 };
    std::vector<ReplayEngine::Scene::EntityId> selected_scene_entity_ids;
    std::vector<ReplayEngine::Scene::SceneEntity> copied_scene_entities;
    std::string      selected_stage_asset_guid;
    std::filesystem::path current_scene_path{ "resources/Scenes/Main.replayscene" };
    std::string      scene_document_status{ "V‹KƒV[ƒ“" };
    std::string      shader_preset_status{ "ƒvƒŠƒZƒbƒg–¢‘I‘ğ" };
    bool             async_stage_load_active{ false };

    bool             enable_deferred { true };
    bool             enable_particles{ false };
    bool             enable_trail    { false };

    // ƒ_ƒ~[–@üƒeƒNƒXƒ`ƒƒ (–@üƒ}ƒbƒv‚ª–³‚¢ƒ}ƒeƒŠƒAƒ‹—pƒtƒH[ƒ‹ƒoƒbƒN)
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> dummy_normal_srv;

    int toon_preset_index{ 0 };

    framework(HWND hwnd);
    ~framework();

    framework(const framework&) = delete;
    framework& operator=(const framework&) = delete;
    framework(framework&&) noexcept = delete;
    framework& operator=(framework&&) noexcept = delete;

    // I—¹——R‚Æå—v‚Èisó‹µ‚ğ Saved/engine_log.txt ‚Ö’Ç‹L‚·‚éB
    // u‚È‚º—‚¿‚½‚©v‚ª•ª‚©‚ç‚È‚¢‚ÆŒ´ˆö‚ÌØ‚è•ª‚¯‚ª‚Å‚«‚È‚¢‚½‚ßA
    // —áŠO‚âˆÙíI—¹‚¾‚¯‚Å‚È‚­³íI—¹‚ÌŒo˜H‚àc‚·B
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
        log_shutdown_reason("=== ‹N“® ===");
        if (!initialize())
        {
            log_shutdown_reason("initialize() ‚ª false ‚ğ•Ô‚µ‚½‚½‚ßI—¹");
            return 0;
        }
        log_shutdown_reason("initialize() Š®—¹");

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
                // •`‰æ’†‚Ì–¢•ß‘¨—áŠO‚ÅÃ‚©‚É—‚¿‚é‚ÆŒ´ˆö‚ª’Ç‚¦‚È‚¢‚½‚ßA
                // ‚±‚±‚Å•ß‚Ü‚¦‚Ä——R‚ğc‚·B
                try
                {
                    update(tictoc.time_interval());
                    render(tictoc.time_interval());
                }
                catch (const std::exception& exception)
                {
                    log_shutdown_reason((std::string("—áŠO: ") + exception.what()).c_str());
                    throw;
                }
                catch (...)
                {
                    log_shutdown_reason("•s–¾‚È—áŠO");
                    throw;
                }
            }
        }
        log_shutdown_reason("ƒƒbƒZ[ƒWƒ‹[ƒv‚ğ”²‚¯‚½ (WM_QUIT)");

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
                save_scene_document((GetKeyState(VK_SHIFT) & 0x8000) != 0);
                return 0;
            }
            if (msg == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000) &&
                !ImGui::GetIO().WantTextInput && (wparam == 'Z' || wparam == 'Y'))
            {
                std::string label;
                if (wparam == 'Z') scene_undo_stack.Undo(editor_scene_document, label);
                else scene_undo_stack.Redo(editor_scene_document, label);
                scene_document_status = label.empty() ? scene_document_status : label;
                selected_scene_entity_ids.erase(std::remove_if(selected_scene_entity_ids.begin(),
                    selected_scene_entity_ids.end(), [this](ReplayEngine::Scene::EntityId id)
                    { return editor_scene_document.Find(id) == nullptr; }), selected_scene_entity_ids.end());
                if (editor_scene_document.Find(selected_scene_entity_id) == nullptr)
                    selected_scene_entity_id = selected_scene_entity_ids.empty() ? 0 : selected_scene_entity_ids.back();
                return 0;
            }
            if (shortcut_pressed && wparam == 'D')
            {
                duplicate_selected_entities();
                return 0;
            }
            if (msg == WM_KEYDOWN && control_down &&
                !search_input_active && !ImGui::GetIO().WantTextInput &&
                (wparam == 'C' || wparam == 'V'))
            {
                if (wparam == 'C') copy_selected_entities();
                else paste_copied_entities();
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
        case WM_KEYDOWN:
            if (scene_manager.OnKeyDown(wparam))
            {
                break;
            }
            if (wparam == VK_ESCAPE)
            {
                log_shutdown_reason("ESCƒL[");
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
    void store_object_world(DirectX::XMFLOAT4X4& world) const;
    // SSAO/SSR/TAA‚ª‹¤—L‚·‚éƒtƒŒ[ƒ€’è”‚ğì‚Á‚Äb9‚ÖÚ‚¹‚éB
    void update_frame_constants(const DirectX::XMMATRIX& view,
        const DirectX::XMMATRIX& projection, float elapsed_time);
    ID3D11PixelShader* skinned_forward_shader(int shading) const;
    ID3D11PixelShader* static_forward_shader(int shading) const;
    unsigned int deferred_shading_model(int shading) const;
    void bind_gbuffer_material(unsigned int shading_model, bool stage_surface = false,
        float pixelate_size = 6.0f, float pixelate_strength = 1.0f);
    void apply_toon_preset(int preset);
    void reset_editor_values();
    void draw_editor();
    void draw_editor_toolbar();
    void draw_search_results();
    void draw_scene_hierarchy();
    void draw_inspector();
    void draw_shader_adjustment_workspace();
    void draw_shader_stack(const char* id, int& base_shader, bool& outline_pass,
        ReplayEngine::Rendering::ShaderLayerStack& layers, float& pixel_grid,
        float& pixelate_strength);
    void draw_screen_effect_stack();
    // SSAO / SSR / TAA / CSM ‚Ì—LŒø‰»‚Æ’²®€–ÚB
    void draw_screen_space_settings();
    // ƒ|ƒŠƒSƒ“”Eƒhƒ[ƒR[ƒ‹”‚È‚Ç‚Ì•`‰æ“ŒvƒI[ƒo[ƒŒƒCB
    void draw_render_stats_overlay();
    void draw_character_material_controls(const char* id, int& base_shader, bool& outline_pass,
        ReplayEngine::Rendering::ShaderLayerStack& layers,
        ReplayEngine::Rendering::CharacterMaterialProfile& profile, float& pixel_grid,
        float& pixelate_strength);
    bool browse_stage_asset();
    bool load_stage_asset(const std::wstring& filename);
    bool load_stage_asset_now(const std::wstring& filename);
    // ƒ[ƒJ[ƒXƒŒƒbƒh‚©‚çŒÄ‚×‚éƒ‚ƒfƒ‹æ“Ç‚İBƒLƒƒƒbƒVƒ…‚ÖÚ‚¹‚é‚¾‚¯‚Å
    // framework‚Ì•\¦ó‘Ô‚ÍG‚ç‚È‚¢‚½‚ßA•À—ñƒ[ƒh’†‚ÉˆÀ‘S‚Ég‚¦‚éB
    bool prewarm_model_asset(const std::filesystem::path& path);
    void draw_stage_placement_controls();
    void draw_scene_document_toolbar();
    void draw_scene_entity_inspector();
    void draw_transform_gizmo_controls();
    void handle_viewport_selection();
    void select_scene_entity(ReplayEngine::Scene::EntityId id, bool additive);
    void copy_selected_entities();
    void paste_copied_entities();
    void duplicate_selected_entities();
    void save_scene_document(bool choose_path);
    bool load_scene_document(bool choose_path,
        ReplayEngine::Scene::EntityId preferred_entity_id = 0);
    void save_editor_session();
    void restore_editor_session();
    void create_scene_document(const std::string& name);
    void save_selected_prefab();
    void load_prefab();
    void cook_selected_mesh_collision();
    void sync_selected_entity_to_stage();
    void store_stage_shader_layers(ReplayEngine::Scene::ModelRendererData& renderer) const;
    void restore_stage_shader_layers(const ReplayEngine::Scene::ModelRendererData& renderer);
    ReplayEngine::Scene::SceneDocument& active_scene_document() noexcept;
    const ReplayEngine::Scene::SceneDocument& active_scene_document() const noexcept;
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

    enum class editor_selection
    {
        world,
        camera,
        player,
        stage,
        scene_entity,
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
    editor_selection selected_editor_object{ editor_selection::world };
    editor_workspace active_editor_workspace{ editor_workspace::general };
    bool editor_layout_checked{ false };
    bool editor_layout_dirty{ false };
    bool editor_hide_requested{ false };
    bool editor_session_active{ false };
    // ‚±‚ÌƒtƒŒ[ƒ€‚ÅImGui::NewFrame()‚ğ’Ê‚µ‚½‚©B
    // ƒ[ƒhŠ®—¹ƒtƒŒ[ƒ€‚Ì‚æ‚¤‚Éupdate()‚ª‘Šúreturn‚µ‚½’¼Œã‚Éeditor_mode‚ª
    // —§‚Âê‡‚ª‚ ‚èANewFrame–³‚µ‚ÅRender()‚·‚é‚ÆImGui‚ªassert‚·‚é‚½‚ßA
    // NewFrame‚ÆRender/EndFrame‚Ì‘Î‚ğ‚±‚Ìƒtƒ‰ƒO‚Å•ÛØ‚·‚éB
    bool imgui_frame_active{ false };
    bool edit_mode_active{ false };
    bool search_input_active{ false };
    bool focus_search_requested{ false };
    char editor_search_text[256]{};
    char editor_command_text[256]{};
    std::string editor_command_result{ "help ‚ÅƒRƒ}ƒ“ƒhˆê——‚ğ•\¦" };
    bool viewport_drag_selecting{ false };
    POINT viewport_drag_start{};
};
