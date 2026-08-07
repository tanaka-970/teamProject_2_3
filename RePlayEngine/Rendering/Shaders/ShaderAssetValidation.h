#pragma once

namespace ReplayEngine::Rendering::Validation
{
    // シェーダのアセット化の検証。
    //
    //   #pragma 解析 / GUID 採番 / Catalog / 定数パッキング /
    //   cbuffer 自動生成 / 走査から D3DCompile まで / 保存検出
    //
    //   3dgp.exe --validate-shader-asset
    //
    // 終了コードは 950 から連番。項目数ぶん伸びるので上限は決めていない
    // （Windows の終了コードは 32bit なので問題ない）。
    // D3D デバイスは不要。d3dcompiler.dll だけ使う。
    int RunShaderAssetValidation();
}
