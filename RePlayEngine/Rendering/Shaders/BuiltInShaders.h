#pragma once

#include "ShaderAsset.h"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace ReplayEngine::Rendering
{
    // 組み込みシェーダの固定 GUID。
    //
    // 【なぜ決め打ちなのか】
    //   自動採番だと環境ごとに違う値になる。
    //   そうすると「shading_model 1 のマテリアルを PBR へ移す」という
    //   移行の宛先が定まらず、人の環境ごとに別のシェーダを指すことになる。
    //
    // 【絶対に変えないこと】
    //   一度でも保存されたマテリアルがこの値を持っている。
    //   変えた瞬間、全部 Missing Shader になる。
    //   .hlsl 側の #pragma replay_guid と必ず一致させること。
    namespace BuiltInShaders
    {
        // shading_model の番号と 1 対 1 で対応する。
        //   0 fbx_default / 1 pbr / 2 toon / 3 unlit / 4 pixelate
        inline constexpr ShaderID FbxDefault =
            Reflection::MakeTypeGUID("00000000000000000000000000000001");
        inline constexpr ShaderID Pbr =
            Reflection::MakeTypeGUID("00000000000000000000000000000002");
        inline constexpr ShaderID Toon =
            Reflection::MakeTypeGUID("00000000000000000000000000000003");
        inline constexpr ShaderID Unlit =
            Reflection::MakeTypeGUID("00000000000000000000000000000004");
        inline constexpr ShaderID Pixelate =
            Reflection::MakeTypeGUID("00000000000000000000000000000005");

        struct Definition final
        {
            ShaderID id;
            int shading_model = 0;
            const char* display_name = "";
            const char* relative_path = "";
        };

        // 表示順は shading_model の番号順。
        const std::vector<Definition>& All();

        // shading_model の番号から引く。
        //
        // 知らない番号のときは無効な ID を返す。
        // 勝手に既定値へ丸めないこと。丸めると
        // 「保存されていた選択と違うものが黙って付く」ことになる。
        ShaderID FromShadingModel(int shading_model) noexcept;

        // 組み込みかどうか。
        bool IsBuiltIn(ShaderID id) noexcept;

        // Shader/ からの相対パス。見つからなければ空。
        std::filesystem::path RelativePath(ShaderID id);
    }
}
