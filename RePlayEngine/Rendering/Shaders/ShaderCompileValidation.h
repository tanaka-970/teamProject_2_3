#pragma once

namespace ReplayEngine::Rendering::Validation
{
    // 実行時シェーダコンパイル基盤の検証。
    //
    //   3dgp.exe --validate-shader-compile
    //
    // 既存の --validate-* と同じく、終了コードで結果を返す。
    // 0 が成功。失敗すると最初に落ちた検査の番号を返す。
    // 終了コード帯は 900-949。
    //
    // D3D デバイスを必要としない。D3DCompile はデバイス非依存のため、
    // ヘッドレスで走らせられる。
    int RunShaderCompileValidation();
}
