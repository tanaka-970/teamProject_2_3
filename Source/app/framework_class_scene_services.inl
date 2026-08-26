// Scene/Runtime/描画提出/カメラ/衝突/Project Browser 接続。
// framework_class.h の class framework 内部からのみ include する。

    // --- GameObject / Component 基盤との接続 -------------------------------
    // 実装はすべて Source/app/Runtime/framework_gameobject_scene.cpp にある。
    void initialize_object_scene();
    void update_object_scene(float elapsed_time);
    object_ui_viewport object_ui_viewport_target() const noexcept;
    bool save_object_scene(bool choose_path);
    bool load_object_scene(bool choose_path);
    bool load_object_scene_from_path(const std::filesystem::path& path);
    bool autosave_object_scene();
    void check_object_scene_recovery();
    void draw_object_scene_recovery_prompt();
    void request_object_scene_action(object_scene_action action,
        std::filesystem::path path = {});
    void execute_pending_object_scene_action();
    void draw_unsaved_object_scene_prompt();
    bool confirm_object_scene_close();
    void add_recent_object_scene(const std::filesystem::path& path);
    void register_object_scene_asset();
    void discard_object_scene_autosave();
    void enter_object_play_mode();
    void exit_object_play_mode();
    ReplayEngine::Scene::Scene& active_object_scene() noexcept;
    const ReplayEngine::Scene::Scene& active_object_scene() const noexcept;

    // --- Runtime World の結線 (framework_gameobject_scene.cpp) --------------
    //
    // World の実体が入れ替わったら、それに紐づくものを全部張り直す。
    // 呼び出しは毎フレームの安全点 1 か所だけ。
    void initialize_runtime_services();
    void tick_runtime_scene_flow();
    void rebind_runtime_world_if_changed();

    // Startup Scene からの起動。空・無効・失敗はすべて診断状態にする。
    void begin_startup_scene();
    void set_runtime_blocked(const std::string& reason);
    void clear_runtime_blocked() noexcept;
    bool runtime_blocked() const noexcept { return object_runtime_blocked; }

    // Editor の Runtime 診断パネル。読み取り専用。
    void draw_runtime_diagnostics_panel();
    gltf_model* resolve_object_gltf(const std::string& asset_guid);
    // Model Asset の実ファイルを引く共通処理。形式ごとの分岐をここへ集約する。
    enum class model_source_format { unsupported, fbx_cereal, gltf };
    model_source_format resolve_model_source(
        const std::string& asset_guid,
        std::filesystem::path& out_source,
        std::string& out_reason) const;
    skinned_mesh* resolve_object_mesh(const std::string& asset_guid);
    static_mesh* resolve_builtin_primitive_mesh(const std::string& builtin_id);
    bool build_builtin_primitive_cpu(const std::string& builtin_id,
        std::vector<static_mesh::vertex>& vertices,
        std::vector<std::uint32_t>& indices) const;
    const ReplayEngine::Rendering::MaterialAsset* resolve_object_material(
        const std::string& asset_guid);
    ReplayEngine::Rendering::RenderItem resolve_render_item_material(
        const ReplayEngine::Rendering::RenderItem& item);
        // DX12 Phase 2 Bridge。Engine 所有の RenderItem List を Cache 可能な Static
        // Geometry/Material Submission へ変換する。実際の Skinned Animation は Phase 3 に残す。
    bool build_dx12_static_scene(
        ReplayEngine::Rendering::DX12::D3D12StaticSceneSubmission& submission,
        float elapsed_time);
    // Canvas/RectTransform の解決結果を、GPU APIを呼ばないDX12 UIコマンドへ変換する。
    bool build_dx12_ui(
        ReplayEngine::Rendering::DX12::D3D12UIFrame& frame);
    bool build_dx12_scene_effects(
        ReplayEngine::Rendering::DX12::D3D12SceneEffectSubmission& submission);
    bool build_dx12_ui_for_scene(
        ReplayEngine::Rendering::DX12::D3D12UIFrame& frame,
        ReplayEngine::Scene::Scene& scene,
        std::uint32_t target_width, std::uint32_t target_height,
        const object_ui_viewport& viewport);
    void clear_object_mesh_cache() noexcept;
    void clear_object_material_cache() noexcept;
    bool object_runtime_active() const noexcept;
    void update_object_fixed_step(float elapsed_time);
    void update_object_camera_follow(float elapsed_time);
    void refresh_object_scene_services();
    const ReplayEngine::Motion::MotionAsset* resolve_motion_asset(
        const std::string& asset_guid);
    const ReplayEngine::Motion::CompositionAsset* resolve_composition_asset(
        const std::string& asset_guid);
    void prepare_material_motion_bindings(ReplayEngine::Scene::Scene& scene);
    void prepare_ui_effect_shader_schemas(ReplayEngine::Scene::Scene& scene);
    void evaluate_motion_players(ReplayEngine::Scene::Scene& scene,
        float scaled_delta_time, float unscaled_delta_time);
    void update_ui_sprite_animators(ReplayEngine::Scene::Scene& scene, float elapsed_time);
    void update_ui_number_displays(ReplayEngine::Scene::Scene& scene);
    void sync_object_lights();

    // --- 新規 Scene 作成 ---------------------------------------------------
    //
    // Empty  … GameObject を 1 つも作らない。操作対象は未設定。
    // Default … 編集可能なLandscape Ground + Sunを作り、設定済みなら
    //           Default Controlled Character Prefabも配置する。
    //
    // どちらも「ユーザーが新規作成を選んだとき」しか呼ばれない。
    // 起動時や Scene 読み込み時に Prefab を配置することは決してない。
    bool create_object_scene(const std::string& name, bool place_default_character);
    ReplayEngine::Core::GameObject* create_default_landscape_ground(
        ReplayEngine::Scene::Scene& scene);

    // --- プロジェクト設定 --------------------------------------------------
    void load_project_settings();
    bool save_project_settings();
    bool undo_external_file_edit();
    bool redo_external_file_edit();
    void reload_external_file_edit_target(const std::filesystem::path& path);
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
    void draw_editor_camera_preset_manager();
    void draw_ui_focus_style_manager();
    void draw_editor_camera_top_menu();

    // Camera preset lifecycle。active 選択だけは local Saved へ保存する。
    void ensure_editor_camera_presets_loaded();
    ReplayEngine::Editor::EditorCameraPreset& active_editor_camera_preset();
    const ReplayEngine::Editor::EditorCameraPreset& active_editor_camera_preset() const;
    bool switch_editor_camera_preset(const std::string& preset_id);
    bool save_active_editor_camera_preset();
    bool make_active_editor_camera_preset_personal_copy();

    // 編集カメラ状態の保存・復元。Scene ファイルには一切書き込まない。
    void load_editor_camera_state();
    void save_editor_camera_state();
    std::string make_editor_camera_state_key() const;

    // Scene とは独立した Editor 全体の移動速度設定。
    void load_editor_camera_move_speed_preference();
    bool save_editor_camera_move_speed_preference();

    // Default Controlled Character Prefab の現在の解決結果。
    ReplayEngine::Project::PrefabReferenceStatus resolve_default_character_prefab() const;

    bool refresh_csharp_scripts();

    // Catalog 更新と Assembly 再コンパイルを 1 回でやる。
    bool rebuild_all_csharp();

    // 編集 Scene の ScriptComponent へ Schema を引き直させる。
    // Catalog 更新は Play セッションの Component にしか届かないため、
    // 編集側はここで明示的に引き直す。戻り値は解決できた件数。
    std::size_t resolve_editor_script_schemas();
    bool build_and_reload_csharp_scripts();
    bool create_csharp_behaviour_asset();
    bool open_selected_csharp_asset(int line = 0);
    void snapshot_csharp_script_write_times();
    void poll_csharp_script_changes(float elapsed_time);
    void push_editor_log(std::string severity, std::string message,
        std::filesystem::path file = {}, int line = 0, int column = 0);

    // Play 直後に実行用 World の Script Component を数える診断用。
    void count_runtime_script_instances(ReplayEngine::Core::GameObject& object,
        std::size_t& total, std::size_t& with_instance);

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
        skinned_mesh& mesh, int clip_index, float animation_time, bool loop) const;
    const skinned_mesh::animation::keyframe* resolve_render_item_keyframe(
        skinned_mesh& mesh, const ReplayEngine::Rendering::RenderItem& item,
        skinned_mesh::animation::keyframe& blended_keyframe) const;
    void draw_project_panel();

    // --- Project ブラウザ (Unity 型 2 ペイン) ------------------------------
    // 左にフォルダツリー、右にそのフォルダの中身。
    // 実装は Source/app/Editor/framework_project_browser.cpp。
    void draw_project_browser();
    void draw_project_folder_tree(const std::filesystem::path& folder, int depth);
    void draw_project_folder_contents();
    void draw_project_create_submenu(const std::filesystem::path& target_folder);
    void draw_project_entry_context_items(const std::filesystem::path& path);
    bool project_open_entry(const std::filesystem::path& path);
    void set_project_folder(const std::filesystem::path& folder);
    bool project_create_folder(const std::string& name);
    bool project_create_csharp_behaviour(const std::string& class_name);
    bool project_create_material(const std::string& name);
    bool project_create_motion(const std::string& name);
    bool project_create_composition(const std::string& name);
    bool project_create_sprite_atlas(const std::string& name);
    bool open_sprite_atlas_asset(const ReplayEngine::Assets::AssetRecord& asset);
    bool save_current_sprite_atlas();
    void draw_sprite_atlas_editor();
    void begin_sprite_atlas_edit(const std::string& label);
    void commit_sprite_atlas_edit();
    void cancel_sprite_atlas_edit();
    bool undo_sprite_atlas_edit();
    bool redo_sprite_atlas_edit();
    bool project_create_scene_flow(const std::string& name);
    bool project_create_localization(const std::string& name);
    bool project_create_effect_preset(const std::string& name);
    bool project_create_input_action_asset(const std::string& name);
    bool load_active_input_action_asset();
    bool project_create_surface_shader(const std::string& name);
    bool project_create_layer_shader(const std::string& name);
    bool project_create_shader_composer(const std::string& name,
        ReplayEngine::Rendering::ShaderDomain domain);
    bool project_rename_entry(const std::filesystem::path& path,
        const std::string& new_name);
    bool project_move_entry(const std::filesystem::path& path,
        const std::filesystem::path& destination_folder);
    bool project_duplicate_entry(const std::filesystem::path& path);
    void project_show_in_explorer(const std::filesystem::path& path);
    void project_copy_path(const std::filesystem::path& path, bool absolute);
    void project_record_created_path(const std::filesystem::path& path,
        const std::string& label);
    void project_apply_external_history_change();
    void project_notify_path_relocated(const std::filesystem::path& from,
        const std::filesystem::path& to);
    void project_request_delete(const std::filesystem::path& path);
    void draw_project_delete_popup();
    bool project_delete_confirmed();
    void project_begin_rename_selected();
    ReplayEngine::Assets::AssetKind project_kind_for(
        const std::filesystem::path& path) const;

    void draw_console_panel();
    void execute_editor_command(const std::string& command);
    void draw_workspace_panel();
    void draw_ui_hierarchy();
    void draw_ui_preview();
    void draw_ui_inspector();
    void draw_ui_scene_overlay();
    void ui_preview_resolution_size(int& width, int& height) const noexcept;
    bool open_motion_asset(const ReplayEngine::Assets::AssetRecord& asset);
    bool save_current_motion_asset();
    // S キーと Key追加 ボタンの共通経路。現在のプレビュー時刻へキーを打つ。
    bool add_motion_key_at_preview_time();
    bool undo_motion_edit();
    bool redo_motion_edit();
    void draw_motion_layers();
    void draw_motion_preview();
    void draw_motion_inspector();
    void draw_motion_timeline();
    void draw_motion_graph_editor();
    void stop_motion_preview();
    void capture_motion_preview_targets();
    void apply_motion_preview_time();
    void set_editor_workspace(editor_workspace workspace);
    void remember_active_editor_view();
    void apply_remembered_editor_view(editor_workspace workspace);
    void configure_editor_style();
    void set_edit_mode(bool enabled);
    bool resize_back_buffers(UINT width, UINT height);
    void apply_pending_resize();
    bool toggle_fullscreen();
    bool is_fullscreen() const;

private:
    high_resolution_timer tictoc;
    // 終了時リソース解放の検査を要求されているか。
    bool shutdown_regression_requested{ false };

    std::uint32_t automated_smoke_test_frames{ 0 };
    std::uint32_t automated_smoke_test_frames_rendered{ 0 };
    bool automated_frame_capture_pending{ false };

    bool profile_benchmark_mode{ false };
    std::uint32_t profile_benchmark_frames{ 300 };
    std::uint32_t profile_benchmark_warmup_frames{ 30 };
    std::uint32_t profile_benchmark_frame_index{ 0 };
    std::uint32_t profile_benchmark_drain_frames{ 0 };
    std::string profile_benchmark_output_name{ "benchmark" };
    bool profile_benchmark_export_attempted{ false };
    bool profile_benchmark_export_ok{ false };
    bool profile_benchmark_gpu_drain_timeout{ false };
    bool profile_benchmark_scene_ready{ false };
    bool profile_benchmark_startup_failed{ false };
    bool profile_benchmark_startup_profile_ok{ false };
    std::array<double, 5> profile_benchmark_initialize_stage_ms{};
    double profile_benchmark_initialize_total_ms{ 0.0 };
    std::uint64_t profile_benchmark_max_draw_calls{ 0 };
    std::uint64_t profile_benchmark_max_objects{ 0 };
    std::uint64_t profile_benchmark_max_components{ 0 };
    std::chrono::steady_clock::time_point profile_benchmark_startup_begin{};
    float shader_composer_time{ 0.0f }; // elapsed_time が 0 の Golden Capture 中は進めない
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
    std::filesystem::path content_root_path_;
    std::filesystem::path saved_root_path_;
    UINT client_width{ SCREEN_WIDTH };
    UINT client_height{ SCREEN_HEIGHT };
    UINT pending_client_width{ SCREEN_WIDTH };
    UINT pending_client_height{ SCREEN_HEIGHT };
    bool resize_pending{ false };
    bool borderless_fullscreen{ false };
    bool startup_fullscreen_requested{ false };
    DWORD windowed_style{ WS_OVERLAPPEDWINDOW | WS_VISIBLE };
    DWORD windowed_ex_style{ 0 };
    RECT windowed_rect{ 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };

    // 旧 Player 用の項目 (player) は撤去した。
    // 操作対象は Scene 内の通常 GameObject なので game_object で選択される。
