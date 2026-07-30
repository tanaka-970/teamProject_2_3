// 編集地形で平面を置き換える判定処理を定義する。
#ifndef __TERRAIN_CARVE_HLSLI__
#define __TERRAIN_CARVE_HLSLI__

// TerrainCarve.hlsli : スカルプト地形による「平らな地面の置き換え」判定
//
// 地面はGround.glbのタイル(y=0の平面)を敷き詰めているため、地形を掘り下げても
// そのままでは平面が穴を覆ってしまう。スカルプト済みチャンクの範囲では
// 平面(y≈0)のピクセルをdiscardし、代わりに地形メッシュが描画される。
//
// マスクは1テクセル=1チャンクのR8テクスチャ。C++側(TerrainSculptSystem)が
// t8/b4 へバインドする。バインドされないシーンでは enabled=0 のままなので
// 既存の見た目に影響しない。

Texture2D TerrainCarveMask : register(t8);

cbuffer CbTerrainCarve : register(b4)
{
    // x=領域原点X y=領域原点Z z=1/チャンクサイズ(m) w=有効フラグ
    float4 terrainCarveParams;
    // x=マスクの1辺テクセル数 y=discardするY帯の半幅(m) zw=予備
    float4 terrainCarveParams2;
};

bool ShouldCarveTerrain(float3 worldPosition)
{
    if (terrainCarveParams.w < 0.5f)
    {
        return false;
    }
    // 平面(y=0付近)だけを対象にする。段差のある構造物は消さない。
    if (abs(worldPosition.y) > terrainCarveParams2.y)
    {
        return false;
    }
    const float2 local =
        (worldPosition.xz - terrainCarveParams.xy) * terrainCarveParams.z;
    const float size = terrainCarveParams2.x;
    if (local.x < 0.0f || local.y < 0.0f || local.x >= size || local.y >= size)
    {
        return false;
    }
    const int3 texel = int3((int) local.x, (int) local.y, 0);
    return TerrainCarveMask.Load(texel).r > 0.5f;
}

#endif
