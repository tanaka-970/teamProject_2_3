// Point / Spot Light の動的シャドウマップをサンプルする共通処理。
//
// Directional の CSM (csm_common.hlsli) と分けてある。平行光は正射影の
// カスケードで足りるが、位置を持つ光は透視射影が要り、Point はさらに
// 全方向を覆うため 1 灯あたり 6 面必要になる。投影方法が違うものを
// 1 つのサンプリング経路へ混ぜると、バイアスもフィルタも両方に合わなくなる。
//
// 格納は 1 枚の Texture2DArray。
//   スライス 0..3   … Spot 1 灯につき 1 枚
//   スライス 4..15  … Point 1 灯につき 6 枚 (+X,-X,+Y,-Y,+Z,-Z の順)
// TextureCube ではなく 2D Array にしてあるので、Spot と Point で
// 同じ比較サンプラーと同じ PCF をそのまま使える。
#ifndef __LOCAL_SHADOW_COMMON_HLSLI__
#define __LOCAL_SHADOW_COMMON_HLSLI__

// RePlayEngine/Rendering/Shadows/LocalShadowAtlas.h の定数と一致させる。
#define LOCAL_SHADOW_POINT_FACE_COUNT 6
#define LOCAL_SHADOW_SLICE_COUNT 16

struct LocalShadowSlice
{
    row_major float4x4 view_projection;
    float4 params; // x=near, y=far, z=depth_bias, w=予約
};

StructuredBuffer<LocalShadowSlice> local_shadow_slices : register(t21);
Texture2DArray local_shadow_atlas : register(t13);
SamplerComparisonState local_shadow_sampler : register(s7);

// Point の 6 面のどれを見るかを決める。
// LocalShadowAtlas の kFaceForward と同じ順番 (+X,-X,+Y,-Y,+Z,-Z)。
int local_shadow_select_face(float3 light_to_surface)
{
    float3 magnitude = abs(light_to_surface);
    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z)
        return light_to_surface.x > 0.0f ? 0 : 1;
    if (magnitude.y >= magnitude.z)
        return light_to_surface.y > 0.0f ? 2 : 3;
    return light_to_surface.z > 0.0f ? 4 : 5;
}

// 1 スライスぶんの可視率。戻り値 1 = 遮蔽なし、0 = 完全に影。
float local_shadow_sample_slice(int slice, float3 world_position,
                                float3 world_normal, float light_distance, float NoL)
{
    LocalShadowSlice source = local_shadow_slices[slice];

    // 解像度は Editor から変えられるので、定数で持たずテクスチャへ聞く。
    // スカラー演算なのでコンパイラがループ外へ追い出す。
    uint width, height, elements, levels;
    local_shadow_atlas.GetDimensions(0, width, height, elements, levels);
    float map_size = max((float) width, 1.0f);

    // 法線方向へずらす量は「影マップ 1 テクセルが覆うワールド長」に比例させる。
    // 透視射影なので遠いほどテクセルが大きい。距離に比例させないと、
    // 近くで影が浮き (ピーターパン)、遠くでアクネが出るという両立しない
    // 症状が同時に起きる。
    float texel_world = light_distance * 2.0f / map_size;
    float slope = saturate(1.0f - NoL);
    float3 offset_position = world_position +
        world_normal * texel_world * (1.5f + slope * 3.0f);

    float4 clip = mul(float4(offset_position, 1.0f), source.view_projection);
    if (clip.w <= 0.0f) return 1.0f; // ライトの後ろ側
    float3 ndc = clip.xyz / clip.w;
    float2 uv = float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f);
    // 範囲外は「影マップの守備範囲外」なので遮蔽なし扱い。
    if (any(uv < 0.0f) || any(uv > 1.0f)) return 1.0f;
    if (ndc.z <= 0.0f || ndc.z >= 1.0f) return 1.0f;

    float reference = ndc.z - source.params.z * (1.0f + slope * 2.0f);

    // 3x3 PCF。Point は 1 灯あたり 6 面ぶん作るため、ここでタップ数を
    // 増やすと影マップ生成側と合わせて一気に重くなる。縁を柔らかくする
    // 目的にはこれで足りる。
    float texel = 1.0f / map_size;
    float visibility = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            visibility += local_shadow_atlas.SampleCmpLevelZero(local_shadow_sampler,
                float3(uv + float2(x, y) * texel, (float) slice), reference);
        }
    }
    return visibility * (1.0f / 9.0f);
}

// Spot Light 1 灯ぶん。slice < 0 なら影マップを持たないライト。
float local_shadow_spot(int slice, float strength, float3 world_position,
                        float3 world_normal, float light_distance, float NoL)
{
    if (slice < 0) return 1.0f;
    float visibility = local_shadow_sample_slice(
        slice, world_position, world_normal, light_distance, NoL);
    return lerp(1.0f, visibility, saturate(strength));
}

// Point Light 1 灯ぶん。base_slice は 6 面の先頭。
float local_shadow_point(int base_slice, float strength, float3 light_position,
                         float3 world_position, float3 world_normal, float NoL)
{
    if (base_slice < 0) return 1.0f;
    float3 light_to_surface = world_position - light_position;
    int face = local_shadow_select_face(light_to_surface);
    float visibility = local_shadow_sample_slice(base_slice + face,
        world_position, world_normal, length(light_to_surface), NoL);
    return lerp(1.0f, visibility, saturate(strength));
}

#endif
