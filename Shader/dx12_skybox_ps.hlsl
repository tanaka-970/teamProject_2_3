#include "dx12_lighting_common.hlsli"

struct SkyOutput
{
    float4 color : SV_Target0;
    float2 velocity : SV_Target1;
};

float2 SkyUvToNdc(float2 uv)
{
    return float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
}

float3 SkyDirection(float2 uv)
{
    const float4 farWorld = mul(float4(SkyUvToNdc(uv), 1.0f, 1.0f),
        inverseViewProjection);
    const float safeW = max(abs(farWorld.w), 1.0e-5f) *
        (farWorld.w < 0.0f ? -1.0f : 1.0f);
    return normalize(farWorld.xyz / safeW - cameraPosition.xyz);
}

float2 SkyDirectionToPano(float3 direction)
{
    return float2(atan2(direction.z, direction.x) * 0.15915494309189535f + 0.5f,
        asin(clamp(direction.y, -1.0f, 1.0f)) * 0.3183098861837907f + 0.5f);
}

float3 SkyPanoToDirection(float2 uv)
{
    const float longitude = (uv.x - 0.5f) * 6.28318530718f;
    const float latitude = (uv.y - 0.5f) * 3.14159265359f;
    const float cosLatitude = cos(latitude);
    return normalize(float3(cosLatitude * cos(longitude), sin(latitude),
        cosLatitude * sin(longitude)));
}

float SkyHash(float2 position)
{
    return frac(sin(dot(position, float2(127.1f, 311.7f))) * 43758.5453f);
}

float SkyValueNoise(float2 position)
{
    const float2 cell = floor(position);
    const float2 fraction = frac(position);
    const float2 smooth = fraction * fraction * (3.0f - 2.0f * fraction);
    const float lower = lerp(SkyHash(cell), SkyHash(cell + float2(1.0f, 0.0f)), smooth.x);
    const float upper = lerp(SkyHash(cell + float2(0.0f, 1.0f)),
        SkyHash(cell + float2(1.0f, 1.0f)), smooth.x);
    return lerp(lower, upper, smooth.y);
}

float SkyCloudCoverage(float3 lookupDirection, float4 parameters, float time, float seed)
{
    if (parameters.w <= 0.0f) return 0.0f;
    float2 uv = SkyDirectionToPano(lookupDirection) + parameters.xy * time;
    uv.x = frac(uv.x);
    uv.y = clamp(uv.y, 0.0f, 1.0f);
    const float2 samplePosition = uv * parameters.z;
    const float noise = (SkyValueNoise(samplePosition) +
        0.45f * SkyValueNoise(samplePosition * 2.17f + float2(seed, seed))) / 1.45f;
    const float horizon = smoothstep(-0.1f, 0.35f, lookupDirection.y);
    return saturate((noise - (1.0f - parameters.w)) * 5.0f) * horizon;
}

float2 SkyVelocityForWorldDirection(float2 uv, float3 direction)
{
    const float4 previousClip = mul(float4(direction, 0.0f), previousViewProjection);
    const float safeW = max(abs(previousClip.w), 1.0e-5f) *
        (previousClip.w < 0.0f ? -1.0f : 1.0f);
    float2 currentNdc = SkyUvToNdc(uv);
    const float2 previousNdc = previousClip.xy / safeW;
    currentNdc -= skyJitter.xy;
    const float2 correctedPreviousNdc = previousNdc - skyJitter.zw;
    return (currentNdc - correctedPreviousNdc) * float2(0.5f, -0.5f);
}

float2 SkyRotationVelocity(float2 uv, float3 direction)
{
    const float3 currentLookupDirection = normalize(mul(direction,
        (float3x3)skyRotation));
    const float3 previousWorldDirection = normalize(mul(currentLookupDirection,
        transpose((float3x3)previousSkyRotation)));
    return SkyVelocityForWorldDirection(uv, previousWorldDirection);
}

float2 SkyCloudVelocity(float2 uv, float3 lookupDirection, float2 speed)
{
    const float deltaTime = skyMotion.x - skyMotion.y;
    if (deltaTime == 0.0f || length(speed) <= 1.0e-6f)
        return SkyRotationVelocity(uv,
            normalize(mul(lookupDirection, transpose((float3x3)skyRotation))));
    float2 previousPano = SkyDirectionToPano(lookupDirection) - speed * deltaTime;
    previousPano.x = frac(previousPano.x);
    previousPano.y = clamp(previousPano.y, 0.0f, 1.0f);
    const float3 previousLookupDirection = SkyPanoToDirection(previousPano);
    const float3 previousWorldDirection = normalize(mul(previousLookupDirection,
        transpose((float3x3)previousSkyRotation)));
    return SkyVelocityForWorldDirection(uv, previousWorldDirection);
}

SkyOutput main(float4 position : SV_POSITION, float2 uv : TEXCOORD0)
{
    SkyOutput output;
    const float3 direction = SkyDirection(uv);
    const float3 lookupDirection = normalize(mul(direction, (float3x3)skyRotation));
    float3 skyColor = dx12SkySource.SampleLevel(dx12IblSampler, lookupDirection, 0).rgb;
    if (skyBlend.y > 0.5f && skyBlend.x > 0.0f)
        skyColor = lerp(skyColor, dx12SkySourceSecondary.SampleLevel(
            dx12IblSampler, lookupDirection, 0).rgb, saturate(skyBlend.x));

    const float cloud1 = SkyCloudCoverage(lookupDirection, cloudLayer1Params,
        skyMotion.x, 0.0f);
    const float cloud2 = SkyCloudCoverage(lookupDirection, cloudLayer2Params,
        skyMotion.x, 19.0f);
    const float cloudAlpha1 = cloud1 * saturate(cloudLayer1Color.a);
    const float cloudAlpha2 = cloud2 * saturate(cloudLayer2Color.a);
    skyColor = lerp(skyColor, cloudLayer1Color.rgb, cloudAlpha1);
    skyColor = lerp(skyColor, cloudLayer2Color.rgb, cloudAlpha2);

    const float daylight = sin(3.14159265359f * saturate(skyBlend.z));
    const float night = saturate(1.0f - smoothstep(0.05f, 0.5f, daylight));
    if (starParams.z > 0.5f && starParams.y > 0.0f)
    {
        const float2 starPosition = SkyDirectionToPano(lookupDirection) *
            float2(720.0f, 360.0f);
        const float2 starCell = floor(starPosition);
        const float starSeed = SkyHash(starCell);
        const float starProbability = step(1.0f - saturate(starParams.x), starSeed);
        const float starShape = 1.0f - smoothstep(0.04f, 0.22f,
            length(frac(starPosition) - 0.5f));
        const float twinkle = 0.75f + 0.25f * sin(skyMotion.x * 2.0f + starSeed * 40.0f);
        skyColor += starColor.rgb * starProbability * starShape * twinkle *
            starParams.y * night;
    }
    if (moonParams.z > 0.5f && moonParams.y > 0.0f)
    {
        const float3 moonLookupDirection = normalize(moonDirection.xyz);
        const float moonDot = saturate(dot(lookupDirection, moonLookupDirection));
        const float disc = smoothstep(1.0f - moonParams.x,
            1.0f - moonParams.x * 0.35f, moonDot);
        const float halo = smoothstep(1.0f - moonParams.x * 4.0f,
            1.0f - moonParams.x, moonDot) * 0.12f;
        skyColor += moonColor.rgb * (disc + halo) * moonParams.y * night;
    }

    float2 velocity = SkyRotationVelocity(uv, direction);
    if (cloud1 > 0.0f)
        velocity = lerp(velocity, SkyCloudVelocity(uv, lookupDirection,
            cloudLayer1Params.xy), cloud1);
    if (cloud2 > 0.0f)
        velocity = lerp(velocity, SkyCloudVelocity(uv, lookupDirection,
            cloudLayer2Params.xy), cloud2);
    output.color = float4(skyColor * max(iblParams.w, 0.0f), 1.0f);
    output.velocity = velocity;
    return output;
}
