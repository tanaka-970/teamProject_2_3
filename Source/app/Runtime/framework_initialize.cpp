#include "framework.h"
#include "shader.h"
#include "texture.h"
#include "skinned_mesh.h"
#include "../../RePlayEngine/Scene/BootLogoScene.h"
#include "../../RePlayEngine/Scene/LoadingScene.h"

#include <filesystem>
#include <string>

// 【削除済み】lower_copy / find_animation_clip
//   起動時に固定のプレイヤーモデルからクリップ名を探し、
//   旧 Player の clip_idle / clip_walk / clip_jump へ割り当てるための補助だった。
//   クリップの割り当ては AnimatorComponent のプロパティ
//   (idle_clip / walk_clip / jump_clip) が持ち、Scene へ保存される。

bool framework::initialize()
{
    HRESULT hr{ S_OK };

    std::string asset_database_error;
    if (!asset_database.Load(asset_database_error))
        object_editor_context.SetStatus("AssetDatabase: " + asset_database_error);

    UINT create_device_flags{ 0 };
#ifdef _DEBUG
    create_device_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL feature_levels{ D3D_FEATURE_LEVEL_11_0 };

    DXGI_SWAP_CHAIN_DESC swap_chain_desc{};
    swap_chain_desc.BufferCount = 2;
    swap_chain_desc.BufferDesc.Width  = SCREEN_WIDTH;
    swap_chain_desc.BufferDesc.Height = SCREEN_HEIGHT;
    swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_chain_desc.BufferDesc.RefreshRate.Numerator   = 60;
    swap_chain_desc.BufferDesc.RefreshRate.Denominator = 1;
    swap_chain_desc.BufferUsage   = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc.OutputWindow  = hwnd;
    swap_chain_desc.SampleDesc.Count = 1;
    swap_chain_desc.Windowed      = !FULLSCREEN;
    swap_chain_desc.SwapEffect    = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, create_device_flags,
        &feature_levels, 1, D3D11_SDK_VERSION, &swap_chain_desc,
        swap_chain.GetAddressOf(), device.GetAddressOf(), NULL, immediate_context.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer{};
    hr = swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
        reinterpret_cast<LPVOID*>(back_buffer.GetAddressOf()));
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    hr = device->CreateRenderTargetView(back_buffer.Get(), NULL, render_target_view.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    Microsoft::WRL::ComPtr<ID3D11Texture2D> depth_stencil_buffer{};
    D3D11_TEXTURE2D_DESC td{};
    td.Width      = SCREEN_WIDTH;
    td.Height     = SCREEN_HEIGHT;
    td.MipLevels  = 1;
    td.ArraySize  = 1;
    td.Format     = DXGI_FORMAT_D24_UNORM_S8_UINT;
    td.SampleDesc.Count = 1;
    td.Usage      = D3D11_USAGE_DEFAULT;
    td.BindFlags  = D3D11_BIND_DEPTH_STENCIL;
    hr = device->CreateTexture2D(&td, NULL, depth_stencil_buffer.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    D3D11_DEPTH_STENCIL_VIEW_DESC dvd{};
    dvd.Format        = td.Format;
    dvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    hr = device->CreateDepthStencilView(depth_stencil_buffer.Get(), &dvd, depth_stencil_view.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = 0; viewport.TopLeftY = 0;
    viewport.Width  = (float)SCREEN_WIDTH;
    viewport.Height = (float)SCREEN_HEIGHT;
    viewport.MinDepth = 0.0f; viewport.MaxDepth = 1.0f;
    immediate_context->RSSetViewports(1, &viewport);

    D3D11_SAMPLER_DESC sd{};
    sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    sd.MaxAnisotropy = 16;
    sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    sd.MinLOD = 0; sd.MaxLOD = D3D11_FLOAT32_MAX;
    device->CreateSamplerState(&sd, sampler_states[(size_t)SAMPLER_STATE::POINT].GetAddressOf());
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&sd, sampler_states[(size_t)SAMPLER_STATE::LINEAR].GetAddressOf());
    sd.Filter = D3D11_FILTER_ANISOTROPIC;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    device->CreateSamplerState(&sd, sampler_states[(size_t)SAMPLER_STATE::ANISOTROPIC].GetAddressOf());
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&sd, sampler_states[(size_t)SAMPLER_STATE::ANISOTROPIC_CLAMP].GetAddressOf());

    D3D11_DEPTH_STENCIL_DESC dsd{};
    dsd.DepthEnable = TRUE; dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    device->CreateDepthStencilState(&dsd, depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_ON].GetAddressOf());
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    device->CreateDepthStencilState(&dsd, depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_OFF].GetAddressOf());
    dsd.DepthEnable = FALSE; dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    device->CreateDepthStencilState(&dsd, depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_ON].GetAddressOf());
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    device->CreateDepthStencilState(&dsd, depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].GetAddressOf());

    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = FALSE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&bd, blend_states[(size_t)BLEND_STATE::NONE].GetAddressOf());

    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    device->CreateBlendState(&bd, blend_states[(size_t)BLEND_STATE::ALPHA].GetAddressOf());

    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    device->CreateBlendState(&bd, blend_states[(size_t)BLEND_STATE::ADD].GetAddressOf());

    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO; bd.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_COLOR;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_DEST_ALPHA; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    device->CreateBlendState(&bd, blend_states[(size_t)BLEND_STATE::MULTIPLY].GetAddressOf());

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_BACK;
    rd.FrontCounterClockwise = TRUE; rd.DepthClipEnable = TRUE;
    device->CreateRasterizerState(&rd, rasterizer_states[(size_t)RASTER_STATE::SOLID].GetAddressOf());
    rd.FillMode = D3D11_FILL_WIREFRAME;
    device->CreateRasterizerState(&rd, rasterizer_states[(size_t)RASTER_STATE::WIREFRAME].GetAddressOf());
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE;
    device->CreateRasterizerState(&rd, rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].GetAddressOf());
    rd.FillMode = D3D11_FILL_WIREFRAME;
    device->CreateRasterizerState(&rd, rasterizer_states[(size_t)RASTER_STATE::WIREFRAME_CULL_NONE].GetAddressOf());

    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = sizeof(scene_constants);
    cbd.Usage = D3D11_USAGE_DEFAULT; cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    device->CreateBuffer(&cbd, nullptr, constant_buffers[0].GetAddressOf());

    framebuffers[0] = std::make_unique<framebuffer>(device.Get(), SCREEN_WIDTH,     SCREEN_HEIGHT);
    bit_block_transfer = std::make_unique<fullscreen_quad>(device.Get());

    pbr.initialize(device.Get());

    // 追加描画パス用シェーダーと材質定数バッファを準備する。
    create_ps_from_cso(device.Get(), "static_mesh_unlit_ps.cso", static_mesh_unlit_ps.GetAddressOf());
    create_ps_from_cso(device.Get(), "skinned_mesh_unlit_ps.cso", skinned_mesh_unlit_ps.GetAddressOf());
    create_ps_from_cso(device.Get(), "static_mesh_gbuffer_ps.cso", static_mesh_gbuffer_ps.GetAddressOf());
    create_ps_from_cso(device.Get(), "skinned_mesh_gbuffer_ps.cso", skinned_mesh_gbuffer_ps.GetAddressOf());
    create_ps_from_cso(device.Get(), "object_pixelate_ps.cso", object_pixelate_ps.GetAddressOf());
    create_ps_from_cso(device.Get(), "skinned_mesh_stylized_character_ps.cso",
        skinned_stylized_character_ps.GetAddressOf());
    create_ps_from_cso(device.Get(), "static_mesh_stylized_character_ps.cso",
        static_stylized_character_ps.GetAddressOf());
    cbd.ByteWidth = sizeof(material_override_constants);
    device->CreateBuffer(&cbd, nullptr, material_override_cb.GetAddressOf());
    cbd.ByteWidth = sizeof(ReplayEngine::Rendering::ShaderLayerGpuData);
    device->CreateBuffer(&cbd, nullptr, shader_layer_cb.GetAddressOf());
    cbd.ByteWidth = sizeof(ReplayEngine::Rendering::CharacterMaterialGpuData);
    device->CreateBuffer(&cbd, nullptr, character_material_cb.GetAddressOf());

    toon.initialize(device.Get());
    csm.initialize(device.Get());
    test_trail.initialize(device.Get());
    particles.initialize(device.Get());
    post_process.Initialize(device.Get());
    bloom_pass.Initialize(device.Get(), SCREEN_WIDTH, SCREEN_HEIGHT);
    enable_deferred = deferred.initialize(device.Get(), SCREEN_WIDTH, SCREEN_HEIGHT);

    // SSAO/SSR/TAAが共有するフレーム定数バッファ(b9)。
    cbd.ByteWidth = sizeof(ReplayEngine::Rendering::FrameConstants);
    device->CreateBuffer(&cbd, nullptr, frame_constants_cb.GetAddressOf());
    ssao_pass.Initialize(device.Get(), SCREEN_WIDTH, SCREEN_HEIGHT);
    ssr_pass.Initialize(device.Get(), SCREEN_WIDTH, SCREEN_HEIGHT);
    taa_pass.Initialize(device.Get(), SCREEN_WIDTH, SCREEN_HEIGHT);
    tiled_deferred.Initialize(device.Get(), SCREEN_WIDTH, SCREEN_HEIGHT);
    // ポリゴン数計測用のパイプライン統計クエリ。
    ReplayEngine::Rendering::Stats().Initialize(device.Get());
    lights.initialize(device.Get());
    uiManager.Initalize(device.Get());
    lights.data.light_counts = { 0, 0, 0, 0 };

    // 法線テクスチャを持たない材質で使うダミー法線を作る。kwjkshhakjwhhwhhsbkkwhiiwnzkkhjsowjjw

    //iiwjjwjisu
    {
        uint8_t pixel[4] = { 128, 128, 255, 255 };
        D3D11_TEXTURE2D_DESC td2{};
        td2.Width = 1; td2.Height = 1;
        td2.MipLevels = 1; td2.ArraySize = 1;
        td2.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td2.SampleDesc.Count = 1;
        td2.Usage = D3D11_USAGE_DEFAULT;
        td2.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{ pixel, 4, 0 };
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        hr = device->CreateTexture2D(&td2, &init, tex.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
        hr = device->CreateShaderResourceView(tex.Get(), nullptr, dummy_normal_srv.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
    }

    // ロゴ、ロード、ゲームの順に進め、重いモデルと画像はロードシーン内で生成する。
    // これによりロゴ表示前の起動停止を防ぐ。
    scene_manager.SetScene(
        std::make_unique<ReplayEngine::Scene::BootLogoScene>(), device.Get());
    auto loading_scene = std::make_unique<ReplayEngine::Scene::LoadingScene>();
    // 任意アセットの読み込みは「無ければスキップして続行」に統一する。
    // 実行に必須ではないファイルの不足で起動が止まらないようにするため。
    // 失敗は OutputDebugString へ理由付きで出す（Visual Studio の出力ウィンドウで読める）。
    loading_scene->AddTask("UI image", [this]
    {
        const wchar_t* ui_image = L".\\resources\\screenshot.jpg";
        std::error_code filesystem_error;
        if (!std::filesystem::exists(ui_image, filesystem_error) || filesystem_error)
        {
            OutputDebugStringW(L"[Assets] 背景画像が見つかりません: "
                L".\\resources\\screenshot.jpg （背景表示は無効のまま続行します）\n");
            sprite_batches[0].reset();
            draw_background_image = false;
            return true;
        }
        sprite_batches[0] = std::make_unique<sprite_batch>(device.Get(), ui_image, 1);
        return true;
    });
    // キャラクターモデルは SkinnedMeshRendererComponent の
    // mesh_asset (AssetGUID) が指し、resolve_object_mesh() が読み込む。
    loading_scene->AddTask("Debug mesh", [this]
    {
        // static_mesh は .obj 専用。構築前に can_load で検証し、
        // 失敗を assert ではなくログとして上へ返す。
        const wchar_t* debug_mesh = L".\\resources\\cube.obj";
        std::wstring reason;
        if (!static_mesh::can_load(debug_mesh, &reason))
        {
            OutputDebugStringW((L"[Assets] デバッグ用メッシュを読み込めません: " +
                reason + L" （静的メッシュ表示は無効のまま続行します）\n").c_str());
            static_meshes[0].reset();
            enable_static_meshes = false;
            return true;
        }

        auto candidate = std::make_unique<static_mesh>(device.Get(), debug_mesh, true);
        if (!candidate->is_loaded())
        {
            OutputDebugStringW((L"[Assets] デバッグ用メッシュの解析に失敗しました: " +
                candidate->load_error() + L"\n").c_str());
            static_meshes[0].reset();
            enable_static_meshes = false;
            return true;
        }
        static_meshes[0] = std::move(candidate);
        return true;
    });
    loading_scene->AddTask("IBL images", [this]
    {
        pbr.load_ibl(device.Get(), L".\\resources\\ibl\\diffuse_iem.dds",
            L".\\resources\\ibl\\specular_pmrem.dds",
            L".\\resources\\ibl\\lut_ggx.dds");
        return true;
    });
    // AssetDatabaseのモデルは1件ずつ独立したタスクにして、ロード画面の
    // ワーカー群へそのまま流す。セッション復元やステージ切り替えは
    // ConcurrentResourceCacheのヒットで待たされなくなる。
    for (const auto& record : asset_database.Records())
    {
        if (record.kind != ReplayEngine::Assets::AssetKind::Model) continue;
        const std::filesystem::path source = record.source_path;
        loading_scene->AddTask("Prewarm " + record.display_name, [this, source]
        {
            prewarm_model_asset(source);
            // 先読みは最適化なので、失敗してもロード全体は成功扱いにする。
            return true;
        });
    }
    scene_manager.QueueScene(std::move(loading_scene), device.Get());

    scene_manager.QueueSceneFactory([this]() -> std::unique_ptr<ReplayEngine::Scene::IScene>
    {
        // GameScene が持つのはカメラ操作だけ。
        // 操作キャラクターのモデルもアニメーションクリップも渡さない。
        // それらは Scene 内の GameObject が Component として持っている。
        auto next_scene = std::make_unique<GameScene>(
            static_cast<float>(SCREEN_WIDTH) / static_cast<float>(SCREEN_HEIGHT));
        game_scene = next_scene.get();
        restore_editor_session();
        return next_scene;
    });

    // GameObject / Component 基盤の初期化。
    // Component 型の登録・編集用 Scene の準備・既定 Scene ファイルの読み込みを行う。
    // AssetDatabase の読み込み後に呼ぶ必要がある（Asset 参照を解決するため）。
    initialize_object_scene();

    return true;
}
