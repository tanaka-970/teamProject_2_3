#pragma once

#include "../Handles/RuntimeHandles.h"

#include <DirectXMath.h>

#include <cstdint>

namespace ReplayEngine::Runtime
{
    // 接触の段階。Enter -> Stay -> Exit の順に 1 回ずつ届く。
    enum class ContactPhase : std::int32_t
    {
        Enter = 0,
        Stay = 1,
        Exit = 2,
    };

    const char* ToString(ContactPhase phase) noexcept;

    // Trigger の接触 1 件分。
    //
    // Core::TriggerContact との違い:
    //   TriggerContact … Collision 層が扱う最小の組（ObjectID + ColliderID）。
    //   TriggerEvent   … Behaviour が受け取る形。「自分」「相手」の視点へ
    //                    整理し直し、Handle と Layer とフレーム番号を足したもの。
    //
    // 同じ接触ペアでも、Trigger 側と入った側の両方へ配送される。
    // どちらの立場かは self_is_trigger で判別する。
    //
    // 生ポインタを持たない。Handle から必ず引き直すこと。
    struct TriggerEvent final
    {
        ContactPhase phase = ContactPhase::Enter;

        // このコールバックを受け取っている側。
        ObjectHandle self;
        Scene::ColliderID self_collider = Scene::invalid_collider_id;

        // 接触している相手。
        ObjectHandle other;
        Scene::ColliderID other_collider = Scene::invalid_collider_id;

        // Collider の Layer 番号。引けなかった場合は -1。
        // 「Layer 0」と「不明」を取り違えないよう、既定値を 0 にしない。
        int self_layer = -1;
        int other_layer = -1;

        // 自分が Trigger 側か、Trigger へ入った側か。
        bool self_is_trigger = false;

        // 配送されたフレーム。同一フレームの重複配送を Behaviour 側で
        // 弾きたい場合に使う。
        std::uint64_t frame_index = 0;

        // 相手がまだ解決できる状態か。
        // Exit は「相手が消えたこと」で発生する場合があり、そのとき false になる。
        bool other_valid = false;
    };

    // Collision Event が「どういう接触から来たか」。
    //
    // 【重要】RePlayEngine には剛体物理エンジンが無い。
    //   一般的な Collider どうしの衝突解決は存在せず、実際に取得できるのは
    //   CharacterMotor が自分の移動を解決するために撃っている問い合わせの結果だけ。
    //   それを汎用の Collision であるかのように配送しない。
    //   どの種類の接触なのかを必ず hit_kind で示す。
    enum class CollisionHitKind : std::int32_t
    {
        // 分類できないもの。通常は使わない。
        Unknown = 0,

        // CharacterMotor の接地問い合わせ (QueryGround) が返した床。
        // 接触点と面法線が取れる。
        CharacterGround = 1,

        // CharacterMotor の移動掃引 (SweepSphere) が当たった壁。
        // 接触時の球中心と面法線が取れる。
        CharacterWall = 2,
    };

    const char* ToString(CollisionHitKind kind) noexcept;

    // Collision の接触 1 件分。
    //
    // 【取得できる情報の範囲】
    //   contact_point / contact_normal … 取得できる（問い合わせの戻り値）
    //   penetration_depth              … 取得できない。めり込み量を解く処理が無い
    //   relative_velocity              … 取得できない。相手側の速度を持つ仕組みが無い
    //
    //   取得できない項目はフィールド自体を置いていない。
    //   0 を入れて「取れているように見せる」ことを避けるため。
    //   将来 RigidBody を導入したときに足す。
    struct CollisionEvent final
    {
        ContactPhase phase = ContactPhase::Enter;
        CollisionHitKind hit_kind = CollisionHitKind::Unknown;

        ObjectHandle self;
        ObjectHandle other;
        Scene::ColliderID other_collider = Scene::invalid_collider_id;

        // ワールド座標。CharacterGround は接地点、CharacterWall は接触時の球中心。
        DirectX::XMFLOAT3 contact_point{ 0.0f, 0.0f, 0.0f };

        // ワールドの面法線。
        DirectX::XMFLOAT3 contact_normal{ 0.0f, 1.0f, 0.0f };

        std::uint64_t frame_index = 0;

        bool other_valid = false;
    };
}
