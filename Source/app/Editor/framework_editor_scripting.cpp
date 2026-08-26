// Editor のうち「Editor ログと C# Catalog / Build / Reload」だけを持つ。
#include "framework.h"

#include "../../RePlayEngine/Components/Core/PivotComponent.h"
#include "../../RePlayEngine/Components/Gameplay/CharacterMotorComponent.h"
#include "../../RePlayEngine/Components/Gameplay/PlayerControllerComponent.h"
#include "../../RePlayEngine/Components/Gameplay/PlayerInputComponent.h"
#include "../../RePlayEngine/Editor/Style/EditorStyle.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpProject.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpScriptBackend.h"
#include "../../RePlayEngine/Scripting/Core/ScriptComponent.h"
#include "../../RePlayEngine/Scripting/Core/ScriptRuntime.h"
#include "../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../RePlayEngine/Scene/Serialization/SceneSerializer.h"
#include "skinned_mesh.h"

#include <filesystem>
#include <fstream>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
void framework::push_editor_log(std::string severity, std::string message,
    std::filesystem::path file, int line, int column)
{
    // 画面のログはコピーしないと外へ出せない。
    // 同じ内容をファイルへも落としておく。
    //
    // 起動ごとに切り詰める（append ではなく trunc を初回だけ）。
    // 追記し続けると前回の実行と混ざり、どれが今回のものか分からなくなる。
    {
        static bool truncated = false;
        std::error_code folder_error;
        const std::filesystem::path folder = saved_path("Diagnostics");
        std::filesystem::create_directories(folder, folder_error);

        std::ofstream sink(folder / "editor_log.txt",
            std::ios::binary | (truncated ? std::ios::app : std::ios::trunc));
        truncated = true;
        if (sink)
        {
            sink << '[' << severity << "] " << message;
            if (!file.empty())
            {
                sink << "  (" << file.generic_u8string();
                if (line > 0) sink << ':' << line;
                if (column > 0) sink << ':' << column;
                sink << ')';
            }
            sink << '\n';
        }
    }

    editor_log_entry entry;
    entry.severity = std::move(severity);
    entry.message = std::move(message);
    entry.file = std::move(file);
    entry.line = line;
    entry.column = column;
    editor_log_entries.push_back(std::move(entry));
    if (editor_log_entries.size() > 500)
    {
        editor_log_entries.erase(editor_log_entries.begin());
        if (selected_editor_log_index > 0) --selected_editor_log_index;
    }
}
void framework::snapshot_csharp_script_write_times()
{
    namespace CSharp = ReplayEngine::Scripting::CSharp;

    csharp_source_write_times.clear();
    for (const CSharp::CSharpBehaviourInfo& info :
        CSharp::CSharpProject::DiscoverBehaviours(content_root_path()))
    {
        std::error_code error;
        const std::filesystem::file_time_type time =
            std::filesystem::last_write_time(info.source_path, error);
        if (error) continue;
        csharp_source_write_times[info.source_path.generic_u8string()] = time;
    }
    csharp_scripts_dirty = false;
}

void framework::poll_csharp_script_changes(float elapsed_time)
{
    namespace CSharp = ReplayEngine::Scripting::CSharp;

    if (standalone_game_mode) return;

    csharp_scan_accumulator += elapsed_time;
    if (csharp_scan_accumulator < 1.0f) return;
    csharp_scan_accumulator = 0.0f;

    // この回で新しく変化を見つけたか。
    // 見つけた直後は再ビルドしない。Visual Studio は複数ファイルを
    // 続けて保存するので、検出のたびに走らせるとビルドが重なる。
    // 「変化が落ち着いた次の回」で 1 度だけ走らせる。
    bool changed_this_scan = false;

    for (const CSharp::CSharpBehaviourInfo& info :
        CSharp::CSharpProject::DiscoverBehaviours(content_root_path()))
    {
        std::error_code error;
        const std::filesystem::file_time_type time =
            std::filesystem::last_write_time(info.source_path, error);
        if (error) continue;

        const std::string key = info.source_path.generic_u8string();
        const auto found = csharp_source_write_times.find(key);
        if (found == csharp_source_write_times.end())
        {
            csharp_source_write_times[key] = time;
            csharp_scripts_dirty = true;
            changed_this_scan = true;
            push_editor_log("Info", "C# source detected: " + key, info.source_path);
            continue;
        }

        if (found->second != time)
        {
            found->second = time;
            csharp_scripts_dirty = true;
            changed_this_scan = true;
            push_editor_log("Info", "C# source changed: " + key, info.source_path);
        }
    }

    // 保存を検出したら自動で再コンパイルする。
    //
    // 手で「Build && Reload C#」を押す運用だと、押し忘れたまま
    // 「直したのに動かない」と悩む時間が生まれる。実際にそれで詰まった。
    //
    // コンパイルに失敗しても直前に成功した Assembly が維持されるので、
    // 自動で走らせても編集中のシーンは壊れない。
    if (csharp_scripts_dirty && csharp_auto_reload && !changed_this_scan)
    {
        csharp_scripts_dirty = false;
        push_editor_log("Info", "C# の変更を検出したので自動で再コンパイルします");
        build_and_reload_csharp_scripts();
    }
}

// C# を一括で作り直す。
// Catalog の更新と Assembly の再コンパイルを 1 回でやる。
bool framework::rebuild_all_csharp()
{
    push_editor_log("Info", "===== C# 一括更新 開始 =====");

    // 先に Catalog。新しく増えた .cs をここで拾う。
    const bool catalog_ok = refresh_csharp_scripts();

    // 次に Assembly。Catalog に載った型が実際に生成できる状態になる。
    const bool build_ok = build_and_reload_csharp_scripts();

    // 変更検出の基準を今の状態へ揃える。
    // これをしないと、直後の巡回でもう一度自動再コンパイルが走る。
    snapshot_csharp_script_write_times();
    csharp_scripts_dirty = false;

    push_editor_log(catalog_ok && build_ok ? "Info" : "Error",
        std::string("===== C# 一括更新 終了 (Catalog=") +
        (catalog_ok ? "OK" : "NG") + " / Build=" + (build_ok ? "OK" : "NG") + ") =====");

    return catalog_ok && build_ok;
}

bool framework::refresh_csharp_scripts()
{
    namespace CSharp = ReplayEngine::Scripting::CSharp;
    namespace Scripting = ReplayEngine::Scripting;

    initialize_runtime_services();
    if (!object_script_runtime)
    {
        push_editor_log("Error", "ScriptRuntime is not initialized.");
        return false;
    }

    std::string error;
    if (!CSharp::CSharpProject::RefreshCatalog(content_root_path(),
        asset_database, object_script_runtime->Catalog(), error))
    {
        editor_command_result = "C# Catalog 更新失敗: " + error;
        push_editor_log("Error", editor_command_result);
        return false;
    }

    for (const Scripting::ScriptTypeDescriptor& descriptor :
        object_script_runtime->Catalog().All())
    {
        if (descriptor.language != Scripting::ScriptLanguage::CSharp) continue;
        object_script_runtime->RequestSchemaReload(descriptor.type_id);
    }

    // Schema の差し替えをここで 1 回通す。
    // ApplyPendingSchemaSwaps は Play セッションに登録済みの Component へしか
    // 配らない（内部で world_ が無ければ抜ける）ので、編集 Scene の
    // Component は下の resolve_editor_script_schemas で自分で引き直す。
    object_script_runtime->ApplyPendingSchemaSwaps(0.0f);

    // 編集 Scene の ScriptComponent を再解決する。
    //
    // これが無いと、Catalog を更新しても編集 Scene の Component は
    // Unresolved のまま残る。Scene を読み込んだ時点では Catalog がまだ
    // 空なので、起動直後は必ずこの状態になっていた。
    const std::size_t resolved = resolve_editor_script_schemas();

    snapshot_csharp_script_write_times();
    editor_command_result = "C# Catalog を更新しました（編集 Scene の Script " +
        std::to_string(resolved) + " 件を再解決）";
    push_editor_log("Info", editor_command_result);
    return true;
}

// 編集 Scene の ScriptComponent へ Schema を引き直させる。
// 戻り値は Schema を持てた Component の数。
std::size_t framework::resolve_editor_script_schemas()
{
    std::size_t resolved = 0;

    // 再帰で階層を降りる。ラムダ再帰を使わず素直に書く。
    struct Walker
    {
        static void Visit(ReplayEngine::Core::GameObject& object, std::size_t& count)
        {
            for (std::size_t index = 0; index < object.ComponentCount(); ++index)
            {
                ReplayEngine::Core::Component* component = object.ComponentAt(index);
                if (component == nullptr) continue;
                auto* script =
                    dynamic_cast<ReplayEngine::Scripting::ScriptComponent*>(component);
                if (script == nullptr) continue;
                if (script->ResolveSchema()) ++count;
            }
            for (ReplayEngine::Core::GameObject* child : object.Children())
            {
                if (child != nullptr) Visit(*child, count);
            }
        }
    };

    for (ReplayEngine::Core::GameObject* root : object_scene.RootGameObjects())
    {
        if (root != nullptr) Walker::Visit(*root, resolved);
    }
    return resolved;
}

bool framework::build_and_reload_csharp_scripts()
{
    namespace CSharp = ReplayEngine::Scripting::CSharp;
    namespace Scripting = ReplayEngine::Scripting;

    initialize_runtime_services();
    if (!object_script_runtime)
    {
        push_editor_log("Error", "ScriptRuntime is not initialized.");
        return false;
    }

    auto* backend = dynamic_cast<CSharp::CSharpScriptBackend*>(
        object_script_runtime->Backend(Scripting::ScriptLanguage::CSharp));
    if (backend == nullptr)
    {
        editor_command_result = "C# Backend が接続されていません";
        push_editor_log("Error", editor_command_result);
        return false;
    }

    CSharp::CSharpBuildResult build;
    const bool reloaded = backend->CompileAndReload(&build);
    for (const CSharp::CSharpDiagnostic& diagnostic : build.diagnostics)
    {
        std::string severity = "Info";
        if (diagnostic.severity == CSharp::CSharpDiagnostic::Severity::Warning)
            severity = "Warning";
        else if (diagnostic.severity == CSharp::CSharpDiagnostic::Severity::Error)
            severity = "Error";

        push_editor_log(severity,
            diagnostic.code + ": " + diagnostic.message,
            diagnostic.file, diagnostic.line, diagnostic.column);
    }

    if (!reloaded)
    {
        editor_command_result =
            "C# Compile/Reload 失敗。直前に成功した Assembly は維持しています。";
        if (build.diagnostics.empty() && !build.output_text.empty())
        {
            push_editor_log("Error", build.output_text);
        }
        return false;
    }

    refresh_csharp_scripts();
    editor_command_result = "C# Compile/Reload 成功: " +
        build.output_assembly.generic_u8string();
    push_editor_log("Info", editor_command_result, build.output_assembly);
    csharp_scripts_dirty = false;
    return true;
}

bool framework::create_csharp_behaviour_asset()
{
    namespace CSharp = ReplayEngine::Scripting::CSharp;

    CSharp::CSharpBehaviourInfo info;
    std::string error;
    if (!CSharp::CSharpProject::CreateBehaviour(content_root_path(),
        new_csharp_behaviour_name, new_csharp_namespace, info, error))
    {
        editor_command_result = "C# Behaviour 作成失敗: " + error;
        push_editor_log("Error", editor_command_result);
        return false;
    }

    const ReplayEngine::Assets::AssetRecord& record =
        asset_database.Register(info.source_path, ReplayEngine::Assets::AssetKind::Script);
    selected_asset_guid = record.guid;
    if (!asset_database.Save(error))
    {
        push_editor_log("Warning", "C# script asset registration could not be saved: " + error,
            info.source_path);
    }

    refresh_csharp_scripts();

    std::string open_error;
    if (!CSharp::CSharpProject::OpenVisualStudio(info.source_path, 1, open_error))
    {
        push_editor_log("Warning", open_error, info.source_path);
    }

    editor_command_result = "C# Behaviour を作成しました: " +
        info.source_path.generic_u8string();
    push_editor_log("Info", editor_command_result, info.source_path, 1);
    return true;
}

bool framework::open_selected_csharp_asset(int line)
{
    namespace CSharp = ReplayEngine::Scripting::CSharp;

    const ReplayEngine::Assets::AssetRecord* selected_asset =
        selected_asset_guid.empty() ? nullptr : asset_database.FindByGuid(selected_asset_guid);
    if (selected_asset == nullptr ||
        selected_asset->kind != ReplayEngine::Assets::AssetKind::Script ||
        selected_asset->source_path.extension() != ".cs")
    {
        editor_command_result = "C# script asset が選択されていません";
        push_editor_log("Warning", editor_command_result);
        return false;
    }

    std::string error;
    if (!CSharp::CSharpProject::OpenVisualStudio(selected_asset->source_path, line, error))
    {
        editor_command_result = error;
        push_editor_log("Error", error, selected_asset->source_path, line);
        return false;
    }

    editor_command_result = "Visual Studio で開きました: " +
        selected_asset->source_path.generic_u8string();
    push_editor_log("Info", editor_command_result, selected_asset->source_path, line);
    return true;
}
