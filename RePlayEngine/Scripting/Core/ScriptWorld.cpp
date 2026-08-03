#include "ScriptWorld.h"

#include "ScriptComponent.h"

#include <algorithm>

namespace ReplayEngine::Scripting
{
    void ScriptWorld::Register(ScriptComponent& component)
    {
        // 二重登録を弾く。件数は多くても数千なので線形探索で足りる。
        const auto found = std::find(components_.begin(), components_.end(), &component);
        if (found != components_.end()) return;

        components_.push_back(&component);
        ++register_count_;
    }

    void ScriptWorld::Unregister(ScriptComponent& component) noexcept
    {
        const auto found = std::find(components_.begin(), components_.end(), &component);
        if (found == components_.end()) return;

        // 並びは実行順の意味を持たないので、末尾と入れ替えて詰める。
        //
        // 実行順は Scene の GameObject 順 × Component 順が決めており、
        // ここの並びは診断表示にしか使わない。
        *found = components_.back();
        components_.pop_back();
        ++unregister_count_;
    }
}
