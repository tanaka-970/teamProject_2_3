#pragma once

namespace ReplayEngine::Rendering::Validation
{
    // 組み込みシェーダ 5 種の移植を検証する。
    //
    //   3dgp.exe --validate-shader-builtin
    //
    // 実際のプロジェクトの Shader/ を走査するので、
    // 実行時のカレントディレクトリがプロジェクト直下であること。
    //
    // 終了コードは 1200 から連番。
    // D3D デバイスは不要。d3dcompiler.dll だけ使う。
    int RunShaderBuiltInValidation();
}
