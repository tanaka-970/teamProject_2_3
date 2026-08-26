#pragma once

namespace ReplayEngine::Editor::Validation
{
    // Editor Camera の入力関門・操作継続・Preset 永続化をヘッドレスで検証する。
    // ImGui の描画フレームや実際の UI 操作は自動化しない。
    // 終了コード帯: 1900-1999
    int RunEditorCameraValidation();
}
