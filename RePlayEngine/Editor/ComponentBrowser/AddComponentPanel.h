#pragma once

namespace ReplayEngine::Core { class GameObject; }

namespace ReplayEngine::Editor
{
    class EditorContext;

    // Add Component の一覧をポップアップとして描く。
    //
    // 一覧の中身は ComponentRegistry と ScriptTypeCatalog から取る。
    // Component 型名や Script 名をここへ書き並べることはしない。
    // 新しい Component / Script Type を登録すれば、そのままこのパネルへ現れる。
    class AddComponentPanel final
    {
    public:
        // Inspector の「コンポーネントを追加」ボタンから呼ぶ。
        // ポップアップを開く要求だけを立てる。
        void RequestOpen() noexcept { open_requested_ = true; }

        // 毎フレーム呼ぶ。ポップアップが開いていれば描画する。
        // Component を追加したら true を返す。
        // Template 表示設定は値だけ受け取り、永続化は呼び出し側へ返す。
        bool Draw(EditorContext& context, Core::GameObject& target,
            bool& show_game_template_components,
            bool& show_game_template_components_changed);

        void Close() noexcept;

    private:
        static constexpr int search_buffer_size = 128;

        bool open_requested_ = false;
        bool focus_search_ = false;
        char search_text_[search_buffer_size]{};
    };
}
