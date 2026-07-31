#pragma once

#include "../../Core/ObjectID/ObjectID.h"

namespace ReplayEngine::Scene
{
    class Scene;
}

namespace ReplayEngine::Scene
{
    // 「今どの GameObject を操作しているか」を持つだけの小さなサービス。
    //
    // Singleton ではない。framework が値メンバとして 1 つ所有する。
    //
    // なぜ型ではなく ObjectID で持つか:
    //   Player を巨大クラスや巨大 Component で表すと、
    //   操作対象を人型からメカ・ドローンへ変えるたびに型を差し替えることになる。
    //   ObjectID で持てば、GameObject のクラス型は一切変わらない。
    //   「プレイヤーとは、操作対象に選ばれている GameObject」という定義になる。
    //
    // 誰が操作対象になれるか:
    //   PlayerControllerComponent を持つ GameObject。
    //   Resolve() はその条件で自動選出も行う。
    class PlayerControlSystem final
    {
    public:
        Core::ObjectID ControlledObject() const noexcept { return controlled_; }

        // 明示的に操作対象を指定する。
        void SetControlledObject(Core::ObjectID id) noexcept { controlled_ = id; }

        void Clear() noexcept { controlled_ = Core::ObjectID::Invalid(); }

        // 現在の操作対象を確認し、必要なら選び直す。
        //
        //   - 対象が消えていたら解除する
        //   - 未設定なら PlayerControllerComponent を持つ GameObject を 1 体選ぶ
        //   - 複数いても操作対象は必ず 1 体だけ
        //
        // 戻り値は確定した操作対象。見つからなければ無効 ID。
        Core::ObjectID Resolve(const Scene& scene);

        // 操作対象として保持し続けてよいか。
        //
        // 【重要】Controller の有無は条件に入れない。
        //   Controller を削除したら「操作できなくなる」だけで、
        //   操作対象の指定そのものは外れない。GameObject も表示も残る。
        //   ここで Controller を要求すると、削除した瞬間に別の GameObject へ
        //   勝手に乗り移ってしまう。
        static bool IsValidTarget(const Scene& scene, Core::ObjectID id);

        // 実際に操作できる状態か（Controller を持っているか）。
        // 診断表示と、自動選出の候補判定に使う。
        static bool HasController(const Scene& scene, Core::ObjectID id);

    private:
        Core::ObjectID controlled_;
    };
}
