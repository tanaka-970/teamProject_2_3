#pragma once

namespace ReplayEngine::Scripting::Validation
{
    // C# Behaviour 実装の実機寄り検証。
    //
    //   3dgp.exe --validate-csharp-scripting
    int RunCSharpScriptValidation();

    // 起動時の C# ビルド失敗から、ディスクに残った Assembly で復旧するか。
    // hostfxr の初期化は 1 プロセス 1 回なので、別コマンドに分けてある。
    //
    //   3dgp.exe --validate-csharp-startup-recovery
    int RunCSharpStartupRecoveryValidation();
    // 新しい Authoring API (MonoBehaviour) の発見・生成・実行・Inspector。
    // Legacy との同居もここで確かめる。
    //
    //   3dgp.exe --validate-monobehaviour
    int RunMonoBehaviourValidation();

    // C# の Public Landscape API から、既存の地形実装へ届くか。
    //
    //   3dgp.exe --validate-landscape-script
    int RunLandscapeScriptValidation();
}
