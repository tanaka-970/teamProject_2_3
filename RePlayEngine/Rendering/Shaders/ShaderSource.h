#pragma once

#include "ShaderAsset.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Rendering
{
    // .hlsl の #pragma を読み、メタデータへ変換する。
    //
    // 【なぜ #pragma か】
    //   ・HLSL コンパイラが未知の pragma を無視するので、既存の .hlsl が壊れない
    //   ・シェーダ本体と同じファイルに書けるので、実装と説明が食い違わない
    //   ・別ファイル（.meta 相当）にすると必ずどちらかを更新し忘れる
    //
    // 【書式】
    //   #pragma replay_guid     "8f2c1a4b9d3e4f5a8b7c6d5e4f3a2b1c"
    //   #pragma replay_name     "Standard Lit"
    //   #pragma replay_category "Lit/Standard"
    //   #pragma replay_domain   surface
    //
    //   #pragma property color   BaseColor  "基本色"        = (1, 1, 1, 1)
    //   #pragma property texture BaseMap    "基本色マップ"   default white
    //   #pragma property range   Metallic   "金属度"  0..1  = 0.0
    //   #pragma property float3  Emissive   "発光色"        = (0, 0, 0)
    //   #pragma property toggle  DoubleSided "両面描画"     = false
    //   #pragma property enum    CullMode   "カリング" { Off, Front, Back } = Back
    class ShaderSource final
    {
    public:
        struct ParseIssue final
        {
            int line = 0;
            std::string message;
        };

        struct ParseResult final
        {
            bool succeeded = false;
            ShaderSourceInfo info;

            // 解析できなかった pragma。致命ではないが握り潰さない。
            //
            // 握り潰すと「書いたのに欄が出ない」原因が分からなくなる。
            // 呼び出し側は必ずログへ流すこと。
            std::vector<ParseIssue> issues;
        };

        // ファイルを読んで解析する。
        // guid が無ければ out_needs_guid が true になる（書き戻しはしない）。
        static ParseResult ParseFile(const std::filesystem::path& path,
            bool& out_needs_guid);

        // 文字列から解析する。テスト用。
        static ParseResult ParseText(const std::string& text,
            const std::filesystem::path& source_path, bool& out_needs_guid);

        // GUID を採番してファイルの先頭へ書き戻す。
        //
        // 【一度振った GUID は絶対に書き換えないこと】
        //   書き換えると、そのシェーダを使っている全マテリアルの参照が切れる。
        //   この関数は「#pragma replay_guid が無いファイル」にだけ呼ぶ。
        static bool AssignGuid(const std::filesystem::path& path,
            ShaderID& out_id, std::string& error);

        // 32 文字 16 進の GUID を作る。
        static ShaderID GenerateID();
    };
}
