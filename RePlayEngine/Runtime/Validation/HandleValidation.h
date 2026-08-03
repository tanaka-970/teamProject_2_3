#pragma once

namespace ReplayEngine::Runtime::Validation
{
    // ObjectHandle / ComponentHandle 基盤の自動テスト。
    //
    // 実行方法:
    //   3dgp.exe --validate-handles
    //
    // 戻り値:
    //   0                … 全項目合格
    //   80 以上の整数     … 最初に失敗した検査の番号（詳細は stderr へ出力）
    //
    // 既存の --validate-scene / --validate-prefab と同じ「終了コードで結果を返す
    // ヘッドレス検証」の形に揃えてある。D3D11 も Window も使わないため、
    // ビルドマシンでそのまま実行できる。
    //
    // 割り当て済み終了コード帯:
    //   2-6 Scene / 20-28 Landscape / 30-41 Prefab / 50-56 Material /
    //   60-73 LargeScene・D3D11 / 80-139 Handle (ここ)
    int RunHandleValidation();
}
