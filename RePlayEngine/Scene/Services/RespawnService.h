#pragma once

#include "../../Core/ObjectID/ObjectID.h"

#include <DirectXMath.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ReplayEngine::Scene
{
    // 復帰地点の保持と、Gameplay 側で起きた出来事の記録。
    //
    // 【Singleton にしない】
    //   Scene が SceneServices の中へ値で 1 つ持つ。
    //   Play 用 Scene と編集 Scene がそれぞれ自分の状態を持つため、
    //   Play 中に取ったチェックポイントが編集 Scene へ漏れない。
    //
    // 【Player 型に依存しない】
    //   ここが知っているのは ObjectID と座標だけ。
    //   誰が復帰するのかも、何が復帰させるのかも知らない。
    //   Checkpoint / SpawnPoint が書き込み、KillVolume が読む。
    //
    // 【Scene 遷移を持たない】
    //   Goal に到達したことは「出来事」として記録するだけで、
    //   どの Scene を読み込むかはここでは決めない。
    //   決めるのは framework 側（Editor / ゲーム進行）である。
    class RespawnService final
    {
    public:
        struct Point
        {
            // この復帰地点を提供した GameObject（Checkpoint / SpawnPoint）。
            Core::ObjectID source;

            DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };

            // ラジアンのオイラー角。Transform の内部表現と揃える。
            DirectX::XMFLOAT3 rotation{ 0.0f, 0.0f, 0.0f };

            bool valid = false;
        };

        // ---- 既定の開始地点（SpawnPoint が書き込む）------------------------
        //
        // Checkpoint をまだ 1 つも通過していないときの復帰先。
        // 優先度が高い SpawnPoint だけが勝つ。同点なら先に登録したものを残す。

        void OfferSpawnPoint(const Point& point, int priority) noexcept
        {
            if (!point.valid) return;
            if (spawn_.valid && priority <= spawn_priority_) return;
            spawn_ = point;
            spawn_priority_ = priority;
        }

        const Point& SpawnPoint() const noexcept { return spawn_; }

        // ---- 通過済みチェックポイント --------------------------------------

        void SetCheckpoint(const Point& point) noexcept
        {
            if (!point.valid) return;
            checkpoint_ = point;
        }

        const Point& Checkpoint() const noexcept { return checkpoint_; }

        void ClearCheckpoint() noexcept { checkpoint_ = Point{}; }

        // 実際に使う復帰地点。Checkpoint があればそちら、無ければ SpawnPoint。
        // どちらも無ければ valid == false のまま返る（呼び出し側は必ず確かめる）。
        const Point& ActivePoint() const noexcept
        {
            return checkpoint_.valid ? checkpoint_ : spawn_;
        }

        void Reset() noexcept
        {
            spawn_ = Point{};
            checkpoint_ = Point{};
            spawn_priority_ = 0;
        }

    private:
        Point spawn_;
        Point checkpoint_;
        int spawn_priority_ = 0;
    };

    // Gameplay Component が起こした出来事の記録。
    //
    // Component から Scene 遷移や UI を直接触らせないための緩衝材。
    // 「何が起きたか」だけを積み、どう反応するかは外側が決める。
    class GameplayEventLog final
    {
    public:
        enum class Kind
        {
            CheckpointActivated,
            GoalReached,
            KillVolumeEntered,
        };

        struct Event
        {
            Kind kind = Kind::CheckpointActivated;

            // 出来事を起こした側（Checkpoint / Goal / KillVolume の GameObject）。
            Core::ObjectID source;

            // 巻き込まれた側（Trigger へ入った GameObject）。
            Core::ObjectID subject;

            // Component 側の識別番号（checkpoint_id / goal_id）。
            int identifier = 0;
        };

        // 積みすぎて無制限に増えないよう上限を設ける。
        static constexpr std::size_t capacity = 256;

        void Push(const Event& event)
        {
            if (events_.size() >= capacity) events_.erase(events_.begin());
            events_.push_back(event);
        }

        const std::vector<Event>& Events() const noexcept { return events_; }
        void Clear() noexcept { events_.clear(); }

        // 直近に到達した Goal。UI や進行管理が「クリアしたか」を見るのに使う。
        bool GoalReached() const noexcept
        {
            for (const Event& event : events_)
            {
                if (event.kind == Kind::GoalReached) return true;
            }
            return false;
        }

        static const char* ToString(Kind kind) noexcept
        {
            switch (kind)
            {
            case Kind::CheckpointActivated: return "Checkpoint";
            case Kind::GoalReached:         return "Goal";
            case Kind::KillVolumeEntered:   return "KillVolume";
            default:                        break;
            }
            return "(unknown)";
        }

    private:
        std::vector<Event> events_;
    };
}
