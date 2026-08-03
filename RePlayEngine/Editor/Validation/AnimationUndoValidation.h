#pragma once

namespace ReplayEngine::Editor::Validation
{
    // Pre-Scripting Stabilization / Phase A-1 の自動テスト。
    //
    // ---------------------------------------------------------------------
    // 【何を守る検証か】
    //
    //   Undo / Redo は SceneData のスナップショットを Scene へ流し込む方式で、
    //   内部で Scene::Clear() を通る。Clear() は started_ を false へ落とす。
    //
    //   ApplySceneData 自身は Start() を呼ばない仕様なので、
    //   呼び出し側が対で呼ぶ必要がある。実際、
    //     framework::load_object_scene
    //     RuntimeSceneService::SwapWorlds
    //   はどちらも対で呼んでいる。SceneEditHistory の Undo / Redo だけが
    //   これを欠いており、一度 Undo を押すと Scene 上の全 Component が
    //   二度と更新されなくなっていた（Animator が止まって見える症状）。
    //
    //   この検証は「Undo / Redo のあとも Scene の実行状態が保たれること」を
    //   Component の更新回数で直接確かめる。
    //   Ctrl+Z を無効化して症状を隠す形の修正では通らない。
    //
    // ---------------------------------------------------------------------
    // 終了コード帯:
    //   800-859 … animation-undo
    //
    //   3dgp.exe --validate-animation-undo
    int RunAnimationUndoValidation();
}
