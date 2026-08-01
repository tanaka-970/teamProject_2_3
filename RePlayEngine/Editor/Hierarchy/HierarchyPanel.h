#pragma once

#include "../../Core/ObjectID/ObjectID.h"

namespace ReplayEngine::Core { class GameObject; }

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

    private:
        void DrawNode(EditorContext& context, Core::GameObject& object, int depth);
        void DrawContextMenu(EditorContext& context, Core::GameObject* object);
        void HandleDragAndDrop(EditorContext& context, Core::GameObject& object);

        void CreateEmptyGameObject(EditorContext& context, Core::GameObject* parent);
        void DuplicateSelected(EditorContext& context);
        void DestroySelected(EditorContext& context);

        // ツリー走査中に確定させると添字や再帰が壊れるため、
        // 実際の親子変更は走査後にまとめて処理する。
        Core::ObjectID pending_reparent_child_;
        Core::ObjectID pending_reparent_parent_;
        bool pending_reparent_to_root_ = false;

        // 名前変更中の対象。
        Core::ObjectID renaming_;
        static constexpr int rename_buffer_size = 256;
        char rename_buffer_[rename_buffer_size]{};
    };
}
