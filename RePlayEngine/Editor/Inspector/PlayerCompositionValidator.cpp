#include "PlayerCompositionValidator.h"

#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Scene/Runtime/Scene.h"

namespace ReplayEngine::Editor
{
    const std::vector<PlayerCompositionValidator::Requirement>&
        PlayerCompositionValidator::Requirements()
    {
        // 型名は ComponentRegistry の登録名と一致させる。
        // ここへ 1 行足すだけで、診断表示と「不足 Component を追加」の両方へ反映される。
        static const std::vector<Requirement> table = []
        {
            std::vector<Requirement> list;

            const auto add = [&list](const char* type_name, const char* display,
                const char* effect, bool required)
            {
                Requirement requirement;
                requirement.type_name = type_name;
                requirement.display_name = display;
                requirement.missing_effect = effect;
                requirement.required = required;
                list.push_back(std::move(requirement));
            };

            add("PlayerInputComponent", "Player Input",
                "入力を取得できないため操作できません", true);
            add("PlayerControllerComponent", "Player Controller",
                "入力を Character Motor へ渡せないため操作できません", true);
            add("CharacterMotorComponent", "Character Motor",
                "移動要求を処理できないため動きません", true);
            add("SkinnedMeshRendererComponent", "Skinned Mesh Renderer",
                "モデルが描画されません", false);
            add("AnimatorComponent", "Animator",
                "アニメーションが切り替わりません", false);
            add("SphereColliderComponent", "Sphere Collider",
                "接地判定と壁の押し戻しが効きません", false);
            add("CameraTargetComponent", "Camera Target",
                "カメラが追従しません", false);
            add("HealthComponent", "Health",
                "体力を扱えません", false);

            return list;
        }();
        return table;
    }

    PlayerCompositionValidator::Result PlayerCompositionValidator::Validate(
        const Scene::Scene& scene, const Core::GameObject& object)
    {
        Result result;
        result.requirements = Requirements();
        result.is_controlled = scene.Services().ControlledObject() == object.ID();

        for (Requirement& requirement : result.requirements)
        {
            const Core::ComponentTypeInfo* info =
                Core::ComponentRegistry::Find(requirement.type_name);

            requirement.present = info != nullptr &&
                object.FindComponent(info->type_id) != nullptr;

            if (requirement.required && !requirement.present) ++result.missing_required;
        }

        result.complete = result.missing_required == 0;
        return result;
    }
}
