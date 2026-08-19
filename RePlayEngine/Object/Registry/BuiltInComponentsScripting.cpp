#include "BuiltInComponentsInternal.h"

namespace ReplayEngine::Core::Detail
{
        void RegisterScript()
        {
            using Scripting::ScriptComponent;
            using Scripting::ScriptLanguage;
            using Scripting::ScriptLanguageFromInt;
            namespace ScriptNames = Scripting::ScriptNames;

            ComponentRegistry::Register<ScriptComponent>(
                ComponentTypeInfo::Describe("Script", "Scripting")
                    .WithTypeGUID(ScriptComponent::StaticTypeGUID())
                    .InModule(ScriptComponent::module_id)
                    .WithVersion(1)
                    .AllowMultipleInstances()
                    .WithTooltip(
                        "Lua または C# のスクリプトを取り付ける。"
                        "公開変数は選んだスクリプトに応じて Inspector へ出る。"));

            // 保存名へ予約接頭辞を付ける。
            //
            // ユーザーが language / class_name / script_asset という名前の
            // 公開変数を宣言しても、そちらの保存名は field.language のように
            // なるため、構造的に衝突しない。
            //
            // Display() を必ず付けるので、Inspector に接頭辞は出ない。

            PropertyRegistry::Register<ScriptComponent>(
                MakeAccessorProperty<ScriptComponent>(ScriptNames::language,
                    PropertyType::Enum,
                    [](const ScriptComponent& script)
                    {
                        return PropertyValue::MakeEnum(static_cast<int>(script.Language()));
                    },
                    [](ScriptComponent& script, const PropertyValue& value)
                    {
                        script.SetLanguage(ScriptLanguageFromInt(value.AsInt(0)));
                    })
                    .AsEnum({ "Lua", "C#" })
                    .Display("Language"));

            PropertyRegistry::Register<ScriptComponent>(
                MakeAccessorProperty<ScriptComponent>(ScriptNames::asset,
                    PropertyType::AssetReference,
                    [](const ScriptComponent& script)
                    {
                        return PropertyValue::MakeAssetReference(script.ScriptAssetGUID());
                    },
                    [](ScriptComponent& script, const PropertyValue& value)
                    {
                        script.SetScriptAssetGUID(value.AsAssetReference().guid);
                    })
                    .Display("Script")
                    .OfAssetType("Script"));

            PropertyRegistry::Register<ScriptComponent>(
                MakeAccessorProperty<ScriptComponent>(ScriptNames::class_name,
                    PropertyType::String,
                    [](const ScriptComponent& script)
                    {
                        return PropertyValue::MakeString(script.ClassName());
                    },
                    [](ScriptComponent& script, const PropertyValue& value)
                    {
                        script.SetClassName(value.AsString());
                    })
                    .Display("Class")
                    .Tooltip("C# の完全修飾クラス名。Lua では使わない。"));

            // Scene の各更新フェーズ内だけを並べ、後段ステージは動かさない。
            PropertyRegistry::Register<ScriptComponent>(
                MakeAccessorProperty<ScriptComponent>(ScriptNames::execution_order,
                    PropertyType::Int,
                    [](const ScriptComponent& script)
                    {
                        return PropertyValue::MakeInt(
                            static_cast<int>(script.ExecutionOrder()));
                    },
                    [](ScriptComponent& script, const PropertyValue& value)
                    {
                        script.SetExecutionOrder(
                            static_cast<std::int32_t>(value.AsInt(0)));
                    })
                    .Display("Execution Order")
                    .Tooltip("小さいほど各 Update フェーズ内で先に実行。"
                        "同値は Hierarchy / Component の保存順。"
                        "Motion / プロパティ接続 / UI レイアウトなどの後段は動きません。"));

            // Script Asset が一時的に見つからない状態でも
            // 「どのスクリプト型だったか」を保てるようにするため保存する。
            // 人が編集するものではないので Inspector には出さない。
            PropertyRegistry::Register<ScriptComponent>(
                MakeAccessorProperty<ScriptComponent>(ScriptNames::type_id,
                    PropertyType::String,
                    [](const ScriptComponent& script)
                    {
                        return PropertyValue::MakeString(script.ScriptType().ToString());
                    },
                    [](ScriptComponent& script, const PropertyValue& value)
                    {
                        Reflection::TypeGUID parsed;
                        if (Reflection::TypeGUID::TryParse(value.AsString(), parsed))
                        {
                            script.RestoreScriptType(parsed);
                        }
                    })
                    .Display("Script Type ID")
                    .HiddenInEditor());
        }
}
