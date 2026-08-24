// framework の D3D11 描画状態・レンダラ・プロジェクト/Scene 基本状態。
// framework_class.h の class framework 内部からのみ include する。

public:
    CONST HWND hwnd;

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_view;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_stencil_view;

    // DX12 移行 Bootstrap。未移行の Asset/Editor Service が必要とする間は旧 D3D11
    // Device/Context を生かすが、HWND の Swap Chain を所有する API は常に 1 つだけにする。
    // --dx12-framework でこの経路を選択する。
    ReplayEngine::Rendering::DX12::D3D12DeviceContext dx12_device_context;
    bool dx12_framework_requested{ false };
    bool dx12_framework_active{ false };
    bool dx12_framework_render_error_reported{ false };

    std::unique_ptr<sprite_batch> sprite_batches[8];
    std::unique_ptr<framebuffer> framebuffers[8];
    std::unique_ptr<fullscreen_quad> bit_block_transfer;

    enum class SAMPLER_STATE { POINT, LINEAR, ANISOTROPIC, ANISOTROPIC_CLAMP };
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_states[4];

    enum class DEPTH_STATE { ZT_ON_ZW_ON, ZT_ON_ZW_OFF, ZT_OFF_ZW_ON, ZT_OFF_ZW_OFF };
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_stencil_states[4];

    enum class BLEND_STATE { NONE, ALPHA, ADD, MULTIPLY, SCREEN, PREMULTIPLIED };
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend_states[6];

    // CULL_FRONT は world 行列の行列式が負（鏡像）のときに使う。
    enum class RASTER_STATE { SOLID, WIREFRAME, CULL_NONE, WIREFRAME_CULL_NONE, SCISSOR, CULL_FRONT };
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_states[6];

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
    // Imported model preview slots used by the compatibility renderer.
    // Animated models are submitted by SkinnedMeshRendererComponent.
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
        DirectX::XMFLOAT4 base_color_factor{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 emissive_factor{ 0.0f, 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT4 mat_params{ 0.0f, 0.55f, 1.0f, 0.0f };
        unsigned int lighting_model{ 0 };
        float texture_contrast{ 1.0f };
        float pixelate_size{ 0.0f };
        float pixelate_strength{ 0.0f };
        unsigned int texture_mask{ 0 };
        // Mesh Renderer の Receive Shadow。0 で影を受けない。
        float receive_shadow{ 1.0f };
        DirectX::XMFLOAT2 padding{ 0.0f, 0.0f };
    };
    static_assert(sizeof(material_override_constants) == 80,
        "GBUFFER_MATERIAL_CONSTANTS must stay byte-identical to HLSL");
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

    // Shader\shadow_alpha_common.hlsli の SHADOW_ALPHA_CONSTANTS(b7) と一致させる。
    struct shadow_alpha_constants
    {
        // x=抜き方(0/1/2) y=cutoff z=BaseMapの出所(0:t0 1:t40) w=予約
        DirectX::XMFLOAT4 params{ 0.0f, 0.5f, 0.0f, 0.0f };
    };
    Microsoft::WRL::ComPtr<ID3D11Buffer> shadow_alpha_cb;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> shadow_caster_alpha_ps;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> shadow_caster_alpha_skinned_ps;

    // Shader\shadow_coverage_common.hlsli の SHADOW_COVERAGE_CONSTANTS(b8) と一致させる。
    static constexpr int shadow_coverage_max_effects = 4;
    static constexpr int shadow_coverage_max_regions = 4;
    struct shadow_coverage_constants
    {
        DirectX::XMFLOAT4X4 view_projection{};
        DirectX::XMFLOAT4 viewport{ 0.0f, 0.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 rect{ 0.0f, 0.0f, 1.0f, 1.0f };
        // x=Effect数 y=マスク画像を貼ったEffectの番号(-1で無し) z=範囲数 w=予約
        DirectX::XMFLOAT4 control{ 0.0f, -1.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT4 params0[shadow_coverage_max_effects]{};
        DirectX::XMFLOAT4 params1[shadow_coverage_max_effects]{};
        DirectX::XMFLOAT4 params2[shadow_coverage_max_effects]{};
        DirectX::XMFLOAT4 params3[shadow_coverage_max_effects]{};
        DirectX::XMFLOAT4 meta[shadow_coverage_max_effects]{};
        DirectX::XMFLOAT4 region_params[shadow_coverage_max_regions]{};
        DirectX::XMFLOAT4 region_settings[shadow_coverage_max_regions]{};
    };
    Microsoft::WRL::ComPtr<ID3D11Buffer> shadow_coverage_cb;
    // 面消し Effect を持つ GameObject ごとの影用パラメータ。毎フレーム作り直す。
    struct shadow_coverage_entry
    {
        shadow_coverage_constants constants{};
        ID3D11ShaderResourceView* mask = nullptr;
    };
    std::unordered_map<std::uint64_t, shadow_coverage_entry> shadow_coverage_entries;
    // b8 が「面消し 0 件」で埋まっているか。埋まっていれば積み直しを省ける。
    bool shadow_coverage_cb_is_empty{ false };
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
    trail            test_trail;
    particle_system  particles;

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
    bool enable_depth_prepass{ false };
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

    // プロジェクト設定。Default Controlled Character Prefab を持つ。
    // 参照は AssetGUID なので、Prefab の名前やパスを変えても壊れない。
    ReplayEngine::Project::ProjectSettings project_settings;
    std::string project_settings_status{ "プロジェクト設定 未読込" };

    // Scene Flow Editor。ProjectSettings の GUID が Runtime で使う正本。
    ReplayEngine::Runtime::SceneFlowAsset scene_flow_editor_asset;
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

