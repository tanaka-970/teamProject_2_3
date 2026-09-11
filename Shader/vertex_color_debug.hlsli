#ifndef REPLAY_VERTEX_COLOR_DEBUG_HLSLI
#define REPLAY_VERTEX_COLOR_DEBUG_HLSLI
// 実行時のチャンネル番号は０が通常表示、１～４がＲＧＢＡの濃淡表示。
#define REPLAY_VERTEX_COLOR_DEBUG 0
float4 ReplayVertexColorPreview(float4 color, uint channel)
{
    float value = color[min(max(channel, 1u), 4u) - 1u];
    return float4(value.xxx, 1.0f);
}
#endif
