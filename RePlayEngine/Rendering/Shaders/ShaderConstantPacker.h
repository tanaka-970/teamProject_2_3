#pragma once

#include "ShaderAsset.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ReplayEngine::Reflection { class PropertyBag; }

namespace ReplayEngine::Rendering
{
    // #pragma property の並びから定数バッファを組み立てる。
    //
    // 【なぜ自動生成するのか】
    //   cbuffer を人に書かせると、HLSL のパッキング規則を踏み外して
    //   値が 1 つずれる。ずれても絵が「なんとなく変」になるだけで
    //   エラーにならないため、原因の特定が非常に難しい。
    //   宣言 1 か所から C++ と HLSL の両方を作れば、食い違いようがない。
    //
    // 【HLSL のパッキング規則】
    //   定数バッファは 16 バイト（float4）単位の並び。
    //   各要素は「16 バイト境界をまたがない位置」へ置かれる。
    //     float  (4)  … どこにでも入る
    //     float2 (8)  … 残りが 8 未満なら次の境界へ送られる
    //     float3 (12) … 残りが 12 未満なら次の境界へ送られる
    //     float4 (16) … 常に境界から始まる
    //   バッファ全体の大きさも 16 の倍数へ切り上げる。
    //
    //   例: float3 + float3 は 24 バイトではなく 32 バイトになる。
    //       2 つ目の float3 が境界をまたぐため次の境界へ送られる。
    class ShaderConstantPacker final
    {
    public:
        // ユーザー領域の開始レジスタ。
        //
        // b0..b8 と t0..t9 は既存のパスが使っている。
        //   b0 DOF / Fog
        //   b1 SCENE_CONSTANT_BUFFER
        //   b4 frame_constants
        //   b7 DECAL_CONSTANT_BUFFER
        //   b8 INV_VP_CB
        // 侵すと既存の描画が壊れるので、ここから後ろを使う。
        static constexpr std::uint32_t material_constant_register = 9;
        static constexpr std::uint32_t material_texture_base_slot = 10;

        // properties の constant_offset / constant_size / texture_slot を埋める。
        //
        // 入力の並び順を変えない。並べ替えて詰めた方が小さくなるが、
        // 宣言順と cbuffer の並びが食い違うと、
        // HLSL を目で読んだときに追えなくなる。
        static void AssignOffsets(std::vector<ShaderProperty>& properties,
            std::uint32_t& out_buffer_size);

        // 値の取り出し方を呼び出し側から渡す。
        //
        // PropertyBag へ直接依存しないのは、
        //   ・Material（PropertyBag）
        //   ・レイヤ（別の入れ物）
        //   ・検証（その場の配列）
        // の 3 通りから同じ Pack を使いたいため。
        // 依存を増やすと、テストのためだけに PropertyBag を組み立てる
        // ことになって面倒が増える。
        //
        // 見つからなければ false を返すこと。既定値が使われる。
        using ValueLookup = bool (*)(const std::string& saved_name,
            DirectX::XMFLOAT4& out, void* user);

        // Schema と値から GPU へ送るバイト列を作る。
        //
        // lookup が false を返したプロパティは default_value を使う。
        // 「設定していないから 0」ではなく「宣言した既定値」になる。
        static void Pack(const ShaderPropertySchema& schema,
            ValueLookup lookup, void* user,
            std::vector<std::uint8_t>& out_bytes);

        // 既定値だけで詰める。lookup を渡さない版。
        static void PackDefaults(const ShaderPropertySchema& schema,
            std::vector<std::uint8_t>& out_bytes);

        // Schema から HLSL の cbuffer 宣言を作る。
        // コンパイル前にソースの先頭へ差し込む。
        static std::string GenerateHlslDeclaration(
            const ShaderPropertySchema& schema);

        // 16 の倍数へ切り上げる。
        static constexpr std::uint32_t Align16(std::uint32_t value) noexcept
        {
            return (value + 15u) & ~15u;
        }
    };
}
