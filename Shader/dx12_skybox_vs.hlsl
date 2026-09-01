struct VSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

VSOut main(uint vertexId : SV_VertexID)
{
    VSOut output;
    const float2 p = vertexId == 0 ? float2(-1.0f, -1.0f) :
        (vertexId == 1 ? float2(-1.0f, 3.0f) : float2(3.0f, -1.0f));
    output.position = float4(p, 1.0f, 1.0f);
    output.uv = float2(p.x * 0.5f + 0.5f, 0.5f - p.y * 0.5f);
    return output;
}
