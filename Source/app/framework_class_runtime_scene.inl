// GameObject/Runtime World、衝突、編集カメラ、起動設定。
// framework_class.h の class framework 内部からのみ include する。

    // --- GameObject / Component 基盤 ---------------------------------------
    //
    // 所有関係:
    //   編集 Scene   … framework が値で所有する（Editor が編集する唯一の正本）
    //   Runtime World … RuntimeSceneService が unique_ptr で所有する（唯一の正規所有者）
    //
    //   framework は Runtime World を値でも生ポインタでも持たない。
    //   必要なときに object_runtime_scenes.ActiveWorld() から取り直す。
    //   Scene 切り替えで World の実体が入れ替わるため、跨いで参照を持つと
    //   解放済みの Scene を指すことになる。
    //
    // Play Mode:
    //   Play 開始時に編集 Scene を SceneData 経由で RuntimeSceneService へ渡す
    //   （RequestAdopt）。Play 中の変更は Runtime World だけに入るため、
    //   編集内容が汚れない。Play 終了時は ResetToEmptyWorld() で捨てる。
    ReplayEngine::Scene::Scene              object_scene;

    // --- Runtime World の所有と遷移 ----------------------------------------
    //
    // 宣言順が寿命の順序になる。
    //   object_runtime_scenes … World の所有者。最後に壊れる
    //   object_runtime_context … World の view。先に壊れる
    //   object_scene_flow      … Runtime Scene Service の上位。さらに先に壊れる
    // この順でないと、World の破棄中に破棄済みの RuntimeContext を触ることになる。
    // スクリプト機構。World の入れ替えへ IWorldLifecycleListener として接続する。
    //
    // 【宣言位置の意味】
    //   object_runtime_scenes（World の唯一の所有者）より「前」に置くこと。
    //   メンバは宣言と逆順に壊れるので、この順序だと
    //   RuntimeSceneService が先に壊れ、ScriptRuntime が後に壊れる。
    //
    //   逆にすると壊れる:
    //     RuntimeSceneService の破棄は Scene を破棄し、Scene::Clear() が
    //     全 Component の OnRuntimeDestroy を流す。ScriptComponent はそこで
    //     ユーザーの OnDestroy を呼び、インスタンスを解放する。
    //     ScriptRuntime が先に消えていると、解放済みの実体を触ることになる。
    std::unique_ptr<ReplayEngine::Scripting::ScriptRuntime> object_script_runtime;

    ReplayEngine::Runtime::RuntimeSceneService object_runtime_scenes;
    std::unique_ptr<ReplayEngine::Runtime::RuntimeContext> object_runtime_context;
    std::unique_ptr<ReplayEngine::Runtime::SceneFlowService> object_scene_flow;
    ReplayEngine::Runtime::CollisionEventDispatcher object_collision_events;

    // AssetGUID -> Scene ファイルのパス。Runtime 層が AssetDatabase を
    // 直接 include しないための実装側。framework が所有する。
    // 結線は initialize_runtime_services() で行う。
    // メンバ初期化子で *this を渡す形にしないのは、
    // まだ不完全な自分自身をメンバの初期化へ持ち込まないため。
    class scene_asset_resolver final : public ReplayEngine::Runtime::ISceneAssetResolver
    {
    public:
        void Bind(framework& owner) noexcept { owner_ = &owner; }

        ReplayEngine::Runtime::RuntimeStatus ResolveScenePath(
            const std::string& asset_guid, std::string& out_path) const override;

    private:
        framework* owner_ = nullptr;
    };
    scene_asset_resolver object_scene_resolver;

    // Behaviour からの Prefab 生成要求の受け口。
    class runtime_prefab_instantiator final
        : public ReplayEngine::Runtime::IPrefabInstantiator
    {
    public:
        void Bind(framework& owner) noexcept { owner_ = &owner; }

        ReplayEngine::Runtime::RuntimeStatus InstantiatePrefab(
            const std::string& asset_guid, ReplayEngine::Scene::Scene& world,
            const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation_euler,
            const DirectX::XMFLOAT3& scale, ReplayEngine::Core::ObjectID parent,
            ReplayEngine::Core::ObjectID& created_root) override;

    private:
        framework* owner_ = nullptr;
    };
    runtime_prefab_instantiator object_prefab_instantiator;

    // 直近に結線した World の実体番号。
    // これが変わったフレームだけ、衝突世界・EditorContext・Selection を張り直す。
    // 生の Scene* をどこにも溜めないので、張り直しの取りこぼしが起きても
    // 解放済みメモリを触ることはなく、参照先が古いだけで済む。
    ReplayEngine::Core::WorldInstanceID object_bound_world_instance{
        ReplayEngine::Core::invalid_world_instance_id };

    // 起動時に Startup Scene から Runtime を始めるか。
    // main.cpp が --game または .replaygame を受け取ったときだけ true になる。
    bool object_boot_from_startup_scene{ false };

    // 書き出したゲームとして起動しているか。
    // Editor UI を削らず、実行時分岐だけで通さないための判定。
    bool standalone_game_mode{ false };
    std::string standalone_game_name{ "RePlayGame" };
    std::filesystem::path standalone_startup_scene_path;

    // Runtime World が「今このフレームで動かしている World」かどうか。
    //
    // object_scene_play_mode と分けている理由:
    //   Play Mode は Editor の状態（F5 を押したか）。
    //   こちらは「更新・描画・衝突の対象が Runtime World か編集 Scene か」。
    //   Editor を出さない通常のゲーム起動では Play Mode に入らないまま
    //   Runtime World が動くので、Play Mode だけで判断すると
    //   起動した Scene ではなく編集 Scene を動かしてしまう。
    bool object_runtime_world_active{ false };

    // Runtime 開始が診断状態で止まっているか。
    // Editor は落とさず、Runtime の開始だけを止める。
    bool object_runtime_blocked{ false };
    std::string object_runtime_block_reason;

    // Runtime へ渡すフレーム番号。Scene が入れ替わっても連番のまま進める。
    std::uint64_t object_runtime_frame_index{ 0 };
    ReplayEngine::Editor::EditorContext     object_editor_context;
    ReplayEngine::Editor::HierarchyPanel    object_hierarchy_panel;
    ReplayEngine::Editor::InspectorPanel    object_inspector_panel;
    ReplayEngine::Editor::ValidationPanel   object_validation_panel;
    ReplayEngine::Rendering::RenderItemList object_render_items;
    ReplayEngine::UI::FontAtlas             ui_font_atlas;
    bool ui_pointer_down_last{ false };
    float ui_mouse_wheel_delta{ 0.0f };

    // Asset GUID -> メッシュ実体。
    // 読み込めた Asset だけを入れる。null や壊れたエントリは決して登録しない。
    std::unordered_map<std::string, std::unique_ptr<skinned_mesh>> object_mesh_cache;
    // builtin:plane / builtin:cube 等は外部ファイルを要求しない Engine 内蔵 Mesh。
    // GameObject 側は通常の MeshRendererComponent を使うので特別な Object 型は増やさない。
    std::unordered_map<std::string, std::unique_ptr<static_mesh>> builtin_primitive_mesh_cache;
    struct landscape_gpu_cache_entry
    {
        std::uint64_t revision = 0;
        std::unique_ptr<static_mesh> mesh;
    };
    std::unordered_map<std::uint64_t, landscape_gpu_cache_entry> landscape_gpu_mesh_cache;

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

    // Shader GUID から replay_lighting を解決できなかったもの。
    // 同じ警告を毎フレーム出さないため、Material cache と同じ寿命で保持する。
    std::unordered_set<std::string> object_shader_lighting_failures;

    // Startup Scene is restored from the Editor session. If no Scene exists yet,
    // a fresh project starts with an unsaved component-based Basic Scene
    // (Landscape Ground + Sun) instead of depending on a bundled sample asset.
    std::filesystem::path object_scene_path;
    std::string      object_scene_asset_guid;
    bool             object_scene_play_mode{ false };
    bool             object_scene_paused{ false };
    float            object_autosave_elapsed{ 0.0f };
    std::filesystem::path object_recovery_path;
    bool             object_recovery_available{ false };
    bool             object_recovery_prompt_opened{ false };
    std::string      object_autosave_status;
    std::vector<std::filesystem::path> recent_scene_paths;
    enum class object_scene_action
    {
        none,
        new_empty,
        new_default,
        open_path,
        exit_application
    };
    // 終了することがユーザーの操作で確定したか。
    //
    // 【なぜフラグが要るか】
    //   終了は「WM_CLOSE -> 未保存確認 -> WM_CLOSE を投げ直す」という
    //   2 往復で成立する。投げ直した先でもう一度 Dirty を見ているため、
    //   その間に何かが Dirty を立て直すと、確認ダイアログが出続けて
    //   永久に終了できない。
    //   一度ユーザーが「保存して終了 / 破棄して終了」を選んだら、
    //   以降は Dirty を見ずに閉じる。
    bool             object_exit_confirmed{ false };
    object_scene_action pending_object_scene_action{ object_scene_action::none };
    std::filesystem::path pending_object_scene_path;
    bool object_scene_unsaved_prompt_requested{ false };

    // 未保存確認ダイアログで保存に失敗した理由。
    // ステータス行はプロジェクトタブにしか出ず、モーダルの上からは
    // 見えないため、失敗理由をここへ持ってダイアログ内に表示する。
    std::string object_scene_save_failure;

    // 未保存確認ダイアログを実際に開いているか。
    // Esc など「ボタン以外で閉じられた」ことを見分けるために持つ。
    // BeginPopupModal の戻り値だけでは、開いていないのか閉じられたのかが分からない。
    bool object_unsaved_prompt_open{ false };

    bool export_game_dialog_open{ false };
    bool export_overwrite_prompt_open{ false };
    bool export_exporting{ false };
    char export_game_name[128]{ "MyGame" };
    char export_folder[512]{};
    char export_startup_scene[512]{};
    std::filesystem::path export_pending_root;
    std::filesystem::path export_pending_scene;
    std::string export_status;
    std::vector<std::string> export_errors;

    // 操作対象を型ではなく ObjectID で持つ。
    // 人型からメカ・ドローンへ変えても GameObject のクラス型は変わらない。
    ReplayEngine::Scene::PlayerControlSystem player_control_system;

    // 【移行用】Gameplay Component から Camera / Stage 具象型を隠すための橋渡し。
    // 削除条件はそれぞれのヘッダーへ記載してある。
    CameraBasisProvider          object_camera_bridge;
    ReplayEngine::Audio::AudioSystem object_audio_system;
    GameInput::InputState game_input;
    ReplayEngine::Runtime::RuntimeSaveGameService object_save_game;

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
    // Query と Dynamics は別サービスとして維持する。
    ReplayEngine::Scene::PhysicsDynamicsWorld object_physics_dynamics_world;
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
    float            object_time_scale{ 1.0f };
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

    // render() の 1 Camera pass だけが設定する一時 Override。Scene/Prefab へ保存しない。
    // これにより複数 Runtime Camera と Editor の Front/Side/Top が、既存の
    // viewport_* 行列窓口をそのまま共有できる。
    const ReplayEngine::Components::CameraComponent* render_camera_override{ nullptr };
    bool render_matrix_override_active{ false };
    DirectX::XMFLOAT4X4 render_view_override{};
    DirectX::XMFLOAT4X4 render_projection_override{};
    DirectX::XMFLOAT3 render_eye_override{ 0.0f, 0.0f, 0.0f };
    float render_camera_aspect{ 0.0f };
    bool editor_auxiliary_views{ false };

    // 操作方法はユーザーごとの preset。Scene には保存しない。
    // Shared preset は Editor/CameraPresets、Personal preset は Saved/Editor/CameraPresets。
    std::vector<ReplayEngine::Editor::EditorCameraPreset> editor_camera_presets;
    int              active_editor_camera_preset_index{ -1 };
    bool             editor_camera_presets_loaded{ false };
    bool             show_camera_preset_manager{ false };
    bool             show_ui_focus_style_manager{ false };
    bool             gizmo_move_shortcut_was_down{ false };
    bool             gizmo_rotate_shortcut_was_down{ false };
    bool             gizmo_scale_shortcut_was_down{ false };

    // 編集カメラがマウス／キーを消費したフレーム。
    // Gizmo と選択処理を同時に走らせないためのフラグ。
    bool             editor_camera_consumed_input{ false };

    // 現在の Scene に対応する編集カメラ状態の保存キー。
    std::string      editor_camera_state_key;

    bool             enable_deferred { true };
    bool             enable_particles{ false };
    bool             enable_trail    { false };

    int toon_preset_index{ 0 };

    framework(HWND hwnd);
    ~framework();

    framework(const framework&) = delete;
    framework& operator=(const framework&) = delete;
    framework(framework&&) noexcept = delete;
    framework& operator=(framework&&) noexcept = delete;

    // 起動時に Startup Scene から Runtime を開始するか。
    //
    // 【既定を false にしてある理由】
    //   このアプリは Editor と Runtime が同じ実行ファイルに同居している。
    //   editor_mode は「Editor UI を表示しているか」を切り替えるだけのフラグで、
    //   既定が false でも Editor として起動している。
    //   ここを editor_mode で判断すると、通常の Editor 起動でも
    //   Runtime World が有効になり、編集対象が空の World にすり替わる。
    //   起動 Scene から始めるのは、明示的に要求されたときだけにする。
    void request_startup_scene_boot() noexcept
    {
        object_boot_from_startup_scene = true;
    }

    // 終了時のリソース解放を確かめるための一連の操作を実行する。
    //
    // 【Debug 限定にしている理由】
    //   確かめたいのは D3D Debug Layer が報告する Live Object。
    //   通常の Release 起動で Debug Layer や ReportLiveDeviceObjects を
    //   強制的に有効化してしまうと、製品版の動作と実行コストが変わる。
    //   Release では何もしない空実装にしてある。
    void run_shutdown_regression_scenario();

    // --validate-shutdown から呼ぶ。終了直前に 1 回だけ実行される。
    void request_shutdown_regression() noexcept
    {
        shutdown_regression_requested = true;
    }

    void set_automated_smoke_test_frames(std::uint32_t frames) noexcept
    {
        automated_smoke_test_frames = frames;
        automated_smoke_test_frames_rendered = 0;
    }

    // 再現可能な Profiler ベンチマーク。
    // Scene に保存された Runtime Camera と Motion/Script を固定 1/60 秒で進める。
    // 入力は完全抑制し、warmup 中と GPU query drain 中は履歴へ積まない。
    void configure_profile_benchmark(std::uint32_t frames,
        std::uint32_t warmup_frames, std::string output_name,
        std::uint32_t render_output)
    {
        profile_benchmark_mode = true;
        profile_benchmark_frames = (std::max)(1u, frames);
        profile_benchmark_warmup_frames = warmup_frames;
        profile_benchmark_output_name = std::move(output_name);
        profile_benchmark_render_output = (std::min)(render_output, 10u);
        profile_benchmark_frame_index = 0;
        profile_benchmark_drain_frames = 0;
        profile_benchmark_export_attempted = false;
        profile_benchmark_export_ok = false;
        profile_benchmark_gpu_drain_timeout = false;
        profile_benchmark_scene_ready = false;
        profile_benchmark_startup_failed = false;
        profile_benchmark_startup_profile_ok = false;
        profile_benchmark_initialize_stage_ms.fill(0.0);
        profile_benchmark_initialize_total_ms = 0.0;
        profile_benchmark_max_draw_calls = 0;
        profile_benchmark_max_objects = 0;
        profile_benchmark_max_components = 0;
        profile_benchmark_startup_begin = std::chrono::steady_clock::now();

        // ベンチ自体が測定対象へ混ざらないよう、Runtime専用経路へ固定する。
        // configure_standalone_game() は呼ばないため Saved/Profile はProject側のまま。
        standalone_game_mode = true;
        editor_mode = false;
        edit_mode_active = false;
        editor_session_active = false;
        show_render_stats = false;
        csharp_auto_reload = false;
        shader_auto_recompile = false;
        game_input.SetSuppressed(true);
        ReplayEngine::Rendering::Stats().SetEnabled(true);
        ReplayEngine::Rendering::Stats().SetPaused(true);
        ReplayEngine::Rendering::Stats().SetHistoryLimit(
            static_cast<std::size_t>(profile_benchmark_frames));
    }
    void configure_content_root(std::filesystem::path content_root);
