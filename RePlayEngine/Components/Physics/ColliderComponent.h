#pragma once

#include "../../Object/Component/Component.h"
#include "../../Physics/CollisionLayers.h"
#include "../../Scene/Services/IPhysicsQueryService.h"

#include <DirectXMath.h>

#include <string>

namespace ReplayEngine::Components
{
    // Collider の形状種別。
    //
    // 1 つの Component の中で enum 切り替えにはしない。
    // 形状ごとに独立した Component にしてあり、この enum は
    // 「登録表と問い合わせ側が形状を見分けるための札」にすぎない。
    enum class ColliderShape
    {
        Sphere,
        Box,
        Capsule,
        Mesh,
        Landscape,
    };

    const char* ToString(ColliderShape shape) noexcept;

    // 全 Collider の共通基底。
    //
    // 【ここに集約するもの】
    //   ColliderID / 保存用の Collider Key / 中心オフセット /
    //   Layer / Mask / Trigger / Debug 表示
    //
    // 【ここに置かないもの】
    //   形状パラメータ（半径・サイズ・高さ・Mesh Asset）。
    //   形状ごとの Component が自分で持つ。
    //
    // 【ライフサイクルの扱い】
    //   OnAttach / OnDetach / OnEnable / OnDisable は基底で final にしている。
    //   派生が override して基底呼び出しを忘れると、登録表と実体がずれて
    //   「消したはずの Collider に当たり続ける」不具合になるため、
    //   そもそも override できない形にした。
    //   派生は OnColliderAttach / OnColliderDetach / OnColliderEnable /
    //   OnColliderDisable を使う。
    //
    // 【ID の 2 本立て】
    //   collider_id_ … 実行時のみ有効。プロセス内で一意。保存しない。
    //                  Hit の出所表示と、実行中の参照解決に使う。
    //   collider_key … Scene / Prefab へ保存される。同じ GameObject 内で一意。
    //                  「何番目の Collider か」ではないので、順序が変わっても壊れない。
    //                  Character Motor の Primary Collider 参照はこちらを保存する。
    class ColliderComponent : public Core::Component
    {
    public:
        // ---- 形状ごとの実装が答えるもの ------------------------------------

        virtual ColliderShape Shape() const noexcept = 0;

        // ワールド空間の AABB。Broad Phase の粗い絞り込みに使う。
        // 形状が確定できない場合（Mesh の Cook 前など）は false を返す。
        virtual bool ComputeWorldBounds(DirectX::XMFLOAT3& minimum,
            DirectX::XMFLOAT3& maximum) const = 0;

        // Character Motor の移動用 Collider として選べるか。
        // 対応形状だけが true を返し、Trigger は呼び出し側で除外する。
        virtual bool UsableAsCharacterShape() const noexcept { return false; }

        // Inspector へ出す警告文。空なら問題なし。
        virtual std::string StatusMessage() const { return std::string(); }

        // ---- 共通の読み取り -------------------------------------------------

        Scene::ColliderID GetColliderID() const noexcept { return collider_id_; }

        // Owner のワールド位置へ center_offset を足した中心。
        // Owner が居ない場合は原点を返す（Inspector 描画中に消えても落ちない）。
        DirectX::XMFLOAT3 WorldCenter() const noexcept;

        // 押し戻し・接地の対象になるか。Trigger は常に対象外。
        bool BlocksMovement() const noexcept
        {
            return !is_trigger && ActiveInHierarchy();
        }

        // ---- 保存される共通設定 ---------------------------------------------

        // この GameObject の中で Collider を一意に指す番号。1 以上。
        // 0 は「未設定」を表す。OnAttach で自動採番され、
        // Scene から読み込んだ値があればそちらで上書きされる。
        int collider_key = 0;

        // 中心のずらし。Owner の Transform は動かさない。
        DirectX::XMFLOAT3 center_offset{ 0.0f, 0.0f, 0.0f };

        // 所属レイヤーと、衝突を受け付けるレイヤーのビットマスク。
        int collision_layer = Physics::CollisionLayers::Default;
        int collision_mask = Physics::CollisionLayers::all_layers_mask;

        // 通り抜けるが接触は検出したい場合。
        // Character Motor の押し戻しと接地からは必ず除外される。
        bool is_trigger = false;

        // Editor の Scene View へ形状を描くか。
        bool debug_draw = true;

    protected:
        ColliderComponent();

        virtual void OnColliderAttach() {}
        virtual void OnColliderDetach() {}
        virtual void OnColliderEnable() {}
        virtual void OnColliderDisable() {}

        // 所属 Scene の構成世代を進める。
        // 登録表を持つ側（SceneCollisionWorld）が「構成が変わった」ことを
        // 検出するための唯一の合図。
        void NotifySceneStructureChanged() noexcept;

    private:
        // 基底が確定させる。派生は OnCollider* を使う。
        void OnAttach() final;
        void OnDetach() final;
        void OnEnable() final;
        void OnDisable() final;

        // 同じ Owner の中で未使用の collider_key を割り当てる。
        void AssignColliderKey() noexcept;

        Scene::ColliderID collider_id_ = Scene::invalid_collider_id;
    };

    // 与えられた GameObject から collider_key で Collider を引く。
    // 見つからなければ nullptr。削除予約中のものは対象外。
    ColliderComponent* FindColliderByKey(Core::GameObject& owner, int collider_key) noexcept;
    const ColliderComponent* FindColliderByKey(const Core::GameObject& owner,
        int collider_key) noexcept;

    // 実行時 ColliderID で引く。SceneCollisionWorld の解決に使う。
    ColliderComponent* FindColliderByID(Core::GameObject& owner,
        Scene::ColliderID collider_id) noexcept;
}
