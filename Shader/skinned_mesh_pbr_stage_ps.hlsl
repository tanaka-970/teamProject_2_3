// 地形法線を補正しながらステージをPBR描画するピクセルシェーダー。
// ステージ専用PBRシェーダー。
//
// インポートした地形は、三角形化した境界で法線が分断・劣化している場合がある。
// ワールド位置の微分から基準法線を求め、同一平面上の床・壁を連続して見せる。
// 接線空間の法線マップがある場合は、その効果も引き続き適用する。
#include "skinned_mesh.hlsli"
#include "pbr_brdf.hlsli"

cbuffer STAGE_MATERIAL_CONSTANT_BUFFER : register(b11)
{
    float4 stage_options;
    float4 stage_base_tint;
};

float4 main(VS_OUT pin) : SV_TARGET
{
    float4 base_sample = pbr_base_color.Sample(pbr_sampler_anisotropic, pin.texcoord);
    float3 base = pow(max(base_sample.rgb, 0.0f), 2.2f) * stage_base_tint.rgb;

// 現在のFBXステージが持つのはベースカラーと法線マップだけである。
// 未接続のt2／t3をORMとして読むと地形が金属化するため、
// ORMを明示した材質へ移行するまでは非金属向けの既定値を使う。
    float3 imported_normal = normalize(pin.world_normal.xyz);
    float3 geometric_normal = normalize(cross(ddy(pin.world_position.xyz), ddx(pin.world_position.xyz)));
    if (dot(geometric_normal, imported_normal) < 0.0f) geometric_normal = -geometric_normal;
    float3 base_normal = normalize(lerp(imported_normal, geometric_normal,
        saturate(stage_options.x)));

    float3 T = pin.world_tangent.xyz;
    T -= base_normal * dot(base_normal, T);
    if (dot(T, T) < 1.0e-5f)
    {
        float3 seed = abs(base_normal.y) < 0.999f
            ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        T = cross(seed, base_normal);
    }
    T = normalize(T);
    float3 B = normalize(cross(base_normal, T) * pin.world_tangent.w);
    float3 sampled_normal = pbr_normal_map.Sample(
        pbr_sampler_anisotropic, pin.texcoord).xyz * 2.0f - 1.0f;
    sampled_normal = normalize(sampled_normal.x * T + sampled_normal.y * B
        + sampled_normal.z * base_normal);
    float3 N = normalize(lerp(base_normal, sampled_normal,
        saturate(stage_options.y)));
    float3 V = normalize(camera_position.xyz - pin.world_position.xyz);

    // 地形は鏡面GGXとspecular IBLを使わず、粗い非金属面として照明する。
    // metallic=0でも残る誘電体反射を経路ごと外し、広い床面の金属光沢を防ぐ。
    float3 L = normalize(-light_direction.xyz);
    float NoL = saturate(dot(N, L));
    float3 direct = (base / PI) * NoL * directional_color.rgb * directional_color.a;
    direct *= lerp(1.0f, sample_shadow(pin.world_position.xyz), saturate(stage_options.w));
    direct += evaluate_point_lights(pin.world_position.xyz, N, V, base, 1.0f, 0.0f);
    direct += evaluate_spot_lights(pin.world_position.xyz, N, V, base);

    const float3 dielectric_f0 = float3(0.02f, 0.02f, 0.02f);
    float3 indirect = ibl_radiance_lambertian(N, V, 1.0f, base, dielectric_f0)
        * ibl_params.x * saturate(stage_options.z);
    float3 color = (direct + indirect) * max(ibl_params.w, 0.0f);
    return float4(color * pin.color.rgb, base_sample.a * pin.color.a);
}
