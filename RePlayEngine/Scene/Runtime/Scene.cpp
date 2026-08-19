#include "Scene.h"

#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Components/Core/PersistentComponent.h"
#include "../Serialization/SceneData.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <unordered_set>
#include <windows.h>

namespace ReplayEngine::Scene
{
    using Core::GameObject;
    using Core::ObjectID;

    namespace
    {
        struct OrderedComponentCandidate
        {
            Core::Component* component = nullptr;
            std::int32_t execution_order = 0;
            std::size_t discovery_index = 0;
        };

        bool HasNonZeroExecutionOrder(const Scene& scene)
        {
            const std::size_t object_count = scene.GameObjectCount();
            for (std::size_t i = 0; i < object_count &&
                i < scene.GameObjectCount(); ++i)
            {
                GameObject* object = scene.GameObjectAt(i);
                if (object == nullptr) continue;
                const std::size_t component_count = object->ComponentCount();
                for (std::size_t j = 0; j < component_count &&
                    j < object->ComponentCount(); ++j)
                {
                    Core::Component* component = object->ComponentAt(j);
                    if (component == nullptr) continue;
                    if (component->ExecutionOrder() != 0) return true;
                }
            }
            return false;
        }

        std::vector<OrderedComponentCandidate> CollectOrderedCandidates(const Scene& scene)
        {
            std::vector<OrderedComponentCandidate> candidates;
            std::size_t discovery_index = 0;
            const std::size_t object_count = scene.GameObjectCount();
            for (std::size_t i = 0; i < object_count &&
                i < scene.GameObjectCount(); ++i)
            {
                GameObject* object = scene.GameObjectAt(i);
                if (object == nullptr) continue;
                const std::size_t component_count = object->ComponentCount();
                for (std::size_t j = 0; j < component_count &&
                    j < object->ComponentCount(); ++j)
                {
                    Core::Component* component = object->ComponentAt(j);
                    // フェーズ開始時に存在する実体を snapshot する。
                    // Active 判定は呼び出し直前に再確認するため、ここでは絞らない。
                    if (component != nullptr)
                    {
                        candidates.push_back({ component, component->ExecutionOrder(),
                            discovery_index });
                    }
                    ++discovery_index;
                }
            }

            std::sort(candidates.begin(), candidates.end(),
                [](const OrderedComponentCandidate& lhs,
                    const OrderedComponentCandidate& rhs)
                {
                    if (lhs.execution_order != rhs.execution_order)
                        return lhs.execution_order < rhs.execution_order;
                    return lhs.discovery_index < rhs.discovery_index;
                });
            return candidates;
        }

        template<class Callback>
        void RunOrderedCandidates(const Scene& scene, Callback callback)
        {
            const std::vector<OrderedComponentCandidate> candidates =
                CollectOrderedCandidates(scene);
            for (const OrderedComponentCandidate& candidate : candidates)
            {
                Core::Component* component = candidate.component;
                if (component == nullptr || component->PendingDestroy() ||
                    !component->ActiveInHierarchy())
                {
                    continue;
                }
                callback(*component);
            }
        }
    }

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

        // この ObjectID が「何回目の生成か」を進める。
        // 破棄した ID を復元して作り直した場合でも必ず増えるため、
        // 破棄前に取った ObjectHandle はここで無効になる。
        const Core::ObjectGeneration generation = ++generation_by_id_[id];

        // GameObject のコンストラクタは private。Scene だけが friend として呼べる。
        objects_.push_back(
            std::unique_ptr<GameObject>(new GameObject(id, generation, name, this)));
        GameObject* created = objects_.back().get();
        id_lookup_.emplace(id, created);

        // 組み込み扱いの Component を自動で付ける（現状は TransformComponent のみ）。
        // ここで型名を直接書かず ComponentRegistry の built_in フラグを見ることで、
        // 組み込み Component が増えても Scene 側の変更が不要になる。
        for (const Core::ComponentTypeInfo& info : Core::ComponentRegistry::All())
        {
            if (info.built_in) created->AddComponent(info.type_id);
        }

        // 顔ぶれが変わった。登録表を持つ側が次のフレームで突き合わせ直す。
        BumpStructureGeneration();
        return created;
    }

    void Scene::DestroyGameObject(GameObject* object) noexcept
    {
        if (object == nullptr) return;
        if (object->GetScene() != this) return;
        object->Destroy();   // 予約のみ。子も再帰的に予約される。
        BumpStructureGeneration();
    }

    void Scene::DestroyGameObject(ObjectID id) noexcept
    {
        DestroyGameObject(FindGameObjectByID(id));
    }

    void Scene::Destroy(GameObject* object) noexcept
    {
        DestroyGameObject(object);
    }

    std::vector<std::unique_ptr<GameObject>> Scene::DetachPersistentRoots()
    {
        std::vector<GameObject*> roots;
        std::unordered_set<GameObject*> transfer_set;
        std::unordered_set<std::string> persistent_names;

        // 子に付いた印は独立した移行単位にしない。親が Persistent なら
        // 親の階層に含まれて移行し、親が無ければ警告だけでその Scene に残る。
        for (const auto& object : objects_)
        {
            if (!object || object->PendingDestroy()) continue;
            if (object->GetComponent<Components::PersistentComponent>() == nullptr) continue;
            if (object->Parent() != nullptr)
            {
                OutputDebugStringA("[RePlayEngine] 子 GameObject の PersistentComponent は無視します。\n");
                continue;
            }
            if (!persistent_names.insert(object->Name()).second)
            {
                OutputDebugStringA("[RePlayEngine] 同名の Persistent GameObject は先着を残して破棄します。\n");
                DestroyGameObject(object.get());
                continue;
            }
            roots.push_back(object.get());
        }

        ProcessPendingOperations();

        const std::function<void(GameObject*)> collect =
            [&collect, &transfer_set](GameObject* object)
        {
            if (object == nullptr || !transfer_set.insert(object).second) return;
            for (GameObject* child : object->Children()) collect(child);
        };
        for (GameObject* root : roots) collect(root);

        std::vector<std::unique_ptr<GameObject>> detached;
        detached.reserve(transfer_set.size());
        for (auto iterator = objects_.begin(); iterator != objects_.end();)
        {
            GameObject* object = iterator->get();
            if (transfer_set.find(object) == transfer_set.end())
            {
                ++iterator;
                continue;
            }

            RemoveFromLookup(object);
            object->scene_ = nullptr;
            detached.push_back(std::move(*iterator));
            iterator = objects_.erase(iterator);
        }

        if (!detached.empty()) BumpStructureGeneration();
        return detached;
    }

    void Scene::AdoptPersistentRoots(std::vector<std::unique_ptr<GameObject>> roots)
    {
        if (roots.empty()) return;

        std::vector<GameObject*> root_objects;
        std::unordered_set<GameObject*> adopted_objects;
        const std::function<void(GameObject*)> collect =
            [&collect, &adopted_objects](GameObject* object)
        {
            if (object == nullptr || !adopted_objects.insert(object).second) return;
            for (GameObject* child : object->Children()) collect(child);
        };

        for (const auto& root : roots)
        {
            if (!root) continue;
            root_objects.push_back(root.get());
            collect(root.get());
        }

        // 新 Scene 側に同名の Persistent ルートがある場合は、読み込んだ新しい
        // 実体を残さず、旧 World から移した実体を正本にする。
        std::unordered_set<GameObject*> duplicate_objects;
        for (GameObject* old_root : root_objects)
        {
            for (const auto& object : objects_)
            {
                if (!object || object->PendingDestroy() ||
                    object->GetComponent<Components::PersistentComponent>() == nullptr ||
                    object->Parent() != nullptr || object->Name() != old_root->Name())
                {
                    continue;
                }
                duplicate_objects.insert(object.get());
            }
        }
        for (GameObject* duplicate : duplicate_objects) DestroyGameObject(duplicate);
        ProcessPendingOperations();

        // 新 Scene と Persistent 階層の両方より上へ進めておく。
        // 先に上限を確定しないと、衝突した ID の振り直し先が別の ID と重なる。
        for (const auto& object : objects_)
        {
            if (object != nullptr) id_generator_.EnsureAbove(object->ID());
        }
        for (GameObject* adopted : adopted_objects)
        {
            if (adopted != nullptr) id_generator_.EnsureAbove(adopted->ID());
        }

        Serialization::ObjectRemap object_remap;
        for (GameObject* adopted : adopted_objects)
        {
            if (adopted == nullptr) continue;
            if (FindGameObjectByID(adopted->ID()) == nullptr) continue;

            const ObjectID previous_id = adopted->ID();
            object_remap.emplace(previous_id, adopted);
            // 保存ファイルの ID はファイル内の参照が指しているため変えられない。
            // メモリ上の Persistent 階層は参照をその場で直せるので、こちらを振り直す。
            adopted->id_ = id_generator_.Next();
        }

        const std::vector<GameObject*> adopted_list(
            adopted_objects.begin(), adopted_objects.end());
        Serialization::RemapLiveObjectReferences(adopted_list, object_remap);

        // ---- 拡張点: Persistent 階層の外部参照 -------------------------------
        //
        // 【今は入れていない理由】
        //   旧 Scene 側への参照は今回の ID 衝突とは別の問題で、
        //   「切る」か「保持する」かの方針決定が必要なため。
        // 【入れるときにここへ足す】
        //   持ち越し階層の外を指す参照を検出する処理を、上の付け替えの直後へ足す。
        // 【壊してはいけない前提】
        //   MissingComponent の思想に合わせ、値を黙って捨てない。
        // -----------------------------------------------------------------------------

        for (auto& root : roots)
        {
            if (!root) continue;
            const std::function<void(GameObject*)> rebind =
                [&rebind, this](GameObject* object)
            {
                if (object == nullptr) return;
                object->scene_ = this;
                generation_by_id_[object->ID()] =
                    (std::max)(generation_by_id_[object->ID()], object->generation_);
                id_generator_.EnsureAbove(object->ID());
                id_lookup_[object->ID()] = object;
                for (GameObject* child : object->Children()) rebind(child);
            };
            rebind(root.get());
            objects_.push_back(std::move(root));
        }

        BumpStructureGeneration();
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
        generation_by_id_.clear();

        // Scene の中身が総入れ替えになった。
        // 登録表側は古い ObjectID / ColliderID を必ず捨てる必要がある。
        BumpStructureGeneration();

        // World そのものが別物になった。
        // ここで実体番号を採り直すことで、Clear() 前に配られた
        // ObjectHandle / ComponentHandle がすべて WrongWorld として弾かれる。
        // Scene 読み込み (ApplySceneData) は必ずこの Clear() を通るため、
        // 読み込み経路ごとに無効化を書き足す必要がない。
        RenewWorldInstance();

        started_ = false;
    }

    void Scene::RenewWorldInstance() noexcept
    {
        world_instance_id_ = Core::AcquireWorldInstanceID();

        // Component の通し番号も 1 へ戻してよい。
        // World 番号が変わっている以上、古い ComponentHandle は
        // 通し番号を照合するところまで到達しない。
        next_component_instance_id_ = 1;
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

    std::size_t Scene::RootSiblingIndex(const GameObject* object) const noexcept
    {
        if (object == nullptr || object->GetScene() != this || object->Parent() != nullptr)
            return 0u;
        std::size_t root_index = 0u;
        for (const auto& candidate : objects_)
        {
            if (!candidate || candidate->Parent() != nullptr) continue;
            if (candidate.get() == object) return root_index;
            ++root_index;
        }
        return root_index;
    }

    bool Scene::SetRootSiblingIndex(GameObject* object, std::size_t index) noexcept
    {
        if (object == nullptr || object->GetScene() != this || object->Parent() != nullptr)
            return false;

        auto object_it = std::find_if(objects_.begin(), objects_.end(),
            [object](const std::unique_ptr<GameObject>& value) { return value.get() == object; });
        if (object_it == objects_.end()) return false;

        std::vector<GameObject*> roots = RootGameObjects();
        if (roots.empty()) return false;
        const std::size_t old_index = RootSiblingIndex(object);
        // index == root_count は「末尾へ」。移動元を抜くと後ろ側は 1 つ詰まる。
        index = (std::min)(index, roots.size());
        if (index > old_index) --index;
        if (old_index == index) return true;

        // root のみの順番を変え、非 root の所有位置は維持する。
        // unique_ptr を move しても GameObject 実体のアドレスは変わらない。
        std::unique_ptr<GameObject> moving = std::move(*object_it);
        objects_.erase(object_it);

        roots.erase(roots.begin() + static_cast<std::ptrdiff_t>(old_index));
        GameObject* before = index < roots.size() ? roots[index] : nullptr;
        if (before != nullptr)
        {
            auto before_it = std::find_if(objects_.begin(), objects_.end(),
                [before](const std::unique_ptr<GameObject>& value) { return value.get() == before; });
            objects_.insert(before_it, std::move(moving));
        }
        else
        {
            // 最後の root の直後へ置く。末尾に非 root があっても hierarchy root 順だけを変える。
            auto insert_it = objects_.end();
            for (auto it = objects_.begin(); it != objects_.end(); ++it)
            {
                if (*it && (*it)->Parent() == nullptr) insert_it = std::next(it);
            }
            objects_.insert(insert_it, std::move(moving));
        }
        BumpStructureGeneration();
        return true;
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
        if (!HasNonZeroExecutionOrder(*this))
        {
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
        }
        else
        {
            RunOrderedCandidates(*this, [delta_time](Core::Component& component)
                {
                    component.OnUpdate(delta_time);
                });
        }
        updating_ = false;

        ProcessPendingOperations();
    }

    void Scene::FixedUpdate(float fixed_delta_time)
    {
        if (!started_ || loading_) return;

        updating_ = true;
        if (!HasNonZeroExecutionOrder(*this))
        {
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
        }
        else
        {
            RunOrderedCandidates(*this,
                [fixed_delta_time](Core::Component& component)
                {
                    component.OnFixedUpdate(fixed_delta_time);
                });
        }
        updating_ = false;

        ProcessPendingOperations();
    }

    void Scene::LateUpdate(float delta_time)
    {
        if (!started_ || loading_) return;

        updating_ = true;
        if (!HasNonZeroExecutionOrder(*this))
        {
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
        }
        else
        {
            RunOrderedCandidates(*this, [delta_time](Core::Component& component)
                {
                    component.OnLateUpdate(delta_time);
                });
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

        BumpStructureGeneration();

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
