// 形状を各カスケードのシャドウマップへ複製するジオメトリシェーダー。
#include "csm_common.hlsli"

struct GS_IN  { float4 world_position : POSITION; };
struct GS_OUT
{
    float4 position : SV_POSITION;
    uint   slice    : SV_RenderTargetArrayIndex;
};

[maxvertexcount(CSM_CASCADE_COUNT * 3)]
void main(triangle GS_IN ipt[3], inout TriangleStream<GS_OUT> stream)
{
    [unroll] for (int c = 0; c < CSM_CASCADE_COUNT; ++c)
    {
        [unroll] for (int v = 0; v < 3; ++v)
        {
            GS_OUT o;
            o.position = mul(ipt[v].world_position, csm_view_projection[c]);
            o.slice    = c;
            stream.Append(o);
        }
        stream.RestartStrip();
    }
}
