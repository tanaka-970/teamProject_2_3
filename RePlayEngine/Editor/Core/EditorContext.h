#pragma once

#include "../Commands/SceneEditHistory.h"
#include "../Selection/EditorSelection.h"

#include <filesystem>
#include <string>

namespace ReplayEngine::Scene { class Scene; }
namespace ReplayEngine::Assets { class AssetDatabase; }

namespace ReplayEngine::Editor
{
    // 各 Editor パネルが必要とするものを 1 か所へまとめた受け渡し用の束。
    //
    // 目的:
    //   パネルが framework 全体へのポインタを持たないようにすること。
    //   パネルは EditorContext だけを受け取り、そこから必要なものへ辿る。
    //   これにより Editor 側が描画・入力・当たり判定など無関係な部分へ触れなくなる。
    //
    // 依存方向:
    //   Editor -> Runtime (Scene / GameObject / Component / Registry) の一方向。
    //   Runtime 側からこのクラスを参照してはいけない。
    //
    // 所有関係:
    //   ここが持つのは選択状態と履歴と編集中フラグだけ。
    //   Scene と AssetDatabase は非所有参照で、実体は framework が持つ。
    class EditorContext final
    {
    public:
        EditorContext() = default;

        EditorContext(const EditorContext&) = delete;
        EditorContext& operator=(const EditorContext&) = delete;

        // ---- 接続 ----------------------------------------------------------

        // 編集対象の Scene を差し替える。選択と履歴は破棄される。
        void AttachScene(Scene::Scene* scene);
        // 同じ Scene インスタンスの内容を Load/New で総入れ替えしたときに呼ぶ。
        // 毎 Frame の AttachScene では消さない選択と Undo 履歴を明示的に破棄する。
        void ResetSceneState();
        Scene::Scene* GetScene() const noexcept { return scene_; }
        bool HasScene() const noexcept { return scene_ != nullptr; }

        void SetAssetDatabase(const Assets::AssetDatabase* database) noexcept
        {
            asset_database_ = database;
        }
        const Assets::AssetDatabase* GetAssetDatabase() const noexcept { return asset_database_; }

        EditorSelection& Selection() noexcept { return selection_; }
        const EditorSelection& Selection() const noexcept { return selection_; }

        SceneEditHistory& History() noexcept { return history_; }

        // ---- 未保存変更 ----------------------------------------------------

        bool Dirty() const noexcept { return dirty_; }
        void MarkDirty() noexcept { dirty_ = true; }
        void ClearDirty() noexcept { dirty_ = false; }

        const std::filesystem::path& ScenePath() const noexcept { return scene_path_; }
        void SetScenePath(std::filesystem::path path) { scene_path_ = std::move(path); }

        // タイトル表示用。未保存なら末尾へ * が付く。
        std::string DisplayTitle() const;

        const std::string& Status() const noexcept { return status_; }
        void SetStatus(std::string message) { status_ = std::move(message); }

        // ---- Play Mode -----------------------------------------------------
        //
        // Play 中は編集 Scene ではなく実行用 Scene を見ているため、
        // 構造の書き換えを禁止する。Play 中の変更が保存対象へ混ざらないようにするため。

        bool PlayMode() const noexcept { return play_mode_; }
        void SetPlayMode(bool value) noexcept { play_mode_ = value; }

        // 構造を編集してよいか。Scene が無い / Play 中は false。
        bool CanEdit() const noexcept { return scene_ != nullptr && !play_mode_; }

        // ---- 編集トランザクション ------------------------------------------
        //
        // パネルは直接 Scene を書き換える前にこれを呼ぶ。
        // ImGui の描画中に何度呼ばれても、最初の 1 回だけが有効になる。
        //
        //   if (ImGui::Button("削除")) {
        //       context.BeginEdit("Component を削除");
        //       object->RemoveComponent(component);   // 削除予約が立つだけ
        //       context.CommitEdit();
        //   }

        void BeginEdit(std::string label);
        void CommitEdit();
        void CommitLandscapeEdit(Core::ObjectID object,
            std::unique_ptr<Landscape::LandscapeUndoCommand> command);
        void CancelEdit() noexcept;

        bool Undo();
        bool Redo();

    private:
        Scene::Scene* scene_ = nullptr;
        const Assets::AssetDatabase* asset_database_ = nullptr;

        EditorSelection selection_;
        SceneEditHistory history_;

        std::filesystem::path scene_path_;
        std::string status_{ "新規シーン" };
        bool dirty_ = false;
        bool play_mode_ = false;
    };
}
