#include "HierarchyPanel.h"

#include "../Core/EditorContext.h"
#include "../../Assets/AssetDatabase.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Components/Rendering/MeshRendererComponent.h"
#include "../../Components/Rendering/PrimitiveMeshRendererComponent.h"
#include "../../Components/Landscape/LandscapeComponent.h"
#include "../../Components/Landscape/LandscapeRendererComponent.h"
#include "../../Components/Landscape/LandscapeColliderComponent.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Serialization/SceneData.h"
#include "../../Scene/Serialization/SceneSerializer.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ReplayEngine::Editor
{
    using Core::GameObject;
    using Core::ObjectID;

    namespace
    {
        constexpr const char* clipboard_header = "REPLAY_CLIPBOARD 1\n";
        constexpr std::size_t maximum_clipboard_objects = 100000;

        std::string UniqueObjectName(const Scene::Scene& scene, const GameObject* parent,
            const std::string& desired, const GameObject* exclude = nullptr)
        {
            const auto exists = [&](const std::string& candidate)
            {
                for (std::size_t i = 0; i < scene.GameObjectCount(); ++i)
                {
                    const GameObject* object = scene.GameObjectAt(i);
                    if (object == nullptr || object == exclude || object->PendingDestroy()) continue;
                    if (object->Parent() == parent && object->Name() == candidate) return true;
                }
                return false;
            };
            if (!exists(desired)) return desired;

            // 既に "Button (1)" のような連番名を複製しても
            // "Button (1) (1)" にせず、同じ base の次番号を探す。
            std::string base = desired;
            const std::size_t open = desired.rfind(" (");
            if (open != std::string::npos && desired.back() == ')')
            {
                const std::string digits = desired.substr(open + 2,
                    desired.size() - open - 3);
                if (!digits.empty() && std::all_of(digits.begin(), digits.end(),
                    [](unsigned char c) { return std::isdigit(c) != 0; }))
                {
                    base = desired.substr(0, open);
                }
            }
            for (int suffix = 1; suffix < 10000; ++suffix)
            {
                const std::string candidate = base + " (" + std::to_string(suffix) + ")";
                if (!exists(candidate)) return candidate;
            }
            return desired;
        }

        bool ValidateClipboardData(const Scene::Serialization::SceneData& data,
            std::string& error)
        {
            if (data.objects.empty())
            {
                error = "GameObject がありません。";
                return false;
            }
            if (data.objects.size() > maximum_clipboard_objects)
            {
                error = "GameObject は 100,000 件までです。";
                return false;
            }

            std::unordered_map<ObjectID, ObjectID> parents;
            parents.reserve(data.objects.size());
            for (const Scene::Serialization::GameObjectData& object : data.objects)
            {
                if (!object.id.Valid() || !parents.emplace(object.id, object.parent_id).second)
                {
                    error = "GameObject ID が不正または重複しています。";
                    return false;
                }
            }
            for (const auto& entry : parents)
            {
                ObjectID current = entry.first;
                std::unordered_set<ObjectID> ancestors;
                while (current.Valid())
                {
                    if (!ancestors.insert(current).second)
                    {
                        error = "親子関係が循環しています。";
                        return false;
                    }
                    const auto found = parents.find(current);
                    if (found == parents.end())
                    {
                        error = "親 GameObject がクリップボード内にありません。";
                        return false;
                    }
                    current = found->second;
                }
            }
            return true;
        }
    }

    bool HierarchyPanel::CopySelection(EditorContext& context, std::string& clipboard_text,
        std::string& error) const
    {
        clipboard_text.clear();
        error.clear();
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr || context.Selection().Empty())
        {
            error = "コピーする GameObject を選択してください。";
            return false;
        }

        Scene::Serialization::SceneData copied;
        const std::vector<ObjectID> selected = context.Selection().All();
        for (const ObjectID id : selected)
        {
            GameObject* object = scene->FindGameObjectByID(id);
            if (object == nullptr || object->PendingDestroy()) continue;

            bool selected_ancestor = false;
            for (const GameObject* parent = object->Parent(); parent != nullptr;
                parent = parent->Parent())
            {
                if (context.Selection().IsSelected(parent->ID()))
                {
                    selected_ancestor = true;
                    break;
                }
            }
            if (selected_ancestor) continue;

            Scene::Serialization::SceneData subtree;
            if (!Scene::Serialization::CaptureGameObjectSubtree(*scene, id, subtree)) continue;
            if (subtree.objects.size() > maximum_clipboard_objects ||
                copied.objects.size() > maximum_clipboard_objects - subtree.objects.size())
            {
                error = "GameObject は 100,000 件までコピーできます。";
                return false;
            }
            copied.objects.insert(copied.objects.end(), subtree.objects.begin(), subtree.objects.end());
        }
        if (copied.objects.empty())
        {
            error = "コピーできる GameObject がありません。";
            return false;
        }

        std::ostringstream stream;
        if (!Scene::Serialization::SceneSerializer::WriteText(copied, stream, error)) return false;
        clipboard_text = clipboard_header + stream.str();
        return true;
    }

    bool HierarchyPanel::PasteSelection(EditorContext& context,
        const std::string& clipboard_text, std::string& error)
    {
        error.clear();
        if (clipboard_text.compare(0, std::strlen(clipboard_header), clipboard_header) != 0)
        {
            error = "RePlay Engine の GameObject クリップボードではありません。";
            return false;
        }

        Scene::Serialization::SceneData data;
        std::istringstream stream(clipboard_text.substr(std::strlen(clipboard_header)));
        if (!Scene::Serialization::SceneSerializer::ReadText(data, stream, error)) return false;
        return PasteSceneData(context, data, error);
    }

    bool HierarchyPanel::PasteSceneData(EditorContext& context,
        const Scene::Serialization::SceneData& data, std::string& error)
    {
        error.clear();
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr || !context.CanEdit())
        {
            error = "停止してから貼り付けてください。";
            return false;
        }
        if (!ValidateClipboardData(data, error)) return false;

        // 複数選択では最初に選ばれたものを親とする。貼り付け後に選択を更新する前に解決する。
        GameObject* destination_parent = nullptr;
        const std::vector<ObjectID>& selected = context.Selection().All();
        if (!selected.empty()) destination_parent = scene->FindGameObjectByID(selected.front());
        if (destination_parent != nullptr && destination_parent->PendingDestroy()) destination_parent = nullptr;

        std::unordered_set<ObjectID> existing_ids;
        existing_ids.reserve(scene->GameObjectCount());
        for (std::size_t index = 0; index < scene->GameObjectCount(); ++index)
        {
            const GameObject* object = scene->GameObjectAt(index);
            if (object != nullptr && !object->PendingDestroy()) existing_ids.insert(object->ID());
        }

        context.BeginEdit("GameObject を貼り付け");
        Scene::Serialization::SceneLoadReport report;
        if (Scene::Serialization::InstantiateSceneData(data, *scene, report) == nullptr)
        {
            for (std::size_t index = 0; index < scene->GameObjectCount(); ++index)
            {
                GameObject* object = scene->GameObjectAt(index);
                if (object != nullptr && !object->PendingDestroy() &&
                    existing_ids.find(object->ID()) == existing_ids.end())
                {
                    scene->DestroyGameObject(object->ID());
                }
            }
            scene->ProcessPendingOperations();
            context.CancelEdit();
            error = "GameObject を貼り付けられませんでした。";
            return false;
        }

        std::vector<ObjectID> created_roots;
        for (GameObject* root : scene->RootGameObjects())
        {
            if (root == nullptr || root->PendingDestroy() ||
                existing_ids.find(root->ID()) != existing_ids.end()) continue;
            if (destination_parent != nullptr) root->SetParent(destination_parent, false);
            root->SetName(UniqueObjectName(*scene, destination_parent, root->Name() + " コピー", root));
            created_roots.push_back(root->ID());
        }
        if (created_roots.empty())
        {
            for (std::size_t index = 0; index < scene->GameObjectCount(); ++index)
            {
                GameObject* object = scene->GameObjectAt(index);
                if (object != nullptr && !object->PendingDestroy() &&
                    existing_ids.find(object->ID()) == existing_ids.end())
                {
                    scene->DestroyGameObject(object->ID());
                }
            }
            scene->ProcessPendingOperations();
            context.CancelEdit();
            error = "貼り付けた GameObject を特定できませんでした。";
            return false;
        }
        context.CommitEdit();

        context.Selection().Clear();
        for (const ObjectID id : created_roots) context.Selection().Select(id, true);
        context.SetStatus(std::to_string(created_roots.size()) + " 個を貼り付けました");
        return true;
    }

    void HierarchyPanel::DrawCreateMenu(EditorContext& context, GameObject* parent)
    {
        if (!context.CanEdit()) return;

        if (ImGui::MenuItem("空の GameObject"))
        {
            CreateEmptyGameObject(context, parent);
        }

        if (ImGui::BeginMenu("3D Object"))
        {
            if (ImGui::MenuItem("Plane"))
                CreateBuiltInPrimitive(context, parent, "Plane", Components::PrimitiveMeshRendererComponent::Plane);
            if (ImGui::MenuItem("Cube"))
                CreateBuiltInPrimitive(context, parent, "Cube", Components::PrimitiveMeshRendererComponent::Cube);
            if (ImGui::MenuItem("Sphere"))
                CreateBuiltInPrimitive(context, parent, "Sphere", Components::PrimitiveMeshRendererComponent::Sphere);
            if (ImGui::MenuItem("Capsule"))
                CreateBuiltInPrimitive(context, parent, "Capsule", Components::PrimitiveMeshRendererComponent::Capsule);
            if (ImGui::MenuItem("Cylinder"))
                CreateBuiltInPrimitive(context, parent, "Cylinder", Components::PrimitiveMeshRendererComponent::Cylinder);
            if (ImGui::MenuItem("Quad"))
                CreateBuiltInPrimitive(context, parent, "Quad", Components::PrimitiveMeshRendererComponent::Quad);
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Landscape Ground"))
        {
            CreateLandscapeGround(context, parent);
        }
    }

    void HierarchyPanel::CreateBuiltInPrimitive(EditorContext& context, GameObject* parent,
        const char* display_name, int primitive_type)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr || !context.CanEdit() || display_name == nullptr)
            return;

        context.BeginEdit(std::string(display_name) + " を作成");
        GameObject* created = scene->CreateGameObject(UniqueObjectName(*scene, parent, display_name));
        if (created == nullptr)
        {
            context.CancelEdit();
            return;
        }
        if (parent != nullptr) created->SetParent(parent, false);

        auto* renderer = created->AddComponent<Components::PrimitiveMeshRendererComponent>();
        if (renderer == nullptr)
        {
            scene->DestroyGameObject(created->ID());
            context.CancelEdit();
            return;
        }
        // Plane / Cube 等は特別な GameObject ではなく、
        // 普通の GameObject に Primitive Mesh Renderer を付けて表現する。
        renderer->primitive_type = primitive_type;
        renderer->shading_model = 1;
        renderer->visible = true;

        context.CommitEdit();
        context.Selection().Select(created->ID(), false);
        context.SetStatus(std::string(display_name) + " を作成しました");
    }

    void HierarchyPanel::CreateLandscapeGround(EditorContext& context, GameObject* parent)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr || !context.CanEdit()) return;

        context.BeginEdit("Landscape Ground を作成");
        GameObject* ground = scene->CreateGameObject(UniqueObjectName(*scene, parent, "Ground"));
        if (ground == nullptr)
        {
            context.CancelEdit();
            return;
        }
        if (parent != nullptr) ground->SetParent(parent, false);

        auto* landscape = ground->AddComponent<Components::LandscapeComponent>();
        auto* renderer = ground->AddComponent<Components::LandscapeRendererComponent>();
        auto* collider = ground->AddComponent<Components::LandscapeColliderComponent>();
        if (landscape == nullptr || renderer == nullptr || collider == nullptr ||
            !landscape->GenerateFlat(33, 33, 2.0f, 0.0f))
        {
            scene->DestroyGameObject(ground->ID());
            context.CancelEdit();
            return;
        }

        // GenerateFlat が geometry 自体を Pivot 中心へ生成するため、
        // 作成経路によって Transform 補正を変えない。
        renderer->tint = { 0.36f, 0.48f, 0.31f, 1.0f };
        collider->double_sided = true;

        context.CommitEdit();
        context.Selection().Select(ground->ID(), false);
        context.SetStatus("Landscape Ground を作成しました");
    }

    void HierarchyPanel::CreateEmptyGameObject(EditorContext& context, GameObject* parent)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr || !context.CanEdit()) return;

        context.BeginEdit("GameObject を作成");
        GameObject* created = scene->CreateGameObject(UniqueObjectName(*scene, parent, "GameObject"));
        if (created == nullptr)
        {
            context.CancelEdit();
            return;
        }
        if (parent != nullptr) created->SetParent(parent, false);

        context.CommitEdit();
        context.Selection().Select(created->ID(), false);
        context.SetStatus("GameObject を作成しました");
    }

    void HierarchyPanel::DuplicateSelected(EditorContext& context)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr || !context.CanEdit()) return;
        if (context.Selection().Empty()) return;

        // 走査中に Scene が伸びるので、対象 ID を先に控える。
        const std::vector<ObjectID> targets = context.Selection().All();

        context.BeginEdit("GameObject を複製");
        std::vector<ObjectID> created_ids;
        for (const ObjectID id : targets)
        {
            GameObject* source = scene->FindGameObjectByID(id);
            if (source == nullptr) continue;

            GameObject* clone = Scene::Serialization::DuplicateGameObject(*scene, *source, true);
            if (clone != nullptr)
            {
                clone->SetName(UniqueObjectName(*scene, clone->Parent(), clone->Name(), clone));
                created_ids.push_back(clone->ID());
            }
        }

        if (created_ids.empty())
        {
            context.CancelEdit();
            return;
        }
        context.CommitEdit();

        context.Selection().Clear();
        for (const ObjectID id : created_ids) context.Selection().Select(id, true);
        context.SetStatus(std::to_string(created_ids.size()) + " 個を複製しました");
    }

    void HierarchyPanel::DestroySelected(EditorContext& context)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr || !context.CanEdit()) return;
        if (context.Selection().Empty()) return;

        const std::vector<ObjectID> targets = context.Selection().All();

        context.BeginEdit("GameObject を削除");
        for (const ObjectID id : targets)
        {
            // 削除予約が立つだけ。実体は CommitEdit の中で破棄される。
            scene->DestroyGameObject(id);
        }
        context.CommitEdit();

        context.Selection().Clear();
        context.SetStatus(std::to_string(targets.size()) + " 個を削除しました");
    }
}
