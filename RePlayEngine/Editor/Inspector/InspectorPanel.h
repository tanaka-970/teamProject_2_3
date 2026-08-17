#pragma once

#include "../ComponentBrowser/AddComponentPanel.h"
#include "../../Object/Component/ComponentTypeID.h"
#include "../../Object/Component/MissingComponent.h"

#include <string>
#include <vector>

namespace ReplayEngine::Core { class Component; class GameObject; }

namespace ReplayEngine::Editor
{
    class EditorContext;

    // 選択中の GameObject と、その Component を編集するパネル。
    //
    // このクラスに Component 型ごとの分岐は書かない。
    //   Component の一覧 -> GameObject が持つ実体をそのまま走査
    //   表示名・削除可否 -> ComponentRegistry
    //   プロパティ欄     -> PropertyRegistry + PropertyDrawer
    // 新しい Component を登録しても、このファイルは変更不要。
    //
    // 安全性:
    //   Component の削除は即時ではなく削除予約になる（GameObject::RemoveComponent）。
    //   ImGui の描画途中で実体が消えないため、その後の行で無効ポインタを触ることがない。
    //   実際の破棄は EditorContext::CommitEdit() が呼ぶ Scene::ProcessPendingOperations()。
    class InspectorPanel final
    {
    public:
        // ImGui ウィンドウ込みで描く。ウィンドウ名は既存レイアウトに合わせる。
        void Draw(EditorContext& context);

        // 既存ウィンドウの中へ埋め込みたい場合に使う（Begin/End を呼ばない）。
        void DrawContents(EditorContext& context);

        // ProjectSettings の Template 表示値を plain bool で受け取る。
        // Checkbox で値が変わったときだけ true を返す。永続化は呼び出し側の責務。
        bool DrawContents(EditorContext& context, bool& show_game_template_components);

    private:
        void DrawGameObjectHeader(EditorContext& context, Core::GameObject& object);
        void DrawPrefabHeader(EditorContext& context, Core::GameObject& object);
        void DrawMultiSelection(EditorContext& context,
            const std::vector<Core::GameObject*>& objects);
        void DrawCommonComponent(EditorContext& context,
            const std::vector<Core::GameObject*>& objects,
            Core::ComponentTypeID type_id);

        // 操作対象としての構成診断。
        // 型ごとの専用 Editor ではなく、PlayerCompositionValidator の表を描くだけ。
        void DrawPlayerComposition(EditorContext& context, Core::GameObject& object,
            bool show_game_template_components);
        void DrawComponent(EditorContext& context, Core::Component& component);

        // 読み込めなかった Component の預かり内容を読み取り専用で表示する。
        // 値は一切書き換えない。消えるのは明示的な削除操作のときだけ。
        static void DrawMissingComponentDetails(const Core::MissingComponent& missing);

        AddComponentPanel add_component_panel_;

        // 削除ボタンが押された Component。
        //
        // ボタンを押したその場で破棄まで進めると、Component 一覧を添字で走査している
        // 最中にコンテナが詰められ、以降の添字が 1 つずれる。
        // 走査を終えてから処理するため、ここへ 1 件だけ控える。
        // 削除予約は押した瞬間に立つので、この時点で既に一覧からは消えて見える。
        Core::Component* pending_removal_ = nullptr;
        std::string pending_removal_label_;

        // 名前入力欄の一時バッファ。編集中の GameObject が変わったら作り直す。
        static constexpr int name_buffer_size = 256;
        char name_buffer_[name_buffer_size]{};
        unsigned long long name_buffer_owner_ = 0;

        unsigned long long prefab_cache_owner_ = 0;
        std::string prefab_cache_guid_;
        bool prefab_cache_valid_ = false;
        bool prefab_cache_missing_ = false;
        bool prefab_cache_nested_ = false;
        bool prefab_cache_overrides_ = false;
        std::vector<std::string> prefab_cache_details_;
    };
}
