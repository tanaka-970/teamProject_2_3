#include "EditorSelection.h"

#include "../../Scene/Runtime/Scene.h"

#include <algorithm>

namespace ReplayEngine::Editor
{
    void EditorSelection::Clear() noexcept
    {
        ids_.clear();
        primary_ = Core::ObjectID::Invalid();
    }

    void EditorSelection::Select(Core::ObjectID id, bool additive)
    {
        if (!additive) ids_.clear();

        if (!id.Valid())
        {
            if (!additive) primary_ = Core::ObjectID::Invalid();
            return;
        }

        if (std::find(ids_.begin(), ids_.end(), id) == ids_.end()) ids_.push_back(id);
        primary_ = id;
    }

    void EditorSelection::Deselect(Core::ObjectID id) noexcept
    {
        ids_.erase(std::remove(ids_.begin(), ids_.end(), id), ids_.end());
        if (primary_ == id) primary_ = ids_.empty() ? Core::ObjectID::Invalid() : ids_.back();
    }

    void EditorSelection::Toggle(Core::ObjectID id)
    {
        if (IsSelected(id)) Deselect(id);
        else Select(id, true);
    }

    bool EditorSelection::IsSelected(Core::ObjectID id) const noexcept
    {
        return std::find(ids_.begin(), ids_.end(), id) != ids_.end();
    }

    Core::GameObject* EditorSelection::ResolvePrimary(const Scene::Scene& scene) const
    {
        if (!primary_.Valid()) return nullptr;
        Core::GameObject* object = scene.FindGameObjectByID(primary_);

        // 削除予約中のものは「もう無い」ものとして扱う。
        // Inspector が消えかけの GameObject を編集し続けないようにするため。
        if (object != nullptr && object->PendingDestroy()) return nullptr;
        return object;
    }

    void EditorSelection::PruneMissing(const Scene::Scene& scene)
    {
        ids_.erase(std::remove_if(ids_.begin(), ids_.end(),
            [&scene](Core::ObjectID id)
            {
                const Core::GameObject* object = scene.FindGameObjectByID(id);
                return object == nullptr || object->PendingDestroy();
            }),
            ids_.end());

        if (!ids_.empty() && std::find(ids_.begin(), ids_.end(), primary_) != ids_.end()) return;
        primary_ = ids_.empty() ? Core::ObjectID::Invalid() : ids_.back();
    }
}
