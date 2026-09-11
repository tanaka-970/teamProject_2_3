// Exercise the production Inspector, viewport editing and scene submission paths
// without reading/writing the user's Editor session or starting game scripts.
#include "skinned_mesh.h"
#include "../Editor/framework_landscape_editorInternal.h"
#include "../../../RePlayEngine/Rendering/Adapter/SceneRenderCollector.h"

namespace ReplayEngine::Editor
{
    struct LandscapeEditorValidation
    {
        static int Run()
        {
            using namespace ReplayEngine;
            using namespace Rendering;
            using namespace Rendering::DX12;
            std::filesystem::create_directories("Saved/Validation/LandscapeRegression");
            std::ofstream log("Saved/Validation/LandscapeRegression/editor_profile.csv");
            log << "scenario,scope,calls,cpu_ms,frames,scene_captures\n";
            const auto instance = GetModuleHandleW(nullptr);
            WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = instance; wc.lpszClassName = L"ReplayLandscapeEditorValidation";
            RegisterClassW(&wc);
            RECT rect{0,0,1280,720}; AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
            HWND window = CreateWindowW(wc.lpszClassName,L"Landscape Editor validation",
                WS_OVERLAPPEDWINDOW,0,0,rect.right-rect.left,rect.bottom-rect.top,
                nullptr,nullptr,instance,nullptr);
            if (!window) return 80;
            bool passed = true;
            {
                auto application = std::make_unique<framework>(window);
                auto& app = *application;
                std::string error;
                if (!app.asset_database.Load(error)) { std::fprintf(stderr,"%s\n",error.c_str()); return 81; }
                Core::RegisterBuiltInComponents();
                app.object_editor_context.AttachScene(&app.object_scene);
                app.object_editor_context.SetAssetDatabase(&app.asset_database);
                app.editor_mode = true;
                app.edit_mode_active = true;
                app.active_editor_view = framework::editor_view::scene;
                app.selected_editor_object = framework::editor_selection::game_object;
                app.client_width = 1280; app.client_height = 720;
                auto* model = app.object_scene.CreateGameObject("970 bone / 28 slot model");
                auto* skinned = model->AddComponent<Components::SkinnedMeshRendererComponent>();
                skinned->mesh_asset = "45f0b5f4cb85c9288566693cf4916e0d";
                auto* ground = app.object_scene.CreateGameObject("Landscape");
                auto* landscape = ground->AddComponent<Components::LandscapeComponent>();
                landscape->Data().Initialize(225,225,0.25f);
                ground->AddComponent<Components::LandscapeRendererComponent>();
                auto* loaded = app.resolve_object_mesh(skinned->mesh_asset);
                if (!loaded || loaded->meshes.empty()) { std::fprintf(stderr,"Heavy model unavailable\n"); return 82; }
                std::fprintf(stderr,"Loaded model: meshes=%zu slots=%zu\n",loaded->meshes.size(),loaded->MaterialSubsetNames().size());
                if (!app.dx12_device_context.Initialize(window,1280,720,false,false,false)) return 83;
                app.dx12_framework_active = true;
                ImGui::CreateContext();
                auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.LogFilename = nullptr;
                io.DisplaySize = {1280,720}; io.DeltaTime = 1.0f/60;
                unsigned char* pixels; int font_width, font_height;
                io.Fonts->GetTexDataAsRGBA32(&pixels,&font_width,&font_height);
                Stats().Initialize(); Stats().SetEnabled(true); Stats().SetHistoryLimit(100);
                app.editor_camera.LookAt({28,22,10},{28,0,28});
                POINT origin{0,0}; ClientToScreen(window,&origin);
                app.scene_view_min_x = static_cast<float>(origin.x);
                app.scene_view_min_y = static_cast<float>(origin.y);
                app.scene_view_max_x = app.scene_view_min_x+1280;
                app.scene_view_max_y = app.scene_view_min_y+720;
                app.scene_view_hovered = true;
                const char* scopes[] = {"Scene/Capture","Editor/Inspector","Landscape/Viewport",
                    "Landscape/Preview","Landscape/Brush","Landscape/ChunkUpload","Item/RigPose","Item/BonePalette"};
                const char* scenarios[] = {"unselected_closed","selected_closed","selected_open",
                    "multi_open","rig_visible","sculpt","preview_idle"};
                for (int scenario=0; scenario<7; ++scenario)
                {
                    app.object_editor_context.Selection().Clear();
                    if (scenario!=0) app.object_editor_context.Selection().Select(scenario>=5?ground->ID():model->ID());
                    if (scenario==3) app.object_editor_context.Selection().Select(ground->ID(),true);
                    app.show_motion_rig_panel = scenario==4;
                    app.motion_rig_panel_visible = scenario==4;
                    app.active_editor_workspace = scenario==4 ? framework::editor_workspace::motion : framework::editor_workspace::general;
                    app.landscape_edit_enabled = scenario>=5;
                    app.landscape_edit_mode = 0;
                    app.landscape_brush.radius = 2;
                    std::array<double,8> times{}; std::array<unsigned long long,8> calls{};
                    std::uint64_t captures = 0;
                    int draws = 0;
                    for (int frame=0; frame<40; ++frame)
                    {
                        Stats().SetPaused(frame<10); Stats().BeginFrame();
                        if (frame==10) captures = Scene::Serialization::SceneCaptureCount();
                        for (bool& down:io.MouseDown) down = false;
                        for (bool& down:io.KeysDown) down = false;
                        io.MousePos = {app.scene_view_min_x+640,app.scene_view_min_y+360};
                        io.MouseDown[scenario==5?0:1] = scenario!=6 && frame>0 && frame<38;
                        io.KeysDown['W'] = scenario<5;
                        ImGui::NewFrame();
                        if (scenario<5) app.editor_camera.Look(0.1f,0);
                        if (scenario>=2 && scenario<5)
                        {
                            ImGui::SetNextWindowPos({900,0}); ImGui::SetNextWindowSize({380,720});
                            app.draw_inspector();
                        }
                        app.handle_landscape_viewport_edit();
                        RenderItemList items; SceneRenderCollector::Collect(app.object_scene,items);
                        D3D12StaticSceneSubmission submission;
                        passed = app.build_dx12_static_scene(submission,app.object_scene,items,1.0f/60) && passed;
                        draws += static_cast<int>(submission.skinned_draws.size());
                        passed = app.dx12_device_context.PreloadScene3DResources(submission,true) && passed;
                        ImGui::Render(); Stats().EndFrame();
                        if (frame>=10 && !Stats().History().empty())
                            for (const auto& scope : Stats().History().back().scopes)
                                for (int index=0;index<8;++index)
                                    if (scope.name == scopes[index]) {times[index]+=scope.cpu_ms;calls[index]+=scope.calls;}
                    }
                    captures = Scene::Serialization::SceneCaptureCount()-captures;
                    for (int index=0;index<8;++index)
                        log<<scenarios[scenario]<<','<<scopes[index]<<','<<calls[index]<<','<<times[index]<<",30,"<<captures<<'\n';
                    passed = captures==0 && draws>0 && passed;
                    if (scenario==2) passed = calls[1]>0 && calls[6]==0 && passed;
                    if (scenario==4) passed = calls[6]>0 && passed;
                    if (scenario==5) passed = calls[2]>0 && calls[3]>0 && calls[4]>0 && calls[5]>0 && passed;
                    std::fprintf(stderr,"Editor profile: %s captures=%llu skinned_draws=%d\n",scenarios[scenario],static_cast<unsigned long long>(captures),draws);
                }
                app.reset_landscape_editor_state(true);
                app.dx12_device_context.Shutdown();
                ImGui::DestroyContext(); Stats().Release();
            }
            DestroyWindow(window); UnregisterClassW(wc.lpszClassName,instance);
            std::fprintf(stderr,"Editor production paths: %s\n",passed?"PASS":"FAIL");
            return passed?0:84;
        }
    };
}
