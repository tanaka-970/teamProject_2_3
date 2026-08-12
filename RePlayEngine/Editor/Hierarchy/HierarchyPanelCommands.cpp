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

#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace ReplayEngine::Editor
{
    using Core::GameObject;
    using Core::ObjectID;

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
        GameObject* created = scene->CreateGameObject(display_name);
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
        GameObject* ground = scene->CreateGameObject("Ground");
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
        GameObject* created = scene->CreateGameObject("GameObject");
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
            if (clone != nullptr) created_ids.push_back(clone->ID());
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
