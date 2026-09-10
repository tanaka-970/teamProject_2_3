// framework の DX12 描画状態・レンダラ・プロジェクト/Scene 基本状態。
// framework_class.h の class framework 内部からのみ include する。

public:
    CONST HWND hwnd;

    ReplayEngine::Rendering::DX12::D3D12DeviceContext dx12_device_context;
    bool dx12_framework_requested{ true };
    bool dx12_framework_active{ false };
    bool dx12_framework_render_error_reported{ false };
    std::unordered_set<std::string> custom_ui_effect_diagnostics_reported;
    std::unordered_map<std::string,
        ReplayEngine::Rendering::DX12::D3D12UICustomEffectShaderSource>
        pending_custom_ui_effect_shaders;

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
    // Imported model preview slots used by the compatibility renderer.
    // Animated models are submitted by SkinnedMeshRendererComponent.
    // shared_ptr なのは並列ロードとモデルキャッシュ (ConcurrentResourceCache /
    // gltf_model_cache) が同じ実体を共有するため。所有権はここだけにしない。
    std::shared_ptr<static_mesh> static_meshes[8];
    std::shared_ptr<skinned_mesh> skinned_meshes[8];
    std::shared_ptr<gltf_model> stage_gltf_model;

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

    // Point / Spot の動的シャドウマップ。CSM とは投影方法が違うので別リソース。
    ReplayEngine::Rendering::LocalShadowAtlas local_shadows;

    // sync_object_lights() が決めた「今フレーム影マップを作るライト」。
    struct local_shadow_request
    {
        bool point = false;          // false なら Spot
        int base_slice = -1;
        int slice_count = 1;
        DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
        float range = 10.0f;
    };
    std::vector<local_shadow_request> local_shadow_requests;

    // 影の全体設定。個々の Light ではなくプロジェクト全体の上限を持つ。
    bool enable_dynamic_shadows{ true };
    // CSM を使うかのユーザー設定。params.w は毎フレーム作り直すので UI はこちらを触る。
    bool csm_enabled_setting{ true };
    // Light が無い Scene View でプレビュー光から影を出すか。Play では効かない。
    bool editor_preview_light_casts_shadows{ true };

    // Directional Light の解決結果。影を出す判断はこの 3 つに集約する。
    bool directional_light_present{ false };     // Scene に有効な Directional がある
    bool directional_light_is_preview{ false };  // Scene View 用の非保存プレビュー光
    bool directional_shadow_enabled{ false };    // その光が影を落とすか

    // 影の診断表示用。毎フレーム影パスの直前に 0 へ戻す。
    struct shadow_frame_stats
    {
        int primitive_casters = 0;
        int static_casters = 0;
        int skinned_casters = 0;
        int landscape_casters = 0;
        int skipped_cast_shadow = 0;
        int culled_casters = 0;
        // Mesh Asset を解決できず影パスへ出せなかった Skinned Mesh の数。
        int skinned_unresolved = 0;
        int shadow_draw_calls = 0;
        int spot_shadow_lights = 0;
        int point_shadow_lights = 0;
        // Model Effect Stack の面消しを影へ反映しているキャスター数。
        int coverage_casters = 0;
        // 影で再現できない面消し Effect の数。範囲が投げ縄/画像のものを含む。
        int coverage_unsupported = 0;
        // bounding_box が未設定でボリューム選別を掛けられなかったキャスター数。
        int missing_bounds_primitive = 0;
        int missing_bounds_static = 0;
        int missing_bounds_landscape = 0;
        bool directional_light_present = false;
        bool directional_preview_light = false;
        bool directional_shadow_rendered = false;

        void Reset() noexcept { *this = shadow_frame_stats{}; }
        int TotalCasters() const noexcept
        {
            return primitive_casters + static_casters +
                skinned_casters + landscape_casters;
        }
    };
    shadow_frame_stats shadow_stats{};
    // DX12用パーティクルの実行状態。Emitter Componentが設定と要求の正本で、
    // ここはフレームをまたぐ寿命・速度だけを保持する。
    struct dx12_particle_instance final
    {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 velocity{};
        DirectX::XMFLOAT4 color{};
        float age = 0.0f;
        float life = 1.0f;
        float size = 0.1f;
        float rotation = 0.0f;
    };
    std::unordered_map<ReplayEngine::Core::ObjectID,
        std::vector<dx12_particle_instance>> dx12_particle_states;
    std::unordered_map<ReplayEngine::Core::ObjectID, float> dx12_particle_spawn_remainders;

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
    ReplayEngine::Rendering::DX12::D3D12FrameConstants frame_constants{};
    std::unordered_set<ReplayEngine::Core::ObjectID> world_canvas_camera_diagnostics_reported;
    // TAAの再投影に使う前フレームのビュー射影行列。初回は今フレームで埋める。
    DirectX::XMFLOAT4X4 previous_view_projection{};
    bool previous_view_projection_valid{ false };

    struct dx12_scene_frame_history final
    {
        ReplayEngine::Core::WorldInstanceID world_instance_id{
            ReplayEngine::Core::invalid_world_instance_id };
        DirectX::XMFLOAT4X4 previous_view_projection{};
        bool valid{ false };
    };
    dx12_scene_frame_history object_loading_scene_frame_history{};

    void build_dx12_frame_constants_for_scene(
        const ReplayEngine::Scene::Scene& scene,
        std::uint32_t viewport_width, std::uint32_t viewport_height,
        const dx12_scene_frame_history& history,
        ReplayEngine::Rendering::DX12::D3D12FrameConstants& constants,
        DirectX::XMFLOAT4X4& current_view_projection,
        bool& used_fallback_camera,
        const ReplayEngine::Components::CameraComponent* camera_override = nullptr) const;
    static void commit_dx12_scene_frame_history(
        dx12_scene_frame_history& history,
        const ReplayEngine::Scene::Scene& scene,
        const DirectX::XMFLOAT4X4& current_view_projection) noexcept;
    bool build_dx12_lighting_for_scene(
        ReplayEngine::Rendering::DX12::D3D12StaticSceneSubmission& submission,
        const ReplayEngine::Scene::Scene& scene) const;
    bool prewarm_loading_scene_gpu_resources();
    // TAAジッターは現行DX12では未接続で、投影行列へはまだ適用していない。
    DirectX::XMFLOAT2 taa_jitter_ndc{ 0.0f, 0.0f };
    DirectX::XMFLOAT2 previous_taa_jitter_ndc{ 0.0f, 0.0f };
    unsigned int frame_index{ 0 };
    bool enable_ssao{ true };
    bool enable_ssr{ true };
    bool enable_taa{ true };
    // 深度プリパス。G-BufferのPS実行を最前面の1回に抑える。
    // 頂点処理が2回になるため、LODと併用する前提。
    bool enable_depth_prepass{ false };
    // 描画統計オーバーレイの表示。F4で切り替える。
    bool show_render_stats{ true };
    // 初回フレームだけ統計ウィンドウの位置を強制するためのフラグ。
    // imgui.ini に残った旧座標(インスペクタと重なる位置)に
    // 引っ張られるのを防ぐ。
    bool stats_window_placed_{ false };

    GameScene*       game_scene{ nullptr };
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
    std::string      selected_model_asset_path;
    std::string      selected_model_cache_path;
    std::string      model_asset_status{ "未選択" };
    ReplayEngine::Assets::AssetDatabase asset_database;
    ReplayEngine::Assets::ConcurrentResourceCache<static_mesh> static_mesh_cache;
    ReplayEngine::Assets::ConcurrentResourceCache<skinned_mesh> skinned_mesh_cache;
    ReplayEngine::Assets::ConcurrentResourceCache<gltf_model> gltf_model_cache;
    ReplayEngine::Motion::MotionMixer motion_mixer;
    std::unordered_map<std::string, ReplayEngine::Motion::MotionAsset> motion_asset_cache;
    std::unordered_set<std::string> motion_asset_load_failures;
    std::unordered_map<std::string, ReplayEngine::Motion::CompositionAsset> composition_asset_cache;
    std::unordered_set<std::string> composition_asset_load_failures;
    ReplayEngine::Assets::AsyncAssetManager async_asset_manager;

    // スキンメッシュのボーンパレット長。メッシュごとに不変なので毎フレーム求め直さない。
    std::unordered_map<std::string, std::size_t> skinned_palette_size_cache;

    // テクスチャパスの解決結果。描画中に exists() を呼ばないための表。
    std::unordered_map<std::string, std::string> texture_path_resolve_cache;

    // 材質スロットのローカル境界。バインドポーズの座標から作るので不変。
    struct material_subset_bounds_entry
    {
        std::uint32_t start = 0;
        std::uint32_t count = 0;
        ReplayEngine::Rendering::DX12::D3D12MeshLocalBounds bounds;
    };
    std::unordered_map<std::string, std::vector<material_subset_bounds_entry>>
        material_subset_bounds_cache;
    // 静的メッシュの境界。視錐台カリングの判定に使う。メッシュごとに一度だけ求める。
    std::unordered_map<std::string, ReplayEngine::Rendering::DX12::D3D12MeshLocalBounds>
        static_mesh_bounds_cache;

    // プロジェクト設定。Default Controlled Character Prefab を持つ。
    // 参照は AssetGUID なので、Prefab の名前やパスを変えても壊れない。
    ReplayEngine::Project::ProjectSettings project_settings;
    std::string project_settings_status{ "プロジェクト設定 未読込" };

    // Scene Flow Editor。ProjectSettings の GUID が Runtime で使う正本。
    ReplayEngine::Runtime::SceneFlowAsset scene_flow_editor_asset;
    ReplayEngine::Editor::SceneFlowEditHistory scene_flow_edit_history;
    std::filesystem::path scene_flow_editor_path;
    std::string scene_flow_editor_guid;
    std::string scene_flow_editor_status{ "Scene Flow 未選択" };
    bool scene_flow_editor_loaded{ false };
    bool scene_flow_editor_dirty{ false };

    // Play From Here は Editor セッションだけの一時オーバーライド。
    // SceneData / Scene ファイルへは保存しない。
    struct play_spawn_override_state
    {
        bool active = false;
        bool apply_rotation = false;
        bool use_camera_direction = false;
        DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
        // Transform の内部規約に合わせてラジアン。
        DirectX::XMFLOAT3 rotation_radians{ 0.0f, 0.0f, 0.0f };
        std::string label;
    };
    play_spawn_override_state play_spawn_override;

    // 直近で保存した Prefab の AssetGUID。
    // 「保存した Prefab をそのまま既定の操作キャラクターにする」ボタン用。
    std::string last_saved_prefab_guid;

    // 新規シーン作成ダイアログの入力欄。
    char new_object_scene_name[128]{ "NewScene" };

    ReplayEngine::Editor::TransformGizmo transform_gizmo;
    ReplayEngine::Physics::MeshCollisionCooker collision_cooker;
    std::string      selected_model_asset_guid;
    std::string      shader_preset_status{ "プリセット未選択" };
    bool             async_stage_load_active{ false };

