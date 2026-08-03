#pragma once

#include <filesystem>
#include <string>

namespace ReplayEngine::Assets
{
    class AssetDatabase;
}

namespace ReplayEngine::Project
{
    // Asset を GUID で参照したときの解決結果。
    //
    // なぜ GUID だけを保存するか:
    //   Prefab のファイル名も、その中のルート GameObject 名も、ユーザーが
    //   自由に変えられる。名前やパスを保存すると、名前を変えた瞬間に参照が切れる。
    //   GUID なら名前を変えても参照が維持される。
    //
    // なぜ表示名とパスをここへ持つか:
    //   UI へ生の GUID を常時出さないため。表示は AssetDatabase から引き直した
    //   名前とパスで行い、GUID は詳細表示のときだけ出す。
    struct PrefabReferenceStatus
    {
        enum class State
        {
            // そもそも設定されていない。
            Unset,
            // GUID は設定されているが、AssetDatabase に存在しない。
            Missing,
            // 解決できた。
            Resolved,
        };

        State state = State::Unset;

        std::string guid;
        std::string display_name;
        std::filesystem::path path;

        bool IsUnset()    const noexcept { return state == State::Unset; }
        bool IsMissing()  const noexcept { return state == State::Missing; }
        bool IsResolved() const noexcept { return state == State::Resolved; }

        // UI にそのまま出せる 1 行。GUID は含めない。
        std::string DisplayLabel() const;
    };

    // Prefab 以外の Asset 参照にも同じ状態表現を使う。
    //
    // 状態の型を種類ごとに増やさない理由:
    //   Unset / Missing / Resolved という区別は参照先の種類に依存しない。
    //   型を分けると、UI 側で同じ分岐を種類の数だけ書くことになる。
    using AssetReferenceStatus = PrefabReferenceStatus;

    // プロジェクト単位の設定。
    //
    // Singleton ではない。framework が値メンバとして 1 つ所有する。
    // Scene の内容ではなくプロジェクトの内容なので、Scene ファイルには保存しない。
    //
    // 【Default Controlled Character Prefab について】
    //   「新規シーンを Default で作ったときに 1 体だけ配置する Prefab」を指す。
    //   これは起動時や Scene 読み込み時には一切参照されない。
    //   参照されるのは新規 Scene 作成の Default を選んだ瞬間だけ。
    class ProjectSettings final
    {
    public:
        ProjectSettings() = default;

        // ---- Default Controlled Character Prefab ---------------------------

        const std::string& DefaultCharacterPrefabGuid() const noexcept
        {
            return default_character_prefab_guid_;
        }

        void SetDefaultCharacterPrefabGuid(std::string guid)
        {
            default_character_prefab_guid_ = std::move(guid);
        }

        void ClearDefaultCharacterPrefab() noexcept
        {
            default_character_prefab_guid_.clear();
        }

        bool HasDefaultCharacterPrefab() const noexcept
        {
            return !default_character_prefab_guid_.empty();
        }

        // AssetDatabase を通して名前とパスを引き直す。
        // 設定されていなければ Unset、登録が消えていれば Missing を返す。
        // どちらの場合も例外は投げず、assert もしない。
        PrefabReferenceStatus ResolveDefaultCharacterPrefab(
            const Assets::AssetDatabase& database) const;

        // ---- Startup Scene -------------------------------------------------
        //
        // 【Editor が最後に開いた Scene とは別物】
        //   Saved/EditorSession/ には「編集を再開する Scene」が入っている。
        //   あれは作業者ごとの都合であり、プロジェクトの設定ではない。
        //   Startup Scene は「このゲームを起動したら最初に始まる Scene」で、
        //   チーム全員が同じ値を共有する。混ぜると、誰かが別の Scene を
        //   編集しただけでゲームの起動先が変わってしまう。
        //
        // 【空を許す理由】
        //   Scene がまだ 1 つも無いプロジェクトが普通に存在する。
        //   空を禁止すると、新規プロジェクトを作った瞬間に不正な設定になる。
        //   空のときは「未設定」という明確な診断状態へ入るだけで、
        //   適当な Scene を勝手に選ぶことはしない。

        const std::string& StartupSceneGuid() const noexcept
        {
            return startup_scene_guid_;
        }

        void SetStartupSceneGuid(std::string guid)
        {
            startup_scene_guid_ = std::move(guid);
        }

        void ClearStartupScene() noexcept { startup_scene_guid_.clear(); }

        bool HasStartupScene() const noexcept { return !startup_scene_guid_.empty(); }

        // AssetDatabase を通して名前とパスを引き直す。
        // Scene Asset でない GUID を指していた場合も Missing として返す
        // （黙って別種の Asset を起動先として受け入れない）。
        AssetReferenceStatus ResolveStartupScene(
            const Assets::AssetDatabase& database) const;

        // ---- 既定値へ戻す --------------------------------------------------

        void Reset() noexcept
        {
            default_character_prefab_guid_.clear();
            startup_scene_guid_.clear();
        }

    private:
        std::string default_character_prefab_guid_;
        std::string startup_scene_guid_;
    };
}
