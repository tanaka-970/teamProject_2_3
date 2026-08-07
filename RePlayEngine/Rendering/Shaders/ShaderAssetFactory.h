#pragma once

#include "ShaderAsset.h"

#include <filesystem>
#include <string>

namespace ReplayEngine::Rendering
{
    // Editor / Shader Composer から Shader Asset を作る共通入口。
    //
    // 今は HLSL + replay_* pragma が Shader Asset 本体。
    // 将来 Shader Composer がコード生成するときもこの保存規則へ乗せる。
    class ShaderAssetFactory final
    {
    public:
        // 安全な Unlit surface template を Atomic Save する。
        // 既存ファイルは上書きしない。
        static bool CreateSurfaceShader(const std::filesystem::path& path,
            const std::string& display_name, const std::string& category,
            ShaderID& out_id, std::string& error);

        // Material の Shader Stack へ追加できる Layer Shader を作る。
        // Editor / Renderer の C++ へ種類を追加する必要はない。
        static bool CreateLayerShader(const std::filesystem::path& path,
            const std::string& display_name, const std::string& category,
            ShaderID& out_id, std::string& error);
    };
}
