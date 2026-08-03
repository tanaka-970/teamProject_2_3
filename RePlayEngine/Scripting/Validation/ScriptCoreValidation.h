#pragma once

namespace ReplayEngine::Scripting::Validation
{
    // Phase 1（共通スクリプト基盤）の自動テスト。
    //
    // Lua も .NET も使わない。MockScriptBackend の 2 種類のスクリプト型で、
    // 基盤だけが正しいことを確かめる。
    //
    // ---------------------------------------------------------------------
    // 終了コード帯:
    //   620-679  script-core
    //   680-739  script-lifecycle
    //   740-799  script-serialization
    //   800-     Phase 2 以降のスクリプト系コマンド用に予約
    //
    // 全件成功なら 0。失敗したときは「最初に失敗した検査のコード」を返す。
    // コンソールへ PASS と出すだけで 0 を返す実装にはしない。
    // ---------------------------------------------------------------------

    // Schema の共有・目録・ScriptTypeID の導出・予約接頭辞の衝突回避。
    //
    //   3dgp.exe --validate-script-core
    int RunScriptCoreValidation();

    // Awake / OnEnable / Start / Update / OnDisable / OnDestroy の順序と、
    // Enable / Disable の 3 要件、World 入れ替え時の準備順序。
    //
    //   3dgp.exe --validate-script-lifecycle
    int RunScriptLifecycleValidation();

    // 保存・読み込み・Clone・Undo と、型が解決できないときの値の保護。
    //
    //   3dgp.exe --validate-script-serialization
    int RunScriptSerializationValidation();
}
