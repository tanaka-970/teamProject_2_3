// InspectorPanel のうち「Component 単体の表示・診断・削除」だけを持つ。
//
// 単体選択のヘッダー・Prefab 表示は InspectorPanel.cpp、
// 複数選択の一括編集は InspectorPanelMultiSelection.cpp に置く。
#include "InspectorPanel.h"

#include "PropertyDrawer.h"
#include "../Validation/SceneValidator.h"
#include "PlayerCompositionValidator.h"
#include "../../Assets/AssetDatabase.h"
#include "../../Object/Component/MissingComponent.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../Core/EditorContext.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Serialization/PrefabSerializer.h"
#include "../../Scripting/Core/ScriptComponent.h"
#include "../../Scripting/Core/ScriptTypes.h"
#include "../../Components/Landscape/LandscapeComponent.h"
#include "../../Components/Landscape/LandscapeRendererComponent.h"
#include "../../Components/Landscape/LandscapeColliderComponent.h"
#include "../../Components/Rendering/PrimitiveMeshRendererComponent.h"
#include "../../Components/Rendering/LightComponents.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace ReplayEngine::Editor
{
    using Core::ComponentRegistry;
    using Core::ComponentTypeInfo;
    using Core::GameObject;

    // 読み込めなかった Component の中身を、そのまま見せる。
    //
    // 【なぜ表示するのか】
    //   MissingComponent は「型が使えないあいだ預かっているだけ」の入れ物で、
    //   保存し直せば元の型として書き戻される。ここで何も見せないと、
    //   ユーザーには「Component が消えた」ようにしか見えず、
    //   まだ生きているデータを手で消してしまう。
    //
    // 【触らないもの】
    //   PropertyBag は読み取り専用で出す。編集欄を作らない。
    //   型が分からない状態で値を書き換えると、型が戻ったときに解釈できない。
    //   消えるのはユーザーが明示的に Remove Component を押したときだけ。
    void InspectorPanel::DrawMissingComponentDetails(const Core::MissingComponent& missing)
    {
        const Core::MissingComponent::Record& record = missing.Original();

        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
            u8"この Component の型がこのビルドに存在しません");
        ImGui::TextDisabled(u8"%s", missing.DescribeReason().c_str());

        ImGui::Separator();
        ImGui::Text(u8"元の型名: %s", record.type_name.empty()
            ? u8"(不明)" : record.type_name.c_str());
        ImGui::Text(u8"Type GUID: %s", record.type_guid.IsValid()
            ? record.type_guid.ToString().c_str() : u8"(未記録)");
        ImGui::Text(u8"Module ID: %s", record.module_id.empty()
            ? u8"(未記録)" : record.module_id.c_str());
        ImGui::Text(u8"Type Version: %d", record.type_version);

        ImGui::Separator();
        ImGui::Text(u8"保持しているプロパティ: %zu 件", record.properties.Size());

        if (ImGui::TreeNodeEx(u8"Serialized Property（読み取り専用）",
            ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (const Reflection::PropertyBag::Entry& entry : record.properties.Entries())
            {
                // 参照型は「何を指していたか」まで出す。
                // 型が戻ったときに参照が復元されることを確かめられるようにするため。
                switch (entry.value.Type())
                {
                case Reflection::PropertyType::ObjectReference:
                    ImGui::BulletText(u8"%s : ObjectReference -> ObjectID %llu",
                        entry.name.c_str(), static_cast<unsigned long long>(
                            entry.value.AsObjectReference().Value()));
                    break;
                case Reflection::PropertyType::ComponentReference:
                {
                    const Reflection::ComponentReference reference =
                        entry.value.AsComponentReference();
                    ImGui::BulletText(
                        u8"%s : ComponentReference -> ObjectID %llu / StableID %u",
                        entry.name.c_str(),
                        static_cast<unsigned long long>(reference.owner.Value()),
                        static_cast<unsigned int>(reference.component));
                    break;
                }
                case Reflection::PropertyType::AssetReference:
                case Reflection::PropertyType::SceneReference:
                    ImGui::BulletText(u8"%s : %s -> %s", entry.name.c_str(),
                        Reflection::ToString(entry.value.Type()),
                        entry.value.AsString().empty()
                            ? u8"(未設定)" : entry.value.AsString().c_str());
                    break;
                case Reflection::PropertyType::Array:
                    ImGui::BulletText(u8"%s : Array<%s> × %zu", entry.name.c_str(),
                        Reflection::ToString(entry.value.ArrayElementType()),
                        entry.value.ArrayElements().size());
                    break;
                case Reflection::PropertyType::String:
                    ImGui::BulletText(u8"%s : String = \"%s\"", entry.name.c_str(),
                        entry.value.AsString().c_str());
                    break;
                default:
                    ImGui::BulletText(u8"%s : %s", entry.name.c_str(),
                        Reflection::ToString(entry.value.Type()));
                    break;
                }
            }
            if (record.properties.Size() == 0)
            {
                ImGui::TextDisabled(u8"（保持しているプロパティはありません）");
            }
            ImGui::TreePop();
        }

        ImGui::TextDisabled(
            u8"型が使えるようになれば、次に読み込んだ時点で自動的に復元されます。");
        ImGui::TextDisabled(
            u8"保存し直しても、この内容は元の型として書き戻されます。");
    }

    void InspectorPanel::DrawComponent(EditorContext& context, Core::Component& component)
    {
        const ComponentTypeInfo* info = ComponentRegistry::Find(component.TypeID());
        const auto* missing = dynamic_cast<const Core::MissingComponent*>(&component);

        const std::string title = missing != nullptr
            ? missing->DescribeMissingType()
            : (info != nullptr
                ? info->DisplayName()
                : std::string("(未登録) ") + component.TypeName());

        const bool editable = context.CanEdit();
        const bool removable = ComponentRegistry::IsRemovable(component.TypeID());

        // 有効チェックボックスをヘッダーの左へ置く。
        bool component_enabled = component.Enabled();
        if (ImGui::Checkbox("##ComponentEnabled", &component_enabled) && editable)
        {
            context.BeginEdit(title + " の有効状態を変更");
            component.SetEnabled(component_enabled);
            context.CommitEdit();
        }
        ImGui::SameLine();

        const bool opened = ImGui::CollapsingHeader(title.c_str(),
            ImGuiTreeNodeFlags_DefaultOpen);

        if (info != nullptr && !info->tooltip.empty() && ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(info->tooltip.c_str());
            ImGui::EndTooltip();
        }

        if (!opened) return;

        ImGui::Indent();

        if (missing != nullptr)
        {
            // 預かっている内容を出すだけ。PropertyRegistry の対象外なので、
            // 下の通常の描画経路には入れない。
            DrawMissingComponentDetails(*missing);

            // 削除の導線は通常の Component と同じものを使う。
            // 「明示的に消すまで残る」ことと「消せる」ことを両立させる。
            ImGui::Spacing();
            if (!editable)
            {
                ImGui::TextDisabled("実行中は削除できません");
            }
            else if (ImGui::Button("この Missing Component を削除"))
            {
                pending_removal_ = &component;
                pending_removal_label_ = title;
            }
            ImGui::TextDisabled(
                u8"削除すると、預かっている内容も一緒に失われます。");

            ImGui::Unindent();
            return;
        }

        // Script Component は状態と失敗理由をここへ出す。
        //
        // これが無いと「Play しても動かない」ときに手掛かりが一切無く、
        // インスタンスが作られていないのか、コンパイルに失敗しているのか、
        // Play セッションが始まっていないのかを切り分けられない。
        // エンジンは status_ / last_error_ を持っているのに
        // 画面へ出していなかった。
        if (auto* script = dynamic_cast<Scripting::ScriptComponent*>(&component))
        {
            const Scripting::ScriptStatus status = script->Status();

            ImVec4 status_color(0.72f, 0.72f, 0.72f, 1.0f);
            const char* hint = "";
            switch (status)
            {
            case Scripting::ScriptStatus::Running:
                status_color = ImVec4(0.45f, 0.88f, 0.60f, 1.0f);
                hint = "インスタンス生成済み。Update が回っています";
                break;
            case Scripting::ScriptStatus::Loaded:
                status_color = ImVec4(0.55f, 0.78f, 0.98f, 1.0f);
                hint = "型は解決済み。Play を押すとインスタンスが作られます";
                break;
            case Scripting::ScriptStatus::Unresolved:
                status_color = ImVec4(1.0f, 0.78f, 0.35f, 1.0f);
                hint = "型が未解決。Build && Reload C# と Refresh C# Catalog を試してください";
                break;
            case Scripting::ScriptStatus::Unassigned:
                status_color = ImVec4(1.0f, 0.78f, 0.35f, 1.0f);
                hint = "Script が未指定です";
                break;
            case Scripting::ScriptStatus::Error:
                status_color = ImVec4(1.0f, 0.45f, 0.35f, 1.0f);
                hint = "下の理由を確認してください";
                break;
            default:
                break;
            }

            ImGui::TextDisabled("状態");
            ImGui::SameLine();
            ImGui::TextColored(status_color, "%s", Scripting::ToString(status));
            if (hint[0] != '\0')
            {
                ImGui::TextDisabled("  %s", hint);
            }
            ImGui::TextDisabled("  インスタンス: %s",
                script->HasInstance() ? "あり" : "なし");

            if (!script->LastError().empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "  理由");
                ImGui::TextWrapped("  %s", script->LastError().c_str());
            }
            ImGui::Separator();
        }

        // Component の必須/推奨関係と、よくある構成ミスは Inspector 上で即時に見せる。
        if (info != nullptr && component.Owner() != nullptr)
        {
            GameObject& owner = *component.Owner();
            auto draw_relationships = [&](const std::vector<Core::ComponentTypeID>& ids,
                                          const char* label, const ImVec4& color)
            {
                for (Core::ComponentTypeID dependency_id : ids)
                {
                    if (owner.FindComponent(dependency_id) != nullptr) continue;
                    const std::string dependency_name = ComponentRegistry::DisplayNameOf(dependency_id);
                    ImGui::TextColored(color, "%s: %s がありません", label, dependency_name.c_str());
                    if (editable)
                    {
                        ImGui::SameLine();
                        ImGui::PushID(static_cast<int>(dependency_id));
                        if (ImGui::SmallButton("追加"))
                        {
                            context.BeginEdit(dependency_name + " を追加");
                            if (owner.AddComponent(dependency_id) != nullptr)
                            {
                                context.CommitEdit();
                                context.SetStatus(dependency_name + " を追加しました");
                            }
                            else context.CancelEdit();
                        }
                        ImGui::PopID();
                    }
                }
            };
            draw_relationships(info->required_components, "必須", ImVec4(1.0f, 0.38f, 0.32f, 1.0f));
            draw_relationships(info->recommended_components, "推奨", ImVec4(1.0f, 0.72f, 0.30f, 1.0f));
        }

        if (auto* landscape = dynamic_cast<Components::LandscapeComponent*>(&component))
        {
            GameObject* owner = landscape->Owner();
            if (!landscape->LastDeserializeError().empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Landscape データ読み込みエラー");
                ImGui::TextWrapped("%s", landscape->LastDeserializeError().c_str());
            }
            if (owner != nullptr && owner->FindComponent(
                Components::PrimitiveMeshRendererComponent::StaticTypeID()) != nullptr)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.3f, 1.0f),
                    "注意: Primitive Mesh Renderer と Landscape が同じ GameObject にあります。");
                ImGui::TextWrapped("両方を表示すると重なります。Landscape を使う場合は Primitive 側を無効化してください。");
            }
            ImGui::Separator();
            ImGui::TextDisabled("新規平面: %d x %d / セル %.2f",
                landscape->default_resolution, landscape->default_resolution, landscape->default_cell_size);
            if (!editable) ImGui::TextDisabled("実行中は再生成できません");
            else if (ImGui::Button("設定値で平面を再生成")) ImGui::OpenPopup("ConfirmRegenerateLandscape");
            if (ImGui::BeginPopupModal("ConfirmRegenerateLandscape", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("現在の Landscape geometry を新しい平面で置き換えます。Undo できます。");
                if (ImGui::Button("再生成"))
                {
                    context.BeginEdit("Landscape を再生成");
                    if (landscape->GenerateFlat(landscape->default_resolution, landscape->default_resolution,
                        landscape->default_cell_size, 0.0f))
                    {
                        landscape->OnPropertyChanged(nullptr);
                        context.CommitEdit();
                        context.SetStatus("Landscape を中央原点の平面として再生成しました");
                    }
                    else
                    {
                        context.CancelEdit();
                        context.SetStatus("Landscape の再生成に失敗しました");
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("キャンセル")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }

        if (dynamic_cast<Components::DirectionalLightComponent*>(&component) != nullptr)
        {
            Scene::Scene* scene = context.GetScene();
            int active_directional_lights = 0;
            if (scene != nullptr)
            {
                for (std::size_t i = 0; i < scene->GameObjectCount(); ++i)
                {
                    GameObject* candidate = scene->GameObjectAt(i);
                    if (candidate == nullptr || !candidate->ActiveInHierarchy()) continue;
                    auto* light = candidate->GetComponent<Components::DirectionalLightComponent>();
                    if (light != nullptr && light->Enabled()) ++active_directional_lights;
                }
            }
            if (active_directional_lights > 1)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                    "Directional Light が %d 個有効です。Runtime は先頭の 1 個だけを使用します。",
                    active_directional_lights);
            }
        }

        // 静的な登録と、インスタンスごとの申告のどちらかがあれば編集欄を出す。
        // 型ごとの分岐は書かない。Script は後者だけを持つ。
        const bool has_dynamic = component.DynamicProperties() != nullptr &&
            !component.DynamicProperties()->empty();

        if (Reflection::PropertyRegistry::HasProperties(component.TypeID()) || has_dynamic)
        {
            const bool had_transaction = context.History().InTransaction();
            if (editable && !had_transaction) context.BeginEdit(title + " の設定を変更");
            if (!editable)
            {
                ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
            }

            const bool changed = PropertyDrawer::DrawAll(component,
                context.GetAssetDatabase(), context.GetScene());

            if (!editable)
            {
                ImGui::PopStyleVar();
                ImGui::PopItemFlag();
            }
            if (changed)
            {
                context.MarkDirty();
                prefab_cache_valid_ = false;
            }

            // Draw の前に Snapshot を取るため、最初の変更も Undo へ正しく入る。
            // Drag 中は transaction を保持し、マウスを離した Frame で 1 操作として確定する。
            if (editable && context.History().InTransaction() && !ImGui::IsAnyItemActive())
            {
                if (changed || had_transaction)
                {
                    // 編集が確定したこの瞬間に、型へ変更を知らせる。
                    //
                    // ここへ置く理由:
                    //   DrawAll が回している最中に呼ぶと、DynamicProperties() が
                    //   返した配列を型が作り直した場合に、走査中のコンテナが
                    //   入れ替わる。ここは「どの入力欄も掴まれていないフレーム」なので、
                    //   走査はすべて終わっている。
                    //
                    //   Drag 中は毎フレーム呼ばれない。掴んでいる間は
                    //   IsAnyItemActive() が true のままで、この分岐へ来ない。
                    //
                    //   Script では、これによって Script Asset を選び替えた直後に
                    //   Field の顔ぶれが入れ替わる。
                    component.OnPropertyChanged(nullptr);
                    context.CommitEdit();
                }
                else
                {
                    context.CancelEdit();
                }
            }
        }
        else
        {
            ImGui::TextDisabled("編集できる設定はありません");
        }

        ImGui::Spacing();
        if (!removable)
        {
            ImGui::TextDisabled("このコンポーネントは削除できません");
        }
        else if (!editable)
        {
            ImGui::TextDisabled("実行中は削除できません");
        }
        else if (ImGui::Button("コンポーネントを削除"))
        {
            // ここでは予約だけ控える。実際の削除は Component 一覧の走査が終わってから。
            pending_removal_ = &component;
            pending_removal_label_ = title;
        }

        ImGui::Unindent();
    }
}
