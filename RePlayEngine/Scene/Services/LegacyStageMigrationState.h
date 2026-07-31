#pragma once

#include "../../Core/ObjectID/ObjectID.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ReplayEngine::Scene
{
    // 旧 Stage の衝突から MeshCollider GameObject への移行状態。
    //
    // 【なぜ単一 bool をやめたか】
    //   現在の実装では旧 Stage は 1 つで、その衝突メッシュも 1 つの
    //   不可分な三角形配列（Stage::RebuildCollisionMesh が全メッシュを
    //   1 本へ平坦化する）。そのため「今日は」単一 bool でも破綻しない。
    //
    //   ただし旧エディタの SceneDocument は配置物ごとに mesh_collider を
    //   持てる形になっており、読み込み時にそのうち 1 つだけが
    //   Stage の衝突メッシュへ焼き込まれている。つまり
    //   「配置物という単位は既に存在していて、たまたま同時に 1 つしか
    //   有効化できない」状態にある。
    //
    //   ここで単一 bool にしてしまうと、将来 2 つ目の配置物を
    //   衝突対象にした瞬間に「片方だけ移行したら両方が問い合わせ対象から
    //   外れる」という直しにくい不具合になる。
    //   移行元を ID で持つ集合にしておけば、その事故が起きない。
    //
    // 【移行元 ID の決め方】
    //   旧 Stage 本体      … legacy_stage_source_id（固定値 1）
    //   SceneDocument 配置物 … その EntityId をそのまま使う
    //   0 は「不明」を表し、集合には入れない。
    //
    // 【同じ地形を新旧両方へ登録しない】
    //   移行済み集合に入っている移行元は、Backend Mode が Hybrid でも
    //   Legacy 経路へ一切問い合わせない。順序で優先度を付ける方式は採らない。
    class LegacyStageMigrationState final
    {
    public:
        using SourceID = std::uint64_t;

        // 旧 Stage 本体を指す固定 ID。
        static constexpr SourceID stage_source_id = 1;

        bool IsMigrated(SourceID source) const noexcept
        {
            if (source == 0) return false;
            return std::find(migrated_.begin(), migrated_.end(), source) != migrated_.end();
        }

        // 移行済みにする。対応する GameObject を控えておくと、
        // Editor から「どの GameObject へ移ったか」を辿れる。
        void MarkMigrated(SourceID source, Core::ObjectID object)
        {
            if (source == 0) return;
            if (IsMigrated(source))
            {
                for (Entry& entry : entries_)
                {
                    if (entry.source == source) entry.object = object;
                }
                return;
            }
            migrated_.push_back(source);
            Entry entry;
            entry.source = source;
            entry.object = object;
            entries_.push_back(entry);
        }

        // 移行を取り消す。変換を Undo したときに使う。
        void ClearMigrated(SourceID source)
        {
            migrated_.erase(std::remove(migrated_.begin(), migrated_.end(), source),
                migrated_.end());
            entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                [source](const Entry& entry) { return entry.source == source; }),
                entries_.end());
        }

        Core::ObjectID MigratedObject(SourceID source) const noexcept
        {
            for (const Entry& entry : entries_)
            {
                if (entry.source == source) return entry.object;
            }
            return Core::ObjectID::Invalid();
        }

        bool Empty() const noexcept { return migrated_.empty(); }
        std::size_t Count() const noexcept { return migrated_.size(); }

        const std::vector<SourceID>& MigratedSources() const noexcept { return migrated_; }

        void Reset() noexcept
        {
            migrated_.clear();
            entries_.clear();
        }

        void SetMigratedSources(std::vector<SourceID> sources)
        {
            migrated_ = std::move(sources);
            entries_.clear();
            entries_.reserve(migrated_.size());
            for (SourceID source : migrated_)
            {
                Entry entry;
                entry.source = source;
                entries_.push_back(entry);
            }
        }

    private:
        struct Entry
        {
            SourceID source = 0;
            Core::ObjectID object;
        };

        std::vector<SourceID> migrated_;
        std::vector<Entry> entries_;
    };
}
