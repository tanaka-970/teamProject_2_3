#include "SceneFlowAsset.h"
#include "../../Rendering/RenderStats.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <locale>
#include <limits>
#include <sstream>

namespace ReplayEngine::Runtime
{
    namespace
    {
        constexpr const char* magic = "REPLAY_SCENE_FLOW";

        template<class T>
        bool InRange(T value, T minimum, T maximum) noexcept
        {
            return value >= minimum && value <= maximum;
        }
    }

    const char* ToString(SceneFlowConditionType type) noexcept
    {
        switch (type)
        {
        case SceneFlowConditionType::Bool:  return "Bool";
        case SceneFlowConditionType::Int:   return "Int";
        case SceneFlowConditionType::Float: return "Float";
        }
        return "Unknown";
    }

    const char* ToString(SceneFlowCompareOp op) noexcept
    {
        switch (op)
        {
        case SceneFlowCompareOp::Equal:        return "==";
        case SceneFlowCompareOp::NotEqual:     return "!=";
        case SceneFlowCompareOp::Less:         return "<";
        case SceneFlowCompareOp::LessEqual:    return "<=";
        case SceneFlowCompareOp::Greater:      return ">";
        case SceneFlowCompareOp::GreaterEqual: return ">=";
        }
        return "?";
    }

    void SceneFlowAsset::Clear() noexcept
    {
        name = "Scene Flow";
        transitions.clear();
        next_transition_id_ = 1;
    }

    std::uint64_t SceneFlowAsset::AllocateTransitionID() noexcept
    {
        if (next_transition_id_ == 0) next_transition_id_ = 1;
        return next_transition_id_++;
    }

    SceneFlowTransition& SceneFlowAsset::AddTransition()
    {
        SceneFlowTransition transition;
        transition.id = AllocateTransitionID();
        transitions.push_back(std::move(transition));
        return transitions.back();
    }

    bool SceneFlowAsset::RemoveTransition(std::uint64_t id)
    {
        const auto found = std::remove_if(transitions.begin(), transitions.end(),
            [id](const SceneFlowTransition& transition) { return transition.id == id; });
        if (found == transitions.end()) return false;
        transitions.erase(found, transitions.end());
        return true;
    }

    bool SceneFlowAsset::WriteText(const SceneFlowAsset& asset,
        std::ostream& stream, std::string& error)
    {
        stream.imbue(std::locale::classic());
        stream << magic << ' ' << current_version << '\n';
        stream << "NAME " << std::quoted(asset.name) << '\n';
        stream << "TRANSITIONS " << asset.transitions.size() << '\n';

        for (const SceneFlowTransition& transition : asset.transitions)
        {
            stream << "TRANSITION " << transition.id << ' '
                << (transition.enabled ? 1 : 0) << ' ' << transition.priority << ' '
                << std::quoted(transition.from_scene_guid) << ' '
                << std::quoted(transition.event_name) << ' '
                << std::quoted(transition.to_scene_guid) << ' '
                << transition.conditions.size() << '\n';

            for (const SceneFlowCondition& condition : transition.conditions)
            {
                stream << "CONDITION " << static_cast<int>(condition.type) << ' '
                    << static_cast<int>(condition.op) << ' '
                    << std::quoted(condition.key) << ' '
                    << std::setprecision(17) << condition.value << '\n';
            }
            stream << "END_TRANSITION\n";
        }

        if (!stream)
        {
            error = "Scene Flow の書き込みに失敗しました";
            return false;
        }
        return true;
    }

    bool SceneFlowAsset::ReadText(SceneFlowAsset& asset,
        std::istream& stream, std::string& error)
    {
        stream.imbue(std::locale::classic());
        SceneFlowAsset loaded;

        std::string token;
        int version = 0;
        if (!(stream >> token >> version) || token != magic || version != current_version)
        {
            error = "Scene Flow Asset の形式が不正です";
            return false;
        }

        std::size_t declared_count = 0;
        bool declared_transitions = false;
        while (stream >> token)
        {
            if (token == "NAME")
            {
                stream >> std::quoted(loaded.name);
            }
            else if (token == "TRANSITIONS")
            {
                if (!(stream >> declared_count))
                {
                    error = "Scene Flow の Transition 数を読み取れません";
                    return false;
                }
                declared_transitions = true;
                loaded.transitions.reserve(declared_count);
            }
            else if (token == "TRANSITION")
            {
                SceneFlowTransition transition;
                int enabled = 0;
                std::size_t condition_count = 0;
                if (!(stream >> transition.id >> enabled >> transition.priority
                    >> std::quoted(transition.from_scene_guid)
                    >> std::quoted(transition.event_name)
                    >> std::quoted(transition.to_scene_guid)
                    >> condition_count))
                {
                    error = "Scene Flow の Transition を読み取れません";
                    return false;
                }
                transition.enabled = enabled != 0;
                transition.conditions.reserve(condition_count);

                for (std::size_t index = 0; index < condition_count; ++index)
                {
                    std::string condition_token;
                    int type = 0;
                    int op = 0;
                    SceneFlowCondition condition;
                    if (!(stream >> condition_token >> type >> op
                        >> std::quoted(condition.key) >> condition.value) ||
                        condition_token != "CONDITION" ||
                        !InRange(type, 0, 2) || !InRange(op, 0, 5))
                    {
                        error = "Scene Flow の Condition を読み取れません";
                        return false;
                    }
                    condition.type = static_cast<SceneFlowConditionType>(type);
                    condition.op = static_cast<SceneFlowCompareOp>(op);
                    transition.conditions.push_back(std::move(condition));
                }

                std::string end_token;
                if (!(stream >> end_token) || end_token != "END_TRANSITION")
                {
                    error = "Scene Flow の END_TRANSITION がありません";
                    return false;
                }

                if (transition.id == 0) transition.id = loaded.AllocateTransitionID();
                const std::uint64_t maximum =
                    (std::numeric_limits<std::uint64_t>::max)();
                if (transition.id < maximum)
                    loaded.next_transition_id_ =
                        (std::max)(loaded.next_transition_id_, transition.id + 1);
                loaded.transitions.push_back(std::move(transition));
            }
            else
            {
                std::string ignored;
                std::getline(stream, ignored);
            }
        }

        if (declared_transitions && loaded.transitions.size() != declared_count)
        {
            error = "Scene Flow の Transition 数が一致しません";
            return false;
        }

        asset = std::move(loaded);
        return true;
    }

    bool SceneFlowAsset::Save(const SceneFlowAsset& asset,
        const std::filesystem::path& path, std::string& error)
    {
        std::error_code filesystem_error;
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path(), filesystem_error);
            if (filesystem_error)
            {
                error = "Scene Flow 保存フォルダーを作成できません";
                return false;
            }
        }

        std::ostringstream text;
        if (!WriteText(asset, text, error)) return false;

        const std::filesystem::path temporary = path.string() + ".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                error = "Scene Flow ファイルを作成できません";
                return false;
            }
            const std::string buffer = text.str();
            stream.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            if (!stream)
            {
                error = "Scene Flow ファイルを書き込めません";
                return false;
            }
        }

        std::filesystem::rename(temporary, path, filesystem_error);
        if (filesystem_error)
        {
            filesystem_error.clear();
            std::filesystem::copy_file(temporary, path,
                std::filesystem::copy_options::overwrite_existing, filesystem_error);
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            if (filesystem_error)
            {
                error = "Scene Flow ファイルを差し替えられません";
                return false;
            }
        }
        return true;
    }

    bool SceneFlowAsset::Load(SceneFlowAsset& asset,
        const std::filesystem::path& path, std::string& error)
    {
        REPLAY_PROFILE_SCOPE("Asset/SceneFlow");
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            error = "Scene Flow ファイルを開けません: " + path.generic_u8string();
            return false;
        }
        return ReadText(asset, stream, error);
    }
}
