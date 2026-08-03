#pragma once

namespace ReplayEngine::Runtime::Validation
{
    // Phase 7 (Scene Flow / Startup Scene) の自動テスト。
    //
    // D3D11 も Window も使わないヘッドレス検証。
    // ProjectSettings の往復は文字列ストリームで行い、
    // Scene の読み込みは Saved/Validation/SceneFlow/ へ書いた一時ファイルを使う。
    // 既存の Scene 原本・プロジェクト設定原本には一切触れない。
    //
    // 終了コード帯:
    //   460-507 … Engine 側（この関数）
    //   508-519 … Game 側の SceneTransitionBehaviour
    //             （Game::RunSceneTransitionValidation。Runtime から Game を
    //               参照しないよう、呼び出しは main.cpp で 2 段に分けてある）
    //
    //   3dgp.exe --validate-scene-flow
    int RunSceneFlowValidation();
}
