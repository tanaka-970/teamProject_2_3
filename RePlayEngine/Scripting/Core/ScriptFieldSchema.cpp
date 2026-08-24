#include "ScriptFieldSchema.h"

#include "ScriptComponent.h"

#include <algorithm>
#include <utility>

namespace ReplayEngine::Scripting
{
    using Reflection::PropertyDesc;
    using Reflection::PropertyType;

    std::string ScriptFieldDefinition::ResolvedDisplayName() const
    {
        if (!display_name.empty()) return display_name;
        return HumanizeFieldName(name);
    }

    std::string ScriptFieldDefinition::SavedName() const
    {
        return ScriptNames::MakeFieldSavedName(name);
    }

    ScriptValue ScriptFieldSchema::MakeTypeDefault(ScriptValueType type)
    {
        switch (type)
        {
        case PropertyType::Bool:            return ScriptValue::MakeBool(false);
        case PropertyType::Int:             return ScriptValue::MakeInt(0);
        case PropertyType::Int64:           return ScriptValue::MakeInt64(0);
        case PropertyType::UInt64:          return ScriptValue::MakeUInt64(0);
        case PropertyType::Float:           return ScriptValue::MakeFloat(0.0f);
        case PropertyType::Double:          return ScriptValue::MakeDouble(0.0);
        case PropertyType::String:          return ScriptValue::MakeString(std::string());
        case PropertyType::Vector2:         return ScriptValue::MakeVector2({ 0.0f, 0.0f });
        case PropertyType::Vector3:         return ScriptValue::MakeVector3({ 0.0f, 0.0f, 0.0f });
        case PropertyType::Vector4:         return ScriptValue::MakeVector4({ 0.0f, 0.0f, 0.0f, 0.0f });
        case PropertyType::Quaternion:      return ScriptValue::MakeQuaternion({ 0.0f, 0.0f, 0.0f, 1.0f });
        case PropertyType::Color:           return ScriptValue::MakeColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        case PropertyType::Enum:            return ScriptValue::MakeEnum(0);
        case PropertyType::AssetPath:       return ScriptValue::MakeAssetPath(std::string());
        case PropertyType::AssetReference:  return ScriptValue::MakeAssetReference(std::string());
        case PropertyType::SceneReference:  return ScriptValue::MakeSceneReference(std::string());

        // 「未設定」は無効 ObjectID で表す。Lua の nil / C# の null がここへ落ちる。
        case PropertyType::ObjectReference: return ScriptValue::MakeObjectReference(Core::ObjectID::Invalid());
        case PropertyType::ComponentReference: return ScriptValue::MakeComponentReference(Reflection::ComponentReference{});

        case PropertyType::CollisionLayer:    return ScriptValue::MakeCollisionLayer(0);
        case PropertyType::CollisionMask:     return ScriptValue::MakeCollisionMask(0);
        case PropertyType::ColliderReference: return ScriptValue::MakeColliderReference(0);

        case PropertyType::Array:
            return ScriptValue::MakeArray(PropertyType::Bool, {});
        }
        return ScriptValue{};
    }

    std::shared_ptr<const ScriptFieldSchema> ScriptFieldSchema::MakeEmpty(
        ScriptTypeID type_id, std::uint32_t revision)
    {
        return Build(type_id, revision, {}, nullptr);
    }

    std::shared_ptr<const ScriptFieldSchema> ScriptFieldSchema::Build(
        ScriptTypeID type_id, std::uint32_t revision,
        std::vector<ScriptFieldDefinition> fields,
        std::vector<std::string>* rejected)
    {
        // make_shared を使わないのはコンストラクタが private のため。
        // Schema は必ず Build / MakeEmpty から作らせたい。
        std::shared_ptr<ScriptFieldSchema> schema(new ScriptFieldSchema());
        schema->type_id_ = type_id;
        schema->revision_ = revision;
        schema->fields_.reserve(fields.size());

        for (ScriptFieldDefinition& definition : fields)
        {
            const auto reject = [rejected](const std::string& reason)
            {
                if (rejected != nullptr) rejected->push_back(reason);
            };

            if (definition.name.empty())
            {
                reject("名前が空の Field は無視しました");
                continue;
            }

            // 予約接頭辞を含む名前は受け付けない。
            // 受け付けると保存名が "field.__script.language" になり、
            // 読む側が管理情報と取り違えることは無いものの、
            // ログとエラー表示が極端に読みにくくなる。
            if (ScriptNames::IsInternalSavedName(definition.name) ||
                ScriptNames::IsFieldSavedName(definition.name))
            {
                reject("予約された接頭辞を含む Field 名は使えません: " + definition.name);
                continue;
            }

            if (Reflection::IsContainerType(definition.array_element_type))
            {
                reject("配列の要素へコンテナ型は使えません: " + definition.name);
                continue;
            }

            const bool duplicated = std::any_of(schema->fields_.begin(), schema->fields_.end(),
                [&definition](const ScriptFieldDefinition& existing)
                {
                    return existing.name == definition.name;
                });
            if (duplicated)
            {
                reject("同じ名前の Field が重複しています（先に宣言した方を使います）: " +
                    definition.name);
                continue;
            }

            // 既定値が未設定、または宣言した型と食い違う場合は型の既定値へ寄せる。
            // 型が違うまま持たせると、Inspector と保存で解釈が割れる。
            if (definition.type == PropertyType::Array)
            {
                if (definition.default_value.Type() != PropertyType::Array ||
                    definition.default_value.ArrayElementType() != definition.array_element_type)
                {
                    definition.default_value = ScriptValue::MakeArray(
                        definition.array_element_type, {});
                }
            }
            else if (definition.default_value.Type() != definition.type)
            {
                ScriptValue converted;
                if (definition.default_value.ConvertTo(definition.type, converted))
                {
                    definition.default_value = std::move(converted);
                }
                else
                {
                    definition.default_value = MakeTypeDefault(definition.type);
                }
            }

            schema->fields_.push_back(std::move(definition));
        }

        schema->BuildDescs();
        return schema;
    }

    void ScriptFieldSchema::BuildDescs()
    {
        descs_.clear();
        descs_.reserve(fields_.size());

        for (const ScriptFieldDefinition& definition : fields_)
        {
            // 捕捉するのは保存名の文字列だけ。
            // ScriptComponent のインスタンスも this も捕捉しない。
            // これにより 1 つの Schema を全インスタンスで共有できる。
            const std::string saved_name = definition.SavedName();

            PropertyDesc desc;
            desc.name = saved_name;
            desc.display_name = definition.ResolvedDisplayName();
            desc.tooltip = definition.tooltip;
            desc.type = definition.type;
            desc.has_range = definition.has_range;
            desc.minimum = definition.minimum;
            desc.maximum = definition.maximum;
            desc.serializable = definition.serializable;
            desc.editor_visible = definition.visible_in_inspector;
            desc.read_only = definition.read_only;
            desc.asset_type = definition.asset_type;
            desc.category = definition.category;
            desc.enum_labels = definition.enum_labels;
            desc.array_element_type = definition.array_element_type;

            desc.getter = [saved_name](const Core::Component& component) -> ScriptValue
            {
                const ScriptComponent* script = ScriptComponent::From(component);
                if (script == nullptr) return ScriptValue{};
                return script->ReadField(saved_name);
            };

            desc.setter = [saved_name](Core::Component& component, const ScriptValue& value)
            {
                ScriptComponent* script = ScriptComponent::From(component);
                if (script == nullptr) return;
                script->WriteField(saved_name, value);
            };

            descs_.push_back(std::move(desc));
        }
    }

    const ScriptFieldDefinition* ScriptFieldSchema::FindField(
        std::string_view name) const noexcept
    {
        for (const ScriptFieldDefinition& definition : fields_)
        {
            if (definition.name == name) return &definition;
        }
        return nullptr;
    }

    const ScriptFieldDefinition* ScriptFieldSchema::FindBySavedName(
        std::string_view saved_name) const noexcept
    {
        if (!ScriptNames::IsFieldSavedName(saved_name)) return nullptr;
        return FindField(ScriptNames::StripFieldPrefix(saved_name));
    }

    ScriptFieldStorage ScriptFieldSchema::MakeDefaultStorage() const
    {
        ScriptFieldStorage storage;
        for (const ScriptFieldDefinition& definition : fields_)
        {
            storage.Set(definition.SavedName(), definition.default_value);
        }
        return storage;
    }
}
