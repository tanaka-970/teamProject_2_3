#pragma once

namespace ReplayEngine::Runtime::Validation
{
    // Phase 3-5 (Behaviour / Event / Runtime API) の自動テスト。
    //
    // 終了コード帯:
    //   250-289 Behaviour Lifecycle / 290-329 Event / 330-369 Runtime API

    //   3dgp.exe --validate-behaviour
    int RunBehaviourValidation();

    //   3dgp.exe --validate-events
    int RunEventValidation();

    //   3dgp.exe --validate-runtime-api
    int RunRuntimeApiValidation();

    // CharacterMotor の接触を OnCollisionEnter / Stay / Exit へ配送する部分。
    // 終了コード帯: 370-409
    //
    //   3dgp.exe --validate-collision
    int RunCollisionValidation();
}
