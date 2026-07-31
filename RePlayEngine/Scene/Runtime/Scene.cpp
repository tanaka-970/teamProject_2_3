#include "Scene.h"

#include "../../Object/Registry/ComponentRegistry.h"

#include <algorithm>

namespace ReplayEngine::Scene
{
    using Core::GameObject;
    using Core::ObjectID;

    Scene::Scene() = default;

    Scene::Scene(std::string name) : name_(std::move(name))
    {
    }

    Scene::~Scene()
    {
        // 子から先に壊すため、明示的に Clear を通す。
        Clear();
    }

    GameObject* Scene::CreateGameObject(const std::string& name)
    {
        return CreateGameObjectWithID(id_generator_.Next(), name);
    }

    GameObject* Scene::CreateGameObjectWithID(ObjectID id, const std::string& name)
    {
        // 無効 ID や重複 ID を渡された場合は採番し直す。
        // 壊れた Scene ファイルを読んでも読み込み全体を止めないための救済。
        if (!id.Valid() || id_lookup_.find(id) != id_lookup_.end())
        {
            id = id_generator_.Next();
        }
        id_generator_.EnsureAbove(id);

        // GameObject のコンストラクタは private。Scene だけが friend として呼べる。
        objects_.push_back(std::unique_ptr<GameObject>(new GameObject(id, name, this)));
        GameObject* created = objects_.back().get();
        id_lookup_.emplace(id, created);

        // 組み込み扱いの Component を自動で付ける（現状は TransformComponent のみ）。
        // ここで型名を直接書かず ComponentRegistry の built_in フラグを見ることで、
        // 組み込み Component が増えても Scene 側の変更が不要になる。
        for (const Core::ComponentTypeInfo& info : Core::ComponentRegistry::All())
        {
            if (info.built_in) created->AddComponent(info.type_id);
        }
        return created;
    }

    void Scene::DestroyGameObject(GameObject* object) noexcept
    {
        if (object == nullptr) return;
        if (object->GetScene() != this) return;
        object->Destroy();   // 予約のみ。子も再帰的に予約される。
    }

    void Scene::DestroyGameObject(ObjectID id) noexcept
    {
        DestroyGameObject(FindGameObjectByID(id));
    }

    void Scene::Clear()
    {
        // 親子リンクを先に全部外してから破棄する。
        // これにより、破棄途中の GameObject が既に壊れた親や子へ触れることがない。
        for (auto& object : objects_)
        {
            if (object) object->SetParent(nullptr, false);
        }

        id_lookup_.clear();
        objects_.clear();
        started_ = false;
    }

    GameObject* Scene::FindGameObjectByID(ObjectID id) const noexcept
    {
        const auto found = id_lookup_.find(id);
        return found == id_lookup_.end() ? nullptr : found->second;
    }

    GameObject* Scene::FindGameObjectByName(const std::string& name) const noexcept
    {
        for (const auto& object : objects_)
        {
            if (object && !object->PendingDestroy() && object->Name() == name)
            {
                return object.get();
            }
        }
        return nullptr;
    }

    GameObject* Scene::GameObjectAt(std::size_t index) const noexcept
    {
        if (index >= objects_.size()) return nullptr;
        return objects_[index].get();
    }

    std::vector<GameObject*> Scene::RootGameObjects() const
    {
        std::vector<GameObject*> roots;
        roots.reserve(objects_.size());
        for (const auto& object : objects_)
        {
            if (object && object->Parent() == nullptr) roots.push_back(object.get());
        }
        return roots;
    }

    void Scene::Start()
    {
        started_ = true;
        SynchronizeStates();
    }

    void Scene::SynchronizeStates()
    {
        if (!started_) return;

        // OnEnable / OnStart の中から GameObject が追加される可能性があるため、
        // 開始時点の個数を控えて添字で回す。この回で増えたぶんは次のフレームから開始する。
        const std::size_t count = objects_.size();
        for (std::size_t index = 0; index < count && index < objects_.size(); ++index)
        {
            GameObject* object = objects_[index].get();
            if (object == nullptr) continue;
            object->SyncComponentStates();
        }
    }

    void Scene::Update(float delta_time)
    {
        // 読み込み中は一切更新しない。
        // 途中まで構築された Scene（親子未接続・プロパティ未反映）が動かないようにする。
        if (!started_ || loading_) return;

        SynchronizeStates();

        updating_ = true;
        const std::size_t object_count = objects_.size();
        for (std::size_t i = 0; i < object_count && i < objects_.size(); ++i)
        {
            GameObject* object = objects_[i].get();
            if (object == nullptr || !object->ActiveInHierarchy()) continue;

            const std::size_t component_count = object->ComponentCount();
            for (std::size_t j = 0; j < component_count && j < object->ComponentCount(); ++j)
            {
                Core::Component* component = object->ComponentAt(j);
                if (component == nullptr || !component->ActiveInHierarchy()) continue;
                component->OnUpdate(delta_time);
            }
        }
        updating_ = false;

        ProcessPendingOperations();
    }

    void Scene::FixedUpdate(float fixed_delta_time)
    {
        if (!started_ || loading_) return;

        updating_ = true;
        const std::size_t object_count = objects_.size();
        for (std::size_t i = 0; i < object_count && i < objects_.size(); ++i)
        {
            GameObject* object = objects_[i].get();
            if (object == nullptr || !object->ActiveInHierarchy()) continue;

            const std::size_t component_count = object->ComponentCount();
            for (std::size_t j = 0; j < component_count && j < object->ComponentCount(); ++j)
            {
                Core::Component* component = object->ComponentAt(j);
                if (component == nullptr || !component->ActiveInHierarchy()) continue;
                component->OnFixedUpdate(fixed_delta_time);
            }
        }
        updating_ = false;

        ProcessPendingOperations();
    }

    void Scene::LateUpdate(float delta_time)
    {
        if (!started_ || loading_) return;

        updating_ = true;
        const std::size_t object_count = objects_.size();
        for (std::size_t i = 0; i < object_count && i < objects_.size(); ++i)
        {
            GameObject* object = objects_[i].get();
            if (object == nullptr || !object->ActiveInHierarchy()) continue;

            const std::size_t component_count = object->ComponentCount();
            for (std::size_t j = 0; j < component_count && j < object->ComponentCount(); ++j)
            {
                Core::Component* component = object->ComponentAt(j);
                if (component == nullptr || !component->ActiveInHierarchy()) continue;
                component->OnLateUpdate(delta_time);
            }
        }
        updating_ = false;

        ProcessPendingOperations();
    }

    void Scene::ProcessPendingOperations()
    {
        // Update の最中に呼ばれた場合は何もしない。
        // 走査中に objects_ を詰めると添字がずれるため。
        if (updating_) return;

        // 1) Component の削除予約を反映する（OnDisable -> OnDetach -> 破棄）。
        for (std::size_t index = 0; index < objects_.size(); ++index)
        {
            GameObject* object = objects_[index].get();
            if (object != nullptr) object->CompactComponents();
        }

        // 2) GameObject の削除予約を反映する。
        //    子から先に壊すため、階層の深い順に並べてから処理する。
        //    親が先に消えると、子の OnDetach が既に壊れた親を見てしまう。
        std::vector<GameObject*> doomed;
        for (std::size_t index = 0; index < objects_.size(); ++index)
        {
            GameObject* object = objects_[index].get();
            if (object != nullptr && object->PendingDestroy()) doomed.push_back(object);
        }
        if (doomed.empty()) return;

        const auto depth_of = [](const GameObject* object) noexcept
        {
            int depth = 0;
            for (const GameObject* current = object->Parent();
                current != nullptr && depth < 64; current = current->Parent())
            {
                ++depth;
            }
            return depth;
        };
        std::stable_sort(doomed.begin(), doomed.end(),
            [&depth_of](const GameObject* lhs, const GameObject* rhs)
            {
                return depth_of(lhs) > depth_of(rhs);
            });

        for (GameObject* object : doomed)
        {
            object->SetParent(nullptr, false);   // 親子リンクを外す
            object->DetachAllComponents();       // OnDisable -> OnDetach
            RemoveFromLookup(object);
        }

        objects_.erase(
            std::remove_if(objects_.begin(), objects_.end(),
                [](const std::unique_ptr<GameObject>& object)
                {
                    return !object || object->PendingDestroy();
                }),
            objects_.end());
    }

    void Scene::RemoveFromLookup(const GameObject* object) noexcept
    {
        if (object == nullptr) return;
        const auto found = id_lookup_.find(object->ID());
        if (found != id_lookup_.end() && found->second == object)
        {
            id_lookup_.erase(found);
        }
    }
}
