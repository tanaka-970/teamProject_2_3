// ScriptComponent のうち、設定・Field・Schema・保存と表示だけを持つ。
//
//   ScriptComponent.cpp         ... 設定・Field・Schema・保存・表示（このファイル）
//   ScriptComponentRuntime.cpp  ... runtime lifecycle と managed instance

#include "ScriptComponent.h"

#include "ScriptError.h"
#include "ScriptServices.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"

#include <utility>

namespace ReplayEngine::Scripting
{
    using Reflection::PropertyBag;

    ScriptComponent::~ScriptComponent() = default;

    ScriptComponent* ScriptComponent::From(Core::Component& component) noexcept
    {
        if (component.TypeID() != StaticTypeID()) return nullptr;
        return static_cast<ScriptComponent*>(&component);
    }

    const ScriptComponent* ScriptComponent::From(const Core::Component& component) noexcept
    {
        if (component.TypeID() != StaticTypeID()) return nullptr;
        return static_cast<const ScriptComponent*>(&component);
    }

    IScriptServices* ScriptComponent::Services() const noexcept
    {
        const Scene::Scene* scene = GetScene();
        if (scene == nullptr) return nullptr;
        return scene->Services().Scripts();
    }

    // -----------------------------------------------------------------------
    // 設定
    // -----------------------------------------------------------------------

    void ScriptComponent::SetLanguage(ScriptLanguage value)
    {
        if (language_ == value) return;
        language_ = value;
        RefreshScriptType();
    }

    void ScriptComponent::SetScriptAssetGUID(std::string value)
    {
        if (asset_guid_ == value) return;
        asset_guid_ = std::move(value);
        RefreshScriptType();
    }

    void ScriptComponent::SetClassName(std::string value)
    {
        if (class_name_ == value) return;
        class_name_ = std::move(value);
        RefreshScriptType();
    }

    void ScriptComponent::AssignScriptType(const ScriptTypeDescriptor& descriptor)
    {
        const ScriptTypeID assigned_type = descriptor.type_id.IsValid()
            ? descriptor.type_id
            : MakeScriptTypeID(descriptor.language, descriptor.asset_guid,
                descriptor.class_name);

        const bool changed =
            language_ != descriptor.language ||
            asset_guid_ != descriptor.asset_guid ||
            class_name_ != descriptor.class_name ||
            script_type_ != assigned_type;

        language_ = descriptor.language;
        asset_guid_ = descriptor.asset_guid;
        class_name_ = descriptor.class_name;
        script_type_ = assigned_type;

        if (changed)
        {
            if (schema_ && schema_->TypeID() != script_type_)
            {
                BindSchema(ScriptFieldSchemaRef{});
            }
            status_ = script_type_.IsValid() ? ScriptStatus::Unresolved
                : ScriptStatus::Unassigned;
            last_error_.clear();
        }

        if (ResolveSchema() && status_ == ScriptStatus::Unresolved)
        {
            status_ = HasInstance() ? ScriptStatus::Running : ScriptStatus::Loaded;
        }
    }

    void ScriptComponent::RestoreScriptType(ScriptTypeID value)
    {
        if (!value.IsValid()) return;
        script_type_ = value;
    }

    void ScriptComponent::RefreshScriptType()
    {
        ScriptTypeID derived = InvalidScriptTypeID();
        if (IScriptServices* services = Services())
        {
            for (const ScriptTypeDescriptor& descriptor : services->Catalog().All())
            {
                if (descriptor.language != language_) continue;
                if (!asset_guid_.empty() && descriptor.asset_guid != asset_guid_) continue;
                if (language_ == ScriptLanguage::CSharp &&
                    !class_name_.empty() && descriptor.class_name != class_name_) continue;
                derived = descriptor.type_id;
                break;
            }
        }

        if (!derived.IsValid())
        {
            if (language_ == ScriptLanguage::CSharp && script_type_.IsValid())
            {
                // C# は [ReplayGuid] が永続 ID。Scene/Pefab 読み込みで復元した
                // __script.type_id を、ファイル名・クラス名・フォルダ移動だけで
                // asset/class hash に置き換えない。
                derived = script_type_;
            }
            else
            {
                derived = MakeScriptTypeID(language_, asset_guid_, class_name_);
            }
        }

        if (derived == script_type_) return;

        script_type_ = derived;

        // 型が変わったので Schema は無効。次の同期点で引き直す。
        //
        // ここで即座に引き直さないのは、Inspector で Asset を選び替えた
        // その場（描画中）に DynamicProperties() の中身が入れ替わるのを避けるため。
        // 現在の値は BindSchema が pending へ退避する。
        if (schema_ && schema_->TypeID() != script_type_)
        {
            BindSchema(ScriptFieldSchemaRef{});
        }

        status_ = script_type_.IsValid() ? ScriptStatus::Unresolved : ScriptStatus::Unassigned;
        last_error_.clear();
    }

    void ScriptComponent::SetStatus(ScriptStatus status, std::string error_text)
    {
        status_ = status;
        last_error_ = std::move(error_text);
    }

    // -----------------------------------------------------------------------
    // Field
    // -----------------------------------------------------------------------

    ScriptValue ScriptComponent::ReadField(const std::string& saved_name) const
    {
        // Play 中は managed インスタンスの「今の値」を返す。
        //
        // これが無いと、スクリプトがフィールドを書き換えても Inspector は
        // C++ 側のキャッシュ（Play 開始時の値）を表示し続ける。
        // 値の流れが C++ -> C# の一方向になり、
        // 「スクリプトが動いているのかどうか」を確かめる手段が無くなる。
        //
        // 失敗したら黙ってキャッシュへ落とす。Play 停止直後や
        // インスタンス破棄済みの瞬間に Inspector が空を描かないようにするため。
        if (HasInstance())
        {
            if (IScriptServices* services = Services())
            {
                ScriptValue live;
                if (services->PullField(instance_, saved_name, live)) return live;
            }
        }

        if (const ScriptValue* stored = field_values_.Find(saved_name)) return *stored;

        // Schema にあって値がまだ無い場合は既定値を返す。
        // ここで空を返すと、Inspector が一瞬 0 を表示してから既定値へ戻る。
        if (schema_)
        {
            if (const ScriptFieldDefinition* definition = schema_->FindBySavedName(saved_name))
            {
                return definition->default_value;
            }
        }
        return ScriptValue{};
    }

    void ScriptComponent::WriteField(const std::string& saved_name, const ScriptValue& value)
    {
        const ScriptFieldDefinition* definition =
            schema_ ? schema_->FindBySavedName(saved_name) : nullptr;

        if (definition == nullptr)
        {
            // Schema が知らない名前。捨てずに預かる。
            pending_values_.Set(saved_name, value);
            return;
        }

        if (definition->read_only) return;

        // 宣言された型へ寄せてから持つ。
        // 型が違うまま持つと、保存で書かれる型と Inspector の描き方が食い違う。
        if (value.Type() == definition->type)
        {
            field_values_.Set(saved_name, value);
        }
        else
        {
            ScriptValue converted;
            if (value.ConvertTo(definition->type, converted))
            {
                field_values_.Set(saved_name, std::move(converted));
            }
            else
            {
                return;
            }
        }

        // 実行中なら、その場でインスタンスへも反映する。
        // Inspector で Play 中に値を変えたときに、次のフレームから効く。
        if (HasInstance())
        {
            if (IScriptServices* services = Services())
            {
                const ScriptValue* applied = field_values_.Find(saved_name);
                if (applied != nullptr) services->PushField(instance_, saved_name, *applied);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Schema
    // -----------------------------------------------------------------------

    const std::vector<Reflection::PropertyDesc>*
        ScriptComponent::DynamicProperties() const noexcept
    {
        if (!schema_) return nullptr;
        return &schema_->Descs();
    }

    bool ScriptComponent::ResolveSchema()
    {
        if (!script_type_.IsValid()) return false;

        IScriptServices* services = Services();
        if (services == nullptr) return static_cast<bool>(schema_);

        ScriptFieldSchemaRef resolved = services->ResolveSchema(script_type_);
        if (!resolved)
        {
            if (status_ != ScriptStatus::Error) status_ = ScriptStatus::Unresolved;
            return false;
        }

        if (schema_ && schema_.get() == resolved.get()) return true;

        BindSchema(std::move(resolved));
        return true;
    }

    void ScriptComponent::BindSchema(ScriptFieldSchemaRef schema)
    {
        if (schema_.get() == schema.get()) return;

        // 1. 今持っている値を 1 か所へ集める。
        //    Schema が知っていた値も、預かっていた値も同じ扱いにする。
        //    先に field_values_ を入れ、pending は同名があれば上書きしない
        //    （Schema が知っていた方が「正しく型付けされた値」なので優先する）。
        ScriptFieldStorage merged = field_values_;
        for (const PropertyBag::Entry& entry : pending_values_.Entries())
        {
            if (merged.Contains(entry.name)) continue;
            merged.Set(entry.name, entry.value);
        }

        schema_ = std::move(schema);

        field_values_.Clear();
        pending_values_.Clear();

        if (!schema_)
        {
            // Schema が無くなった。全部そのまま預かる。値は 1 つも捨てない。
            pending_values_ = std::move(merged);
            if (script_type_.IsValid() && status_ != ScriptStatus::Error)
            {
                status_ = ScriptStatus::Unresolved;
            }
            return;
        }

        // 2. 新しい顔ぶれで field_values_ を組み直す。
        for (const ScriptFieldDefinition& definition : schema_->Fields())
        {
            const std::string saved_name = definition.SavedName();
            const ScriptValue* previous = merged.Find(saved_name);

            if (previous == nullptr)
            {
                // 新しく増えた Field。既定値で埋める。
                field_values_.Set(saved_name, definition.default_value);
                continue;
            }

            if (previous->Type() == definition.type)
            {
                field_values_.Set(saved_name, *previous);
            }
            else
            {
                // 型が変わった。寄せられるなら寄せる。
                ScriptValue converted;
                if (previous->ConvertTo(definition.type, converted))
                {
                    field_values_.Set(saved_name, std::move(converted));
                }
                else
                {
                    // 寄せられない。既定値で動かしつつ、元の値は預かりへ残す。
                    // Schema を戻したときに復活させられるようにするため。
                    field_values_.Set(saved_name, definition.default_value);
                    pending_values_.Set(saved_name, *previous);
                }
            }
        }

        // 3. 新しい Schema が知らない名前は預かりへ残す。
        for (const PropertyBag::Entry& entry : merged.Entries())
        {
            if (field_values_.Contains(entry.name)) continue;
            if (pending_values_.Contains(entry.name)) continue;
            pending_values_.Set(entry.name, entry.value);
        }

        if (status_ != ScriptStatus::Error)
        {
            status_ = HasInstance() ? ScriptStatus::Running : ScriptStatus::Loaded;
        }

        // 実行中に差し替えられた場合は、新しい値をインスタンスへ流し直す。
        if (HasInstance()) PushAllFields();
    }

    // -----------------------------------------------------------------------
    // 保存
    // -----------------------------------------------------------------------

    void ScriptComponent::OnSerialize(PropertyBag& output) const
    {
        // 静的プロパティ（__script.*）と、Schema 由来の動的プロパティ（field.*）は
        // PropertyRegistry::Capture が先に書き終えている。
        //
        // ここで書くのは「預かっているぶん」だけ。
        // Schema が解決できていない状態で保存しても値が消えないようにする。
        for (const PropertyBag::Entry& entry : pending_values_.Entries())
        {
            if (output.Contains(entry.name)) continue;
            output.Set(entry.name, entry.value);
        }
    }

    void ScriptComponent::OnDeserialize(const PropertyBag& input)
    {
        // 静的プロパティと、Schema が知っている field.* は
        // PropertyRegistry::Apply が先に反映済み。
        //
        // ここでは「Schema が知らない field.*」を拾い直して預かる。
        // Schema がまだ無い場合は、field.* が全部ここへ来る。
        ScriptFieldStorage retained;

        for (const PropertyBag::Entry& entry : input.Entries())
        {
            if (!ScriptNames::IsFieldSavedName(entry.name)) continue;
            if (schema_ && schema_->FindBySavedName(entry.name) != nullptr) continue;
            retained.Set(entry.name, entry.value);
        }

        pending_values_ = std::move(retained);

        if (script_type_.IsValid() && status_ == ScriptStatus::Unassigned)
        {
            status_ = ScriptStatus::Unresolved;
        }
    }

    void ScriptComponent::OnPropertyChanged(const char* /*property_name*/)
    {
        // Inspector や読み込みで __script.asset / __script.class が変わったあと。
        // 導出し直してから Schema を引き直す。
        RefreshScriptType();
        ResolveSchema();
    }

    // -----------------------------------------------------------------------
    // 表示
    // -----------------------------------------------------------------------

    std::string ScriptComponent::DisplayLabel() const
    {
        if (!script_type_.IsValid()) return "Script";

        if (IScriptServices* services = Services())
        {
            std::string name = services->ResolveDisplayName(script_type_);
            if (!name.empty()) return name;
        }

        if (!class_name_.empty()) return class_name_ + " (未解決)";
        return "Script (未解決)";
    }
}
