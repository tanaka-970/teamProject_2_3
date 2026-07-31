#pragma once

#include "../../Core/ObjectID/ObjectID.h"

#include <string>
#include <vector>

namespace ReplayEngine::Scene { class Scene; }
namespace ReplayEngine::Core { class GameObject; }

namespace ReplayEngine::Editor
{
    // 「この GameObject は操作対象として成立しているか」を判定する小さな検証。
    //
    // 型ごとの専用 Editor を作らないための仕組み。
    // 必要な Component の型名と、欠けたときに何が起きるかを表で持ち、
    // InspectorPanel はその表を描くだけにする。
    //
    // 判定は警告であり、旧 Player へ戻す条件ではない。
    // Component が欠けても GameObject も表示もそのまま残る。
    class PlayerCompositionValidator final
    {
    public:
        struct Requirement
        {
            // ComponentRegistry へ登録した型名。
            std::string type_name;

            // Inspector に出す名前。
            std::string display_name;

            // 欠けているときに何が起きるか。
            std::string missing_effect;

            // false なら「あると良い」程度。欠けても警告色にしない。
            bool required = true;

            // 実際に付いているか（Validate が埋める）。
            bool present = false;
        };

        struct Result
        {
            std::vector<Requirement> requirements;

            // 必須がすべて揃っているか。
            bool complete = false;

            // この GameObject が現在の操作対象か。
            bool is_controlled = false;

            // 欠けている必須 Component の数。
            int missing_required = 0;
        };

        PlayerCompositionValidator() = delete;

        // 操作対象として成立するために見る Component の一覧。
        // 「不足 Component を追加」もこの表を使うので、定義はここ 1 か所だけ。
        static const std::vector<Requirement>& Requirements();

        static Result Validate(const Scene::Scene& scene, const Core::GameObject& object);
    };
}
