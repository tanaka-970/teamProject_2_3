// Motion Asset のうち、キーの整列と replaymotion への保存だけを持つ。
//
//   MotionAsset.cpp      ... キーの整列と保存（このファイル）
//   MotionAssetLoad.cpp  ... replaymotion の読み込み

#include "MotionAsset.h"

#include "../Object/Registry/ComponentRegistry.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

namespace ReplayEngine::Motion
{
    namespace
    {
        const char* ToMotionTypeString(Reflection::PropertyType type) noexcept
        {
            using Reflection::PropertyType;
            switch (type)
            {
            case PropertyType::Bool: return "BOOL";
            case PropertyType::Int: return "INT";
            case PropertyType::Float: return "FLOAT";
            case PropertyType::Double: return "DOUBLE";
            case PropertyType::String: return "STRING";
            case PropertyType::Vector2: return "VEC2";
            case PropertyType::Vector3: return "VEC3";
            case PropertyType::Vector4: return "VEC4";
            case PropertyType::Quaternion: return "QUAT";
            case PropertyType::Color: return "COLOR";
            case PropertyType::Enum: return "ENUM";
            case PropertyType::AssetPath: return "ASSET_PATH";
            case PropertyType::ObjectReference: return "OBJECT";
            case PropertyType::CollisionLayer: return "COLLISION_LAYER";
            case PropertyType::CollisionMask: return "COLLISION_MASK";
            case PropertyType::ColliderReference: return "COLLIDER";
            case PropertyType::Int64: return "INT64";
            case PropertyType::UInt64: return "UINT64";
            case PropertyType::AssetReference: return "ASSET";
            case PropertyType::SceneReference: return "SCENE";
            case PropertyType::ComponentReference: return "COMPONENT";
            case PropertyType::Array: return "ARRAY";
            }
            return "FLOAT";
        }

        const char* ToBlendModeString(MotionBlendMode mode) noexcept
        {
            switch (mode)
            {
            case MotionBlendMode::Override: return "Override";
            case MotionBlendMode::Additive: return "Additive";
            case MotionBlendMode::Multiply: return "Multiply";
            case MotionBlendMode::Blend: return "Blend";
            }
            return "Override";
        }

        const char* ToTrackLoopString(MotionTrackLoop loop) noexcept
        {
            switch (loop)
            {
            case MotionTrackLoop::None: return "None";
            case MotionTrackLoop::Repeat: return "Repeat";
            case MotionTrackLoop::PingPong: return "PingPong";
            case MotionTrackLoop::Offset: return "Offset";
            }
            return "None";
        }

        void WriteValue(std::ostream& out, const Reflection::PropertyValue& value)
        {
            using Reflection::PropertyType;
            switch (value.Type())
            {
            case PropertyType::Bool:
                out << (value.AsBool() ? 1 : 0);
                break;
            case PropertyType::Int:
            case PropertyType::Enum:
            case PropertyType::CollisionLayer:
            case PropertyType::CollisionMask:
            case PropertyType::ColliderReference:
                out << value.AsInt();
                break;
            case PropertyType::Int64:
                out << value.AsInt64();
                break;
            case PropertyType::UInt64:
                out << value.AsUInt64();
                break;
            case PropertyType::Float:
                out << value.AsFloat();
                break;
            case PropertyType::Double:
                out << value.AsDouble();
                break;
            case PropertyType::String:
            case PropertyType::AssetPath:
            case PropertyType::AssetReference:
            case PropertyType::SceneReference:
                out << std::quoted(value.AsString());
                break;
            case PropertyType::Vector2:
            {
                const DirectX::XMFLOAT2 v = value.AsVector2();
                out << v.x << ' ' << v.y;
                break;
            }
            case PropertyType::Vector3:
            {
                const DirectX::XMFLOAT3 v = value.AsVector3();
                out << v.x << ' ' << v.y << ' ' << v.z;
                break;
            }
            case PropertyType::Vector4:
            case PropertyType::Quaternion:
            case PropertyType::Color:
            {
                const DirectX::XMFLOAT4 v = value.AsVector4();
                out << v.x << ' ' << v.y << ' ' << v.z << ' ' << v.w;
                break;
            }
            case PropertyType::ObjectReference:
                out << value.AsObjectReference().Value();
                break;
            case PropertyType::ComponentReference:
            {
                const Reflection::ComponentReference reference = value.AsComponentReference();
                out << reference.owner.Value() << ' ' << reference.component;
                break;
            }
            case PropertyType::Array:
                out << 0;
                break;
            }
        }
    }

    void MotionAsset::SortKeys()
    {
        for (MotionTrack& track : tracks)
        {
            std::sort(track.keys.begin(), track.keys.end(),
                [](const MotionKeyframe& a, const MotionKeyframe& b)
                {
                    return a.time < b.time;
                });
        }
        for (MotionEventTrack& track : event_tracks)
        {
            std::sort(track.events.begin(), track.events.end(),
                [](const MotionEvent& a, const MotionEvent& b)
                {
                    return a.time < b.time;
                });
        }
    }

    bool MotionAsset::SaveToFile(const std::filesystem::path& path,
        const MotionAsset& asset, std::string& error)
    {
        if (path.has_parent_path())
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec)
            {
                error = "Motion Assetの保存先を作成できません: " + ec.message();
                return false;
            }
        }

        std::ofstream file(path);
        if (!file)
        {
            error = "Motion Assetを書き込めません: " + path.string();
            return false;
        }

        file << std::setprecision(std::numeric_limits<float>::max_digits10);
        file << "MOTION " << std::quoted(asset.name) << '\n';
        file << "MOTION_VERSION " << MotionAsset::current_version << '\n';
        file << "DURATION " << asset.duration << '\n';
        if (asset.time_remap.IsAssigned())
            file << "TIME_REMAP " << std::quoted(asset.time_remap.guid) << '\n';
        file << '\n';

        for (const MotionTrack& track : asset.tracks)
        {
            file << "TRACK " << std::quoted(track.name) << '\n';
            file << "OBJECT " << track.binding.object.Value() << '\n';
            file << "BINDING_ORIGIN " << track.binding.origin << '\n';
            file << "BINDING_PATH " << std::quoted(track.binding.relative_path) << '\n';
            if (const Core::ComponentTypeInfo* info =
                Core::ComponentRegistry::Find(track.binding.component_type))
            {
                file << "COMPONENT_TYPE " << std::quoted(info->type_name) << '\n';
            }
            else
            {
                file << "COMPONENT_TYPE_ID " << track.binding.component_type << '\n';
            }
            file << "COMPONENT_INDEX " << track.binding.component_index << '\n';
            file << "PROPERTY " << std::quoted(track.binding.property) << '\n';
            file << "VALUE_TYPE " << ToMotionTypeString(track.value_type) << '\n';
            file << "ENABLED " << (track.enabled ? 1 : 0) << '\n';
            file << "BLEND_MODE " << ToBlendModeString(track.blend_mode) << '\n';
            const MotionWiggle default_wiggle{};
            if (track.wiggle.enabled != default_wiggle.enabled ||
                track.wiggle.amplitude != default_wiggle.amplitude ||
                track.wiggle.frequency != default_wiggle.frequency ||
                track.wiggle.seed != default_wiggle.seed ||
                track.wiggle.octaves != default_wiggle.octaves)
            {
                file << "WIGGLE " << (track.wiggle.enabled ? 1 : 0) << ' '
                    << track.wiggle.amplitude << ' ' << track.wiggle.frequency << ' '
                    << track.wiggle.seed << ' ' << track.wiggle.octaves << '\n';
            }
            if (track.loop != MotionTrackLoop::None)
                file << "LOOP " << ToTrackLoopString(track.loop) << '\n';

            for (const MotionKeyframe& key : track.keys)
            {
                file << "KEY " << key.time << ' ' << ToMotionTypeString(key.value.Type()) << ' ';
                WriteValue(file, key.value);
                file << " EASING " << ToString(key.easing);
                if (key.easing == MotionEasing::CustomBezier)
                {
                    file << " OUT " << key.bezier.out_handle.x << ' ' <<
                        key.bezier.out_handle.y;
                    file << " IN " << key.bezier.in_handle.x << ' ' <<
                        key.bezier.in_handle.y;
                }
                else if (key.easing == MotionEasing::PresetCurve)
                {
                    file << " CURVE " << std::quoted(key.easing_curve.guid);
                }
                file << '\n';
            }

            file << "END_TRACK\n\n";
        }

        for (const MotionEventTrack& track : asset.event_tracks)
        {
            file << "EVENT_TRACK\n";
            file << "OBJECT " << track.object.Value() << '\n';
            for (const MotionEvent& event : track.events)
            {
                file << "EVENT " << event.time << ' ' << std::quoted(event.name)
                    << ' ' << std::quoted(event.parameter) << '\n';
            }
            file << "END_EVENT_TRACK\n\n";
        }

        if (!file)
        {
            error = "Motion Assetの書き込み中に失敗しました: " + path.string();
            return false;
        }
        return true;
    }
}
