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
}
