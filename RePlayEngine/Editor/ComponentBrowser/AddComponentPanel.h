#pragma once

namespace ReplayEngine::Core { class GameObject; }

namespace ReplayEngine::Editor
{
    class EditorContext;

    // Add Component の一覧をポップアップとして描く。
    //
    // 一覧の中身は ComponentRegistry から取る。
    // Component 型名をここへ書き並べることはしない。
    // 新しい Component を登録すれば、そのままこのパネルへ現れる。
    class AddComponentPanel final
    {
    public:
        // Inspector の「コンポーネントを追加」ボタンから呼ぶ。
        // ポップアップを開く要求だけを立てる。
        void RequestOpen() noexcept { open_requested_ = true; }

        // 毎フレーム呼ぶ。ポップアップが開いていれば描画する。
        // Component を追加したら true を返す。
        bool Draw(EditorContext& context, Core::GameObject& target);

        void Close() noexcept;

    private:
        static constexpr int search_buffer_size = 128;

        bool open_requested_ = false;
        bool focus_search_ = false;
        char search_text_[search_buffer_size]{};
    };
}
