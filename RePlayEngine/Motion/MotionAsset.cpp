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
                input >> std::quoted(track.name);
                asset.tracks.push_back(std::move(track));
                current_track = &asset.tracks.back();
            }
            else if (head == "END_TRACK")
            {
                current_track = nullptr;
            }
            else if (head == "OBJECT" && current_track != nullptr)
            {
                Core::ObjectID::ValueType raw = Core::ObjectID::invalid_value;
                input >> raw;
                current_track->binding.object = Core::ObjectID(raw);
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
        }

        (void)file_version;
        if (asset.duration < 0.0f) asset.duration = 0.0f;
        asset.SortKeys();
        out = std::move(asset);
        return true;
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
        file << '\n';

        for (const MotionTrack& track : asset.tracks)
        {
            file << "TRACK " << std::quoted(track.name) << '\n';
            file << "OBJECT " << track.binding.object.Value() << '\n';
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
                file << '\n';
            }

            file << "END_TRACK\n\n";
        }

        if (!file)
        {
            error = "Motion Assetの書き込み中に失敗しました: " + path.string();
            return false;
        }
        return true;
    }
}
