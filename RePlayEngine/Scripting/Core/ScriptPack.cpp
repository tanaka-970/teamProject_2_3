#include "ScriptPack.h"
#include "../CSharp/CSharpProject.h"
#include "../../Runtime/Packaging/PackIO.h"
#include "../../Scene/Serialization/SceneSerializer.h"

#include <set>
#include <cctype>

namespace ReplayEngine::Scripting::ScriptPack
{
    namespace
    {
        using Runtime::Packaging::Json;
        using Runtime::Packaging::Require;
        using Reflection::PropertyType;
        using Reflection::PropertyValue;
        using Project = CSharp::CSharpProject;

        Json SaveValue(const PropertyValue& value)
        {
            Require(value.IsFinite(), "Non-finite script default");
            Json body;
            switch (value.Type())
            {
            case PropertyType::Bool: body = value.AsBool(); break;
            case PropertyType::Int: case PropertyType::Enum:
            case PropertyType::CollisionLayer: case PropertyType::CollisionMask:
            case PropertyType::ColliderReference: body = value.AsInt(); break;
            case PropertyType::Int64: body = value.AsInt64(); break;
            case PropertyType::UInt64: body = value.AsUInt64(); break;
            case PropertyType::Float: body = value.AsFloat(); break;
            case PropertyType::Double: body = value.AsDouble(); break;
            case PropertyType::String: case PropertyType::AssetPath:
            case PropertyType::AssetReference: case PropertyType::SceneReference:
                body = value.AsString(); break;
            case PropertyType::Vector2:
            {
                const auto v = value.AsVector2(); body = { v.x, v.y }; break;
            }
            case PropertyType::Vector3:
            {
                const auto v = value.AsVector3(); body = { v.x, v.y, v.z }; break;
            }
            case PropertyType::Vector4: case PropertyType::Color: case PropertyType::Quaternion:
            {
                const auto v = value.AsVector4(); body = { v.x, v.y, v.z, v.w }; break;
            }
            case PropertyType::ObjectReference: body = value.AsObjectReference().Value(); break;
            case PropertyType::ComponentReference:
            {
                const auto v = value.AsComponentReference(); body = { v.owner.Value(), v.component }; break;
            }
            case PropertyType::Array:
                Require(!Reflection::IsContainerType(value.ArrayElementType()), "Nested script arrays are unsupported");
                body = Json::array();
                for (const auto& element : value.ArrayElements()) body.push_back(SaveValue(element));
                return { {"type", Reflection::ToString(value.Type())}, {"element", Reflection::ToString(value.ArrayElementType())}, {"value", body} };
            default: throw std::runtime_error("Unsupported script default type");
            }
            return { {"type", Reflection::ToString(value.Type())}, {"value", body} };
        }

        PropertyType ReadType(const Json& j)
        {
            PropertyType type;
            Require(Reflection::TryParsePropertyType(j.get<std::string>(), type), "Unknown script field type");
            return type;
        }

        PropertyValue ReadValue(const Json& j, bool array_element = false)
        {
            const auto type = ReadType(j.at("type"));
            const auto& v = j.at("value");
            switch (type)
            {
            case PropertyType::Bool: return PropertyValue::MakeBool(v.get<bool>());
            case PropertyType::Int: return PropertyValue::MakeInt(v.get<int>());
            case PropertyType::Enum: return PropertyValue::MakeEnum(v.get<int>());
            case PropertyType::CollisionLayer: return PropertyValue::MakeCollisionLayer(v.get<int>());
            case PropertyType::CollisionMask: return PropertyValue::MakeCollisionMask(v.get<int>());
            case PropertyType::ColliderReference: return PropertyValue::MakeColliderReference(v.get<int>());
            case PropertyType::Int64: return PropertyValue::MakeInt64(v.get<std::int64_t>());
            case PropertyType::UInt64: return PropertyValue::MakeUInt64(v.get<std::uint64_t>());
            case PropertyType::Float: return PropertyValue::MakeFloat(v.get<float>());
            case PropertyType::Double: return PropertyValue::MakeDouble(v.get<double>());
            case PropertyType::String: return PropertyValue::MakeString(v.get<std::string>());
            case PropertyType::AssetPath: return PropertyValue::MakeAssetPath(v.get<std::string>());
            case PropertyType::AssetReference: return PropertyValue::MakeAssetReference(v.get<std::string>());
            case PropertyType::SceneReference: return PropertyValue::MakeSceneReference(v.get<std::string>());
            case PropertyType::Vector2: return PropertyValue::MakeVector2({ v.at(0).get<float>(), v.at(1).get<float>() });
            case PropertyType::Vector3: return PropertyValue::MakeVector3({ v.at(0).get<float>(), v.at(1).get<float>(), v.at(2).get<float>() });
            case PropertyType::Vector4: case PropertyType::Color: case PropertyType::Quaternion:
            {
                const DirectX::XMFLOAT4 vector{ v.at(0).get<float>(), v.at(1).get<float>(), v.at(2).get<float>(), v.at(3).get<float>() };
                if (type == PropertyType::Color) return PropertyValue::MakeColor(vector);
                if (type == PropertyType::Quaternion) return PropertyValue::MakeQuaternion(vector);
                return PropertyValue::MakeVector4(vector);
            }
            case PropertyType::ObjectReference: return PropertyValue::MakeObjectReference(Core::ObjectID(v.get<std::uint64_t>()));
            case PropertyType::ComponentReference:
            {
                Reflection::ComponentReference reference;
                reference.owner = Core::ObjectID(v.at(0).get<std::uint64_t>());
                reference.component = v.at(1).get<Core::ComponentStableID>();
                return PropertyValue::MakeComponentReference(reference);
            }
            case PropertyType::Array:
            {
                Require(!array_element && v.is_array() && v.size() <= 65536, "Invalid script array");
                const auto element_type = ReadType(j.at("element"));
                Require(!Reflection::IsContainerType(element_type), "Nested script array");
                std::vector<PropertyValue> elements;
                for (const auto& item : v)
                {
                    auto element = ReadValue(item, true);
                    Require(element.Type() == element_type, "Script array element type mismatch");
                    elements.push_back(std::move(element));
                }
                return PropertyValue::MakeArray(element_type, std::move(elements));
            }
            default: throw std::runtime_error("Unsupported script default type");
            }
        }

        Json SaveSchema(const ScriptFieldSchema& schema)
        {
            Json fields = Json::array();
            for (const auto& f : schema.Fields())
                fields.push_back({ {"name", f.name}, {"display", f.display_name}, {"tooltip", f.tooltip},
                    {"type", Reflection::ToString(f.type)}, {"default", SaveValue(f.default_value)},
                    {"range", f.has_range}, {"min", f.minimum}, {"max", f.maximum},
                    {"visible", f.visible_in_inspector}, {"serializable", f.serializable},
                    {"readonly", f.read_only}, {"asset_type", f.asset_type}, {"category", f.category},
                    {"enum", f.enum_labels}, {"element", Reflection::ToString(f.array_element_type)} });
            return { {"id", schema.TypeID().ToString()}, {"revision", schema.Revision()},
                {"inactive", schema.InstantiateWhenInactive()}, {"fields", fields} };
        }

        ScriptFieldSchemaRef ReadSchema(const Json& j, ScriptTypeID id)
        {
            Require(j.at("id") == id.ToString(), "Script schema ID mismatch");
            const auto& list = j.at("fields");
            Require(list.is_array() && list.size() <= 4096, "Invalid script field count");
            std::vector<ScriptFieldDefinition> fields;
            for (const auto& v : list)
            {
                ScriptFieldDefinition f;
                f.name = v.at("name").get<std::string>();
                f.display_name = v.at("display").get<std::string>();
                f.tooltip = v.at("tooltip").get<std::string>();
                f.type = ReadType(v.at("type"));
                f.default_value = ReadValue(v.at("default"));
                Require(f.default_value.IsFinite(), "Non-finite script default: " + f.name);
                f.has_range = v.at("range").get<bool>();
                f.minimum = v.at("min").get<double>();
                f.maximum = v.at("max").get<double>();
                f.visible_in_inspector = v.at("visible").get<bool>();
                f.serializable = v.at("serializable").get<bool>();
                f.read_only = v.at("readonly").get<bool>();
                f.asset_type = v.at("asset_type").get<std::string>();
                f.category = v.at("category").get<std::string>();
                f.enum_labels = v.at("enum").get<std::vector<std::string>>();
                f.array_element_type = ReadType(v.at("element"));
                fields.push_back(std::move(f));
            }
            std::vector<std::string> rejected;
            auto schema = ScriptFieldSchema::Build(id, j.at("revision").get<std::uint32_t>(),
                std::move(fields), &rejected, j.at("inactive").get<bool>());
            Require(schema && rejected.empty(), "Invalid script schema: " + id.ToString());
            return schema;
        }
    }

    bool Save(const std::filesystem::path& root, const ScriptTypeCatalog& catalog,
        const std::string& configuration, const std::filesystem::path& output,
        std::vector<std::string>& errors)
    {
        const auto initial_errors = errors.size();
        try
        {
            Require(configuration == "Debug" || configuration == "Release", "Invalid script assembly configuration");
            const auto scripts = Project::ScriptsRoot(root);
            for (std::filesystem::recursive_directory_iterator it(scripts), end; it != end; ++it)
            {
                if (it->is_directory() && (it->path().filename() == "bin" || it->path().filename() == "obj"))
                {
                    it.disable_recursion_pending();
                    continue;
                }
                if (it->is_regular_file() && it->path().extension() == ".cs")
                    (void)Runtime::Packaging::FileFingerprint(it->path());
            }
            Json types = Json::array();
            for (const auto& d : catalog.All())
            {
                if (d.language != ScriptLanguage::CSharp) continue;
                if (!d.type_id.IsValid() || d.class_name.empty() || d.asset_guid.empty() ||
                    !d.schema || d.status != ScriptStatus::Loaded || d.schema->TypeID() != d.type_id)
                {
                    errors.push_back("Missing C# catalog key: " + d.type_id.ToString() + " / " + d.class_name + ": " + d.last_error);
                    continue;
                }
                try
                {
                    const auto schema = SaveSchema(*d.schema);
                    (void)ReadSchema(schema, d.type_id);
                    types.push_back({ {"id", d.type_id.ToString()}, {"language", static_cast<int>(d.language)},
                        {"name", d.script_name}, {"display", d.display_name}, {"asset", d.asset_guid},
                        {"class", d.class_name}, {"category", d.category}, {"schema", schema} });
                }
                catch (const std::exception& exception)
                {
                    errors.push_back("C# catalog key: " + d.type_id.ToString() + " / " +
                        d.class_name + ": " + exception.what());
                }
            }
            if (errors.size() != initial_errors) return false;
            const auto game = Project::GameScriptsAssemblyPath(root, configuration);
            const auto api = Project::ManagedApiAssemblyPath(root, configuration);
            const auto config = Project::ManagedApiRuntimeConfigPath(root, configuration);
            Runtime::Packaging::WritePack(output, { {"kind", "scripts"}, {"version", 1},
                {"configuration", configuration}, {"game", Runtime::Packaging::FileFingerprint(game)},
                {"api", Runtime::Packaging::FileFingerprint(api)},
                {"runtimeconfig", Runtime::Packaging::FileFingerprint(config)}, {"types", types} });
            return true;
        }
        catch (const std::exception& exception) { errors.push_back(exception.what()); return false; }
    }

    bool Load(const std::filesystem::path& root, ScriptTypeCatalog& catalog,
        std::string& configuration, std::string& error)
    {
        try
        {
            const auto data = Runtime::Packaging::ReadPack(root / "resources" / "ScriptCatalog.replaypack", "scripts");
            const auto selected = data.at("configuration").get<std::string>();
            Require(selected == "Release" || selected == "Debug", "Invalid script assembly configuration");
            Require(data.at("game") == Runtime::Packaging::FileFingerprint(Project::GameScriptsAssemblyPath(root, selected)), "ScriptCatalog game DLL mismatch");
            Require(data.at("api") == Runtime::Packaging::FileFingerprint(Project::ManagedApiAssemblyPath(root, selected)), "ScriptCatalog managed API DLL mismatch");
            Require(data.at("runtimeconfig") == Runtime::Packaging::FileFingerprint(Project::ManagedApiRuntimeConfigPath(root, selected)), "ScriptCatalog runtimeconfig mismatch");
            const auto& types = data.at("types");
            Require(types.is_array() && types.size() <= 65536, "Invalid script catalog size");
            ScriptTypeCatalog replacement;
            std::set<std::string> classes;
            for (const auto& j : types)
            {
                ScriptTypeDescriptor d;
                Require(ScriptTypeID::TryParse(j.at("id").get<std::string>(), d.type_id) &&
                    d.type_id.IsValid() && replacement.Find(d.type_id) == nullptr, "Invalid or duplicate script ID");
                Require(j.at("language") == static_cast<int>(ScriptLanguage::CSharp), "Unsupported packed script language");
                d.language = ScriptLanguage::CSharp;
                d.script_name = j.at("name").get<std::string>();
                d.display_name = j.at("display").get<std::string>();
                d.asset_guid = j.at("asset").get<std::string>();
                d.class_name = j.at("class").get<std::string>();
                d.category = j.at("category").get<std::string>();
                Require(!d.asset_guid.empty() && !d.class_name.empty() && classes.insert(d.class_name).second, "Invalid or duplicate script class");
                d.schema = ReadSchema(j.at("schema"), d.type_id);
                d.status = ScriptStatus::Loaded;
                replacement.Register(std::move(d));
            }
            configuration = selected;
            catalog = std::move(replacement);
            return true;
        }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    bool ValidateReferences(const std::filesystem::path& root,
        const ScriptTypeCatalog& catalog, std::vector<std::string>& errors)
    {
        const auto initial_errors = errors.size();
        try
        {
            for (const auto& file : std::filesystem::recursive_directory_iterator(root / "resources"))
            {
                if (!file.is_regular_file()) continue;
                auto extension = file.path().extension().u8string();
                for (auto& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (extension != ".replayscene" && extension != ".replayprefab") continue;
                Scene::Serialization::SceneData scene;
                std::string error;
                if (!Scene::Serialization::SceneSerializer::LoadFromFile(scene, file.path(), error))
                {
                    errors.push_back(file.path().generic_u8string() + ": " + error);
                    continue;
                }
                for (const auto& object : scene.objects)
                {
                    for (const auto& component : object.components)
                    {
                        const auto& properties = component.properties;
                        const auto text = [&](const char* name)
                        {
                            const auto* value = properties.Find(name);
                            return value != nullptr ? value->AsString() : std::string();
                        };
                        ScriptTypeID id;
                        ScriptTypeID::TryParse(text(ScriptNames::type_id), id);
                        const auto asset = text(ScriptNames::asset);
                        const auto class_name = text(ScriptNames::class_name);
                        if (!id.IsValid() && asset.empty() && class_name.empty()) continue;
                        const auto* language = properties.Find(ScriptNames::language);
                        const auto* descriptor = id.IsValid() ? catalog.Find(id) : nullptr;
                        if (descriptor == nullptr && (!asset.empty() || !class_name.empty()))
                        {
                            for (const auto& candidate : catalog.All())
                            {
                                if (language != nullptr && static_cast<int>(candidate.language) != language->AsInt()) continue;
                                if (!asset.empty() && candidate.asset_guid != asset) continue;
                                if (!class_name.empty() && candidate.class_name != class_name) continue;
                                descriptor = &candidate;
                                break;
                            }
                        }
                        if (descriptor == nullptr)
                            errors.push_back("Missing C# catalog key: " + id.ToString() + " / " + class_name +
                                " / " + file.path().generic_u8string() + " / " + object.name);
                    }
                }
            }
        }
        catch (const std::exception& exception) { errors.push_back(exception.what()); }
        return errors.size() == initial_errors;
    }
}
