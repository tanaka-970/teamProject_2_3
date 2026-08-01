#pragma once

#include "../Component/Component.h"
#include "../../Core/Math/Transform.h"
#include "../../Core/ObjectID/ObjectID.h"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ReplayEngine::Scene { class Scene; }

namespace ReplayEngine::Core
{
    // Scene 内に存在する基本オブジェクト。
    //
    // 責任は次の 7 つに限定する。プレイヤー固有・敵固有・床固有の処理は一切持たない。
    //   識別 / 名前 / 有効状態 / 親子階層 / Component 所有 / ライフサイクル管理 / 削除予約
    //
    // 所有関係:
    //   Scene          が GameObject を unique_ptr で所有する
    //   GameObject     が Component  を unique_ptr で所有する
    //   Component      は GameObject を非所有参照する
    //   GameObject     は Scene      を非所有参照する
    //   親 GameObject  は子を「非所有の生ポインタ」で参照するだけ。
    //                  子の所有権は常に Scene に残る（親を消しても所有権が移動しない）。
    //
    // Transform:
    //   GameObject が値メンバとして 1 つだけ所有する。これが唯一の正しい所在。
    //   TransformComponent は自前の座標を持たず、この Transform を編集するビューに徹する。
    //
    // Component コンテナの安全性:
    //   components_ は vector<unique_ptr<Component>>。
    //   追加は末尾へ push_back するだけで、既存 Component の実体アドレスは動かない。
    //   削除は即 erase せず「削除予約」を立て、Scene の同期点でまとめて詰める。
    //   これにより Scene が添字で走査している最中に追加・削除しても添字が壊れない。
    class GameObject final
    {
    public:
        ~GameObject();

        GameObject(const GameObject&) = delete;
        GameObject& operator=(const GameObject&) = delete;
        GameObject(GameObject&&) = delete;
        GameObject& operator=(GameObject&&) = delete;

        // ---- 識別 ----------------------------------------------------------

        ObjectID ID() const noexcept { return id_; }

        const std::string& Name() const noexcept { return name_; }
        void SetName(std::string name);

        // ---- 有効・無効 ----------------------------------------------------

        // この GameObject 自身のフラグ。親の状態は見ない。
        bool Enabled() const noexcept { return enabled_; }
        void SetEnabled(bool enabled) noexcept;

        // 親をたどってすべて有効かどうか。削除予約中は常に false。
        bool ActiveInHierarchy() const noexcept;

        // ---- Transform -----------------------------------------------------

        Transform& GetTransform() noexcept { return transform_; }
        const Transform& GetTransform() const noexcept { return transform_; }

        // ---- Scene ---------------------------------------------------------

        Scene::Scene* GetScene() const noexcept { return scene_; }

        // ---- 親子階層 ------------------------------------------------------

        GameObject* Parent() const noexcept { return parent_; }
        const std::vector<GameObject*>& Children() const noexcept { return children_; }

        // 親を変更する。
        //  - parent == nullptr で Scene 直下へ移動する。
        //  - 自分自身や自分の子孫を親に指定した場合は false を返して何もしない（循環防止）。
        //  - keep_world_transform が true なら見た目のワールド姿勢を維持する。
        //    false ならローカル値をそのまま保ち、見た目は親に追従して動く。
        bool SetParent(GameObject* parent, bool keep_world_transform = true);

        bool IsDescendantOf(const GameObject* candidate_ancestor) const noexcept;

        // ---- Component の追加 ----------------------------------------------

        // 型が複数追加を許可していない場合、既にあるインスタンスをそのまま返す。
        // 追加できないことを理由に nullptr を返したりクラッシュしたりはしない。
        template<class T, class... Args>
        T* AddComponent(Args&&... args)
        {
            static_assert(std::is_base_of_v<Component, T>,
                "T must derive from ReplayEngine::Core::Component");

            if (Component* existing = FindSingleInstanceConflict(T::StaticTypeID()))
            {
                return static_cast<T*>(existing);
            }

            auto created = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw = created.get();
            return AttachComponent(std::move(created)) ? raw : nullptr;
        }

        // 型 ID / 型名から生成する。ComponentRegistry を経由するため、
        // Editor の Add Component と Scene 読み込みが同じ経路を通る。
        // 未登録の型や重複禁止の衝突では nullptr を返す（クラッシュしない）。
        Component* AddComponent(ComponentTypeID type_id);
        Component* AddComponent(const std::string& type_name);

        // 既に構築済みの Component を引き取る。ComponentRegistry と読み込み処理が使う。
        bool AttachComponent(std::unique_ptr<Component> component);

        // ---- Component の取得 ----------------------------------------------

        template<class T>
        T* GetComponent() noexcept
        {
            static_assert(std::is_base_of_v<Component, T>,
                "T must derive from ReplayEngine::Core::Component");
            return const_cast<T*>(static_cast<const GameObject*>(this)->GetComponent<T>());
        }

        template<class T>
        const T* GetComponent() const noexcept
        {
            static_assert(std::is_base_of_v<Component, T>,
                "T must derive from ReplayEngine::Core::Component");
            for (const auto& component : components_)
            {
                if (!component || component->PendingDestroy()) continue;
                // 具象型の一致を先に見る（速い）。派生を base 型で引くときだけ dynamic_cast へ落ちる。
                if (component->TypeID() == T::StaticTypeID())
                {
                    return static_cast<const T*>(component.get());
                }
                if (const auto* casted = dynamic_cast<const T*>(component.get()))
                {
                    return casted;
                }
            }
            return nullptr;
        }

        template<class T>
        bool HasComponent() const noexcept
        {
            return GetComponent<T>() != nullptr;
        }

        // 同じ型を複数持てる Component 用。削除予約中のものは含めない。
        template<class T>
        std::vector<T*> GetComponents() const
        {
            std::vector<T*> result;
            for (const auto& component : components_)
            {
                if (!component || component->PendingDestroy()) continue;
                if (auto* casted = dynamic_cast<T*>(component.get())) result.push_back(casted);
            }
            return result;
        }

        // 型 ID で引く。Editor と読み込み処理が使う非テンプレート版。
        Component* FindComponent(ComponentTypeID type_id) const noexcept;

        // 削除予約中を含めた実体の並び。Editor の表示と保存処理が添字で走査する。
        std::size_t ComponentCount() const noexcept { return components_.size(); }
        Component* ComponentAt(std::size_t index) const noexcept;

        // ---- Component の削除 ----------------------------------------------
        // いずれも即座に破棄せず削除予約を立てる。実際の破棄は Scene の同期点。
        // Inspector を描画している最中に呼んでも無効ポインタが生まれない。

        template<class T>
        bool RemoveComponent()
        {
            T* found = GetComponent<T>();
            return found != nullptr && RemoveComponent(static_cast<Component*>(found));
        }

        bool RemoveComponent(Component* component);
        bool RemoveComponent(ComponentTypeID type_id);

        // ---- 削除 ----------------------------------------------------------

        bool PendingDestroy() const noexcept { return pending_destroy_; }

        // 削除を予約する。子 GameObject も再帰的に削除される。
        void Destroy() noexcept;

    private:
        friend class ReplayEngine::Scene::Scene;

        // 生成は Scene だけが行う。利用側は Scene::CreateGameObject を使うこと。
        GameObject(ObjectID id, std::string name, Scene::Scene* scene);

        // 重複禁止の型で既存インスタンスがあればそれを返す。無ければ nullptr。
        Component* FindSingleInstanceConflict(ComponentTypeID type_id) const noexcept;

        // Scene の同期点から呼ばれるライフサイクル駆動部。
        void SyncComponentStates();
        void CompactComponents();
        void DetachAllComponents();
        void MarkPendingDestroyRecursive() noexcept;

        // 親子リンクの張り替え（Transform の親も同時に更新する）。
        void LinkChild(GameObject* child);
        void UnlinkChild(GameObject* child) noexcept;

        ObjectID id_{};
        std::string name_{ "GameObject" };
        bool enabled_ = true;
        bool pending_destroy_ = false;

        Transform transform_{};

        // すべて非所有参照。所有権は Scene にある。
        Scene::Scene* scene_ = nullptr;
        GameObject* parent_ = nullptr;
        std::vector<GameObject*> children_;

        std::vector<std::unique_ptr<Component>> components_;
    };
}
