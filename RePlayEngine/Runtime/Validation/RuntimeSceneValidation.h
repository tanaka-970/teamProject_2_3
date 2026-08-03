#pragma once

namespace ReplayEngine::Runtime::Validation
{
    // Phase 6 (Runtime Scene Service) の自動テスト。
    //
    // D3D11 も Window も使わないヘッドレス検証で、終了コードで結果を返す。
    // 既存の --validate-serialization / --validate-behaviour と同じ形。
    //
    // 割り当て済み終了コード帯:
    //   2-6 Scene / 20-28 Landscape / 30-41 Prefab / 50-56 Material /
    //   60-73 LargeScene・D3D11 / 80-139 Handle / 140-179 Serialization /
    //   180-209 MissingComponent / 210-249 SceneVersion / 250-289 Behaviour /
    //   290-329 Event / 330-369 RuntimeApi / 370-409 Collision /
    //   410-459 RuntimeScene
    //
    // 検証で使う Scene ファイルは Saved/Validation/RuntimeScene/ 配下へ
    // その場で書き出す。既存の Scene 原本は一切読み書きしない。
    //   3dgp.exe --validate-runtime-scene
    int RunRuntimeSceneValidation();
}
