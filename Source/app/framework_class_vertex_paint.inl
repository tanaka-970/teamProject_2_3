    struct vertex_paint_mesh final
    {
        ReplayEngine::VertexPaint::MeshSurface surface;
        std::vector<DirectX::XMFLOAT3> bind_positions;
        ReplayEngine::Assets::VertexColorFingerprint fingerprint;
        std::uint32_t mesh_index = 0;
        bool visible = false;
    };
    struct vertex_paint_model final
    {
        std::shared_ptr<ReplayEngine::VertexPaint::PaintAsset> asset;
        std::vector<vertex_paint_mesh> meshes;
        std::filesystem::path path;
        std::string suffix;
        bool skinned = false;
        std::uint64_t snapshot_revision = 0;
        std::shared_ptr<const ReplayEngine::Assets::VertexColorAsset> snapshot;
    };
    bool vertex_paint_enabled = false;
    ReplayEngine::VertexPaint::Brush vertex_paint_brush;
    std::unordered_map<std::string, vertex_paint_model> vertex_paint_models;
    std::string vertex_paint_target;
    std::uint64_t vertex_paint_owner = 0;
    int vertex_paint_frame = -1;
    std::unique_ptr<ReplayEngine::VertexPaint::ColorEdit> vertex_paint_stroke;
    std::uint64_t vertex_paint_stroke_owner = 0;
    bool vertex_paint_dirty_before = false;
    std::vector<std::vector<float>> vertex_paint_values;
    std::string vertex_paint_status;
    bool vertex_paint_active() const;
    bool prepare_vertex_paint_model(const ReplayEngine::Rendering::RenderItem& item);
    void submit_vertex_paint(ReplayEngine::Rendering::DX12::D3D12StaticSceneSubmission& submission,
        const ReplayEngine::Rendering::RenderItemList& items, bool capture);
    void draw_vertex_paint_panel();
    bool handle_vertex_paint_viewport();
    void finish_vertex_paint_stroke(bool cancel);
    bool save_vertex_paint(bool reload);
