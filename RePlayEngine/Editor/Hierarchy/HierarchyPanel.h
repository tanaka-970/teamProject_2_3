#pragma once

#include "../../Core/ObjectID/ObjectID.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ReplayEngine::Core { class GameObject; }
namespace ReplayEngine::Scene::Serialization { struct SceneData; }

namespace ReplayEngine::Editor
{
    class EditorContext;

    // Scene 内の GameObject をツリー表示し、選択・作成・削除・親子変更を行うパネル。
    //
    // 構造を変える操作（作成・削除・複製・親子変更）は、
    // すべて EditorContext のトランザクション経由で行う。
    // パネルが Scene を直接書き換えて Undo 履歴から漏れることがないようにするため。
    //
    // 削除はその場で実体を壊さない。GameObject::Destroy() が予約を立てるだけで、
    // 破棄は EditorContext::CommitEdit() の中で行われる。
    // ImGui のツリーを描いている最中に対象が消えないので、無効ポインタが生まれない。
    class HierarchyPanel final
    {
    public:
        void Draw(EditorContext& context);
        void DrawContents(EditorContext& context);

        // Main Menu / Shortcut からも Hierarchy と同じ安全な経路を使う。
        void CreateEmpty(EditorContext& context) { CreateEmptyGameObject(context, nullptr); }
        void DuplicateSelection(EditorContext& context) { DuplicateSelected(context); }
        void DestroySelection(EditorContext& context) { DestroySelected(context); }
        void BeginRenameSelection(EditorContext& context);

        // Clipboard I/O itself belongs to framework (ImGui).  This panel owns the
        // scene-safe part so keyboard, menu, command and validation share one path.
        bool CopySelection(EditorContext& context, std::string& clipboard_text,
            std::string& error) const;
        bool PasteSelection(EditorContext& context, const std::string& clipboard_text,
            std::string& error);
        bool PasteSceneData(EditorContext& context,
            const Scene::Serialization::SceneData& data, std::string& error);

    private:
        void DrawNode(EditorContext& context, Core::GameObject& object, int depth,
            const std::vector<Core::GameObject*>& siblings);
        bool NodeMatchesFilter(const Core::GameObject& object) const;
        void DrawContextMenu(EditorContext& context, Core::GameObject* object);

        void CreateEmptyGameObject(EditorContext& context, Core::GameObject* parent);
        void DrawCreateMenu(EditorContext& context, Core::GameObject* parent);
        void CreateBuiltInPrimitive(EditorContext& context, Core::GameObject* parent,
            const char* display_name, int primitive_type);
        void CreateLandscapeGround(EditorContext& context, Core::GameObject* parent);
        void DuplicateSelected(EditorContext& context);
        void DestroySelected(EditorContext& context);

        // ツリー走査中に確定させると添字や再帰が壊れるため、
        // 実際の親子・兄弟順変更は走査後にまとめて処理する。
        enum class DropPlacement : int { Child = 0, Before = 1, After = 2, Root = 3 };
        Core::ObjectID pending_reparent_child_;
        Core::ObjectID pending_reparent_parent_;
        DropPlacement pending_drop_placement_ = DropPlacement::Child;

        // 名前変更中の対象。
        Core::ObjectID renaming_;
        static constexpr int rename_buffer_size = 256;
        char rename_buffer_[rename_buffer_size]{};

        static constexpr int search_buffer_size = 256;
        char search_buffer_[search_buffer_size]{};
        Core::ObjectID selection_anchor_;
    };
}
