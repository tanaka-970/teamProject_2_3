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
        // 【b9 を選んだ理由】
        //   既存の Shader/*.hlsl を全部調べて、b0..b13 のうち
        //   前方描画のピクセルシェーダで空いているのが b9 だった。
        //     b0 DOF / Fog / static_mesh    b1 SCENE_CONSTANT_BUFFER
        //     b2 b3 pbr_common              b4 frame_constants
        //     b5 csm_common                 b6 toon / motion_vector
        //     b7 DECAL / outline            b8 INV_VP / motion_vector_skinning
        //     b9 空き（gbuffer PS の material_override だけが使用）
        //     b10 lights_common / pixelate  b11 stylized_character
        //     b12 ssao / taa / tiled        b13 ssr
        //
        //   b9 は今 material_override_constants（メッシュ単位の上書き）に
        //   使われている。役割が同じなので、フェーズ 6 でそれを
        //   自動生成の cbuffer に置き換えて 1 本化する。
        //   別の番号にすると、同じ「マテリアル定数」が 2 か所へ散る。
        static constexpr std::uint32_t material_constant_register = 9;

        // 【t40 を選んだ理由】
        //   t10 では足りない。既存のパスが飛び飛びで使っている。
        //     t0..t4   diffuse / pbr_common
        //     t6..t8   deferred_lighting
        //     t12      csm_common の影マップ / pixelate の GBuffer
        //     t20      tiled_light_common
        //     t33..t35 pbr_common の IBL
        //   t10 から順に振ると 3 枚目で t12 に当たって影が壊れる。
        //   エラーは出ず、絵だけおかしくなるので気付きにくい。
        //   既存の最大が t35 なので、余裕を見て t40 から始める。
        static constexpr std::uint32_t material_texture_base_slot = 40;

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
