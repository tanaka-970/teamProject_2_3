#include "SceneFlowValidation.h"

#include "../API/RuntimeContext.h"
#include "../Core/RuntimeResult.h"
#include "../Scene/RuntimeSceneService.h"
#include "../Scene/SceneFlowService.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/BuiltInComponents.h"
#include "../../Project/ProjectSettings.h"
#include "../../Project/ProjectSettingsSerializer.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Serialization/SceneData.h"
#include "../../Scene/Serialization/SceneSerializer.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ReplayEngine::Runtime::Validation
{
    namespace
    {
        namespace Serialization = Scene::Serialization;

        using Core::ObjectID;

        // 定数は名前空間スコープへ置く（関数ローカルの constexpr を
        // キャプチャ無しラムダから参照すると MSVC が C3493 で落ちるため）。
        constexpr const char* guid_title = "f10w0000000000000000000000000001";
        constexpr const char* guid_game = "f10w0000000000000000000000000002";
        constexpr const char* guid_result = "f10w0000000000000000000000000003";
        constexpr const char* guid_corrupt = "f10w0000000000000000000000000004";
        constexpr const char* guid_unknown = "f10w000000000000000000000000ffff";

        constexpr const char* prefab_guid_sample = "9e3f0000000000000000000000000001";

        // 履歴上限を確実に超える回数。上限 16 に対して余裕を持たせる。
        constexpr int history_overflow_transitions = 24;

        class Checker final
        {
        public:
            explicit Checker(int first_code) : next_code_(first_code) {}

            void Expect(bool condition, const char* what)
            {
                const int code = next_code_++;
                ++total_;
                if (condition) return;
                ++failures_;
                if (first_failure_ == 0) first_failure_ = code;
                std::fprintf(stderr, "  [FAIL %d] %s\n", code, what);
            }

            int Report(const char* title) const
            {
                if (first_failure_ == 0)
                {
                    std::fprintf(stderr, "%s OK: %d checks passed\n", title, total_);
                    return 0;
                }
                std::fprintf(stderr, "%s FAILED: %d/%d checks failed (first=%d)\n",
                    title, failures_, total_, first_failure_);
                return first_failure_;
            }

        private:
            int next_code_ = 0;
            int first_failure_ = 0;
            int total_ = 0;
            int failures_ = 0;
        };

        // GUID からパスを引くだけのテスト実装。
        // AssetDatabase を使わないので、この検証は Asset 登録の状態に依存しない。
        class TestSceneAssetResolver final : public ISceneAssetResolver
        {
        public:
            void Map(std::string guid, const std::filesystem::path& path)
            {
                Entry entry;
                entry.guid = std::move(guid);
                entry.path = path.string();
                entries_.push_back(std::move(entry));
            }

            RuntimeStatus ResolveScenePath(const std::string& asset_guid,
                std::string& out_path) const override
            {
                for (const Entry& entry : entries_)
                {
                    if (entry.guid != asset_guid) continue;
                    out_path = entry.path;
                    return RuntimeStatus::Ok;
                }
                return RuntimeStatus::AssetMissing;
            }

        private:
            struct Entry
            {
                std::string guid;
                std::string path;
            };
            std::vector<Entry> entries_;
        };

        // 1 つだけ GameObject を持つ最小の Scene。
        // 遷移そのものを確かめるのが目的なので、中身は名前で見分けられれば足りる。
        Serialization::SceneData BuildNamedScene(const char* scene_name,
            const char* object_name)
        {
            Serialization::SceneData data;
            data.scene_name = scene_name;

            Serialization::GameObjectData object;
            object.id = ObjectID{ 1 };
            object.name = object_name;
            data.objects.push_back(std::move(object));
            return data;
        }

        bool WriteRawFile(const std::filesystem::path& path, const char* text)
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream) return false;
            stream << text;
            return static_cast<bool>(stream);
        }

        // ProjectSettings をテキストへ書いて読み戻す。ファイルは触らない。
        bool SettingsRoundTrip(const Project::ProjectSettings& source,
            Project::ProjectSettings& restored, std::string& text, std::string& error)
        {
            std::ostringstream out;
            if (!Project::ProjectSettingsSerializer::WriteText(source, out, error))
            {
                return false;
            }
            text = out.str();

            std::istringstream in(text);
            return Project::ProjectSettingsSerializer::ReadText(restored, in, error);
        }

        bool ReadSettingsFromText(Project::ProjectSettings& settings,
            const std::string& text, std::string& error)
        {
            std::istringstream in(text);
            return Project::ProjectSettingsSerializer::ReadText(settings, in, error);
        }

        // 現在の Scene 名を返す。読み込み結果の判別に使う。
        std::string CurrentSceneName(const RuntimeSceneService& scenes)
        {
            return scenes.ActiveWorld().Name();
        }
    }

    // =====================================================================
    // Scene Flow / Startup Scene
    // =====================================================================

    int RunSceneFlowValidation()
    {
        Core::RegisterBuiltInComponents();
        Checker check(460);

        // -----------------------------------------------------------------
        // ProjectSettings v2 と v1 からの移行
        // -----------------------------------------------------------------

        std::string error;
        std::string text;

        Project::ProjectSettings saved;
        saved.SetStartupSceneGuid(guid_title);
        saved.SetDefaultCharacterPrefabGuid(prefab_guid_sample);

        Project::ProjectSettings restored;
        const bool round_tripped = SettingsRoundTrip(saved, restored, text, error);
        check.Expect(round_tripped && restored.StartupSceneGuid() == guid_title,
            "Startup Scene が AssetGUID として往復する");
        check.Expect(round_tripped &&
            restored.DefaultCharacterPrefabGuid() == prefab_guid_sample,
            "Startup Scene と Default Character Prefab が独立して保存される");
        check.Expect(text.find("REPLAY_PROJECT 2") == 0,
            "保存されるプロジェクト設定のバージョンが 2 になる");

        Project::ProjectSettings empty_startup;
        empty_startup.SetDefaultCharacterPrefabGuid(prefab_guid_sample);
        Project::ProjectSettings empty_restored;
        const bool empty_ok =
            SettingsRoundTrip(empty_startup, empty_restored, text, error);
        check.Expect(empty_ok && !empty_restored.HasStartupScene() &&
            empty_restored.DefaultCharacterPrefabGuid() == prefab_guid_sample,
            "空の Startup Scene も設定値として往復する");

        // v1 のファイル。STARTUP_SCENE 行がまだ無い。
        const std::string v1_text =
            "REPLAY_PROJECT 1\n"
            "DEFAULT_CONTROLLED_CHARACTER_PREFAB \"" +
            std::string(prefab_guid_sample) + "\"\n";

        Project::ProjectSettings migrated;
        const bool v1_read = ReadSettingsFromText(migrated, v1_text, error);
        check.Expect(v1_read &&
            migrated.DefaultCharacterPrefabGuid() == prefab_guid_sample,
            "v1 のプロジェクト設定を読み込める");
        check.Expect(v1_read && !migrated.HasStartupScene(),
            "v1 から移行しても Startup Scene は未設定のまま（値を推測しない）");

        std::ostringstream migrated_out;
        const bool migrated_written =
            Project::ProjectSettingsSerializer::WriteText(migrated, migrated_out, error);
        check.Expect(migrated_written &&
            migrated_out.str().find("REPLAY_PROJECT 2") == 0 &&
            migrated_out.str().find("STARTUP_SCENE") != std::string::npos,
            "v1 を読み込んで保存すると v2 形式になる");

        Project::ProjectSettings poisoned;
        poisoned.SetStartupSceneGuid(guid_title);
        const bool future_rejected = !ReadSettingsFromText(poisoned,
            "REPLAY_PROJECT 99\nSTARTUP_SCENE \"zzz\"\n", error);
        check.Expect(future_rejected && !poisoned.HasStartupScene() &&
            !poisoned.HasDefaultCharacterPrefab(),
            "未対応バージョンの設定は拒否され、安全な既定値へ戻る");

        Project::ProjectSettings broken;
        broken.SetStartupSceneGuid(guid_title);
        const bool broken_rejected =
            !ReadSettingsFromText(broken, "NOT_A_PROJECT_FILE\n", error);
        check.Expect(broken_rejected && !broken.HasStartupScene(),
            "プロジェクト設定でないファイルは拒否され、安全な既定値へ戻る");

        // -----------------------------------------------------------------
        // 一時 Scene の書き出し
        // -----------------------------------------------------------------

        const std::filesystem::path folder =
            std::filesystem::path("Saved") / "Validation" / "SceneFlow";
        std::error_code directory_error;
        std::filesystem::create_directories(folder, directory_error);

        const std::filesystem::path path_title = folder / "Title.replayscene";
        const std::filesystem::path path_game = folder / "Game.replayscene";
        const std::filesystem::path path_result = folder / "Result.replayscene";
        const std::filesystem::path path_corrupt = folder / "Corrupt.replayscene";

        const bool scenes_written = !directory_error &&
            Serialization::SceneSerializer::SaveToFile(
                BuildNamedScene("FlowTitle", "TitleRoot"), path_title, error) &&
            Serialization::SceneSerializer::SaveToFile(
                BuildNamedScene("FlowGame", "GameRoot"), path_game, error) &&
            Serialization::SceneSerializer::SaveToFile(
                BuildNamedScene("FlowResult", "ResultRoot"), path_result, error) &&
            WriteRawFile(path_corrupt, "NOT_A_SCENE 1\n");
        check.Expect(scenes_written, "検証用の一時 Scene を書き出せる");
        if (!scenes_written)
        {
            std::fprintf(stderr, "  Scene 準備に失敗: %s\n", error.c_str());
            return check.Report("Scene flow validation");
        }

        // -----------------------------------------------------------------
        // SceneFlowService 未接続
        // -----------------------------------------------------------------

        {
            Scene::Scene bare_world("Bare");
            RuntimeContext bare_runtime(bare_world);
            check.Expect(!bare_runtime.SceneFlowAvailable() &&
                bare_runtime.LoadScene(guid_title) == RuntimeStatus::ServiceUnavailable,
                "Scene Flow 未接続の LoadScene は ServiceUnavailable を返す");
            check.Expect(
                bare_runtime.ReloadCurrentScene() == RuntimeStatus::ServiceUnavailable &&
                bare_runtime.ReturnToPreviousScene() == RuntimeStatus::ServiceUnavailable &&
                bare_runtime.QuitApplication() == RuntimeStatus::ServiceUnavailable,
                "Scene Flow 未接続の Reload / Return / Quit も ServiceUnavailable");
            check.Expect(!bare_runtime.SceneTransitionInProgress() &&
                bare_runtime.CurrentSceneGuid().empty(),
                "Scene Flow 未接続でも状態問い合わせが安全に答える");
        }

        // -----------------------------------------------------------------
        // サービスの組み立て
        // -----------------------------------------------------------------

        TestSceneAssetResolver resolver;
        resolver.Map(guid_title, path_title);
        resolver.Map(guid_game, path_game);
        resolver.Map(guid_result, path_result);
        resolver.Map(guid_corrupt, path_corrupt);

        RuntimeSceneService scenes;
        RuntimeContext runtime(scenes.ActiveWorld());
        scenes.ActiveWorld().Services().SetRuntime(&runtime);
        scenes.SetRuntimeContext(&runtime);
        scenes.SetAssetResolver(&resolver);

        SceneFlowService flow(scenes);
        runtime.SetSceneFlow(&flow);

        // -----------------------------------------------------------------
        // Startup Scene
        // -----------------------------------------------------------------

        check.Expect(flow.BeginStartupScene(std::string()) == RuntimeStatus::SceneMissing &&
            flow.StartupState() == StartupSceneState::NotConfigured,
            "Startup Scene 未設定は NotConfigured という診断状態になる");

        flow.Tick();
        check.Expect(scenes.ActiveWorld().GameObjectCount() == 0 &&
            scenes.CurrentSceneGUID().empty() && flow.StartupBlocked(),
            "Startup Scene 未設定のとき、別の Scene を勝手に読み込まない");

        check.Expect(Succeeded(flow.BeginStartupScene(guid_unknown)),
            "存在しない Startup Scene でも要求そのものは受理される");
        flow.Tick();
        check.Expect(flow.StartupState() == StartupSceneState::Failed &&
            flow.LastResult() == RuntimeStatus::AssetMissing && flow.StartupBlocked(),
            "無効な Startup Scene は Failed という診断状態になる");
        check.Expect(scenes.ActiveWorld().GameObjectCount() == 0,
            "Startup Scene の読み込みに失敗しても代わりの Scene へ落ちない");

        check.Expect(Succeeded(flow.BeginStartupScene(guid_title)),
            "正常な Startup Scene の起動要求が受理される");
        flow.Tick();
        flow.Tick();
        check.Expect(flow.StartupState() == StartupSceneState::Ready &&
            !flow.StartupBlocked() && CurrentSceneName(scenes) == "FlowTitle",
            "Startup Scene の読み込みが完了し Ready になる");
        check.Expect(flow.History().empty() && !flow.CanReturn(),
            "起動 Scene は履歴の起点であり、戻り先を作らない");

        // -----------------------------------------------------------------
        // Load / 履歴
        // -----------------------------------------------------------------

        Reflection::SceneReference game_reference{ std::string(guid_game) };
        check.Expect(Succeeded(flow.LoadScene(game_reference)) &&
            flow.CurrentTransitionState() == SceneTransitionState::Requested,
            "SceneReference からの遷移要求が受理される");

        flow.Tick();
        flow.Tick();
        check.Expect(flow.CurrentTransitionState() == SceneTransitionState::Completed &&
            flow.CurrentSceneGUID() == guid_game && CurrentSceneName(scenes) == "FlowGame",
            "遷移が完了し現在の Scene が切り替わる");
        check.Expect(flow.History().size() == 1 && flow.History().back() == guid_title,
            "成功した切替で直前の Scene が履歴へ積まれる");
        check.Expect(flow.CanReturn(), "履歴があれば戻れる");

        check.Expect(Succeeded(flow.LoadScene(std::string(guid_result))),
            "2 回目の遷移要求が受理される");
        flow.Tick();
        flow.Tick();
        check.Expect(flow.History().size() == 2 && flow.History().back() == guid_game &&
            flow.CurrentSceneGUID() == guid_result,
            "履歴が積み上がり、末尾が 1 つ前の Scene になる");

        const std::size_t history_before_reload = flow.History().size();
        check.Expect(Succeeded(flow.ReloadCurrentScene()), "再読み込み要求が受理される");
        flow.Tick();
        flow.Tick();
        check.Expect(flow.History().size() == history_before_reload &&
            flow.CurrentSceneGUID() == guid_result,
            "Reload は履歴を増やさず、現在の Scene も変えない");

        // -----------------------------------------------------------------
        // 失敗した Load
        // -----------------------------------------------------------------

        const std::vector<std::string> history_before_failure = flow.History();
        check.Expect(Succeeded(flow.LoadScene(std::string(guid_corrupt))),
            "壊れた Scene への遷移要求も受理はされる");
        flow.Tick();
        check.Expect(flow.CurrentTransitionState() == SceneTransitionState::Failed &&
            Failed(flow.LastResult()) && !flow.LastError().empty(),
            "壊れた Scene への遷移は Failed になり理由が残る");
        check.Expect(flow.CurrentSceneGUID() == guid_result &&
            CurrentSceneName(scenes) == "FlowResult",
            "遷移に失敗しても現在の Scene は維持される");
        check.Expect(flow.History() == history_before_failure && flow.CanReturn(),
            "遷移に失敗しても履歴は 1 つも動かない");

        // -----------------------------------------------------------------
        // Return
        // -----------------------------------------------------------------

        check.Expect(Succeeded(flow.ReturnToPreviousScene()), "戻る要求が受理される");
        flow.Tick();
        flow.Tick();
        check.Expect(flow.CurrentSceneGUID() == guid_game &&
            flow.History().size() == 1 && flow.History().back() == guid_title,
            "1 つ前の Scene へ戻り、履歴が 1 件減る");

        check.Expect(Succeeded(flow.ReturnToPreviousScene()), "続けて戻れる");
        flow.Tick();
        flow.Tick();
        check.Expect(flow.CurrentSceneGUID() == guid_title && flow.History().empty() &&
            !flow.CanReturn(),
            "履歴を使い切ると戻れなくなる（往復で履歴が増えない）");

        check.Expect(flow.ReturnToPreviousScene() == RuntimeStatus::SceneMissing,
            "戻り先が無い状態の Return は SceneMissing");

        // -----------------------------------------------------------------
        // 連続要求
        // -----------------------------------------------------------------

        check.Expect(Succeeded(flow.LoadScene(std::string(guid_game))) &&
            flow.LoadScene(std::string(guid_result)) ==
                RuntimeStatus::TransitionInProgress,
            "遷移中の追加要求は TransitionInProgress で拒否される");
        check.Expect(flow.TransitionInProgress() && !flow.CanReturn(),
            "遷移中は戻る操作も受け付けない");
        flow.Tick();
        flow.Tick();
        check.Expect(flow.CurrentSceneGUID() == guid_game,
            "連続要求のあとも最初に受理した遷移先になる");

        check.Expect(flow.LoadScene(std::string()) == RuntimeStatus::InvalidArgument,
            "空の遷移先は InvalidArgument で、履歴も現在の Scene も動かさない");

        // -----------------------------------------------------------------
        // 履歴の上限
        // -----------------------------------------------------------------

        bool overflow_ok = true;
        for (int index = 0; index < history_overflow_transitions; ++index)
        {
            const char* target = (index % 2 == 0) ? guid_result : guid_game;
            if (Failed(flow.LoadScene(std::string(target)))) { overflow_ok = false; break; }
            flow.Tick();
            flow.Tick();
            if (flow.History().size() > SceneFlowService::maximum_history)
            {
                overflow_ok = false;
                break;
            }
        }
        check.Expect(overflow_ok &&
            flow.History().size() == SceneFlowService::maximum_history,
            "履歴は上限で頭打ちになり、古いものから捨てられる");

        // 戻り先スタックとして正しい形かを確かめる。
        //
        // 「履歴のどこにも現在の Scene が現れない」ことは要求しない。
        // A -> B -> A -> B と往復すれば、深い位置に現在と同じ Scene が
        // 残るのが正しい戻り先の並びになる。
        // 禁じたいのは「1 つ前が自分自身」という即座のループと、空 GUID。
        bool history_clean = !flow.History().empty();
        for (const std::string& entry : flow.History())
        {
            if (entry.empty()) history_clean = false;
        }
        if (!flow.History().empty() &&
            flow.History().back() == flow.CurrentSceneGUID())
        {
            history_clean = false;
        }
        check.Expect(history_clean,
            "履歴に空 GUID が入らず、1 つ前が現在の Scene と同じにならない");

        // -----------------------------------------------------------------
        // Quit 要求
        // -----------------------------------------------------------------

        check.Expect(!flow.QuitRequested(), "何もしなければ終了要求は立っていない");
        check.Expect(Succeeded(flow.QuitApplication("validation")) &&
            flow.QuitRequested() && flow.QuitReason() == "validation" &&
            flow.QuitRequestCount() == 1,
            "終了要求が記録される（プロセスは終了しない）");
        flow.Tick();
        check.Expect(flow.QuitRequested(),
            "終了要求は受け取られるまで保持される");
        flow.ClearQuitRequest();
        check.Expect(!flow.QuitRequested() && flow.QuitReason().empty(),
            "アプリケーション層が受け取ったあと終了要求を降ろせる");

        return check.Report("Scene flow validation");
    }
}
