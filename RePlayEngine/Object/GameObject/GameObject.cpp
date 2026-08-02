#include "GameObject.h"

#include "../Registry/ComponentRegistry.h"
#include "../../Scene/Runtime/Scene.h"

#include <algorithm>

using namespace DirectX;

namespace ReplayEngine::Core
{
    namespace
    {
        // 親子チェーンをたどる際の保険。SetParent 側で循環を弾いているが、
        // 壊れた Scene ファイルを読んだ場合などに無限ループへ落ちないようにする。
        constexpr int maximum_hierarchy_depth = 64;
    }

    GameObject::GameObject(ObjectID id, ObjectGeneration generation, std::string name,
        Scene::Scene* scene)
        : id_(id), generation_(generation), name_(std::move(name)), scene_(scene)
    {
    }

    GameObject::~GameObject()
    {
        // Scene が正しい順序で破棄していれば、ここへ来る時点で親子リンクは切れている。
        // それでも保険として切っておく。Scene の破棄順序に依存しないようにするため。
        if (parent_ != nullptr)
        {
            parent_->UnlinkChild(this);
            parent_ = nullptr;
        }
        for (GameObject* child : children_)
        {
            if (child == nullptr) continue;
            child->parent_ = nullptr;
            child->transform_.SetParentInternal(nullptr);
        }
        children_.clear();
        transform_.SetParentInternal(nullptr);

        DetachAllComponents();
    }

    void GameObject::SetName(std::string name)
    {
        name_ = std::move(name);
    }

    void GameObject::SetEnabled(bool enabled) noexcept
    {
        // Component の OnEnable / OnDisable は Scene の同期点でまとめて反映される。
        if (enabled_ == enabled) return;
        enabled_ = enabled;

        // 無効化された GameObject の Collider は問い合わせ対象から外れる必要がある。
        // Component 側の OnDisable でも世代は進むが、そちらは同期点まで遅れるため、
        // ここでも即座に進めておく。
        if (scene_ != nullptr) scene_->BumpStructureGeneration();
    }

    bool GameObject::ActiveInHierarchy() const noexcept
    {
        if (pending_destroy_) return false;

        const GameObject* current = this;
        for (int depth = 0; current != nullptr && depth < maximum_hierarchy_depth; ++depth)
        {
            if (!current->enabled_ || current->pending_destroy_) return false;
            current = current->parent_;
        }
        return true;
    }

    bool GameObject::IsDescendantOf(const GameObject* candidate_ancestor) const noexcept
    {
        if (candidate_ancestor == nullptr) return false;

        const GameObject* current = parent_;
        for (int depth = 0; current != nullptr && depth < maximum_hierarchy_depth; ++depth)
        {
            if (current == candidate_ancestor) return true;
            current = current->parent_;
        }
        return false;
    }

    bool GameObject::SetParent(GameObject* parent, bool keep_world_transform)
    {
        if (parent == parent_) return true;

        // 自分自身や自分の子孫を親にすると階層が循環する。
        if (parent == this) return false;
        if (parent != nullptr && parent->IsDescendantOf(this)) return false;

        // Scene をまたいだ親子関係は作らない。所有権が Scene 単位で完結しなくなるため。
        if (parent != nullptr && parent->scene_ != scene_) return false;

        // 削除予約中の GameObject を親にすると、次の同期点で巻き添えに消える。
        if (parent != nullptr && parent->pending_destroy_) return false;

        const XMMATRIX world = transform_.WorldMatrix();

        if (parent_ != nullptr) parent_->UnlinkChild(this);
        if (parent != nullptr) parent->LinkChild(this);
        else
        {
            parent_ = nullptr;
            transform_.SetParentInternal(nullptr);
        }

        if (keep_world_transform)
        {
            transform_.SetFromWorldMatrix(world);
        }
        return true;
    }

    void GameObject::LinkChild(GameObject* child)
    {
        if (child == nullptr) return;
        if (std::find(children_.begin(), children_.end(), child) == children_.end())
        {
            children_.push_back(child);
        }
        child->parent_ = this;
        child->transform_.SetParentInternal(&transform_);
    }

    void GameObject::UnlinkChild(GameObject* child) noexcept
    {
        if (child == nullptr) return;
        children_.erase(std::remove(children_.begin(), children_.end(), child), children_.end());
        if (child->parent_ == this)
        {
            child->parent_ = nullptr;
            child->transform_.SetParentInternal(nullptr);
        }
    }

    Component* GameObject::AddComponent(ComponentTypeID type_id)
    {
        return ComponentRegistry::Create(type_id, *this);
    }

    Component* GameObject::AddComponent(const std::string& type_name)
    {
        return ComponentRegistry::Create(type_name, *this);
    }

    bool GameObject::AttachComponent(std::unique_ptr<Component> component)
    {
        return AttachComponentWithStableID(std::move(component), invalid_component_stable_id);
    }

    bool GameObject::AttachComponentWithStableID(std::unique_ptr<Component> component,
        ComponentStableID stable_id)
    {
        if (!component) return false;

        // 削除予約中の GameObject へ足しても次の同期点で消えるだけなので受け付けない。
        if (pending_destroy_) return false;

        if (!ComponentRegistry::AllowsMultiple(component->TypeID()) &&
            FindComponent(component->TypeID()) != nullptr)
        {
            return false;
        }

        Component* raw = component.get();

        // 識別番号は「実体を配列へ入れる前」に決める。
        // OnAttach の中から StableID / InstanceID を読んでも 0 にならないようにするため。
        AssignComponentIdentity(*raw, stable_id);

        components_.push_back(std::move(component));

        // OnAttach の中でさらに AddComponent が呼ばれても、raw はヒープ上の実体を指しており
        // vector の再確保では動かないため安全。
        raw->AttachTo(this);

        // Component の顔ぶれが変わった。
        // Collider の登録表がこの世代番号だけを見て突き合わせ直す。
        if (scene_ != nullptr) scene_->BumpStructureGeneration();
        return true;
    }

    void GameObject::AssignComponentIdentity(Component& component,
        ComponentStableID requested_stable_id) noexcept
    {
        // 保存されていた番号を尊重する。ただし 0 や既存との衝突は採番し直す。
        // 壊れたファイルを読んでも「同じ番号の Component が 2 つ」にならないようにする。
        ComponentStableID stable_id = requested_stable_id;
        if (stable_id == invalid_component_stable_id ||
            FindComponentByStableID(stable_id) != nullptr)
        {
            stable_id = next_component_stable_id_;
        }
        EnsureComponentStableIDAbove(stable_id);

        // 実行時の通し番号は必ず新しく採る。復元も再利用もしない。
        // Scene が無い状態（Registry が単体で作った実体）では 0 のままにしておき、
        // 結線されたときに割り当てられるようにする。
        const ComponentInstanceID instance_id =
            scene_ != nullptr ? scene_->AcquireComponentInstanceID()
                              : invalid_component_instance_id;

        component.AssignIdentity(stable_id, instance_id);
    }

    Component* GameObject::FindComponent(ComponentTypeID type_id) const noexcept
    {
        if (type_id == invalid_component_type_id) return nullptr;
        for (const auto& component : components_)
        {
            if (!component || component->PendingDestroy()) continue;
            if (component->TypeID() == type_id) return component.get();
        }
        return nullptr;
    }

    Component* GameObject::FindComponentByStableID(ComponentStableID stable_id) const noexcept
    {
        if (stable_id == invalid_component_stable_id) return nullptr;

        // 削除予約中も対象に含める。これは「同一性の照合」であって生存確認ではない。
        // 生存の判断は Handle を解決する側 (HandleResolver) が行う。
        // ここで予約中を隠すと、同じ StableID が別の Component へ再利用されてしまい、
        // 保存済みの ComponentReference が別物を指す事故につながる。
        for (const auto& component : components_)
        {
            if (!component) continue;
            if (component->StableID() == stable_id) return component.get();
        }
        return nullptr;
    }

    Component* GameObject::FindComponentByInstanceID(
        ComponentInstanceID instance_id) const noexcept
    {
        if (instance_id == invalid_component_instance_id) return nullptr;

        for (const auto& component : components_)
        {
            if (!component) continue;
            if (component->InstanceID() == instance_id) return component.get();
        }
        return nullptr;
    }

    Component* GameObject::FindSingleInstanceConflict(ComponentTypeID type_id) const noexcept
    {
        if (ComponentRegistry::AllowsMultiple(type_id)) return nullptr;
        return FindComponent(type_id);
    }

    Component* GameObject::ComponentAt(std::size_t index) const noexcept
    {
        if (index >= components_.size()) return nullptr;
        return components_[index].get();
    }

    bool GameObject::RemoveComponent(Component* component)
    {
        if (component == nullptr) return false;
        if (component->Owner() != this) return false;
        if (component->PendingDestroy()) return true;   // 既に予約済み。二重呼び出しを許す。
        if (!ComponentRegistry::IsRemovable(component->TypeID())) return false;

        component->MarkPendingDestroy();
        if (scene_ != nullptr) scene_->BumpStructureGeneration();
        return true;
    }

    bool GameObject::RemoveComponent(ComponentTypeID type_id)
    {
        Component* found = FindComponent(type_id);
        return found != nullptr && RemoveComponent(found);
    }

    void GameObject::Destroy() noexcept
    {
        MarkPendingDestroyRecursive();
    }

    void GameObject::MarkPendingDestroyRecursive() noexcept
    {
        if (pending_destroy_) return;   // 二重呼び出しと万一の循環を止める
        pending_destroy_ = true;

        // 親を消したら子も一緒に消す。Scene 直下へ逃がすより挙動が読みやすい。
        for (GameObject* child : children_)
        {
            if (child != nullptr) child->MarkPendingDestroyRecursive();
        }
    }

    void GameObject::SyncComponentStates()
    {
        // SyncEnableState の中（OnEnable / OnStart）から AddComponent される可能性があるため、
        // 開始時点の個数を控えて添字で回す。この回で追加されたぶんは次のフレームから開始する。
        const std::size_t count = components_.size();
        for (std::size_t index = 0; index < count && index < components_.size(); ++index)
        {
            Component* component = components_[index].get();
            if (component == nullptr) continue;
            component->SyncEnableState();
        }
    }

    void GameObject::CompactComponents()
    {
        // OnDetach の中で AddComponent される可能性があるので、参照ではなく添字で回す。
        for (std::size_t index = 0; index < components_.size(); ++index)
        {
            Component* component = components_[index].get();
            if (component == nullptr || !component->PendingDestroy()) continue;
            if (component->Owner() == nullptr) continue;   // 既に片付け済み

            // 順序は OnDisable -> OnRuntimeDestroy -> OnDetach。
            // OnRuntimeDestroy の時点ではまだ Owner とプロパティへ触れる。
            component->ForceDisable();
            component->RaiseRuntimeDestroy();
            component->OnDetach();
            component->owner_ = nullptr;
        }

        components_.erase(
            std::remove_if(components_.begin(), components_.end(),
                [](const std::unique_ptr<Component>& component)
                {
                    return !component || component->PendingDestroy();
                }),
            components_.end());
    }

    void GameObject::DetachAllComponents()
    {
        for (std::size_t index = 0; index < components_.size(); ++index)
        {
            Component* component = components_[index].get();
            if (component == nullptr || component->Owner() == nullptr) continue;
            // 順序は OnDisable -> OnRuntimeDestroy -> OnDetach。
            // OnRuntimeDestroy の時点ではまだ Owner とプロパティへ触れる。
            component->ForceDisable();
            component->RaiseRuntimeDestroy();
            component->OnDetach();
            component->owner_ = nullptr;
        }
        components_.clear();
    }
}
