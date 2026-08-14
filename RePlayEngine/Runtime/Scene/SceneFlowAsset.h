#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace ReplayEngine::Runtime
{
    enum class SceneFlowConditionType : std::int32_t
    {
        Bool = 0,
        Int = 1,
        Float = 2,
    };

    enum class SceneFlowCompareOp : std::int32_t
    {
        Equal = 0,
        NotEqual = 1,
        Less = 2,
        LessEqual = 3,
        Greater = 4,
        GreaterEqual = 5,
    };

    struct SceneFlowCondition final
    {
        SceneFlowConditionType type = SceneFlowConditionType::Bool;
        SceneFlowCompareOp op = SceneFlowCompareOp::Equal;
        std::string key;
        double value = 1.0;
    };

    struct SceneFlowTransition final
    {
        std::uint64_t id = 0;
        bool enabled = true;
        int priority = 0;

        // 空なら「どの Scene からでも」。AssetGUID で保持し、Scene 名は使わない。
        std::string from_scene_guid;
        std::string event_name{ "Next" };
        std::string to_scene_guid;

        // 全条件が成立したときだけ遷移する (AND)。空ならイベントだけで遷移。
        std::vector<SceneFlowCondition> conditions;
    };

    class SceneFlowAsset final
    {
    public:
        static constexpr int current_version = 1;
        static constexpr const char* file_extension = ".replaysceneflow";

        std::string name{ "Scene Flow" };
        std::vector<SceneFlowTransition> transitions;

        void Clear() noexcept;
        std::uint64_t AllocateTransitionID() noexcept;
        SceneFlowTransition& AddTransition();
        bool RemoveTransition(std::uint64_t id);

        static bool Save(const SceneFlowAsset& asset,
            const std::filesystem::path& path, std::string& error);
        static bool Load(SceneFlowAsset& asset,
            const std::filesystem::path& path, std::string& error);

        static bool WriteText(const SceneFlowAsset& asset,
            std::ostream& stream, std::string& error);
        static bool ReadText(SceneFlowAsset& asset,
            std::istream& stream, std::string& error);

    private:
        std::uint64_t next_transition_id_ = 1;
    };

    const char* ToString(SceneFlowConditionType type) noexcept;
    const char* ToString(SceneFlowCompareOp op) noexcept;
}
