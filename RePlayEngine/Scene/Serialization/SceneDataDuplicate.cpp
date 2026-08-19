#include "SceneData.h"
#include "SceneDataInternal.h"

#include "../Runtime/Scene.h"
#include "../../Object/Component/MissingComponent.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Reflection/Registry/PropertyRegistry.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ReplayEngine::Scene::Serialization
{
    using Core::ComponentRegistry;
    using Core::ComponentTypeInfo;
    using Core::GameObject;
    using Core::MissingComponent;
    using Core::ObjectID;
    using Reflection::PropertyRegistry;
    using Detail::RemapReferences;

    namespace
    {
        // 複製元の部分木を、親→子の順で集める。
        // 深さに上限を設けて、壊れた階層でも無限再帰しない。
        void CollectSubtree(const GameObject& root, bool include_children,
            std::vector<const GameObject*>& output)
        {
            output.push_back(&root);
            if (!include_children) return;

            for (std::size_t index = 0; index < output.size(); ++index)
            {
                if (output.size() > 100000) break;

                const GameObject* current = output[index];
                for (const GameObject* child : current->Children())
                {
                    if (child == nullptr || child->PendingDestroy()) continue;
                    output.push_back(child);
                }
            }
        }

        // Component を 1 つ複製する。StableID は元と同じ値を引き継ぐ。
        //
        // 引き継ぐ理由:
        //   複製した部分木の中で ComponentReference が閉じている場合、
        //   所有 ObjectID さえ付け替えれば参照がそのまま成立する。
        //   StableID を振り直すと、その対応表まで作らなければならなくなる。
        void DuplicateComponent(const Core::Component& original, GameObject& clone,
            const ObjectRemap& remap)
        {
            const ComponentTypeInfo* info = ComponentRegistry::Find(original.TypeID());

            // 組み込み Component は GameObject 生成時に自動で付いている。
            // Transform の値は呼び出し側でコピー済みなので、ここでは触らない。
            if (info != nullptr && info->built_in) return;

            Core::Component* copy = ComponentRegistry::CreateWithStableID(
                original.TypeID(), clone, original.StableID());
            if (copy == nullptr) return;

            copy->SetEnabled(original.Enabled());

            // 値をいったん取り出し、参照だけ付け替えてから流し込む。
            //
            // PropertyRegistry::CopyValues を直接使わないのは、あれが
            // 「値をそのまま写す」ためのものであり、ObjectID の付け替えを行わないため。
            // 複製先は必ず新しい ObjectID を持つので、付け替えないと
            // 複製した側が元のオブジェクトを指したままになる。
            Reflection::PropertyBag values;
            PropertyRegistry::Capture(original, values);
            PropertyRegistry::Apply(*copy, RemapReferences(values, &remap, true));

            // MissingComponent は PropertyRegistry の対象外なので、預かり内容を直接写す。
            if (original.TypeID() == MissingComponent::StaticTypeID() &&
                copy->TypeID() == MissingComponent::StaticTypeID())
            {
                MissingComponent::Record record =
                    static_cast<const MissingComponent&>(original).Original();
                record.properties = RemapReferences(record.properties, &remap, true);
                static_cast<MissingComponent*>(copy)->SetOriginal(std::move(record));
            }
        }
    }

    GameObject* DuplicateGameObject(Scene& scene, const GameObject& source,
        bool include_children)
    {
        // 3 段階に分ける。
        //   1) 複製する GameObject をすべて先に作る
        //   2) 保存 ObjectID -> 複製先 の対応表を完成させる
        //   3) その対応表を使って Component を写す
        //
        // 以前は 1 体ずつ完結させていたため、まだ作られていない兄弟を指す
        // ObjectReference / ComponentReference を付け替えられなかった。
        std::vector<const GameObject*> originals;
        CollectSubtree(source, include_children, originals);

        ObjectRemap remap;
        remap.reserve(originals.size());

        std::vector<GameObject*> clones;
        clones.reserve(originals.size());

        for (const GameObject* original : originals)
        {
            // 名前に「コピー」を付けるのは複製の起点だけでよい。
            GameObject* clone = scene.CreateGameObject(
                original == &source ? original->Name() + " コピー" : original->Name());
            if (clone == nullptr)
            {
                clones.push_back(nullptr);
                continue;
            }

            clone->SetEnabled(original->Enabled());

            const Core::Transform& source_transform = original->GetTransform();
            clone->GetTransform().SetLocal(
                source_transform.LocalPosition(),
                source_transform.LocalRotationEuler(),
                source_transform.LocalScale());

            // Prefab instance の情報も引き継ぐ。instance root は下で張り替える。
            if (original->IsPrefabInstance())
            {
                clone->SetPrefabInstanceInfo(original->PrefabSourceGUID(),
                    original->PrefabLocalID(), clone->ID());
            }

            clones.push_back(clone);
            remap.emplace(original->ID(), clone);
        }

        GameObject* root_clone = clones.empty() ? nullptr : clones.front();
        if (root_clone == nullptr) return nullptr;

        // 階層を張り直す。複製元の親が部分木の中にいれば複製先の親へ、
        // 外にいれば（＝起点）元と同じ親へぶら下げる。
        for (std::size_t index = 0; index < originals.size(); ++index)
        {
            GameObject* clone = clones[index];
            if (clone == nullptr) continue;

            const GameObject* original_parent = originals[index]->Parent();
            if (original_parent == nullptr)
            {
                continue;
            }

            const auto found = remap.find(original_parent->ID());
            clone->SetParent(found != remap.end() ? found->second
                : const_cast<GameObject*>(original_parent), false);
        }

        // Prefab instance root を複製先の起点へ揃える。
        for (GameObject* clone : clones)
        {
            if (clone == nullptr || !clone->IsPrefabInstance()) continue;
            clone->SetPrefabInstanceInfo(clone->PrefabSourceGUID(),
                clone->PrefabLocalID(), root_clone->ID());
        }

        for (std::size_t index = 0; index < originals.size(); ++index)
        {
            GameObject* clone = clones[index];
            if (clone == nullptr) continue;

            const GameObject& original = *originals[index];
            for (std::size_t slot = 0; slot < original.ComponentCount(); ++slot)
            {
                const Core::Component* component = original.ComponentAt(slot);
                if (component == nullptr || component->PendingDestroy()) continue;
                DuplicateComponent(*component, *clone, remap);
            }
        }

        return root_clone;
    }
}
