#pragma once

#include "MotionEasing.h"
#include "../Core/ObjectID/ObjectID.h"
#include "../Object/Component/ComponentTypeID.h"
#include "../Reflection/Property/PropertyValue.h"
#include "../Reflection/Property/References.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Motion
{
    // Motion の対象 GameObject をどこから解決するか。
    // 保存済み Asset との互換性のため、値は途中へ挿入せず末尾へ追加する。
    enum class MotionBindingOrigin : int
    {
        Absolute = 0,
        Self = 1,
        Parent = 2,
        ChildPath = 3,
    };

    enum class MotionBlendMode
    {
        Override = 0,
        Additive = 1,
        Multiply = 2,
        Blend = 3,
    };

    struct MotionBinding
    {
        // 新しく Editor で作る Binding は Self を既定にする。
        // LoadFromFile は起点情報のない旧 Asset を Absolute へ明示的に戻す。
        int origin = static_cast<int>(MotionBindingOrigin::Self);
        Core::ObjectID object;
        Core::ComponentTypeID component_type = Core::invalid_component_type_id;
        int component_index = 0;
        std::string property;
        std::string relative_path;

        bool Valid() const noexcept
        {
            if (origin < static_cast<int>(MotionBindingOrigin::Absolute) ||
                origin > static_cast<int>(MotionBindingOrigin::ChildPath))
            {
                return false;
            }
            if (origin == static_cast<int>(MotionBindingOrigin::Absolute) && !object.Valid())
            {
                return false;
            }
            if (origin == static_cast<int>(MotionBindingOrigin::ChildPath) &&
                relative_path.empty())
            {
                return false;
            }
            return component_type != Core::invalid_component_type_id &&
                component_index >= 0 && !property.empty();
        }
    };

    struct MotionKeyframe
    {
        float time = 0.0f;
        Reflection::PropertyValue value;
        MotionEasing easing = MotionEasing::Linear;
        MotionBezierHandles bezier;
        Reflection::AssetReference easing_curve;
    };

    struct MotionWiggle
    {
        bool enabled = false;
        float amplitude = 0.0f;
        float frequency = 2.0f;
        int seed = 0;
        int octaves = 1;
    };

    struct MotionExpression
    {
        bool enabled = false;
        std::string source;
    };

    enum class MotionTrackLoop : int
    {
        None = 0,
        Repeat = 1,
        PingPong = 2,
        Offset = 3,
    };

    struct MotionTrack
    {
        std::string name;
        MotionBinding binding;
        Reflection::PropertyType value_type = Reflection::PropertyType::Float;
        bool enabled = true;
        MotionBlendMode blend_mode = MotionBlendMode::Override;
        std::vector<MotionKeyframe> keys;
        MotionWiggle wiggle;
        MotionTrackLoop loop = MotionTrackLoop::None;
        MotionExpression expression;
    };

    struct MotionEvent
    {
        float time = 0.0f;
        std::string name;
        std::string parameter;
    };

    struct MotionEventTrack
    {
        Core::ObjectID object;
        std::vector<MotionEvent> events;
    };

    class MotionAsset final
    {
    public:
        static constexpr const char* file_extension = ".replaymotion";
        static constexpr int current_version = 7;

        std::string name{ "Motion" };
        float duration = 1.0f;
        Reflection::AssetReference time_remap;
        std::vector<MotionTrack> tracks;
        std::vector<MotionEventTrack> event_tracks;

        void SortKeys();
        bool Empty() const noexcept { return tracks.empty() && event_tracks.empty(); }

        static bool LoadFromFile(const std::filesystem::path& path,
            MotionAsset& out, std::string& error);
        static bool SaveToFile(const std::filesystem::path& path,
            const MotionAsset& asset, std::string& error);
    };
}
