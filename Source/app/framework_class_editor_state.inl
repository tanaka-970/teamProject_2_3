// Editor 選択・Dock/Workspace・Motion/UI/Landscape/Shader/Project 状態。
// framework_class.h の class framework 内部からのみ include する。

    enum class editor_selection
    {
        world,
        camera,
        // Scene 内の GameObject / Component 基盤で選択された GameObject。
        game_object,
        directional_light,
        point_lights,
        rendering,
        post_process,
        // Project Browser の file/folder。末尾追加で既存 enum 値を変えない。
        asset
    };
    enum class editor_workspace
    {
        general,
        placement,
        modeling,
        animation,
        rendering,
        shader_adjustment,
        ui,
        motion
    };
    enum class editor_view
    {
        scene,
        game
    };
    // DockBuilder の構成を変えたら上げる。
    // imgui.ini に残った古い split を再利用させず、次回起動時に再構築するための版。
    static constexpr int editor_layout_version{ 4 };
    editor_selection selected_editor_object{ editor_selection::world };
    editor_workspace active_editor_workspace{ editor_workspace::general };
    editor_view active_editor_view{ editor_view::scene };
    // Scene/Game タブは Workspace ごとに覚える。
    // UI は完成画面をすぐ見たいので初回だけ Game、Motion は 3D 編集用なので Scene。
    std::array<editor_view, 8> editor_view_by_workspace{
        editor_view::scene,
        editor_view::scene,
        editor_view::scene,
        editor_view::scene,
        editor_view::scene,
        editor_view::scene,
        editor_view::scene,
        editor_view::scene
    };
    bool editor_view_tab_sync_pending{ false };
    int editor_layout_saved_version{ 0 };
    bool editor_layout_checked{ false };
    bool editor_layout_dirty{ true };
    bool editor_hide_requested{ false };
    bool editor_session_active{ false };
    bool show_hierarchy_panel{ true };
    bool show_inspector_panel{ true };
    bool show_project_panel{ true };
    bool show_console_panel{ true };
    bool show_workspace_panel{ true };
    bool show_validation_panel{ true };
    bool show_dx12_debug_panel{ false };
    bool show_scene_view{ true };
    bool show_scene_notes_panel{ false };
    bool show_scene_flow_panel{ false };
    bool show_ui_hierarchy_panel{ true };
    bool show_ui_preview_panel{ false };
    // Canvas Preview は ImGui の近似描画ではなく Runtime UIRenderer の出力を表示する。
    int ui_preview_runtime_width{ 0 };
    int ui_preview_runtime_height{ 0 };
    bool ui_preview_runtime_requested{ false };
    // Scene背景とは別に、透明なCanvas Previewの下地色を選べるようにする。
    DirectX::XMFLOAT4 ui_preview_background_color{
        36.0f / 255.0f, 38.0f / 255.0f, 42.0f / 255.0f, 1.0f };
    bool show_ui_inspector_panel{ true };
    bool show_motion_layers_panel{ true };
    bool show_motion_preview_panel{ true };
    bool show_motion_inspector_panel{ true };
    bool show_motion_timeline_panel{ true };
    bool show_motion_graph_panel{ true };
    bool show_motion_rig_panel{ true };
    bool show_sprite_atlas_editor_panel{ false };
    bool show_easing_editor_panel{ false };
    ReplayEngine::Motion::EasingCurveAsset easing_editor_asset;
    std::filesystem::path easing_editor_path;
    std::string easing_editor_guid;
    std::string easing_editor_status{ u8"イージングカーブが未選択です" };
    bool easing_editor_loaded{ false };
    bool easing_editor_dirty{ false };
    char easing_editor_name_buffer[128]{};
    int easing_editor_preset_index{ 0 };
    int easing_editor_formula_preset_index{ -1 };
    bool easing_editor_drawing{ false };
    std::vector<DirectX::XMFLOAT2> easing_editor_freehand_points;
    int easing_editor_active_control_point{ -1 };
    int easing_editor_active_sample{ -1 };
    int easing_editor_context_control_point{ -1 };
    DirectX::XMFLOAT2 easing_editor_context_point{ 0.5f, 0.5f };

    ReplayEngine::Assets::SpriteAtlasAsset sprite_atlas_editor_asset;
    std::filesystem::path sprite_atlas_editor_path;
    std::string sprite_atlas_editor_guid;
    std::string sprite_atlas_editor_status;
    bool sprite_atlas_editor_loaded{ false };
    bool sprite_atlas_editor_dirty{ false };
    int sprite_atlas_selected_region{ -1 };
    float sprite_atlas_zoom{ 0.8f };
    bool sprite_atlas_draw_region_mode{ false };
    bool sprite_atlas_region_dragging{ false };
    ImVec2 sprite_atlas_drag_start{ 0.0f, 0.0f };
    // Atlas の視覚編集: -1=なし, 0..7=resize handle, 8=移動。
    int sprite_atlas_active_handle{ -1 };
    bool sprite_atlas_region_transform_dragging{ false };
    DirectX::XMFLOAT4 sprite_atlas_transform_start_uv{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::vector<DirectX::XMFLOAT2> sprite_atlas_transform_start_path_points;
    ImVec2 sprite_atlas_transform_start_mouse{ 0.0f, 0.0f };
    int sprite_atlas_active_point{ -1 };
    bool sprite_atlas_pixel_snap{ true };
    bool sprite_atlas_editor_keyboard_focus{ false };
    bool sprite_atlas_pan_dragging{ false };

    struct SpriteAtlasHistoryEntry
    {
        ReplayEngine::Assets::SpriteAtlasAsset before;
        ReplayEngine::Assets::SpriteAtlasAsset after;
        std::string label;
    };
    std::vector<SpriteAtlasHistoryEntry> sprite_atlas_history;
    std::size_t sprite_atlas_history_cursor{ 0 };
    bool sprite_atlas_history_transaction{ false };
    ReplayEngine::Assets::SpriteAtlasAsset sprite_atlas_history_before;
    std::string sprite_atlas_history_label;

    ReplayEngine::Motion::MotionAsset motion_editor_asset;
    ReplayEngine::Motion::CompositionAsset motion_editor_composition;
    ReplayEngine::Editor::MotionEditHistory motion_edit_history;
    ReplayEngine::Editor::CompositionEditHistory composition_edit_history;
    ReplayEngine::Editor::FileEditHistory external_file_history;
    std::uint64_t external_file_reload_generation{ 0 };
    bool project_settings_file_undo_enabled{ false };
    std::filesystem::path motion_editor_path;
    std::string motion_editor_guid;
    std::string motion_editor_status{ "Motion Asset が未選択です" };
    bool motion_editor_loaded{ false };
    bool motion_editor_dirty{ false };
    bool motion_composition_loaded{ false };
    // Track を追加するときのプロパティ検索欄。
    // 1 つの GameObject でも動かせる項目は数十個あるので、絞り込みが無いと探せない。
    std::array<char, 64> motion_property_picker_filter{};
    int motion_selected_track{ -1 };
    int motion_selected_key{ -1 };
    int motion_selected_event_track{ -1 };
    int motion_selected_event{ -1 };
    // After Effects 的なフレーム編集。MotionAsset の保存時間は従来どおり秒のままにし、
    // Editor 表示/スナップだけ FPS を使うので旧 Asset と互換。
    int motion_editor_fps{ 30 };
    bool motion_editor_display_frames{ true };
    bool motion_editor_frame_snap{ true };
    float motion_timeline_zoom{ 1.0f };
    bool motion_graph_speed_mode{ false };
    bool motion_graph_overlay_tracks{ false };
    int motion_graph_channel{ 0 };
    std::vector<int> motion_selected_keys;
    std::vector<ReplayEngine::Motion::MotionKeyframe> motion_key_clipboard;
    float motion_key_time_scale{ 1.0f };
    int motion_key_time_scale_pivot{ 0 };
    ReplayEngine::Reflection::AssetReference motion_selected_easing_curve;
    std::unordered_set<std::string> motion_easing_curve_warning_guids;
    bool motion_box_select_mode{ false };
    bool motion_box_selecting{ false };
    int motion_box_select_track{ -1 };
    float motion_box_select_start_x{ 0.0f };
    float motion_box_select_current_x{ 0.0f };
    bool motion_preview_active{ false };
    bool motion_preview_loop{ false };
    float motion_preview_time{ 0.0f };
    float motion_preview_speed{ 1.0f };
    struct MotionPreviewCapture
    {
        ReplayEngine::Core::ObjectID object;
        ReplayEngine::Core::ComponentStableID component =
            ReplayEngine::Core::invalid_component_stable_id;
        ReplayEngine::Reflection::PropertyBag properties;
    };
    std::vector<MotionPreviewCapture> motion_preview_captures;
    int ui_preview_resolution_index{ 0 };
    int ui_preview_custom_width{ 1920 };
    int ui_preview_custom_height{ 1080 };
    float ui_preview_zoom{ 0.5f };
    // UI Scene View のカメラ移動量。描画・選択枠・入力判定が同じ矩形を使うよう
    // object_ui_viewport_target() の一箇所だけで足す。
    float ui_preview_pan_x{ 0.0f };
    float ui_preview_pan_y{ 0.0f };
    bool ui_preview_panning{ false };
    bool ui_preview_grid{ true };
    float ui_preview_grid_size{ 100.0f };
    ImVec2 ui_preview_pan{ 0.0f, 0.0f };
    bool ui_preview_dragging{ false };
    // UI Scene View の直接編集 sub-control。Rect移動とは別トランザクション。
    int ui_puppet_active_pin{ -1 };
    int ui_puppet_active_radius{ -1 };
    int ui_puppet_selected_pin{ -1 };
    bool ui_puppet_radius_editing{ false };
    int ui_shape_active_point{ -1 };
    int ui_shape_selected_point{ -1 };
    int ui_mask_selected_matte{ -1 };
    int ui_shape_active_handle{ 0 }; // 0 anchor, 1 in, 2 out
    bool ui_subcontrol_dragging{ false };
    bool ui_scene_view_input_consumed{ false };
    ReplayEngine::Core::ObjectID ui_preview_drag_object;
    ImVec2 ui_preview_drag_start_mouse{ 0.0f, 0.0f };
    DirectX::XMFLOAT2 ui_preview_drag_start_position{ 0.0f, 0.0f };

    // Canvas Preview Rect Tool。ハンドルを押しただけでは履歴を作らず、
    // 実際に drag threshold を越えた瞬間だけ BeginEdit する。
    // これにより 1 drag = 1 Undo を維持する。
    bool ui_preview_resize_candidate{ false };
    bool ui_preview_resizing{ false };
    int ui_preview_resize_handle{ -1 };
    ReplayEngine::Core::ObjectID ui_preview_resize_object;
    ImVec2 ui_preview_resize_start_mouse{ 0.0f, 0.0f };
    DirectX::XMFLOAT4 ui_preview_resize_start_rect{ 0.0f, 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 ui_preview_resize_parent_rect{ 0.0f, 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4X4 ui_preview_resize_start_matrix{};

    // Effect Stack の適用範囲は RectTransform のリサイズとは別操作にする。
    // 8方向ハンドルと回転ハンドルを持つ Scene View 専用の一時状態。
    bool ui_effect_region_candidate{ false };
    bool ui_effect_region_editing{ false };
    int ui_effect_region_index{ 0 };
    int ui_effect_region_handle{ -1 }; // 0..7 resize, 8 rotation
    int ui_effect_region_point{ -1 }; // 自由形状の頂点
    ReplayEngine::Core::ObjectID ui_effect_region_object;
    int ui_effect_region_selected_index{ -1 };
    int ui_effect_region_selected_point{ -1 };
    ReplayEngine::Core::ObjectID ui_effect_region_selected_object;
    ImVec2 ui_effect_region_start_mouse{ 0.0f, 0.0f };
    DirectX::XMFLOAT2 ui_effect_region_start_center{ 0.5f, 0.5f };
    DirectX::XMFLOAT2 ui_effect_region_start_size{ 0.5f, 0.5f };
    float ui_effect_region_start_rotation{ 0.0f };

    bool scene_view_hovered{ false };
    bool scene_view_focused{ false };
    ImVec2 scene_view_overlay_position{ 0.0f, 0.0f };
    ImVec2 scene_view_overlay_size{ 0.0f, 0.0f };
    bool scene_view_overlay_valid{ false };
    float scene_view_min_x{ 0.0f };
    float scene_view_min_y{ 0.0f };
    float scene_view_max_x{ 0.0f };
    float scene_view_max_y{ 0.0f };
    // Viewport 右クリック位置は Popup を操作している間も保持する。
    // 右クリックは camera look と共用するため、短い click / drag を区別する。
    bool scene_context_world_point_valid{ false };
    DirectX::XMFLOAT3 scene_context_world_point{ 0.0f, 0.0f, 0.0f };
    bool scene_context_right_click_tracking{ false };
    bool scene_context_right_click_dragged{ false };
    float scene_context_right_click_start_x{ 0.0f };
    float scene_context_right_click_start_y{ 0.0f };
    int scene_view_draw_mode{ 0 };

    // --- Landscape / World Editing ---------------------------------------
    // Landscape 自体は普通の GameObject + Component。ここに置くのは
    // Scene View の一時的な選択・ブラシ状態だけで、Scene 保存対象ではない。
    bool landscape_edit_enabled{ false };
    int landscape_edit_mode{ 0 }; // 0=Sculpt, 1=Topology
    int landscape_topology_selection_mode{ 0 }; // 0=Face, 1=Edge Bridge
    int landscape_brush_mode{ 0 };
    int landscape_brush_preview_mode{ 1 }; // 0=Ring, 1=Falloff, 2=Grid, 3=Contour, 4=Grid+Contour
    ReplayEngine::Landscape::LandscapeBrush landscape_brush{};
    ReplayEngine::Landscape::LandscapeEditorTool landscape_editor_tool;
    std::size_t landscape_selected_face{ static_cast<std::size_t>(-1) };
    // Bridge は同じ mesh 上の 2 edge を順に選ぶ。Editor transient state だけなので
    // Component/Scene には保存しない。
    std::uint32_t landscape_bridge_a0{ static_cast<std::uint32_t>(-1) };
    std::uint32_t landscape_bridge_a1{ static_cast<std::uint32_t>(-1) };
    std::uint32_t landscape_bridge_b0{ static_cast<std::uint32_t>(-1) };
    std::uint32_t landscape_bridge_b1{ static_cast<std::uint32_t>(-1) };
    bool landscape_stroke_transaction{ false };
    float landscape_extrude_distance{ 1.0f };
    float landscape_inset_amount{ 0.25f };
    float landscape_tunnel_depth{ 8.0f };
    int landscape_tunnel_segments{ 6 };
    float landscape_tunnel_end_scale{ 1.0f };

    // --- シェーダ資産（フェーズ 1〜3）--------------------------------------
    //
    // Shader/Materials, Shader/Layers, Shader/PostProcess を走査して
    // #pragma から宣言を読み、目録を作る。
    // DX12 の固定描画経路は RenderItem へ解決済み Layer を渡す。
    ReplayEngine::Rendering::ShaderLibrary shader_library;
    std::unordered_map<std::string,
        const ReplayEngine::Rendering::ShaderCatalog::Entry*>
        custom_ui_effect_shader_catalog_cache;
    std::uint64_t custom_ui_effect_shader_catalog_cache_generation = 0;
    ReplayEngine::Editor::ShaderComposerEditor shader_composer_editor;
    bool show_shader_catalog_panel{ false };

    // .hlsl を保存したら自動でコンパイルし直す。
    // C# の自動リロードと同じ扱い。既定は有効。
    bool shader_auto_recompile{ true };
    float shader_poll_timer{ 0.0f };

    // 起動時に 1 回走査する。ログは push_editor_log へ流す。
    void scan_shader_library();
    void draw_shader_catalog_panel();

    // 保存されたものだけコンパイルし直す。毎フレーム呼んでよい
    // （内部で 1 秒の間隔を取る）。
    void poll_shader_source_changes(float elapsed_time);

    // --- スクリーンショット回帰（フェーズ 18）------------------------------
    //
    // 「見た目が 1 ピクセルも変わらない」を機械で確かめる。
    // 目視では気付けない。色が 1 段ずれても人間には分からない。
    enum class golden_request_kind
    {
        none = 0,
        capture,     // 基準画像として撮る
        compare,     // 撮って基準と比べる
        self_check,  // 2 回撮って一致するか（＝決定論が足りているか）
    };

    std::unique_ptr<ReplayEngine::Editor::GoldenImageState> golden_state_;
    char golden_name_buffer[64]{ "default" };
    bool show_golden_panel{ false };

public:
    void request_automated_frame_capture(const std::string& name);
    void request_automated_exclusive_frame_capture(const std::string& name);
    bool automated_exclusive_frame_capture_attempted() const noexcept;
    bool golden_last_capture_ok() const noexcept;
    const std::string& golden_last_capture_summary() const noexcept;

private:
    bool begin_automated_exclusive_frame_capture();
    void cancel_automated_exclusive_frame_capture();
    // 撮影待ちの間はワールドを止める。update / render から見る。
    bool golden_capture_pending() const noexcept;

    void request_golden(golden_request_kind kind);

    // Present の直前に呼ぶ。Present のあとはバックバッファの中身が保証されない。
    void tick_golden_capture();
    // DX12 は Present 前に GPU Readback を記録し、Present 後に回収する。
    bool prepare_dx12_golden_capture() noexcept;

    void draw_golden_panel();

    // --- UI の見た目設定（Window メニュー →「UI の見た目」で変更）----------
    // 個人の見た目プリセットから読み込む表示設定。Scene/Projectへは保存しない。
    float ui_button_scale{ 1.0f };      // ボタンとメニューの余白倍率
    float ui_font_scale{ 1.0f };        // 文字の大きさ倍率
    float ui_text_color[3]{ 1.0f, 1.0f, 1.0f };   // 文字色（既定は白）
    bool  ui_style_overridden{ false };  // 一度でも触ったか
    std::vector<ReplayEngine::Editor::EditorStylePreset> editor_style_presets;
    int active_editor_style_preset_index{ -1 };
    bool editor_style_presets_loaded{ false };
    bool editor_style_active_selection_loaded{ false };
    std::array<char, 128> editor_style_name_buffer{};
    std::string editor_style_name_buffer_id;

    char asset_search_text[192]{};
    int asset_type_filter{ 0 };
    std::string selected_asset_guid;
    bool asset_drop_add_collider{ false };
    char new_csharp_behaviour_name[128]{ "NewBehaviour" };
    char new_csharp_namespace[128]{ "Game" };
    bool csharp_scripts_dirty{ false };

    // .cs の保存を検出したら自動で再コンパイルするか。
    // 既定で有効。コンパイル失敗時は直前に成功した Assembly が
    // 維持されるので、自動で走らせても編集中の状態は壊れない。
    bool csharp_auto_reload{ true };
    float csharp_scan_accumulator{ 0.0f };
    std::unordered_map<std::string, std::filesystem::file_time_type>
        csharp_source_write_times;
    char new_material_name[128]{ "NewMaterial" };

    // --- Project ブラウザの状態 -------------------------------------------
    // project_current_folder はプロジェクトルートからの相対パス。
    // 空文字列はルート自身を指す。
    std::filesystem::path project_current_folder;
    std::filesystem::path project_rename_target;
    std::filesystem::path project_selected_entry_path;
    std::filesystem::path project_delete_target;
    std::vector<std::string> project_delete_references;
    std::vector<std::string> project_delete_contents;
    bool project_delete_popup_pending{ false };
    bool project_browser_focused{ false };
    // 左の Project Tree はフォルダだけでなくファイルも表示する。右ペインで
    // 選択したAssetを左ツリーへ自動Revealするための一時要求と、
    // Drag中に閉じたフォルダへ一定時間Hoverしたときの自動展開状態。
    bool project_tree_reveal_selection_pending{ false };
    std::filesystem::path project_tree_drag_hover_folder;
    double project_tree_drag_hover_started{ 0.0 };
    char project_rename_buffer[192]{};
    bool project_rename_focus_pending{ false };
    char project_new_item_name[128]{ "NewItem" };
    float project_thumbnail_size{ 84.0f };
    float project_tree_width{ 210.0f };
    bool project_grid_view{ true };
    std::string project_browser_status;
    // 欠損アセットの判定はファイルシステムへ問い合わせるので毎フレームやらない。
    std::vector<std::array<std::string, 3>> project_missing_assets;
    std::size_t project_missing_assets_source_count = static_cast<std::size_t>(-1);

    ReplayEngine::Rendering::MaterialAsset material_editor_asset;
    std::string material_editor_guid;
    std::string material_editor_status;
    bool material_editor_loaded{ false };
    bool material_editor_dirty{ false };
    std::filesystem::file_time_type material_editor_write_time{};
    bool material_editor_write_time_valid{ false };
    ReplayEngine::Editor::MaterialEditHistory material_editor_history;
    ReplayEngine::Editor::EditorStyleEditHistory editor_style_history;

    bool create_material_asset();
    bool load_material_editor(const ReplayEngine::Assets::AssetRecord& asset);
    bool save_material_editor();
    void refresh_material_editor_preview();
    void begin_material_reorder_history();
    static void capture_material_reorder_history(void* owner);
    bool undo_material_editor();
    bool redo_material_editor();
    bool undo_editor_style();
    bool redo_editor_style();
    void ensure_editor_style_presets_loaded();
    bool switch_editor_style_preset(const std::string& id);
    bool save_active_editor_style_preset();
    bool make_active_editor_style_preset_personal_copy();
    ReplayEngine::Editor::EditorStylePreset capture_editor_style_preset() const;
    ReplayEngine::Editor::EditorStyleEditHistory::Snapshot capture_editor_style_snapshot() const;
    void apply_editor_style_snapshot(
        const ReplayEngine::Editor::EditorStyleEditHistory::Snapshot& snapshot);
    void draw_material_asset_editor();
    bool gizmo_local_space{ false };
    bool show_scene_grid{ true };
    // Pivot の Snap は Transform の Grid Snap と別物。
    // 選択オブジェクトの CookedMeshCollision 上の実ジオメトリへ吸着する。
    bool pivot_edit_mode{ false };
    int pivot_snap_mode{ 1 }; // 0=Surface, 1=Vertex, 2=Edge
    bool snap_primary_pivot_to_mesh(int mode);
    DirectX::XMFLOAT3 resolve_object_pivot_world(ReplayEngine::Core::GameObject& object,
        ReplayEngine::Scene::Scene& scene) const;
    float scene_grid_step{ 1.0f };
    struct ObjectGizmoState
    {
        ReplayEngine::Core::ObjectID id;
        DirectX::XMFLOAT3 world_position{};
        DirectX::XMFLOAT3 local_rotation{};
        DirectX::XMFLOAT3 local_scale{ 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT3 pivot_world{};
        DirectX::XMFLOAT4X4 world_matrix{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    };
    std::vector<ObjectGizmoState> object_gizmo_states;
    bool object_gizmo_dragging{ false };
    // ギズモへ渡す行列。掴んでいる間は前フレームの結果と開始時の姿を持ち回る。
    DirectX::XMFLOAT4X4 object_gizmo_matrix{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    DirectX::XMFLOAT4X4 object_gizmo_start_matrix{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    bool normal_adjust_gizmo_dragging{ false };
    ReplayEngine::Core::ObjectID normal_adjust_gizmo_object;
    ReplayEngine::Core::ComponentStableID normal_adjust_gizmo_component{ 0 };
    DirectX::XMFLOAT3 normal_adjust_gizmo_start_center{};
    DirectX::XMFLOAT3 normal_adjust_gizmo_start_world{};
    DirectX::XMFLOAT4X4 normal_adjust_gizmo_start_matrix{};
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
    struct editor_log_entry
    {
        std::string severity;
        std::string message;
        std::filesystem::path file;
        int line = 0;
        int column = 0;
    };
    std::vector<editor_log_entry> editor_log_entries;
    int selected_editor_log_index{ -1 };
    bool viewport_drag_selecting{ false };

    // 直前フレームで実際にカメラへ渡した入力。
    // 診断表示が同じ条件を書き写すとズレるため、本物をそのまま保持して読ませる。
    ReplayEngine::Editor::EditorCameraInput last_editor_camera_input{};
    POINT viewport_drag_start{};
