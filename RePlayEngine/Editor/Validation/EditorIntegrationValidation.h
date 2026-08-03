#pragma once

// この検証は Editor 側に置く。
// Runtime から Editor を参照しない方針を守るため、
// EditorContext / EditorSelection に触る検証は Editor モジュールが持つ。
namespace ReplayEngine::Editor::Validation
{
    // Phase 8 (Editor 統合) の自動テスト。
    //
    // ---------------------------------------------------------------------
    // 【ImGui の操作そのものは検証しない】
    //
    //   ボタンを押した結果を自動で確かめるには、Editor を実際に立ち上げて
    //   入力を流し込む仕組みが要る。D3D11 と Window が必要になるため、
    //   ヘッドレスの検証には載らない。
    //
    //   代わりに「UI が呼ぶのと同じ内部 API」を直接叩いて、
    //   データが壊れないこと・状態が正しく遷移することを確かめる。
    //   UI の描画そのものは手動確認手順へ回す。
    //
    //   この分け方にしておくと、UI を作り替えても検証は生き残る。
    //
    // ---------------------------------------------------------------------
    // 終了コード帯:
    //   520-579 … Editor 統合（この関数）
    //
    //   3dgp.exe --validate-editor-integration
    int RunEditorIntegrationValidation();
}
