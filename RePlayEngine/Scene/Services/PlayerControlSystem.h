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
    // 【自動選出はしない】
    //   以前は「操作対象が未設定なら PlayerControllerComponent を持つ
    //   GameObject を 1 体自動で選ぶ」という処理を持っていた。これは次の 2 つの
    //   問題を生むため撤去した。
    //
    //     1. Prefab を配置しただけで操作対象が勝手に入れ替わる。
    //        「controlledObjectId は自動変更しない」という決まりを守れない。
    //     2. 「操作対象が設定されていない Scene」という状態を表現できず、
    //        Editor が警告を出せない。
    //
    //   操作対象を決めるのは次の 3 つだけ。
    //     - Scene ファイルへ保存された controlledObjectId
    //     - Inspector の「操作対象に設定」
    //     - Default Scene 作成時に配置した Prefab のルート
    //
    //   GameObject 名や Prefab 名で操作対象を探すことは決してしない。
    class PlayerControlSystem final
    {
    public:
        Core::ObjectID ControlledObject() const noexcept { return controlled_; }

        // 明示的に操作対象を指定する。
        void SetControlledObject(Core::ObjectID id) noexcept { controlled_ = id; }

        void Clear() noexcept { controlled_ = Core::ObjectID::Invalid(); }

        // 現在の操作対象がまだ Scene に居るかを確認する。
        //
        //   - 居ればそのまま維持する
        //   - 消えていれば無効化する（別の GameObject へは乗り移らない）
        //
        // 戻り値は確定した操作対象。居なければ無効 ID。
        Core::ObjectID Resolve(const Scene& scene);

        // 操作対象として保持し続けてよいか。
        //
        // 【重要】Controller の有無は条件に入れない。
        //   Controller を削除したら「操作できなくなる」だけで、
        //   操作対象の指定そのものは外れない。GameObject も表示も残る。
        //   ここで Controller を要求すると、削除した瞬間に操作対象が外れてしまう。
        static bool IsValidTarget(const Scene& scene, Core::ObjectID id);

        // 実際に操作できる状態か（Controller を持っているか）。
        // Editor の診断表示にだけ使う。操作対象の選択には使わない。
        static bool HasController(const Scene& scene, Core::ObjectID id);

    private:
        Core::ObjectID controlled_;
    };
}
