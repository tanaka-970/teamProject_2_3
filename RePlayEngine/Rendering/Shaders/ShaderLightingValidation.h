#pragma once

namespace ReplayEngine::Rendering::Validation
{
    // Shader Asset の replay_lighting と、GBuffer の照明モデル分離を検証する。
    //jjshkjwbkndohhclmmmsnnnxkkdmmisjjjdfnkkxnjdkksnxmmktejusudumodeurhhihhs
    //   3dgp.exe --validate-shader-lighting
    //
    // D3D11 デバイスは不要。実プロジェクトの Shader/ を読むため、
    // カレントディレクトリはプロジェクト直下にすること。
    int RunShaderLightingValidation();
}
