#pragma once

namespace ReplayEngine::Runtime::Validation
{
    // Phase 9 の耐久・回帰検査。
    //
    // 1 回で通るかどうかではなく、繰り返しても壊れないかを見る。
    //   - 100 回以上の Scene Load / Reload
    //   - 100 回以上の連続した読み込み失敗
    //   - 1000 以上の GameObject
    //   - 削除予約中の切り替え
    //   - Event 購読と Collision 接触状態の持ち越し
    //   - 古い ObjectHandle
    //   - Play 開始 / 停止の反復
    //   - SceneFlow の履歴上限と往復
    //
    // 終了コード帯: 580-619
    //   3dgp.exe --validate-stress
    int RunStressValidation();
}
