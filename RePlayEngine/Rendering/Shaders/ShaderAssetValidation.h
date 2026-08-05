#pragma once

namespace ReplayEngine::Rendering::Validation
{
    // シェーダのアセット化（#pragma 解析 / GUID 採番 / Catalog）の検証。
    //
    //   3dgp.exe --validate-shader-asset
    //
    // 終了コード帯は 950-999。D3D デバイスは不要。
    int RunShaderAssetValidation();
}
