// Deferred照明パスで共有するG-Buffer参照と定数を定義する。
#ifndef __DEFERRED_LIGHTING_HLSLI__
#define __DEFERRED_LIGHTING_HLSLI__

// ライティングパス共通定義 (メモを読んで光を足し算する係たちの土台)
// 各ライトのPS(Directional/Point/Spot/Emissive/Indirect)はこれをincludeする。
// 出力は加算ブレンドで重ねるので、各パスは「自分の光のぶんだけ」を返すように、
#include "GBuffer.hlsli"

// メモ帳4枚 + シャドウマップ
Texture2D GBufferBaseColor   : register(t0);
Texture2D GBufferEmissive    : register(t1);
Texture2D GBufferNormalDepth : register(t2);
Texture2D GBufferMaterial    : register(t3);
Texture2D ShadowMap          : register(t4);
// IBL用の環境マップ。iblParams.w で cube / latlong を使い分ける。
TextureCube EnvironmentCube  : register(t5);
Texture2D   EnvironmentLatLong : register(t6);

SamplerState PointSampler : register(s0);
SamplerComparisonState ShadowSampler : register(s1);
SamplerState LinearSampler : register(s2);

static const float IBL_PI = 3.14159265f;

// シーン共通の定数
cbuffer CbDeferredScene : register(b0)
{
    row_major float4x4 inverseViewProjection; // 深度→ワールド座標の復元用
    row_major float4x4 cameraViewProjection;  // ローカルライトの遮蔽判定用
    row_major float4x4 shadowViewProjection;  // シャドウマップ参照用
    float4 cameraPosition;      // xyz=カメラ位置
    float4 directionalDirection; // xyz=太陽光の向き
    float4 directionalColor;    // rgb=太陽光の色*強さ
    float4 ambientColor;        // rgb=環境光の色 a=強さ
    float4 shadowParams;        // x=有効 y=バイアス z=影の濃さ w=1テクセル
    float4 lightCounts;         // x=ポイント数 y=スポット数 z=ローカル露出数
    float4 toonRimColor[3];
    float4 toonSpecularColor[3];
    float4 toonShadowTint[3];
    float4 toonParams[3];
    float4 toonSpecularParams[3];
    float4 toonExtraParams[3];
    float4 pbrToonShadowColor[3];
    float4 pbrToonRimColor[3];
    float4 pbrToonParams0[3];
    float4 pbrToonParams1[3];
    float4 pbrToonParams2[3];
    float4 flatOverlayColor[3];
    float4 postParams;
    float4 fogColor;  // xyzが確定した霧の色、wが有効フラグ。
    float4 fogParams; // 開始距離、範囲、濃さ、予備。
    float4 categorySaturation; // x=ステージ y=プレイヤー z=エネミー の彩度 (1=そのまま <1=くすませる >1=鮮やか) w=予備
    float4 iblParams; // x=拡散強度 y=反射強度 z=有効(0/1) w=isCube(0/1)
};
// 共通の陰影計算を使う。
#include "ShadingModels.hlsli"

// 方向ベクトルから環境マップの色を得る(mipで粗さを表現)。
// cube と 緯度経度(2D)マップの両対応。cbuffer宣言後に置くこと(iblParams参照)。
float3 SampleEnvironment(float3 direction, float mip)
{
    if (iblParams.w > 0.5f)
    {
        return EnvironmentCube.SampleLevel(LinearSampler, direction, mip).rgb;
    }
    float2 uv;
    uv.x = atan2(direction.z, direction.x) / (2.0f * IBL_PI) + 0.5f;
    uv.y = acos(clamp(direction.y, -1.0f, 1.0f)) / IBL_PI;
    return EnvironmentLatLong.SampleLevel(LinearSampler, uv, mip).rgb;
}

// ポイント/スポットライトの配列 (ライト用の定数バッファ)
static const int MAX_POINT_LIGHTS = 16;
static const int MAX_SPOT_LIGHTS = 16;
static const int MAX_LOCAL_EXPOSURE_LIGHTS = 16;
cbuffer CbDeferredLights : register(b1)
{
    float4 pointPositionRadius[MAX_POINT_LIGHTS]; // xyz=位置 w=届く距離
    float4 pointColor[MAX_POINT_LIGHTS];          // rgb=色*強さ
    float4 spotPositionRange[MAX_SPOT_LIGHTS];    // xyz=位置 w=届く距離
    float4 spotDirectionInner[MAX_SPOT_LIGHTS];   // xyz=向き w=cos(内側角)
    float4 spotColorOuter[MAX_SPOT_LIGHTS];       // rgb=色*強さ w=cos(外側角)
    float4 localExposurePositionRadius[MAX_LOCAL_EXPOSURE_LIGHTS]; // xyz=位置 w=半径
    float4 localExposureParams[MAX_LOCAL_EXPOSURE_LIGHTS]; // x=減光強度 y=対象マスク
};

// フルスクリーンVSからの入力
struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// 画面上のこの場所のメモを全部読む
GBufferData SampleGBuffer(float2 uv)
{
    float4 rt0 = GBufferBaseColor.Sample(PointSampler, uv);
    float4 rt1 = GBufferEmissive.Sample(PointSampler, uv);
    float4 rt2 = GBufferNormalDepth.Sample(PointSampler, uv);
    float4 rt3 = GBufferMaterial.Sample(PointSampler, uv);
    return DecodeGBuffer(rt0, rt1, rt2, rt3, uv, inverseViewProjection);
}

// G-Buffer深度を光源までレイマーチし、画面内に見えている遮蔽物で
// ポイント/スポットライトを遮る。専用シャドウマップ外でも光漏れを抑える。
float SampleLocalLightVisibility(float3 worldPosition, float3 lightPosition)
{
    float3 ray = lightPosition - worldPosition;
    float rayLength = length(ray);
    if (rayLength <= 0.001f) return 1.0f;
    const float3 rayDirection = ray / rayLength;

    static const int STEP_COUNT = 8;
    [unroll]
    for (int stepIndex = 1; stepIndex <= STEP_COUNT; ++stepIndex)
    {
        float t = (float)stepIndex / (float)(STEP_COUNT + 1);
        float3 samplePosition = worldPosition + ray * t;
        float4 clip = mul(float4(samplePosition, 1.0f), cameraViewProjection);
        if (clip.w <= 0.0001f) continue;

        float3 projected = clip.xyz / clip.w;
        float2 uv = projected.xy * float2(0.5f, -0.5f) + 0.5f;
        if (uv.x <= 0.0f || uv.x >= 1.0f || uv.y <= 0.0f || uv.y >= 1.0f ||
            projected.z <= 0.0f || projected.z >= 1.0f)
        {
            continue;
        }

        float sceneDepth = GBufferNormalDepth.SampleLevel(PointSampler, uv, 0).w;
        if (sceneDepth >= 0.999999f) continue;

        float2 ndcXY = float2(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f);
        float4 scenePosition4 = mul(
            float4(ndcXY, sceneDepth, 1.0f), inverseViewProjection);
        float3 scenePosition = scenePosition4.xyz / scenePosition4.w;

        // 面と光源の間にある物だけを遮蔽物にする。
        // 手前の別物で、カメラ依存の偽の影が出るのを防ぐ。
        const float blockerAlongRay = dot(
            scenePosition - worldPosition, rayDirection);
        const float surfaceBias = max(0.35f, rayLength * 0.01f);
        if (blockerAlongRay <= surfaceBias ||
            blockerAlongRay >= rayLength - 0.08f)
        {
            continue;
        }
        const float3 closestPointOnRay =
            worldPosition + rayDirection * blockerAlongRay;
        const float blockerRadius = max(0.16f, blockerAlongRay * 0.012f);
        if (distance(scenePosition, closestPointOnRay) > blockerRadius)
        {
            continue;
        }

        float sampleCameraDistance = distance(cameraPosition.xyz, samplePosition);
        float sceneCameraDistance = distance(cameraPosition.xyz, scenePosition);
        float thickness = max(0.18f, rayLength * 0.0125f);
        if (sceneCameraDistance + thickness < sampleCameraDistance)
        {
            return 0.0f;
        }
    }
    return 1.0f;
}

// メモ→ライティング用の表面情報へ
// 太陽シャドウマップを使った「空の見え具合」。0=屋根の下 1=空が見える。
// 旧実装はスクリーンスペースのレイマーチで空可視を0/1判定していたが、
// レイが画面外へ出ると「可視」扱いになりカメラ依存のまだら模様
// (壁や床に出るブロック状の白いアーティファクト)が発生していた。
// シャドウマップ判定はカメラ非依存で滑らか(PCF)なので模様が出ない。
// 対象点の真上方向に数点サンプルして平均する: 屋根の下は全点が影で0、
// 屋外の壁ぎわは上空サンプルが日向になり滑らかに1へ戻る。
float SampleSkyVisibility(float3 worldPosition)
{
    if (shadowParams.x <= 0.0f)
    {
        return 1.0f;
    }

    static const float SAMPLE_HEIGHTS[3] = { 0.5f, 4.0f, 12.0f };
    float visibility = 0.0f;
    [unroll]
    for (int sampleIndex = 0; sampleIndex < 3; ++sampleIndex)
    {
        const float3 samplePosition =
            worldPosition + float3(0.0f, SAMPLE_HEIGHTS[sampleIndex], 0.0f);
        const float4 shadowPosition =
            mul(float4(samplePosition, 1.0f), shadowViewProjection);
        const float3 projected = shadowPosition.xyz / shadowPosition.w;
        const float2 uv = projected.xy * float2(0.5f, -0.5f) + 0.5f;
        if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f ||
            projected.z < 0.0f || projected.z > 1.0f)
        {
            // シャドウマップの範囲外は「空が見える」扱い(従来のIBLの見た目を維持)
            visibility += 1.0f;
            continue;
        }

        float pcf = 0.0f;
        const float2 texel = shadowParams.w.xx;
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                pcf += ShadowMap.SampleCmpLevelZero(
                    ShadowSampler,
                    uv + float2(x, y) * texel,
                    projected.z - shadowParams.y);
            }
        }
        visibility += pcf / 9.0f;
    }
    return visibility / 3.0f;
}

SurfaceInfo BuildSurface(GBufferData g)
{
    SurfaceInfo s = (SurfaceInfo) 0;
    s.baseColor    = g.baseColor;
    s.N            = g.worldNormal;
    s.V            = normalize(cameraPosition.xyz - g.worldPosition);
    s.metallic     = g.metallic;
    s.roughness    = g.roughness;
    s.ambientOcclusion = g.ambientOcclusion;
    s.exposure     = g.exposure;
    s.toonSteps    = g.toonSteps;
    s.shadingModel = g.shadingModel;
    return s;
}

// 何も描かれていないピクセル(空)かどうか
bool IsEmptyPixel(GBufferData g)
{
    return g.ndcDepth >= 0.999999f;
}

float ComputeDistanceFogFactor(float3 worldPosition)
{
    float fogFactor = 0.0f;
    if (fogColor.a > 0.0f && fogParams.z > 0.0f)
    {
        if (fogParams.w > 0.5f)
        {
            fogFactor = 1.0f;
        }
        else
        {
            float distanceFromCamera = distance(cameraPosition.xyz, worldPosition);
            float fadeRange = max(fogParams.y, 1.0f);
            float linearT = saturate((distanceFromCamera - fogParams.x) / fadeRange);
            float easedT = linearT * linearT * (3.0f - 2.0f * linearT);
            fogFactor = saturate(easedT * fogParams.z);
        }
    }
    return fogFactor;
}

// 露出やスペキュラで1.0を大きく超えた値だけを、いきなり真っ白にクリップせず
// なだらかに丸め込む簡易トーンマップ。
// 前回はRGB各chを個別にReinhard圧縮していたため、1.0未満の通常の色まで
// 暗く・彩度落ちして見えてしまっていた(空の夕焼けが灰色っぽくなった原因)。
// 今回は「輝度(luma)が1.0以下ならそのまま」「1.0を超えた分だけ丸める」に変更し、
// 色味(色相・彩度)を保ったまま明るさの飽和だけ緩和する。
float3 ToneMap(float3 c)
{
    float luma = max(dot(c, float3(0.2126f, 0.7152f, 0.0722f)), 0.0f);
    if (luma <= 1.0f)
    {
        return c; // 通常範囲は今まで通り(色を歪めない)
    }
    float excess = luma - 1.0f;
    float mapped = 1.0f + excess / (1.0f + excess); // 1.0超のぶんだけなだらかに圧縮
    return c * (mapped / luma); // 色相・彩度を保ったまま明るさだけ圧縮
}

// シェーディングモデルID → 彩度カテゴリ (0=ステージ 1=プレイヤー 2=エネミー)
int SaturationCategory(int shadingModel)
{
    switch (shadingModel)
    {
        case SHADING_MODEL_TOON_PLAYER:
        case SHADING_MODEL_PBR_TOON_PLAYER:
        case SHADING_MODEL_UNLIT_PLAYER:
        case SHADING_MODEL_PHONG_PLAYER:
        case SHADING_MODEL_PBR_PLAYER:
            return 1;
        case SHADING_MODEL_TOON_ENEMY:
        case SHADING_MODEL_PBR_TOON_ENEMY:
        case SHADING_MODEL_UNLIT_ENEMY:
        case SHADING_MODEL_PHONG_ENEMY:
        case SHADING_MODEL_PBR_ENEMY:
            return 2;
        default:
            return 0;
    }
}

// ステージ/プレイヤー/エネミーごとの彩度調整。
// ポスト全体の彩度と違い、対象のライティング結果だけを鮮やかに/くすませられる。
// 1.0=そのまま、<1でモノクロ寄り、>1で鮮やかに(輝度は変えず色差だけ伸ばす)。
// Sky(SkyMap/SkyBox)やEmissive(発光VFX)はこの経路を通らないため影響しない。
float3 ApplyCategorySaturation(float3 c, int shadingModel)
{
    float sat = max(categorySaturation[SaturationCategory(shadingModel)], 0.0f);
    float luma = dot(c, float3(0.2126f, 0.7152f, 0.0722f));
    // sat>1では外挿になりチャンネルが負に振れ得るので0で下留めする
    return max(lerp(float3(luma, luma, luma), c, sat), 0.0f);
}

// シャドウマップで「太陽から見て隠れているか」を調べる (3x3 PCF)
float SampleShadowVisibility(float3 worldPosition)
{
    if (shadowParams.x <= 0.0f)
    {
        return 1.0f;
    }

    float4 shadowPosition = mul(float4(worldPosition, 1.0f), shadowViewProjection);
    float3 projected = shadowPosition.xyz / shadowPosition.w;
    float2 uv = projected.xy * float2(0.5f, -0.5f) + 0.5f;
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f ||
        projected.z < 0.0f || projected.z > 1.0f)
    {
        return 1.0f;
    }

    float visibility = 0.0f;
    float2 texel = shadowParams.w.xx;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            visibility += ShadowMap.SampleCmpLevelZero(
                ShadowSampler,
                uv + float2(x, y) * texel,
                projected.z - shadowParams.y);
        }
    }
    visibility /= 9.0f;

    // 影の濃さ(strength)ぶんだけ暗くする
    return lerp(1.0f, visibility, shadowParams.z);
}

#endif
