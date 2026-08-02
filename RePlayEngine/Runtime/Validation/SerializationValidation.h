#pragma once

namespace ReplayEngine::Runtime::Validation
{
    // Phase 2 (Serialization Foundation) の自動テスト。
    //
    // どれも D3D11 も Window も使わないヘッドレス検証で、
    // 終了コードで結果を返す。既存の --validate-scene / --validate-prefab と同じ形。
    //
    // 割り当て済み終了コード帯:
    //   2-6 Scene / 20-28 Landscape / 30-41 Prefab / 50-56 Material /
    //   60-73 LargeScene・D3D11 / 80-139 Handle /
    //   140-179 Serialization / 180-209 MissingComponent / 210-249 SceneVersion

    // 型付き SerializedValue・参照型・StableID・Prefab 参照付け替えの検証。
    //   3dgp.exe --validate-serialization
    int RunSerializationValidation();

    // Missing Component / Unknown Property の保持と往復の検証。
    //   3dgp.exe --validate-missing-component
    int RunMissingComponentValidation();

    // Scene Version の移行 (v7〜v10 -> v11) と往復、新しすぎる形式の拒否。
    //   3dgp.exe --validate-scene-version
    int RunSceneVersionValidation();
}
