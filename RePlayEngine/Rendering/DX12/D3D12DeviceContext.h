#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include "D3D12DescriptorHeapAllocator.h"
#include "D3D12Diagnostics.h"
#include "D3D12FrameConstants.h"
#include "D3D12FrameResource.h"
#include "D3D12MeshBuffer.h"
#include "D3D12RenderItemBatch.h"
#include "D3D12ResourceStateTracker.h"
#include "D3D12ScreenBounds.h"
#include "D3D12ShaderCompiler.h"
#include "D3D12UploadContext.h"
#include "../../UI/Effects/UIEffect.h"

#include <DirectXMath.h>

#include <cstdint>
#include <array>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef USE_IMGUI
struct ImDrawData;
#endif

namespace ReplayEngine::Rendering::DX12
{
    // Deferred GBuffer の枚数はここだけで決める。RT を足すときはこの値を増やす。
    inline constexpr std::uint32_t kScene3DGBufferCount = 6;

    struct D3D12StaticVertex final
    {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 normal{ 0.0f, 1.0f, 0.0f };
        DirectX::XMFLOAT2 texcoord{};
    };

    static_assert(sizeof(D3D12StaticVertex) == 32,
        "DX12 static vertex ABI must stay position/normal/texcoord (32 bytes).");

    struct D3D12StaticMeshSource final
    {
        // Submission は DrawStaticScene が消費するまで Cache Miss の Geometry を所有する。
        // 旧 CPU Mesh を必要時に変換する際の Dangling Pointer を防ぐ。
        std::string key;
        std::vector<D3D12StaticVertex> vertices;
        std::vector<std::uint32_t> indices;
        // 同じFrame slotの動的Line/Trailを再アップロードするときだけ置換する。
        // BeginFrameが該当slotのFenceを待った後なので、GPU使用中のResourceを解放しない。
        bool replace_existing = false;
    };

    struct D3D12StaticTextureSource final
    {
        std::string key;
        std::filesystem::path source_path;
        std::vector<std::uint8_t> rgba;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    // Custom Surface Shader の正本は既存の ShaderCatalog/PropertySchema とする。
    // Framework は Source Path と生成済み b9/t40+ 宣言だけを渡し、DX12 Backend が
    // DXC Compile と PSO Cache を担当する。
    struct D3D12StaticShaderSource final
    {
        std::string key;
        std::filesystem::path source_path;
        std::string generated_declaration;
    };

    struct D3D12StaticMaterialTexture final
    {
        std::uint32_t slot = 0;
        std::string texture_key;
    };

    enum class D3D12StaticAlphaMode : std::uint32_t
    {
        Opaque = 0,
        Mask = 1,
        Blend = 2,
    };

    struct D3D12StaticDrawItem final
    {
        std::string mesh_key;
        std::uint64_t owner_id = 0;
        std::uint32_t material_slot = 0;
        D3D12MeshLocalBounds material_bounds;
        std::uint32_t rendering_layer = 0;
        // Object + mesh/subset の安定キー。Static Transform の motion vector 履歴に使う。
        std::string motion_key;
        std::string base_color_texture_key;
        // 空文字なら Phase 2 Material Bridge の Pixel Shader を使う。空でない Key は、
        // Static Phase 2 Root Signature に適合する場合に DXC Compile 済みの
        // ShaderCatalog Surface Shader を使い、Custom PSO の失敗時は安全に戻す。
        std::string shader_key;
        std::vector<std::uint8_t> material_constants;
        std::vector<D3D12StaticMaterialTexture> material_textures;
        // ResolvedMaterialBinding::TextureSemantic の bit mask。
        // slot番号だけで Toon RampMap 等を NormalMap と誤認しないために使う。
        std::uint32_t material_texture_semantic_mask = 0;
        // BuiltIn シェーダの固有表現。x=効果ID、y/z/w=引数。0 なら何もしない。
        DirectX::XMFLOAT4 builtin_params{ 0.0f, 0.0f, 0.0f, 0.0f };
        // Toon の追加枠。rgb=ShadowTint、w=RimPower。既定は効果オフ。
        DirectX::XMFLOAT4 builtin_params1{ 0.0f, 0.0f, 0.0f, 0.0f };
        // Toon の追加枠。rgb=RimColor、w=SpecularPower。
        DirectX::XMFLOAT4 builtin_params2{ 0.0f, 0.0f, 0.0f, 1.0f };
        // Toon の追加枠。rgb=SpecularTint、w=予約。
        DirectX::XMFLOAT4 builtin_params3{ 0.0f, 0.0f, 0.0f, 0.0f };
        std::uint32_t start_index = 0;
        std::uint32_t index_count = 0; // 0 は Cache 済み Index Buffer 全体を描画する。
        DirectX::XMFLOAT4X4 world{
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1 };
        DirectX::XMFLOAT4 base_color{ 1, 1, 1, 1 };
        DirectX::XMFLOAT4 vertex_tint{ 1, 1, 1, 1 };
        DirectX::XMFLOAT3 emissive{ 0, 0, 0 };
        float emissive_strength = 0.0f;
        float metallic = 0.0f;
        float roughness = 0.55f;
        float ambient_occlusion = 1.0f;
        float alpha_cutoff = 0.5f;
        D3D12StaticAlphaMode alpha_mode = D3D12StaticAlphaMode::Opaque;
        std::int32_t lighting_model = 0; // ShaderLightingModel::Pbr
        bool double_sided = false;
        bool cast_shadow = true;
        bool receive_shadow = true;
    };

    struct D3D12SkinnedVertex final
    {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 normal{ 0.0f, 1.0f, 0.0f };
        DirectX::XMFLOAT4 tangent{ 1.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT2 texcoord{};
        DirectX::XMFLOAT4 bone_weights{ 1.0f, 0.0f, 0.0f, 0.0f };
        std::uint32_t bone_indices[4]{};
        DirectX::XMFLOAT3 morph_position{};
        DirectX::XMFLOAT3 morph_normal{};
    };

    static_assert(sizeof(D3D12SkinnedVertex) == 104,
        "DX12 skinned vertex ABI must match the existing skinned_mesh::vertex payload.");

    struct D3D12SkinnedMeshSource final
    {
        std::string key;
        std::vector<D3D12SkinnedVertex> vertices;
        std::vector<std::uint32_t> indices;
    };

    struct D3D12SkinnedDrawItem final
    {
        D3D12StaticDrawItem surface;
        // owner + mesh/subset を含む安定キー。前フレームの Bone/World 履歴に使う。
        std::string motion_key;
        // CPU Animator が確定した姿勢から生成した可変長 Bone Palette。
        // 256/600 などの固定長定数バッファにはしない。
        std::vector<DirectX::XMFLOAT4X4> bone_palette;
        float morph_weight = 0.0f;
    };

    struct D3D12DirectionalLightSubmission final
    {
        DirectX::XMFLOAT3 direction{ 0.0f, -1.0f, 0.0f };
        float intensity = 0.0f;
        DirectX::XMFLOAT3 color{ 1.0f, 1.0f, 1.0f };
        float shadow_strength = 1.0f;
        bool enabled = false;
        bool cast_shadows = false;
    };

    struct D3D12PointLightSubmission final
    {
        DirectX::XMFLOAT3 position{};
        float range = 1.0f;
        DirectX::XMFLOAT3 color{ 1.0f, 1.0f, 1.0f };
        float intensity = 1.0f;
        bool cast_shadows = false;
        float shadow_strength = 1.0f;
        std::int32_t shadow_slice = -1;
    };

    struct D3D12SpotLightSubmission final
    {
        DirectX::XMFLOAT3 position{};
        float range = 1.0f;
        DirectX::XMFLOAT3 direction{ 0.0f, 0.0f, 1.0f };
        float inner_cos = 0.95f;
        DirectX::XMFLOAT3 color{ 1.0f, 1.0f, 1.0f };
        float outer_cos = 0.85f;
        float intensity = 1.0f;
        bool cast_shadows = false;
        float shadow_strength = 1.0f;
        std::int32_t shadow_slice = -1;
    };

    struct D3D12DirectionalShadowSubmission final
    {
        static constexpr std::uint32_t CascadeCount = 4;
        DirectX::XMFLOAT4X4 view_projection[CascadeCount]{};
        DirectX::XMFLOAT4 split_distances{};
        DirectX::XMFLOAT4 params{};
        DirectX::XMFLOAT4 params2{};
        DirectX::XMFLOAT4 params3{};
        DirectX::XMFLOAT4 texel_world{};
        std::uint32_t resolution = 2048;
        bool enabled = false;
    };

    struct D3D12LocalShadowSliceSubmission final
    {
        DirectX::XMFLOAT4X4 view_projection{};
        DirectX::XMFLOAT4 params{};
    };

    struct D3D12LocalShadowSubmission final
    {
        static constexpr std::uint32_t SliceCount = 16;
        D3D12LocalShadowSliceSubmission slices[SliceCount]{};
        std::uint32_t used_slice_mask = 0;
        std::uint32_t resolution = 1024;
        bool enabled = false;
    };

    struct D3D12PostProcessSubmission final
    {
        float exposure = 0.619f;
        float bloom_intensity = 0.25f;
        float bloom_threshold = 1.0f;
        float vignette_strength = 0.138f;
        float fxaa_enable = 1.0f;
        float taa_blend = 0.88f;
        float ssao_strength = 1.0f;
        float ssr_strength = 1.0f;
        DirectX::XMFLOAT4 color_filter{ 1, 1, 1, 1 };
        DirectX::XMFLOAT4 ssao_params0{ 0.75f, 1.6f, 1.0f, 0.35f };
        DirectX::XMFLOAT4 ssao_params1{ 4.0f, 8.0f, 60.0f, 140.0f };
        DirectX::XMFLOAT4 ssao_params2{ 1.0f, 1.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT4 ssr_params0{ 40.0f, 0.55f, 3.0f, 32.0f };
        DirectX::XMFLOAT4 ssr_params1{ 4.0f, 0.6f, 0.08f, 0.001f };
        DirectX::XMFLOAT4 ssr_params2{ 0.0f, 1.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT4 taa_params0{ 1.0f, 0.0f, 48.0f, 0.0f };
        std::uint32_t render_output = 0;
        std::uint32_t deferred_debug_mode = 0;
        bool bloom_enabled = true;
        bool vignette_enabled = false;
        bool fxaa_enabled = true;
        bool taa_enabled = true;
        bool ssao_enabled = true;
        bool ssr_enabled = true;
        bool luminance_enabled = true;
        bool final_pass_enabled = true;
    };

    struct D3D12StaticSceneSubmission final
    {
        std::vector<D3D12StaticMeshSource> mesh_sources;
        std::vector<D3D12SkinnedMeshSource> skinned_mesh_sources;
        std::vector<D3D12StaticTextureSource> texture_sources;
        std::vector<D3D12StaticShaderSource> shader_sources;
        std::vector<D3D12StaticDrawItem> draws;
        std::vector<D3D12SkinnedDrawItem> skinned_draws;
        D3D12DirectionalLightSubmission directional_light{};
        std::vector<D3D12PointLightSubmission> point_lights;
        std::vector<D3D12SpotLightSubmission> spot_lights;
        D3D12DirectionalShadowSubmission directional_shadow{};
        D3D12LocalShadowSubmission local_shadows{};
        D3D12PostProcessSubmission post_process{};
        DirectX::XMFLOAT4 background_color{ 0, 0, 0, 1 };
    };

    struct D3D12Scene3DDrawOptions final
    {
        bool manage_shadow_targets = true;
        bool allow_static_mesh_cache_replacement = true;
        bool read_motion_history = true;
        bool write_motion_history = true;
        bool read_scene_history = true;
        bool write_scene_history = true;
    };

    struct D3D12Scene3DStateSnapshot final
    {
        std::size_t static_mesh_cache_size = 0;
        std::size_t skinned_mesh_cache_size = 0;
        std::size_t texture_cache_size = 0;
        std::size_t static_mesh_bounds_cache_size = 0;
        std::size_t skinned_mesh_bounds_cache_size = 0;
        std::size_t motion_history_size = 0;
        std::uint64_t motion_frame_serial = 0;
        std::uint64_t scene_history_write_serial = 0;
        std::uint64_t scene_effect_history_write_serial = 0;
        std::size_t scene_effect_history_size = 0;
        bool scene_history_valid = false;
        std::uintptr_t gbuffer_resources[kScene3DGBufferCount]{};
        std::uintptr_t depth_resource = 0;
        std::uintptr_t history_resource = 0;
        std::uintptr_t directional_shadow_resource = 0;
        std::uintptr_t local_shadow_resource = 0;
        D3D12_RESOURCE_STATES gbuffer_states[kScene3DGBufferCount]{};
        D3D12_RESOURCE_STATES depth_state = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES history_state = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES directional_shadow_state = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES local_shadow_state = D3D12_RESOURCE_STATE_COMMON;
        std::uint32_t directional_shadow_resolution = 0;
        std::uint32_t local_shadow_resolution = 0;
        D3D12FrameConstants frame_constants{};
    };

    enum class D3D12UIBlendMode : std::uint32_t
    {
        Alpha = 0,
        Additive = 1,
        Multiply = 2,
        Screen = 3,
        Premultiplied = 4,
    };

    struct D3D12UIVertex final
    {
        DirectX::XMFLOAT2 position{};
        DirectX::XMFLOAT2 uv{};
        DirectX::XMFLOAT4 color{ 1, 1, 1, 1 };
        DirectX::XMFLOAT4 uv_bounds{ 0, 0, 1, 1 };
    };

    struct D3D12UIVisualConstants final
    {
        DirectX::XMFLOAT4 screen_size{ 1, 1, 0, 0 };
        DirectX::XMFLOAT4 fill_color_2{ 1, 1, 1, 1 };
        DirectX::XMFLOAT4 fill_color_3{ 1, 1, 1, 1 };
        DirectX::XMFLOAT4 fill_color_4{ 1, 1, 1, 1 };
        // x/y/zは色2/3/4の位置。y/zが負なら色3/4は未設定。
        DirectX::XMFLOAT4 fill_stops{ 1, -1, -1, 0 };
        DirectX::XMFLOAT4 stroke_color_2{ 1, 1, 1, 1 };
        // xはStroke方式。残りは将来の線端・結合方式用。
        DirectX::XMFLOAT4 stroke_parameters{ 0, 0, 0, 0 };
        // xはShape種別、yはText/SDF、zはOutline幅、wは予約領域。
        DirectX::XMFLOAT4 mode{ 0, 0, 0, 0 };
        DirectX::XMFLOAT4 outline_color{ 0, 0, 0, 1 };
        DirectX::XMFLOAT4 shadow_offset{ 0, 0, 0, 0 };
        DirectX::XMFLOAT4 shadow_color{ 0, 0, 0, 0 };
        // x/yはAtlas寸法、zはSDF spread、wは予約領域。
        DirectX::XMFLOAT4 atlas_size{ 2048, 2048, 8, 0 };
        // xはGradient角度、y/zは中心、wはFill方式。
        DirectX::XMFLOAT4 fill_parameters{ 0, 0.5f, 0.5f, 0 };
        // xはClip形状、yは反転、zはFeather画素、wは正規化角丸半径。
        DirectX::XMFLOAT4 clip_parameters{ 0, 0, 0, 0 };
        // 任意Clipの画素座標left/top/right/bottom。
        DirectX::XMFLOAT4 clip_bounds{ 0, 0, 0, 0 };
        // xはMask数、yは反転。
        DirectX::XMFLOAT4 mask_parameters{ 0, 0, 0, 0 };
        // Atlas内Matte領域のUV offset/scale。
        DirectX::XMFLOAT4 mask_uv{ 0, 0, 1, 1 };
        // 最大4枚のTrack Matte UVと合成方式・Lumaフラグ。
        DirectX::XMFLOAT4 mask_uvs[4]{
            { 0, 0, 1, 1 }, { 0, 0, 1, 1 },
            { 0, 0, 1, 1 }, { 0, 0, 1, 1 } };
        DirectX::XMFLOAT4 mask_operations{ 0, 0, 0, 0 };
        DirectX::XMFLOAT4 mask_luma{ 0, 0, 0, 0 };
        // xは辺数、yは星の内径、zは画面上の回転角、wは予約領域。
        DirectX::XMFLOAT4 clip_shape{ 0, 0, 0, 0 };
        // 画面座標を各Matteの0..1矩形へ戻す原点と逆行列。
        DirectX::XMFLOAT4 mask_origins[4]{};
        DirectX::XMFLOAT4 mask_inverses[4]{};
        DirectX::XMFLOAT4 mask_inverts{ 0, 0, 0, 0 };
        DirectX::XMFLOAT4 mask_rotated{ 0, 0, 0, 0 };
        // 形状Clipは最大4階層を積み、全てのalphaを交差させる。
        DirectX::XMFLOAT4 clip_state{ 0, 0, 0, 0 };
        DirectX::XMFLOAT4 clip_parameters_extra[3]{};
        DirectX::XMFLOAT4 clip_bounds_extra[3]{};
        DirectX::XMFLOAT4 clip_shapes_extra[3]{};
        DirectX::XMFLOAT4X4 world_canvas_matrix{
            1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        DirectX::XMFLOAT4X4 world_view_projection{
            1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        // xはWorld Space有効、y/zはCanvas平面幅・高さ。
        DirectX::XMFLOAT4 world_canvas_parameters{ 0, 0, 0, 0 };
        // Scene Viewを含む描画領域left/top/width/height。
        DirectX::XMFLOAT4 world_viewport{ 0, 0, 1, 1 };
    };

    static_assert(sizeof(D3D12UIVisualConstants) % 16 == 0);

    struct D3D12UIFontAtlasSource final
    {
        std::string key;
        std::vector<std::uint8_t> rgba;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint64_t revision = 0;
    };

    struct D3D12UIClip final
    {
        DirectX::XMFLOAT4 bounds{};
        DirectX::XMFLOAT4 parameters{};
        DirectX::XMFLOAT4 shape{};
    };

    struct D3D12UIMask final
    {
        std::string texture_key;
        bool luma = false;
        bool invert = false;
        bool rotated = false;
        std::int32_t operation = 0; // 0=加算、1=減算、2=交差。
        DirectX::XMFLOAT4 uv{ 0, 0, 1, 1 };
        DirectX::XMFLOAT2 screen_origin{ 0, 0 };
        // x/yがlocal X、z/wがlocal Yを求める逆行列の各行。
        DirectX::XMFLOAT4 screen_inverse{ 1, 0, 0, 1 };
    };

    struct D3D12UIBatch final
    {
        std::vector<D3D12UIVertex> vertices;
        std::string texture_key;
        D3D12UIVisualConstants constants{};
        D3D12UIBlendMode blend = D3D12UIBlendMode::Alpha;
        D3D12_RECT scissor{ 0, 0, 0, 0 };
        bool scissor_enabled = false;
        D3D12UIClip clip{};
        std::array<D3D12UIClip, 4> clips{};
        std::uint32_t clip_count = 0;
        bool clip_enabled = false;
        std::array<D3D12UIMask, 4> masks{};
        std::uint32_t mask_count = 0;
        bool mask_enabled = false;
        // -1は直接描画。それ以外は所属するEffect Groupの番号。
        std::int32_t effect_group = -1;
    };

    struct D3D12UIEffectCommand final
    {
        // UIEffectKind と同じ連番。Sceneの保存値を変換せずPSO選択へ使う。
        std::uint32_t kind = 0;
        float radius = 0.0f;
        float intensity = 1.0f;
        float threshold = 0.0f;
        float amount = 1.0f;
        float angle = 0.0f;
        float progress = 0.0f;
        float softness = 0.0f;
        float speed = 0.0f;
        float seed = 0.0f;
        float time = 0.0f;
        std::int32_t waveform = 0;
        std::string custom_shader;
        Reflection::PropertyBag custom_parameters;
        DirectX::XMFLOAT2 direction{ 0.0f, 0.0f };
        DirectX::XMFLOAT4 color{ 1, 1, 1, 1 };
        DirectX::XMFLOAT4 color_2{ 1, 1, 1, 1 };
        DirectX::XMFLOAT4 color_3{ 1, 1, 1, 1 };
        DirectX::XMFLOAT4 color_4{ 1, 1, 1, 1 };
        DirectX::XMFLOAT4 color_stops{ 0.333333f, 0.666667f, 1.0f, 0.0f };
        std::string auxiliary_texture_key;
        bool temporal = false;
        std::uint64_t history_key = 0;
        bool brush_atlas = false;
        DirectX::XMFLOAT4 brush_pattern_settings{};
        std::array<DirectX::XMFLOAT4, 4> brush_pattern_weights{};
        bool region_enabled = false;
        std::string region_mask_texture_key;
        DirectX::XMFLOAT4 effect_region_params{ 0.5f, 0.5f, 0.5f, 0.5f };
        DirectX::XMFLOAT4 effect_region_settings{ 0, 0, 1, -1 };
        std::array<DirectX::XMFLOAT4, 7> effect_region_extra_params{};
        std::array<DirectX::XMFLOAT4, 7> effect_region_extra_settings{};
        DirectX::XMFLOAT4 effect_region_count{};
        std::array<DirectX::XMFLOAT4, 8> effect_region_path_counts{};
        std::array<std::array<DirectX::XMFLOAT4, 32>, 8>
            effect_region_path_points{};
    };

    struct D3D12UIEffectConstants final
    {
        DirectX::XMFLOAT4 effect_color{ 1, 1, 1, 1 };
        DirectX::XMFLOAT4 effect_params0{};
        DirectX::XMFLOAT4 effect_params1{};
        DirectX::XMFLOAT4 effect_params2{};
        DirectX::XMFLOAT4 target_size{ 1, 1, 1, 1 };
        DirectX::XMFLOAT4 effect_color_2{ 1, 1, 1, 1 };
        DirectX::XMFLOAT4 effect_color_3{ 1, 1, 1, 1 };
        DirectX::XMFLOAT4 effect_color_4{ 1, 1, 1, 1 };
        DirectX::XMFLOAT4 effect_color_stops{ 0.333333f, 0.666667f, 1, 0 };
        DirectX::XMFLOAT4 effect_params3{};
        DirectX::XMFLOAT4 brush_pattern_settings{};
        std::array<DirectX::XMFLOAT4, 4> brush_pattern_weights{};
        DirectX::XMFLOAT4 effect_region_params{ 0.5f, 0.5f, 0.5f, 0.5f };
        DirectX::XMFLOAT4 effect_region_settings{ 0, 0, 1, -1 };
        std::array<DirectX::XMFLOAT4, 7> effect_region_extra_params{};
        std::array<DirectX::XMFLOAT4, 7> effect_region_extra_settings{};
        DirectX::XMFLOAT4 effect_region_count{};
        std::array<DirectX::XMFLOAT4, 8> effect_region_path_counts{};
        std::array<std::array<DirectX::XMFLOAT4, 32>, 8>
            effect_region_path_points{};
    };

    struct D3D12UIEffectGroup final
    {
        std::uint32_t first_batch = 0;
        std::uint32_t batch_count = 0;
        std::vector<D3D12UIEffectCommand> effects;
        bool capture_backdrop = false;
        D3D12_RECT composite_scissor{ 0, 0, 0, 0 };
        bool composite_scissor_enabled = false;
        // 0 = Self、1 = Subtree。診断と検証でも保存値をそのまま使う。
        std::int32_t target_scope = 0;
    };

    struct D3D12UIFrame final
    {
        std::uint32_t target_width = 0;
        std::uint32_t target_height = 0;
        DirectX::XMFLOAT4 clear_color{ 0, 0, 0, 0 };
        std::vector<D3D12StaticTextureSource> texture_sources;
        std::vector<D3D12UIFontAtlasSource> font_atlases;
        std::vector<D3D12UIBatch> batches;
        std::uint32_t draw_commands = 0;
        std::uint32_t vertex_count = 0;
        std::uint32_t texture_count = 0;
        std::uint32_t mask_depth = 0;
        std::uint32_t clipped_commands = 0;
        std::vector<D3D12UIEffectCommand> effects;
        std::vector<D3D12UIEffectGroup> effect_groups;
        bool requires_offscreen = false;
        bool capture_backdrop = false;
        bool preserve_output = false;
        bool background_only = false;
        // Screen Effect Stack の Temporal 履歴は Runtime UI と別の表で持つ。
        bool scene_effect_history = false;
    };

    struct D3D12OffscreenTarget final
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> color;
        Microsoft::WRL::ComPtr<ID3D12Resource> depth;
        D3D12DescriptorAllocation rtv{};
        D3D12DescriptorAllocation dsv{};
        D3D12DescriptorAllocation srv{};
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

        bool IsValid() const noexcept
        {
            return color != nullptr && depth != nullptr && rtv.IsValid() &&
                dsv.IsValid() && srv.IsValid() && width != 0 && height != 0;
        }
    };

    struct D3D12ModelEffectStackSubmission final
    {
        std::uint64_t owner_id = 0;
        std::uint32_t target_slot_mask = 0xFFFFFFFFu;
        std::int32_t depth_mode = 0;
        bool isolate_from_scene = true;
        D3D12_RECT scissor{ 0, 0, 0, 0 };
        bool scissor_enabled = false;
        std::vector<D3D12UIEffectCommand> effects;
    };

    struct D3D12ScreenEffectStackSubmission final
    {
        std::uint64_t owner_id = 0;
        std::int32_t apply_stage = 0;
        std::int32_t target_mode = 0;
        std::uint32_t target_rendering_layer_mask = 0;
        std::vector<D3D12UIEffectCommand> effects;
    };

    struct D3D12SceneEffectSubmission final
    {
        std::vector<D3D12StaticTextureSource> texture_sources;
        std::vector<D3D12ModelEffectStackSubmission> model_effects;
        std::vector<D3D12ScreenEffectStackSubmission> screen_effects;

        void Clear()
        {
            texture_sources.clear();
            model_effects.clear();
            screen_effects.clear();
        }
    };

    class D3D12DeviceContext final
    {
    public:
        static constexpr std::uint32_t FrameCount = 2;
        static constexpr std::uint64_t FrameUploadCapacity = 8ull * 1024ull * 1024ull;

        D3D12DeviceContext() = default;
        ~D3D12DeviceContext();

        D3D12DeviceContext(const D3D12DeviceContext&) = delete;
        D3D12DeviceContext& operator=(const D3D12DeviceContext&) = delete;

        bool Initialize(HWND window, std::uint32_t width, std::uint32_t height,
            bool enable_debug_layer = false, bool force_warp = false,
            bool create_validation_resources = true,
            bool enable_gpu_validation = false,
            bool enable_dred = false) noexcept;
        void Shutdown() noexcept;
        bool Resize(std::uint32_t width, std::uint32_t height) noexcept;

        bool BeginFrame(const float clear_color[4]) noexcept;
        bool SubmitFrameConstants(const D3D12FrameConstants& constants) noexcept;
        bool SubmitRenderItems(
            const ::ReplayEngine::Rendering::RenderItemList& items) noexcept;
        bool DrawStaticScene(const D3D12StaticSceneSubmission& submission) noexcept;
        // Scene 3D: Static + Skinned + GBuffer + Deferred + Forward Transparent。
        bool DrawScene3D(const D3D12StaticSceneSubmission& submission,
            D3D12Scene3DDrawOptions options = {}) noexcept;
        bool PreloadScene3DResources(const D3D12StaticSceneSubmission& submission,
            bool allow_static_mesh_cache_replacement = false) noexcept;
        D3D12Scene3DStateSnapshot CaptureScene3DState() const noexcept;
        bool CacheMeshLocalBounds(const D3D12StaticSceneSubmission& submission,
            bool allow_static_mesh_cache_replacement = true) noexcept;
        bool GetStaticMeshLocalBounds(const std::string& key,
            D3D12MeshLocalBounds& bounds) const noexcept;
        bool GetSkinnedMeshLocalBounds(const std::string& key,
            D3D12MeshLocalBounds& bounds) const noexcept;
        std::size_t StaticMeshBoundsCacheSize() const noexcept
        {
            return static_mesh_bounds_cache_.size();
        }
        std::size_t SkinnedMeshBoundsCacheSize() const noexcept
        {
            return skinned_mesh_bounds_cache_.size();
        }
        // Runtime Canvas のCPUコマンドを、Scene3D/PostProcess後の同じBack Bufferへ記録する。
        bool DrawRuntimeUI(const D3D12UIFrame& frame) noexcept;
        void SetSceneEffects(D3D12SceneEffectSubmission submission)
        {
            scene_effect_submission_ = std::move(submission);
        }
        // Editor Canvas Preview。Runtime と同じ UI frame を専用 offscreen target へ記録する。
        bool EnsureUIPreviewTarget(std::uint32_t width, std::uint32_t height) noexcept;
        bool DrawRuntimeUIPreview(const D3D12UIFrame& frame) noexcept;
        bool DrawValidationTriangle() noexcept;
        bool EndFrame() noexcept;
        bool WaitForGpu() noexcept;
        // Present 前に現在の Back Buffer を Readback へコピーする要求を積む。
        bool RequestBackBufferCapture() noexcept;
        // Present 済みの Readback を CPU 側の RGBA8 画像として回収する。
        bool ConsumeBackBufferCapture(std::vector<std::uint8_t>& rgba,
            std::uint32_t& width, std::uint32_t& height) noexcept;
#ifdef USE_IMGUI
        // ImGuiのWin32入力は既存Platform Backendを使い、描画だけをDX12へ接続する。
        bool InitializeImGui() noexcept;
        bool DrawImGui(::ImDrawData* draw_data) noexcept;
        bool ImGuiReady() const noexcept { return imgui_ready_; }
        // Editorが持つAsset pathを、DX12 ImGui用SRVへ遅延登録する。
        // 戻り値はD3D11 SRVポインタではなく、このContext専用の安定したID。
        void* ImGuiTextureForPath(const std::filesystem::path& source_path) noexcept;
        // UI Preview offscreen SRVをImGui TextureIdとして返す。
        void* ImGuiTextureForUIPreview() const noexcept;
#endif
        // Scene/Asset Reload の境界。Cache 済み Static Mesh/Texture を解放する前に
        // GPU Idle を待ち、古い Asset を保持し続けないようにする。
        bool ClearStaticAssetCaches() noexcept;

        bool IsInitialized() const noexcept { return device_ != nullptr; }
        bool IsFrameOpen() const noexcept { return frame_open_; }
        std::uint32_t Width() const noexcept { return width_; }
        std::uint32_t Height() const noexcept { return height_; }
        std::uint32_t FrameIndex() const noexcept { return frame_index_; }
        std::uint64_t LastSignaledFenceValue() const noexcept
        {
            return last_signaled_fence_value_;
        }
        std::uint64_t CompletedFenceValue() const noexcept
        {
            if (fence_ == nullptr) return 0;
            const std::uint64_t value = fence_->GetCompletedValue();
            return value == (std::numeric_limits<std::uint64_t>::max)() ? 0 : value;
        }
        std::uint64_t FrameFenceValue(std::uint32_t index) const noexcept
        {
            return index < FrameCount ? frame_resources_[index].fence_value : 0;
        }
        std::uint64_t CurrentFrameUploadUsed() const noexcept
        {
            return frame_resources_[frame_index_].upload_allocator.Used();
        }
        std::uint64_t CurrentFrameUploadCapacity() const noexcept
        {
            return frame_resources_[frame_index_].upload_allocator.Capacity();
        }
        bool DebugLayerEnabled() const noexcept { return debug_layer_enabled_; }
        bool GpuValidationEnabled() const noexcept { return gpu_validation_enabled_; }
        bool DredEnabled() const noexcept { return dred_enabled_; }
        bool HasFatalError() const noexcept { return fatal_error_; }
        HRESULT LastDeviceRemovedReason() const noexcept
        {
            return last_device_removed_reason_;
        }
        const char* LastInitializationStage() const noexcept
        {
            return last_initialization_stage_;
        }
        HRESULT LastInitializationResult() const noexcept
        {
            return last_initialization_result_;
        }

        ID3D12Device* Device() const noexcept { return device_.Get(); }
        ID3D12CommandQueue* CommandQueue() const noexcept { return command_queue_.Get(); }
        ID3D12GraphicsCommandList* CommandList() const noexcept { return command_list_.Get(); }
        D3D12UploadContext& UploadContext() noexcept { return upload_context_; }
        D3D12DescriptorHeapAllocator& ResourceDescriptorAllocator() noexcept
        {
            return resource_descriptor_allocator_;
        }
        D3D12DescriptorHeapAllocator& SamplerDescriptorAllocator() noexcept
        {
            return sampler_descriptor_allocator_;
        }
        D3D12ResourceStateTracker& ResourceStateTracker() noexcept
        {
            return resource_state_tracker_;
        }
        const D3D12ResourceStateTracker& ResourceStateTracker() const noexcept
        {
            return resource_state_tracker_;
        }
        const D3D12GpuTimingSnapshot& GpuTiming() const noexcept
        {
            return diagnostics_.LatestTiming();
        }
        std::uint32_t GpuPassSequence(D3D12GpuPass pass) const noexcept
        {
            return diagnostics_.PassSequence(pass);
        }
        void ConsumeDebugMessages(std::vector<D3D12DebugMessage>& out)
        {
            diagnostics_.ConsumeMessages(out);
        }
        void SetDebugLogPath(std::filesystem::path path)
        {
            diagnostics_.SetDebugLogPath(std::move(path));
        }
        void SetBreakOnError(bool enabled) noexcept
        {
            diagnostics_.SetBreakOnError(enabled);
            resource_state_tracker_.SetBreakOnError(enabled);
        }
        void SetStatsCsvPath(std::filesystem::path path)
        {
            diagnostics_.SetStatsCsvPath(std::move(path));
        }
        void SetFrameDumpCount(std::uint32_t count) noexcept
        {
            diagnostics_.SetFrameDumpCount(count);
        }
        const D3D12RuntimeStats& RuntimeStats() const noexcept
        {
            return diagnostics_.RuntimeStats();
        }
        bool ForceDeviceRemovedDiagnostic() noexcept;
        bool ReportLiveObjects(std::uint32_t& live_lines,
            std::uint32_t& detail_lines) noexcept
        {
            return diagnostics_.ReportLiveObjects(live_lines, detail_lines);
        }
        std::uint32_t LastShutdownLiveObjectLines() const noexcept
        {
            return last_shutdown_live_object_lines_;
        }
        std::uint32_t LastShutdownLiveObjectDetailLines() const noexcept
        {
            return last_shutdown_live_object_detail_lines_;
        }
        bool LastShutdownLiveObjectReportOk() const noexcept
        {
            return last_shutdown_live_object_report_ok_;
        }
        const D3D12RenderItemBatch& RenderItemBatch() const noexcept
        {
            return render_item_batches_[frame_index_];
        }
        std::size_t StaticMeshCacheSize() const noexcept
        {
            return static_mesh_cache_.size();
        }
        std::size_t TextureCacheSize() const noexcept
        {
            return texture_cache_.size();
        }
        std::size_t UIFontTextureCacheSize() const noexcept
        {
            return ui_font_texture_cache_.size();
        }
        // 同じ版の Font Atlas を既に持っているか。提出側が 2048x2048 の
        // RGBA を毎フレーム複製しないための問い合わせ。
        bool HasUIFontTexture(const std::string& key,
            std::uint64_t revision) const noexcept
        {
            if (ui_font_texture_cache_.find(key) == ui_font_texture_cache_.end())
                return false;
            const auto stored = ui_font_texture_revisions_.find(key);
            return stored != ui_font_texture_revisions_.end() &&
                stored->second == revision;
        }
        std::size_t RuntimeUIEffectHistoryCount() const noexcept
        {
            return ui_effect_history_targets_.size();
        }
        std::size_t UIPreviewEffectHistoryCount() const noexcept
        {
            return ui_preview_effect_history_targets_.size();
        }
        std::size_t SceneEffectHistoryCount() const noexcept
        {
            return scene_effect_history_targets_.size();
        }
        std::uint32_t LastModelEffectStackCount() const noexcept
        {
            return last_model_effect_stack_count_;
        }
        std::uint32_t LastScreenEffectStackCount() const noexcept
        {
            return last_screen_effect_stack_count_;
        }
        std::uint32_t LastShadowCoverageDrawCount() const noexcept
        {
            return last_shadow_coverage_draw_count_;
        }
        bool HasStaticMesh(const std::string& key) const noexcept
        {
            return static_mesh_cache_.find(key) != static_mesh_cache_.end();
        }
        bool HasSkinnedMesh(const std::string& key) const noexcept
        {
            return skinned_mesh_cache_.find(key) != skinned_mesh_cache_.end();
        }
        bool HasStaticTexture(const std::string& key) const noexcept
        {
            // File Decodeの失敗も解決済みとして扱う。DrawStaticSceneはWhite Textureへ戻し、
            // build_dx12_static_scene が同じ不正 Asset を毎フレーム再試行しないようにする。
            return key.empty() || texture_cache_.find(key) != texture_cache_.end() ||
                static_texture_failures_.find(key) != static_texture_failures_.end();
        }
        bool HasStaticShader(const std::string& key) const noexcept
        {
            return key.empty() || custom_static_pipelines_.find(key) !=
                custom_static_pipelines_.end() ||
                custom_static_shader_failures_.find(key) != custom_static_shader_failures_.end();
        }
        bool HasCompiledStaticShader(const std::string& key) const noexcept
        {
            return !key.empty() && custom_static_pipelines_.find(key) !=
                custom_static_pipelines_.end();
        }
        ID3D12Resource* CurrentRenderTarget() const noexcept
        {
            return render_targets_[frame_index_].Get();
        }
        D3D12_CPU_DESCRIPTOR_HANDLE CurrentRenderTargetView() const noexcept;
        D3D12_CPU_DESCRIPTOR_HANDLE CurrentDepthStencilView() const noexcept;

        const D3D12OffscreenTarget& SceneViewTarget() const noexcept
        {
            return scene_view_target_;
        }
        const D3D12OffscreenTarget& GameViewTarget() const noexcept
        {
            return game_view_target_;
        }

    private:
        bool ConfigureDebug(bool enable_debug_layer, bool enable_gpu_validation,
            bool enable_dred) noexcept;
        bool CreateDevice(bool enable_debug_layer, bool force_warp,
            bool enable_gpu_validation, bool enable_dred) noexcept;
        bool CreateSwapChain(HWND window, std::uint32_t width,
            std::uint32_t height) noexcept;
        bool CreateRenderTargets() noexcept;
        bool CreateOffscreenTarget(D3D12OffscreenTarget& target,
            std::uint32_t width, std::uint32_t height,
            DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT,
            const wchar_t* debug_name = L"Offscreen") noexcept;
        bool DrawRuntimeUIToTarget(const D3D12UIFrame& frame,
            D3D12OffscreenTarget* output_target,
            D3D12OffscreenTarget* effect_targets) noexcept;
        bool CompositeSceneEffectTarget(const D3D12OffscreenTarget& source,
            D3D12OffscreenTarget& destination, const D3D12_RECT* scissor) noexcept;
        bool BuildRenderingLayerLdrSource(const D3D12OffscreenTarget& layer_source,
            D3D12OffscreenTarget& ldr_output) noexcept;
        bool CompositeUIEffectTargetToCurrent(const D3D12OffscreenTarget& source) noexcept;
        bool CreateValidationTriangleResources() noexcept;
        bool CreateStaticRendererResources() noexcept;
        void ReleaseStaticRendererResources() noexcept;
        bool CreateScene3DRendererResources() noexcept;
        void ReleaseScene3DRendererResources() noexcept;
        bool CreateUIRendererResources() noexcept;
        void ReleaseUIRendererResources() noexcept;
        bool CreateUIEffectResources() noexcept;
        void ReleaseUIEffectResources() noexcept;
#ifdef USE_IMGUI
        bool CreateImGuiRendererResources() noexcept;
        void ReleaseImGuiRendererResources() noexcept;
#endif
        bool EnsureScene3DRenderTargets() noexcept;
        void ReleaseScene3DRenderTargets() noexcept;
        bool EnsureScene3DShadowTargets(const D3D12StaticSceneSubmission& submission) noexcept;
        void ReleaseScene3DShadowTargets() noexcept;
        bool CacheSkinnedMeshLocalBounds(const D3D12SkinnedMeshSource& source) noexcept;
        bool EnsureStaticMesh(const D3D12StaticMeshSource& source) noexcept;
        bool EnsureSkinnedMesh(const D3D12SkinnedMeshSource& source) noexcept;
        bool EnsureStaticTexture(const D3D12StaticTextureSource& source) noexcept;
        bool EnsureUIFontTexture(const D3D12UIFontAtlasSource& source) noexcept;
        bool EnsureStaticShader(const D3D12StaticShaderSource& source) noexcept;
        bool CreateSolidStaticTexture(const char* key, std::uint32_t rgba) noexcept;
        struct StaticPipelineSet;
        bool CreateStaticPipelineSet(const std::vector<std::uint8_t>& pixel_shader,
            StaticPipelineSet& pipelines, std::string_view debug_key = {}) noexcept;
        ID3D12PipelineState* StaticPipeline(const std::string& shader_key,
            bool double_sided, D3D12StaticAlphaMode alpha_mode) const noexcept;
        void ReleaseRenderTargets() noexcept;
        void ReleaseOffscreenTarget(D3D12OffscreenTarget& target) noexcept;
        void ReleaseValidationTriangleResources() noexcept;
        bool WaitForFrame(std::uint32_t frame_index) noexcept;
        bool TransitionCurrentRenderTarget(D3D12_RESOURCE_STATES after) noexcept;
        bool RecordBackBufferCapture() noexcept;
        void ReleaseBackBufferCapture() noexcept;
        std::uint64_t SignalQueue() noexcept;
        void ReclaimDeferredDescriptors() noexcept;
        void ReportDeviceRemoved(HRESULT trigger) noexcept;
        void WriteDeviceRemovedReport(HRESULT trigger, HRESULT reason,
            bool forced_validation) noexcept;
        D3D12RuntimeStats BuildRuntimeStats() const noexcept;
        void SetInitializationFailure(const char* stage, HRESULT result) noexcept;
        void BeginGpuPass(D3D12GpuPass pass) noexcept
        {
            diagnostics_.BeginPass(command_list_.Get(), pass);
        }
        void EndGpuPass(D3D12GpuPass pass) noexcept
        {
            diagnostics_.EndPass(command_list_.Get(), pass);
        }

        struct StaticTextureResource final
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            D3D12DescriptorAllocation srv{};
            D3D12DescriptorAllocation srgb_srv{};
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::uint16_t mip_levels = 1;
            DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
        };

        struct StaticObjectConstants final
        {
            DirectX::XMFLOAT4X4 world{};
            DirectX::XMFLOAT4 material_color{ 1, 1, 1, 1 };
        };
        struct StaticSceneConstants final
        {
            DirectX::XMFLOAT4X4 view_projection{};
            DirectX::XMFLOAT4 light_direction{ 0, -1, 0, 0 };
            DirectX::XMFLOAT4 camera_position{};
        };
        struct StaticFrameCompatibilityConstants final
        {
            DirectX::XMFLOAT4X4 frame_view{};
            DirectX::XMFLOAT4X4 frame_projection{};
            DirectX::XMFLOAT4X4 frame_view_projection{};
            DirectX::XMFLOAT4X4 frame_inv_view{};
            DirectX::XMFLOAT4X4 frame_inv_projection{};
            DirectX::XMFLOAT4X4 frame_inv_view_projection{};
            DirectX::XMFLOAT4X4 frame_prev_view_projection{};
            DirectX::XMFLOAT4 frame_camera_position{};
            DirectX::XMFLOAT4 frame_screen_size{};
            DirectX::XMFLOAT4 frame_camera_planes{};
            DirectX::XMFLOAT4 frame_jitter{};
            DirectX::XMFLOAT4 frame_params{};
        };
        struct StaticBridgeMaterialConstants final
        {
            DirectX::XMFLOAT4 base_color{};
            DirectX::XMFLOAT4 emissive_strength{};
            DirectX::XMFLOAT4 surface_params{};
            DirectX::XMFLOAT4 render_params{};
        };
        struct StaticPipelineSet final
        {
            Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelines[6];
        };
        static_assert(sizeof(StaticObjectConstants) % 16 == 0);
        static_assert(sizeof(StaticSceneConstants) % 16 == 0);
        static_assert(sizeof(StaticFrameCompatibilityConstants) % 16 == 0);
        static_assert(sizeof(StaticBridgeMaterialConstants) % 16 == 0);

        struct Scene3DTarget final
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            D3D12DescriptorAllocation rtv{};
            D3D12DescriptorAllocation srv{};
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        };

        struct Scene3DDepthTarget final
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            D3D12DescriptorAllocation dsv{};
            D3D12DescriptorAllocation srv{};
        };

        struct Scene3DHistoryTarget final
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            D3D12DescriptorAllocation srv{};
        };

        struct Scene3DShadowTarget final
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            D3D12DescriptorAllocation dsv{};
            D3D12DescriptorAllocation srv{};
            std::uint32_t resolution = 0;
            std::uint32_t array_size = 0;
        };

        struct Scene3DMotionHistory final
        {
            DirectX::XMFLOAT4X4 world{
                1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
            std::vector<DirectX::XMFLOAT4X4> bones;
            std::uint64_t frame_serial = 0;
            bool valid = false;
        };

        Microsoft::WRL::ComPtr<IDXGIFactory7> factory_;
        Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter_;
        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue_;
        Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> validation_root_signature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> validation_pipeline_;
        D3D12MeshBuffer validation_mesh_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> static_root_signature_;
        std::vector<std::uint8_t> static_vertex_shader_bytecode_;
        StaticPipelineSet static_bridge_pipelines_{};

        // Scene 3D本番RendererのResource。
        Microsoft::WRL::ComPtr<ID3D12RootSignature> scene3d_geometry_root_signature_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> scene3d_lighting_root_signature_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> scene3d_shadow_root_signature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> scene3d_static_gbuffer_pipelines_[6];
        Microsoft::WRL::ComPtr<ID3D12PipelineState> scene3d_skinned_gbuffer_pipelines_[6];
        Microsoft::WRL::ComPtr<ID3D12PipelineState> scene3d_lighting_pipeline_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> scene3d_postprocess_root_signature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> scene3d_temporal_input_pipeline_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> scene3d_taa_resolve_pipeline_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> scene3d_postprocess_pipeline_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> scene3d_static_depth_pipelines_[4];
        Microsoft::WRL::ComPtr<ID3D12PipelineState> scene3d_skinned_depth_pipelines_[4];
        Microsoft::WRL::ComPtr<ID3D12PipelineState> scene3d_static_forward_blend_pipelines_[2];
        Microsoft::WRL::ComPtr<ID3D12PipelineState> scene3d_skinned_forward_blend_pipelines_[2];
        Microsoft::WRL::ComPtr<ID3D12PipelineState> scene3d_static_model_effect_pipelines_[4];
        Microsoft::WRL::ComPtr<ID3D12PipelineState> scene3d_skinned_model_effect_pipelines_[4];
        Microsoft::WRL::ComPtr<ID3D12PipelineState> scene3d_static_model_effect_extract_pipelines_[2];
        Microsoft::WRL::ComPtr<ID3D12PipelineState> scene3d_skinned_model_effect_extract_pipelines_[2];
        Microsoft::WRL::ComPtr<ID3D12PipelineState> scene3d_static_shadow_pipelines_[4];
        Microsoft::WRL::ComPtr<ID3D12PipelineState> scene3d_skinned_shadow_pipelines_[4];
        std::vector<std::uint8_t> scene3d_static_vs_;
        std::vector<std::uint8_t> scene3d_skinned_vs_;
        std::vector<std::uint8_t> scene3d_gbuffer_ps_;
        std::vector<std::uint8_t> scene3d_depth_alpha_ps_;
        std::vector<std::uint8_t> scene3d_forward_ps_;
        std::vector<std::uint8_t> scene3d_model_effect_extract_ps_;
        std::vector<std::uint8_t> scene3d_fullscreen_vs_;
        std::vector<std::uint8_t> scene3d_lighting_ps_;
        std::vector<std::uint8_t> scene3d_temporal_input_ps_;
        std::vector<std::uint8_t> scene3d_taa_resolve_ps_;
        std::vector<std::uint8_t> scene3d_postprocess_ps_;
        std::vector<std::uint8_t> scene3d_shadow_static_vs_;
        std::vector<std::uint8_t> scene3d_shadow_skinned_vs_;
        std::vector<std::uint8_t> scene3d_shadow_alpha_ps_;
        Scene3DTarget scene3d_gbuffer_[kScene3DGBufferCount];
        Scene3DDepthTarget scene3d_depth_{};
        Scene3DTarget scene3d_temporal_input_{};
        Scene3DTarget scene3d_taa_resolved_{};
        Scene3DHistoryTarget scene3d_history_{};
        Scene3DHistoryTarget scene3d_ssr_history_{};
        Scene3DHistoryTarget scene3d_depth_history_{};
        bool scene3d_history_valid_ = false;
        Scene3DShadowTarget scene3d_directional_shadow_{};
        Scene3DShadowTarget scene3d_local_shadow_{};
        D3D12DescriptorAllocation scene3d_null_directional_shadow_srv_{};
        D3D12DescriptorAllocation scene3d_null_local_shadow_srv_{};
        std::uint32_t scene3d_width_ = 0;
        std::uint32_t scene3d_height_ = 0;
        std::unordered_map<std::string, std::unique_ptr<D3D12MeshBuffer>> skinned_mesh_cache_;
        std::unordered_map<std::string, D3D12MeshLocalBounds> skinned_mesh_bounds_cache_;
        std::unordered_map<std::string, Scene3DMotionHistory> scene3d_motion_history_;
        std::uint64_t scene3d_frame_serial_ = 0;
        std::uint64_t scene3d_history_write_serial_ = 0;
        D3D12DescriptorAllocation static_samplers_[3]{};
        std::unordered_map<std::string, std::unique_ptr<D3D12MeshBuffer>> static_mesh_cache_;
        std::unordered_map<std::string, D3D12MeshLocalBounds> static_mesh_bounds_cache_;
        std::unordered_map<std::string, StaticTextureResource> texture_cache_;
        std::unordered_map<std::string, StaticTextureResource> ui_font_texture_cache_;
        std::unordered_map<std::string, std::uint64_t> ui_font_texture_revisions_;
        std::unordered_set<std::string> static_texture_failures_;
        std::unordered_map<std::string, StaticPipelineSet> custom_static_pipelines_;
        std::unordered_set<std::string> custom_static_shader_failures_;
        Microsoft::WRL::ComPtr<ID3D12Resource> depth_stencil_buffer_;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
        D3D12FrameResource frame_resources_[FrameCount];
        Microsoft::WRL::ComPtr<ID3D12Resource> render_targets_[FrameCount];
        Microsoft::WRL::ComPtr<ID3D12Resource> back_buffer_capture_readback_;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT back_buffer_capture_footprint_{};
        std::uint64_t back_buffer_capture_row_size_ = 0;
        std::uint32_t back_buffer_capture_row_count_ = 0;
        std::uint32_t back_buffer_capture_width_ = 0;
        std::uint32_t back_buffer_capture_height_ = 0;
        bool back_buffer_capture_requested_ = false;
        bool back_buffer_capture_recorded_ = false;

        D3D12DescriptorHeapAllocator rtv_allocator_;
        D3D12DescriptorAllocation rtv_allocation_{};
        D3D12DescriptorHeapAllocator dsv_allocator_;
        D3D12DescriptorAllocation dsv_allocation_{};
        D3D12DescriptorHeapAllocator resource_descriptor_allocator_;
        D3D12DescriptorHeapAllocator sampler_descriptor_allocator_;
        D3D12UploadContext upload_context_;
        D3D12ResourceStateTracker resource_state_tracker_;
        D3D12Diagnostics diagnostics_;
        std::uint32_t last_shutdown_live_object_lines_ = 0;
        std::uint32_t last_shutdown_live_object_detail_lines_ = 0;
        bool last_shutdown_live_object_report_ok_ = true;
        D3D12RenderItemBatch render_item_batches_[FrameCount];
        D3D12FrameConstants current_frame_constants_{};
        D3D12OffscreenTarget scene_view_target_{};
        D3D12OffscreenTarget scene3d_deferred_target_{};
        D3D12OffscreenTarget game_view_target_{};
        D3D12OffscreenTarget ui_preview_target_{};
        D3D12OffscreenTarget ui_preview_effect_targets_[4]{};
        // [0]/[1]/[2]はEffectとRegion合成の循環先、[3]はBackdropの退避先。
        D3D12OffscreenTarget ui_effect_targets_[4]{};
        D3D12OffscreenTarget scene_effect_targets_[4]{};
        D3D12SceneEffectSubmission scene_effect_submission_{};
        std::string scene3d_lighting_trace_signature_;
        std::uint32_t last_model_effect_stack_count_ = 0;
        std::uint32_t last_screen_effect_stack_count_ = 0;
        std::uint32_t last_shadow_coverage_draw_count_ = 0;
        struct UIEffectHistoryEntry final
        {
            D3D12OffscreenTarget target{};
            bool valid = false;
        };
        // Game ViewとCanvas Previewは解像度も更新周期も異なるため履歴を混ぜない。
        std::unordered_map<std::uint64_t, UIEffectHistoryEntry>
            ui_effect_history_targets_;
        std::unordered_map<std::uint64_t, UIEffectHistoryEntry>
            ui_preview_effect_history_targets_;
        std::unordered_map<std::uint64_t, UIEffectHistoryEntry>
            scene_effect_history_targets_;
        std::uint64_t scene_effect_history_write_serial_ = 0;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> ui_root_signature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> ui_pipelines_[5];
        Microsoft::WRL::ComPtr<ID3D12PipelineState> ui_hdr_composite_pipeline_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> ui_background_composite_pipeline_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> ui_hdr_background_composite_pipeline_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> ui_screen_layer_extract_pipeline_;
        std::vector<std::uint8_t> ui_vertex_shader_;
        std::vector<std::uint8_t> ui_pixel_shader_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> ui_effect_root_signature_;
        static constexpr std::size_t UIEffectKindCount =
            static_cast<std::size_t>(ReplayEngine::UI::UIEffectKind::Count);
        static_assert(UIEffectKindCount == 74,
            "UIEffectKind の永続化値とDX12 Effect Shader表の対応が変わりました。");
        std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, UIEffectKindCount>
            ui_effect_pipelines_{};
        std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, UIEffectKindCount>
            ui_effect_hdr_pipelines_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> ui_effect_region_pipeline_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> ui_effect_region_hdr_pipeline_;
#ifdef USE_IMGUI
        struct ImGuiTextureRequest final
        {
            std::string key;
            std::filesystem::path source_path;
        };
        Microsoft::WRL::ComPtr<ID3D12RootSignature> imgui_root_signature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> imgui_pipeline_;
        Microsoft::WRL::ComPtr<ID3D12Resource> imgui_font_texture_;
        D3D12DescriptorAllocation imgui_font_srv_{};
        D3D12DescriptorAllocation imgui_fallback_srv_{};
        std::vector<std::uint8_t> imgui_vertex_shader_;
        std::vector<std::uint8_t> imgui_pixel_shader_;
        std::unordered_map<std::string, std::unique_ptr<ImGuiTextureRequest>> imgui_texture_requests_;
        std::unordered_set<const ImGuiTextureRequest*> imgui_texture_request_addresses_;
        std::uint64_t imgui_font_texture_id_ = 0;
        std::uint64_t ui_preview_texture_id_ = 0;
        bool imgui_ready_ = false;
#endif
        HANDLE fence_event_ = nullptr;
        std::uint64_t next_fence_value_ = 1;
        std::uint64_t last_signaled_fence_value_ = 0;
        std::uint64_t frame_upload_peak_ = 0;
        std::uint64_t fence_wait_count_ = 0;
        std::uint64_t fence_wait_nanoseconds_ = 0;
        std::uint64_t pso_cache_hits_ = 0;
        std::uint64_t pso_cache_misses_ = 0;
        std::uint32_t frame_index_ = 0;
        std::uint32_t width_ = 0;
        std::uint32_t height_ = 0;
        bool frame_open_ = false;
        bool debug_layer_enabled_ = false;
        bool gpu_validation_enabled_ = false;
        bool dred_enabled_ = false;
        bool allow_tearing_ = false;
        bool validation_resources_enabled_ = false;
        bool fatal_error_ = false;
        HRESULT last_device_removed_reason_ = S_OK;
        char last_initialization_stage_[128]{};
        HRESULT last_initialization_result_ = S_OK;
    };
}
