#pragma once

#include "MotionEasing.h"
#include "../Core/ObjectID/ObjectID.h"
#include "../Object/Component/ComponentTypeID.h"
#include "../Reflection/Property/PropertyValue.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Motion
{
    struct MotionBinding
    {
        Core::ObjectID object;
        Core::ComponentTypeID component_type = Core::invalid_component_type_id;
        int component_index = 0;
        std::string property;

        bool Valid() const noexcept
        {
            return object.Valid() && component_type != Core::invalid_component_type_id &&
                component_index >= 0 && !property.empty();
        }
    };

    struct MotionKeyframe
    {
        float time = 0.0f;
        Reflection::PropertyValue value;
        MotionEasing easing = MotionEasing::Linear;
        MotionBezierHandles bezier;
    };

    struct MotionTrack
    {
        std::string name;
        MotionBinding binding;
        Reflection::PropertyType value_type = Reflection::PropertyType::Float;
        bool enabled = true;
        std::vector<MotionKeyframe> keys;
    };

    class MotionAsset final
    {
    public:
        static constexpr const char* file_extension = ".replaymotion";

        std::string name{ "Motion" };
        float duration = 1.0f;
        std::vector<MotionTrack> tracks;

        void SortKeys();
        bool Empty() const noexcept { return tracks.empty(); }

        static bool LoadFromFile(const std::filesystem::path& path,
            MotionAsset& out, std::string& error);
        static bool SaveToFile(const std::filesystem::path& path,
            const MotionAsset& asset, std::string& error);
    };
}
