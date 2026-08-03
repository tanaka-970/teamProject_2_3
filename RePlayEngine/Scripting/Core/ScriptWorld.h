#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ReplayEngine::Scripting
{
    class ScriptComponent;

    // 1 回の Play セッションで生きているスクリプトの登録簿。
    //
    // ---------------------------------------------------------------------
    // 【World を走査して集めない】
    //
    //   登録は ScriptComponent の自己申告で行う。
    //   OnRuntimeAwake で自分を入れ、OnRuntimeDestroy で自分を外す。
    //
    //   Runtime 側から Scene を走査して集める方式にすると、
    //   「いつ集めるか」がフレームのどこかに増える。それは実質的に
    //   第二の更新経路であり、Scene の同期点と食い違う瞬間ができる。
    //
    // ---------------------------------------------------------------------
    // 【何に使うか】
    //
    //   - Schema 差し替え時に、値の移送が必要なインスタンスを回す
    //   - Play セッション終了時に、後始末の漏れを数える
    //   - 診断表示（今いくつ動いているか）
    //
    //   ここから Update / FixedUpdate / LateUpdate を回すことは無い。
    //   それは Scene の既存ループが 1 本で担当する。
    //
    // ---------------------------------------------------------------------
    // 保持するのは非所有の生ポインタ。実体は GameObject が持つ。
    // 外れ忘れが起きないよう、登録・解除は必ず対で呼ぶこと。
    class ScriptWorld final
    {
    public:
        explicit ScriptWorld(std::uint64_t generation) noexcept
            : generation_(generation)
        {
        }

        ScriptWorld(const ScriptWorld&) = delete;
        ScriptWorld& operator=(const ScriptWorld&) = delete;

        // Play セッションの世代番号。World が入れ替わるたびに増える。
        // 古いセッションのハンドルを弾くための照合に使う。
        std::uint64_t Generation() const noexcept { return generation_; }

        // 終了処理へ入ったか。true のあいだ新しいインスタンス生成を受け付けない。
        bool Closing() const noexcept { return closing_; }
        void BeginClosing() noexcept { closing_ = true; }

        void Register(ScriptComponent& component);
        void Unregister(ScriptComponent& component) noexcept;

        const std::vector<ScriptComponent*>& Components() const noexcept
        {
            return components_;
        }

        std::size_t Count() const noexcept { return components_.size(); }
        bool Empty() const noexcept { return components_.empty(); }

        // 登録・解除の累計。対になっているかの検証に使う。
        std::uint64_t RegisterCount() const noexcept { return register_count_; }
        std::uint64_t UnregisterCount() const noexcept { return unregister_count_; }

    private:
        std::vector<ScriptComponent*> components_;

        std::uint64_t generation_ = 0;
        std::uint64_t register_count_ = 0;
        std::uint64_t unregister_count_ = 0;

        bool closing_ = false;
    };
}
