// Window 処理、初期化/更新/描画宣言、Editor/制作機能宣言。
// framework_class.h の class framework 内部からのみ include する。

#ifdef USE_IMGUI
        // IME 入力は ImGui へ先に渡す。将来 UIInputField を足すときは
        // WM_IME_* をここから横取りせず、Editor / Runtime の入力所有者で分岐する。
        const bool keyboard_message = msg == WM_KEYDOWN || msg == WM_KEYUP ||
            msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP || msg == WM_CHAR;
        if (ImGui::GetCurrentContext() &&
            (editor_mode || (standalone_game_mode && show_render_stats)))
        {
            ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
        }
        if (editor_mode)
        {
            const bool control_down = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool runtime_ui_text_owner = scene_view_hovered &&
                !ImGui::GetIO().WantTextInput &&
                ReplayEngine::UI::UIInputFieldSystem::HasFocusedInput(
                    active_object_scene());
            const bool shortcut_pressed = msg == WM_KEYDOWN && control_down &&
                (lparam & 0x40000000) == 0 && !ImGui::GetIO().WantTextInput &&
                !runtime_ui_text_owner;

            // Focused Tool > Runtime UI Text > Scene > Global の順で shortcut を所有する。
            const bool focused_tool_owns_shortcut =
                shader_composer_editor.OwnsKeyboardShortcut() && msg == WM_KEYDOWN &&
                !ImGui::GetIO().WantTextInput && !runtime_ui_text_owner &&
                (wparam == VK_DELETE || (control_down && wparam == 'S'));
            if (focused_tool_owns_shortcut) return 0;

            if (shortcut_pressed && wparam == 'S')
            {
                const bool choose_path = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (active_editor_workspace == editor_workspace::motion &&
                    !choose_path && motion_editor_loaded)
                {
                    save_current_motion_asset();
                    return 0;
                }
                // 標準保存はGameObject Sceneだけを対象にし、正本を一つに保つ。
                save_object_scene(choose_path);
                return 0;
            }
            if (msg == WM_KEYDOWN && control_down && !ImGui::GetIO().WantTextInput &&
                !runtime_ui_text_owner && (wparam == 'Z' || wparam == 'Y'))
            {
                if (active_editor_workspace == editor_workspace::motion)
                {
                    if (wparam == 'Z') undo_motion_edit();
                    else redo_motion_edit();
                }
                else
                {
                    const bool external_context =
                        selected_editor_object == editor_selection::asset ||
                        selected_editor_object == editor_selection::world;
                    bool handled = false;
                    if (external_context)
                    {
                        handled = (wparam == 'Z')
                            ? undo_external_file_edit()
                            : redo_external_file_edit();
                    }
                    if (!handled)
                    {
                        if (wparam == 'Z') object_editor_context.Undo();
                        else object_editor_context.Redo();
                    }
                }
                return 0;
            }
            if (shortcut_pressed && wparam == 'D')
            {
                object_hierarchy_panel.DuplicateSelection(object_editor_context);
                return 0;
            }
            if (shortcut_pressed && (wparam == 'C' || wparam == 'V'))
            {
                std::string clipboard_error;
                if (wparam == 'C')
                {
                    std::string clipboard_text;
                    if (object_hierarchy_panel.CopySelection(object_editor_context,
                        clipboard_text, clipboard_error))
                    {
                        ImGui::SetClipboardText(clipboard_text.c_str());
                        object_editor_context.SetStatus("GameObject をコピーしました");
                    }
                    else object_editor_context.SetStatus(clipboard_error);
                }
                else
                {
                    const char* clipboard_text = ImGui::GetClipboardText();
                    if (!object_hierarchy_panel.PasteSelection(object_editor_context,
                        clipboard_text != nullptr ? clipboard_text : "", clipboard_error))
                    {
                        object_editor_context.SetStatus("貼り付けできません: " + clipboard_error);
                    }
                }
                return 0;
            }
            if (msg == WM_KEYDOWN && wparam == VK_DELETE &&
                !ImGui::GetIO().WantTextInput && !runtime_ui_text_owner)
            {
                if (selected_editor_object == editor_selection::asset &&
                    !project_selected_entry_path.empty())
                {
                    project_request_delete(project_selected_entry_path);
                    return 0;
                }
                if (selected_editor_object == editor_selection::game_object)
                {
                    object_hierarchy_panel.DestroySelection(object_editor_context);
                    return 0;
                }
            }
            if (msg == WM_KEYDOWN && wparam == 'F' && !runtime_ui_text_owner &&
                (GetKeyState(VK_CONTROL) & 0x8000))
            {
                focus_search_requested = true;
                set_edit_mode(true);
                return 0;
            }
            if (search_input_active && keyboard_message)
            {
                return 0;
            }
            // Scene Camera が W/A/S/D + Q/E を使うため、Transform Tool は
            // Maya の W/E/R を Shift 付きへ退避する。選択中の GameObject にだけ効く。
            const bool shift_down = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool alt_down = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (msg == WM_KEYDOWN && edit_mode_active && !search_input_active &&
                shift_down && !control_down && !alt_down &&
                selected_editor_object == editor_selection::game_object &&
                object_editor_context.Selection().Primary().Valid())
            {
                if (wparam == 'W')
                {
                    transform_gizmo.SetOperation(ReplayEngine::Editor::GizmoOperation::Translate);
                    return 0;
                }
                if (wparam == 'E')
                {
                    transform_gizmo.SetOperation(ReplayEngine::Editor::GizmoOperation::Rotate);
                    return 0;
                }
                if (wparam == 'R')
                {
                    transform_gizmo.SetOperation(ReplayEngine::Editor::GizmoOperation::Scale);
                    return 0;
                }
            }
        }
        if (!standalone_game_mode && msg == WM_KEYDOWN && wparam == VK_F1)
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
        // F5 は開始専用、Shift+F5 は停止専用。UI 表示と実装を一致させる。
        if (msg == WM_KEYDOWN && wparam == VK_F5 && editor_mode)
        {
            const bool shift_down = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (shift_down)
            {
                if (object_scene_play_mode) exit_object_play_mode();
                else object_editor_context.SetStatus("停止する Play Session はありません");
            }
            else if (!object_scene_play_mode) enter_object_play_mode();
            return 0;
        }
#endif
        // Runtime UI の文字入力は ImGui の text owner 判定が終わった後だけ受ける。
        // Editor では Scene View が入力対象のときだけ流し、Inspector 等の編集を奪わない。
        {
            bool runtime_ui_text_allowed = !profile_benchmark_mode &&
                (standalone_game_mode || object_scene_play_mode);
#ifdef USE_IMGUI
            if (editor_mode)
            {
                const bool imgui_text = ImGui::GetCurrentContext() != nullptr &&
                    ImGui::GetIO().WantTextInput;
                runtime_ui_text_allowed = scene_view_hovered && !imgui_text;
            }
            else if (standalone_game_mode && show_render_stats &&
                ImGui::GetCurrentContext() != nullptr &&
                ImGui::GetIO().WantTextInput)
            {
                runtime_ui_text_allowed = false;
            }
#endif
            const bool text_message = msg == WM_CHAR || msg == WM_KEYDOWN ||
                msg == WM_IME_STARTCOMPOSITION || msg == WM_IME_COMPOSITION ||
                msg == WM_IME_ENDCOMPOSITION;
            if (runtime_ui_text_allowed && text_message &&
                ReplayEngine::UI::UIInputFieldSystem::HandleWindowMessage(
                    active_object_scene(), hwnd, msg, wparam, lparam))
            {
                return 0;
            }
        }
        if ((msg == WM_SYSKEYDOWN && wparam == VK_RETURN && (lparam & (1LL << 29))) ||
            (msg == WM_KEYDOWN && wparam == VK_F11))
        {
            toggle_fullscreen();
            return 0;
        }
        if (msg == WM_KEYDOWN && wparam == VK_F2)
        {
#ifdef USE_IMGUI
            const bool control_down = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (editor_mode && !control_down)
            {
                if (selected_editor_object == editor_selection::asset &&
                    !project_selected_entry_path.empty())
                    project_begin_rename_selected();
                else
                    object_hierarchy_panel.BeginRenameSelection(object_editor_context);
                return 0;
            }
#endif
            // Render Output は Ctrl+F2。Hierarchy 標準の F2 Rename と競合させない。
            render_graph.CycleOutput();
            if (render_graph.RequiresDeferred()) enable_deferred = true;
            return 0;
        }
        switch (msg)
        {
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps); break; }
        case WM_CLOSE:
#ifdef USE_IMGUI
            if (!confirm_object_scene_close()) return 0;
#endif
            log_shutdown_reason("WM_CLOSE");
            return DefWindowProc(hwnd, msg, wparam, lparam);
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
        case WM_MOUSEWHEEL:
            ui_mouse_wheel_delta += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) /
                static_cast<float>(WHEEL_DELTA);
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
    struct object_ui_viewport
    {
        float left{ 0.0f };
        float top{ 0.0f };
        float width{ 1.0f };
        float height{ 1.0f };
        float logical_width{ 1.0f };
        float logical_height{ 1.0f };
    };

    bool initialize();
    void update(float elapsed_time);
    void render(float elapsed_time);
    bool uninitialize();
    // エディタのデバッグ用メッシュ (static_meshes[0]) を置くワールド行列。
    // かつてはここで旧 Player の Transform を返していたが、その分岐は撤去した。
    void store_debug_mesh_world(DirectX::XMFLOAT4X4& world) const;

    // SSAO/SSR/TAAが共有するフレーム定数を作ってb9へ載せる。
    void update_frame_constants(const DirectX::XMMATRIX& view,
        const DirectX::XMMATRIX& projection, float elapsed_time,
        bool advance_effect_time = true);
    ID3D11PixelShader* skinned_forward_shader(int shading) const;
    ID3D11PixelShader* static_forward_shader(int shading) const;
    ReplayEngine::Rendering::ShaderLightingModel deferred_lighting_model(
        int shading) const;
    void bind_gbuffer_material(
        ReplayEngine::Rendering::ShaderLightingModel lighting_model,
        bool stage_surface = false, bool pixelate_enabled = false,
        float pixelate_size = 6.0f, float pixelate_strength = 1.0f,
        float metallic = 0.0f, float roughness = 0.55f,
        float ambient_occlusion = 1.0f, float emissive_strength = 0.0f,
        const DirectX::XMFLOAT4& base_color_factor = DirectX::XMFLOAT4{ 1,1,1,1 },
        const DirectX::XMFLOAT3& emissive_color = DirectX::XMFLOAT3{ 0,0,0 },
        std::uint32_t texture_mask = 0);
    void apply_toon_preset(int preset);
    void reset_editor_values();
    void draw_editor();
    void draw_editor_main_menu();
    void draw_editor_toolbar();
    void open_export_game_dialog();
    void draw_export_game_dialog();
    bool export_standalone_game(const std::filesystem::path& export_root,
        const std::filesystem::path& startup_scene, bool overwrite_existing);
    void draw_scene_view_panel();

    // --- 制作便利機能: Scene Memo / Play From Here / Scene Flow -------------
    void draw_scene_note_overlay();
    void draw_scene_notes_panel();
    ReplayEngine::Core::GameObject* create_scene_note_at(
        const DirectX::XMFLOAT3& world_position, const std::string& text = "ここを修正");
    bool scene_view_mouse_world_point(DirectX::XMFLOAT3& out_position,
        DirectX::XMFLOAT3* out_normal = nullptr) const;
    void request_play_from_here(const DirectX::XMFLOAT3& position,
        bool camera_direction, const char* label);
    void apply_play_spawn_override(ReplayEngine::Scene::Serialization::SceneData& snapshot);
    void draw_play_from_here_context_menu();

    bool load_scene_flow_editor(const ReplayEngine::Assets::AssetRecord& record);
    bool save_scene_flow_editor();
    void draw_scene_flow_panel();
    void sync_runtime_scene_flow_asset();

    void draw_runtime_mode_banner();

    // 操作対象 GameObject の実行時診断。旧 Player の項目は持たない。
    void draw_controlled_character_diagnostics();
    void draw_search_results();
    void draw_scene_hierarchy();
    void draw_inspector();
    void draw_shader_adjustment_workspace();
    // シェーダ編集の唯一の入口（Source/app/Editor/framework_shader_stack.cpp）。
    //
    // 絵柄・レイヤ・キャラ材質・プリセットをここ 1 箇所で編集する。
    // 引数はすべて参照なので、呼び出し側が選択中の GameObject の
    // 値を渡せば、そのオブジェクトだけが変わる。
    // オブジェクトごとに違うシェーダを掛けられる状態を保つこと。
    void draw_shader_inspector(const char* id, const std::string& label,
        int& base_shader, bool& outline_pass,
        ReplayEngine::Rendering::ShaderLayerStack& layers,
        ReplayEngine::Rendering::CharacterMaterialProfile& profile,
        float& pixel_grid, float& pixelate_strength);
    void draw_screen_effect_stack();
    // SSAO / SSR / TAA / CSM の有効化と調整項目。
    void draw_screen_space_settings();
    // ポリゴン数・ドローコール数などの描画統計オーバーレイ。
    void draw_render_stats_overlay();
    // draw_character_material_controls は draw_shader_inspector へ統合された。
    bool browse_model_asset();
    bool load_model_asset_async(const std::wstring& filename);
    bool load_model_asset_now(const std::wstring& filename);
    // ワーカースレッドから呼べるモデル先読み。キャッシュへ載せるだけで
    // frameworkの表示状態は触らないため、並列ロード中に安全に使える。
    bool prewarm_model_asset(const std::filesystem::path& path);
    bool place_asset_in_object_scene(const ReplayEngine::Assets::AssetRecord& asset,
        bool add_mesh_collider, const DirectX::XMFLOAT3* drop_world_position = nullptr,
        ReplayEngine::Core::ObjectID drop_target = ReplayEngine::Core::ObjectID::Invalid());
    void handle_viewport_selection();
    // 選択中の Landscape GameObject にだけ有効な Scene View 編集。
    // Runtime Component へ ImGui 依存を持ち込まず、Editor Tool が data を編集する。
    bool handle_landscape_viewport_edit();
    void draw_landscape_editor_toolbar();
    void reset_landscape_editor_state(bool rollback_stroke = true);
    void draw_scene_grid_overlay();
    bool draw_object_transform_gizmo();
    void save_editor_session();
    void restore_editor_session();

    // 選択中の GameObject を Prefab として保存する。
    // choose_path が true ならファイル名を選ばせる（任意名で保存できる）。
    // 保存した Prefab は AssetDatabase へ登録され、AssetGUID で参照できるようになる。
    void save_selected_prefab(bool choose_path);
    void load_prefab();
