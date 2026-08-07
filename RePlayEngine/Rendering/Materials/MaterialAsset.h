#pragma once

#include "../ShaderStack/ShaderLayerStack.h"

#include <DirectXMath.h>

#include <filesystem>
#include <string>

namespace ReplayEngine::Rendering
{
    enum class MaterialAlphaMode : int
    {
        Opaque = 0,
        Mask = 1,
        Blend = 2
    };

    // GPUリソースを持たない、保存可能な材質定義。
    // Texture欄はAssetDatabaseのGUIDで保持し、ファイル移動で参照を壊さない。
    struct MaterialAsset final
    {
        // version 2 で層構造（layers）を追加した。
        // version 1 のファイルも読める。読んだ場合 layers は空になる。
        static constexpr int current_version = 2;
        static constexpr const char* file_extension = ".replaymaterial";

        DirectX::XMFLOAT4 base_color{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::string base_color_texture;
        std::string normal_texture;
        float metallic = 0.0f;
        std::string metallic_texture;
        float roughness = 0.55f;
        std::string roughness_texture;
        DirectX::XMFLOAT3 emissive{ 0.0f, 0.0f, 0.0f };
        float emissive_strength = 0.0f;
        std::string emissive_texture;
        float ambient_occlusion = 1.0f;
        std::string ambient_occlusion_texture;
        MaterialAlphaMode alpha_mode = MaterialAlphaMode::Opaque;
        float alpha_cutoff = 0.5f;
        bool double_sided = false;
        int shading_model = 1;

        // ---- 重ね掛け（この Material 固有）------------------------------
        //
        // 以前はレイヤがグローバル配列（shader_layers_static[0]）にあり、
        // どの Material を選んでも同じ層構成しか持てなかった。
        // Material が自分の層を持つことで、
        // オブジェクトごとに違う重ね方ができる。
        //
        // 描画順は layers の並び順。先頭から順に描く。
        ShaderLayerStack layers;

        // 輪郭線パスを使うか。layers に Outline が入っていれば true になる。
        bool outline_pass = false;

        // ピクセル化の設定。SHADING_MODEL_PIXELATE と Pixelate レイヤが使う。
        float pixelate_grid = 6.0f;
        float pixelate_strength = 1.0f;

        static bool Save(const MaterialAsset& material,
            const std::filesystem::path& path, std::string& error);
        static bool Load(const std::filesystem::path& path,
            MaterialAsset& material, std::string& error);
    };
}
