// シーン、カメラ、オブジェクトの共通定数を定義する。
cbuffer CbScene : register(b0)
{
    row_major float4x4 viewProjection;
    row_major float4x4 shadowViewProjection;
    float4 lightDirection;
    float4 lightColor;
    float4 cameraPosition;
    float4 shadowParams; // 有効、深度補正、濃さ、一画素の大きさ。
};

// モデルごとの定数を受け取る。
cbuffer CbObject : register(b2)
{
    row_major float4x4 world;
};
