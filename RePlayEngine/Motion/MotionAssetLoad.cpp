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
        std::string Upper(std::string value)
        {
            for (char& c : value)
            {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            return value;
        }

        bool ParseMotionType(std::string token, Reflection::PropertyType& out)
        {
            token = Upper(std::move(token));
            using Reflection::PropertyType;
            if (token == "BOOL") out = PropertyType::Bool;
            else if (token == "INT") out = PropertyType::Int;
            else if (token == "FLOAT") out = PropertyType::Float;
            else if (token == "DOUBLE") out = PropertyType::Double;
            else if (token == "STRING") out = PropertyType::String;
            else if (token == "VEC2" || token == "VECTOR2") out = PropertyType::Vector2;
            else if (token == "VEC3" || token == "VECTOR3") out = PropertyType::Vector3;
            else if (token == "VEC4" || token == "VECTOR4") out = PropertyType::Vector4;
            else if (token == "QUAT" || token == "QUATERNION") out = PropertyType::Quaternion;
            else if (token == "COLOR") out = PropertyType::Color;
            else if (token == "ENUM") out = PropertyType::Enum;
            else if (token == "ASSET_PATH") out = PropertyType::AssetPath;
            else if (token == "OBJECT") out = PropertyType::ObjectReference;
            else if (token == "COLLISION_LAYER") out = PropertyType::CollisionLayer;
            else if (token == "COLLISION_MASK") out = PropertyType::CollisionMask;
            else if (token == "COLLIDER") out = PropertyType::ColliderReference;
            else if (token == "INT64") out = PropertyType::Int64;
            else if (token == "UINT64") out = PropertyType::UInt64;
            else if (token == "ASSET") out = PropertyType::AssetReference;
            else if (token == "SCENE") out = PropertyType::SceneReference;
            else if (token == "COMPONENT") out = PropertyType::ComponentReference;
            else return Reflection::TryParsePropertyType(token, out);
            return true;
        }

        bool ParseBlendMode(std::string token, MotionBlendMode& out)
        {
            token = Upper(std::move(token));
            if (token == "OVERRIDE" || token == "0")
            {
                out = MotionBlendMode::Override;
                return true;
            }
            if (token == "ADDITIVE" || token == "ADD" || token == "1")
            {
                out = MotionBlendMode::Additive;
                return true;
            }
            if (token == "MULTIPLY" || token == "2")
            {
                out = MotionBlendMode::Multiply;
                return true;
            }
            if (token == "BLEND" || token == "MIX" || token == "3")
            {
                out = MotionBlendMode::Blend;
                return true;
            }
            return false;
        }

        bool ReadValue(std::istream& in, Reflection::PropertyType type,
            Reflection::PropertyValue& out)
        {
            using Reflection::PropertyType;
            switch (type)
            {
            case PropertyType::Bool:
            {
                std::string token;
                if (!(in >> token)) return false;
                token = Upper(std::move(token));
                out = Reflection::PropertyValue::MakeBool(
                    token == "1" || token == "TRUE" || token == "ON");
                return true;
            }
            case PropertyType::Int:
            case PropertyType::Enum:
            case PropertyType::CollisionLayer:
            case PropertyType::CollisionMask:
            case PropertyType::ColliderReference:
            {
                int value = 0;
                if (!(in >> value)) return false;
                if (type == PropertyType::Enum) out = Reflection::PropertyValue::MakeEnum(value);
                else if (type == PropertyType::CollisionLayer)
                    out = Reflection::PropertyValue::MakeCollisionLayer(value);
                else if (type == PropertyType::CollisionMask)
                    out = Reflection::PropertyValue::MakeCollisionMask(value);
                else if (type == PropertyType::ColliderReference)
                    out = Reflection::PropertyValue::MakeColliderReference(value);
                else out = Reflection::PropertyValue::MakeInt(value);
                return true;
            }
            case PropertyType::Int64:
            {
                std::int64_t value = 0;
                if (!(in >> value)) return false;
                out = Reflection::PropertyValue::MakeInt64(value);
                return true;
            }
            case PropertyType::UInt64:
            {
                std::uint64_t value = 0;
                if (!(in >> value)) return false;
                out = Reflection::PropertyValue::MakeUInt64(value);
                return true;
            }
            case PropertyType::Float:
            {
                float value = 0.0f;
                if (!(in >> value)) return false;
                out = Reflection::PropertyValue::MakeFloat(value);
                return true;
            }
            case PropertyType::Double:
            {
                double value = 0.0;
                if (!(in >> value)) return false;
                out = Reflection::PropertyValue::MakeDouble(value);
                return true;
            }
            case PropertyType::String:
            case PropertyType::AssetPath:
            case PropertyType::AssetReference:
            case PropertyType::SceneReference:
            {
                std::string value;
                if (!(in >> std::quoted(value))) return false;
                if (type == PropertyType::AssetPath)
                    out = Reflection::PropertyValue::MakeAssetPath(std::move(value));
                else if (type == PropertyType::AssetReference)
                    out = Reflection::PropertyValue::MakeAssetReference(std::move(value));
                else if (type == PropertyType::SceneReference)
                    out = Reflection::PropertyValue::MakeSceneReference(std::move(value));
                else out = Reflection::PropertyValue::MakeString(std::move(value));
                return true;
            }
            case PropertyType::Vector2:
            {
                DirectX::XMFLOAT2 value{};
                if (!(in >> value.x >> value.y)) return false;
                out = Reflection::PropertyValue::MakeVector2(value);
                return true;
            }
            case PropertyType::Vector3:
            {
                DirectX::XMFLOAT3 value{};
                if (!(in >> value.x >> value.y >> value.z)) return false;
                out = Reflection::PropertyValue::MakeVector3(value);
                return true;
            }
            case PropertyType::Vector4:
            case PropertyType::Quaternion:
            case PropertyType::Color:
            {
                DirectX::XMFLOAT4 value{};
                if (!(in >> value.x >> value.y >> value.z >> value.w)) return false;
                if (type == PropertyType::Quaternion)
                    out = Reflection::PropertyValue::MakeQuaternion(value);
                else if (type == PropertyType::Color)
                    out = Reflection::PropertyValue::MakeColor(value);
                else out = Reflection::PropertyValue::MakeVector4(value);
                return true;
            }
            case PropertyType::ObjectReference:
            {
                Core::ObjectID::ValueType raw = Core::ObjectID::invalid_value;
                if (!(in >> raw)) return false;
                out = Reflection::PropertyValue::MakeObjectReference(Core::ObjectID(raw));
                return true;
            }
            case PropertyType::ComponentReference:
            {
                Reflection::ComponentReference reference;
                Core::ObjectID::ValueType raw = Core::ObjectID::invalid_value;
                if (!(in >> raw >> reference.component)) return false;
                reference.owner = Core::ObjectID(raw);
                out = Reflection::PropertyValue::MakeComponentReference(reference);
                return true;
            }
            case PropertyType::Array:
                return false;
            }
            return false;
        }
    }

    bool MotionAsset::LoadFromFile(const std::filesystem::path& path,
        MotionAsset& out, std::string& error)
    {
        std::ifstream file(path);
        if (!file)
        {
            error = "Motion Assetを開けません: " + path.string();
            return false;
        }

        MotionAsset asset;
        MotionTrack* current_track = nullptr;
        MotionEventTrack* current_event_track = nullptr;
        int file_version = 1;
        std::string line;
        int line_number = 0;
        while (std::getline(file, line))
        {
            ++line_number;
            std::istringstream input(line);
            std::string head;
            if (!(input >> head) || head.empty() || head[0] == '#') continue;

            if (head == "MOTION" || head == "MOTION_ASSET")
            {
                input >> std::quoted(asset.name);
            }
            else if (head == "DURATION")
            {
                input >> asset.duration;
            }
            else if (head == "MOTION_VERSION" || head == "VERSION")
            {
                input >> file_version;
            }
            else if (head == "TRACK")
            {
                MotionTrack track;
                // 起点情報を持たない旧 Asset は絶対参照として読む。
                track.binding.origin = static_cast<int>(MotionBindingOrigin::Absolute);
                input >> std::quoted(track.name);
                asset.tracks.push_back(std::move(track));
                current_track = &asset.tracks.back();
                current_event_track = nullptr;
            }
            else if (head == "END_TRACK")
            {
                current_track = nullptr;
            }
            else if (head == "EVENT_TRACK")
            {
                MotionEventTrack track;
                asset.event_tracks.push_back(std::move(track));
                current_event_track = &asset.event_tracks.back();
                current_track = nullptr;
            }
            else if (head == "END_EVENT_TRACK")
            {
                current_event_track = nullptr;
            }
            else if (head == "OBJECT" && current_track != nullptr)
            {
                Core::ObjectID::ValueType raw = Core::ObjectID::invalid_value;
                input >> raw;
                current_track->binding.object = Core::ObjectID(raw);
            }
            else if (head == "OBJECT" && current_event_track != nullptr)
            {
                Core::ObjectID::ValueType raw = Core::ObjectID::invalid_value;
                input >> raw;
                current_event_track->object = Core::ObjectID(raw);
            }
            else if ((head == "BINDING_ORIGIN" || head == "ORIGIN") &&
                current_track != nullptr)
            {
                int origin = static_cast<int>(MotionBindingOrigin::Absolute);
                if (!(input >> origin) ||
                    origin < static_cast<int>(MotionBindingOrigin::Absolute) ||
                    origin > static_cast<int>(MotionBindingOrigin::ChildPath))
                {
                    error = "Motion AssetのBINDING_ORIGINが不正です: line " +
                        std::to_string(line_number);
                    return false;
                }
                current_track->binding.origin = origin;
            }
            else if ((head == "BINDING_PATH" || head == "PATH") &&
                current_track != nullptr)
            {
                input >> std::quoted(current_track->binding.relative_path);
            }
            else if (head == "COMPONENT_TYPE" && current_track != nullptr)
            {
                std::string type_name;
                input >> std::quoted(type_name);
                const Core::ComponentTypeInfo* info =
                    Core::ComponentRegistry::Find(type_name);
                current_track->binding.component_type = info != nullptr
                    ? info->type_id
                    : Core::MakeComponentTypeID(type_name);
            }
            else if (head == "COMPONENT_TYPE_ID" && current_track != nullptr)
            {
                input >> current_track->binding.component_type;
            }
            else if (head == "COMPONENT_INDEX" && current_track != nullptr)
            {
                input >> current_track->binding.component_index;
            }
            else if (head == "PROPERTY" && current_track != nullptr)
            {
                input >> std::quoted(current_track->binding.property);
            }
            else if (head == "VALUE_TYPE" && current_track != nullptr)
            {
                std::string token;
                input >> token;
                if (!ParseMotionType(token, current_track->value_type))
                {
                    error = "Motion AssetのVALUE_TYPEが不正です: line " +
                        std::to_string(line_number);
                    return false;
                }
            }
            else if (head == "ENABLED" && current_track != nullptr)
            {
                int enabled = 1;
                input >> enabled;
                current_track->enabled = enabled != 0;
            }
            else if (head == "BLEND_MODE" && current_track != nullptr)
            {
                std::string token;
                input >> token;
                if (!ParseBlendMode(token, current_track->blend_mode))
                {
                    error = "Motion Asset縺ｮBLEND_MODE縺御ｸ肴ｭ｣縺ｧ縺・ line " +
                        std::to_string(line_number);
                    return false;
                }
            }
            else if (head == "KEY" && current_track != nullptr)
            {
                MotionKeyframe key;
                std::string type_token;
                if (!(input >> key.time >> type_token))
                {
                    error = "Motion Keyが不完全です: line " + std::to_string(line_number);
                    return false;
                }

                Reflection::PropertyType key_type = Reflection::PropertyType::Float;
                if (!ParseMotionType(type_token, key_type) ||
                    !ReadValue(input, key_type, key.value))
                {
                    error = "Motion Keyの値が不正です: line " + std::to_string(line_number);
                    return false;
                }
                current_track->value_type = key_type;

                std::string token;
                while (input >> token)
                {
                    if (token == "EASING")
                    {
                        std::string easing_name;
                        input >> easing_name;
                        MotionEasing easing = MotionEasing::Linear;
                        if (TryParseMotionEasing(easing_name.c_str(), easing))
                        {
                            key.easing = easing;
                        }
                    }
                    else if (token == "OUT")
                    {
                        input >> key.bezier.out_handle.x >> key.bezier.out_handle.y;
                    }
                    else if (token == "IN")
                    {
                        input >> key.bezier.in_handle.x >> key.bezier.in_handle.y;
                    }
                }

                current_track->keys.push_back(std::move(key));
            }
            else if (head == "EVENT" && current_event_track != nullptr)
            {
                MotionEvent event;
                if (!(input >> event.time >> std::quoted(event.name)))
                {
                    error = "Motion Event が不完全です: line " +
                        std::to_string(line_number);
                    return false;
                }
                input >> std::quoted(event.parameter);
                current_event_track->events.push_back(std::move(event));
            }
        }

        (void)file_version;
        if (asset.duration < 0.0f) asset.duration = 0.0f;
        asset.SortKeys();
        out = std::move(asset);
        return true;
    }
}
