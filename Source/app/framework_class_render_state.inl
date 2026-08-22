// framework の D3D11 描画状態・レンダラ・プロジェクト/Scene 基本状態。
// framework_class.h の class framework 内部からのみ include する。

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

    enum class BLEND_STATE { NONE, ALPHA, ADD, MULTIPLY, SCREEN, PREMULTIPLIED };
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend_states[6];

    enum class RASTER_STATE { SOLID, WIREFRAME, CULL_NONE, WIREFRAME_CULL_NONE, SCISSOR };
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_states[5];

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
    // Point / Spot Light の動的シャドウマップ。CSM とは投影方法が違うため
    // 別リソースにしてある。影付きライトが 0 の Scene では GPU メモリを使わない。
    ReplayEngine::Rendering::LocalShadowAtlas local_shadows;

    // sync_object_lights() が決めた「今フレーム影マップを作るライト」。
    // スライスの確保と行列作りは更新側、実際の描画は render() 側に分かれるので、
    // 描画に要る値だけをここへ運ぶ。
    struct local_shadow_request
    {
        bool point = false;          // false なら Spot
        int base_slice = -1;
        int slice_count = 1;
        DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
        float range = 10.0f;
    };
    std::vector<local_shadow_request> local_shadow_requests;

    // 影の全体設定。個々の Light Component の設定ではなく、
    // 「このプロジェクトで影機能をどこまで使うか」の上限を持つ。
    bool enable_dynamic_shadows{ true };
    // CSM を使うかどうかのユーザー設定。
    // シェーダーへ渡る csm.constants.params.w は
    //   enable_dynamic_shadows && csm_enabled_setting && directional_shadow_enabled
    // から毎フレーム作り直すので、UI はこちらを触ること。
    // params.w を直接書くと、Light 側の Cast Shadows と二重管理になる。
    bool csm_enabled_setting{ true };
    // Directional Light が 1 つも無い Scene View で、非保存のプレビュー光から
    // 影を出すかどうか。編集中に物を動かした手応えを出すために既定で有効。
    // Play / Standalone では Scene の照明設定を尊重するのでここは効かない。
    bool editor_preview_light_casts_shadows{ true };

    // Directional Light の解決結果。sync_object_lights() が毎フレーム更新する。
    // 影を出す/出さないの判断はこの 3 つに集約し、描画側で条件を再発明しない。
    bool directional_light_present{ false };     // Scene に有効な Directional がある
    bool directional_light_is_preview{ false };  // Scene View 用の非保存プレビュー光
    bool directional_shadow_enabled{ false };    // その光が影を落とすか

    // 影の診断表示用。毎フレーム影パスの直前に 0 へ戻す。
    // 「影が出ない」ときに、Light が無いのか、Cast Shadow が切れているのか、
    // 提出が 0 件なのかを Editor 上で切り分けるために使う。
    struct shadow_frame_stats
    {
        int primitive_casters = 0;
        int static_casters = 0;
        int skinned_casters = 0;
        int landscape_casters = 0;
        int skipped_cast_shadow = 0;
        int culled_casters = 0;
        // Mesh Asset を解決できず影パスへ出せなかった Skinned Mesh の数。
        // 0 でないときは影ではなく Asset 側の問題（通常描画にも出ていない）。
        int skinned_unresolved = 0;
        int shadow_draw_calls = 0;
        int spot_shadow_lights = 0;
        int point_shadow_lights = 0;
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

