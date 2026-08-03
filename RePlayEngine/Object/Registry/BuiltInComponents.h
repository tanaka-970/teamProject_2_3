#pragma once

namespace ReplayEngine::Core
{
    // 標準 Component を ComponentRegistry と PropertyRegistry へ登録する。
    //
    // 静的初期化に頼らず明示的に呼ぶ方式にしている。
    // 翻訳単位をまたぐ静的オブジェクトの初期化順に依存せず、
    // どこで何が登録されるかがコードを追うだけで分かるため。
    //
    // 呼び出し場所: framework::initialize() の早い段階で 1 回だけ。
    // Scene を作る前・Scene ファイルを読む前に呼ぶこと。
    // 二重に呼んでも登録は 1 回しか行われない（ComponentRegistry が重複を弾く）。
    //
    // 新しい Component を足すときは、この関数へ 1 行足すだけでよい。
    // それだけで次のすべてへ反映される。
    //   - Editor の Add Component 一覧
    //   - Inspector の表示と編集
    //   - Scene ファイルへの保存と復元
    //   - GameObject / Component の複製
    void RegisterBuiltInComponents();
}
