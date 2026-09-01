#pragma once

#include <DirectXMath.h>

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

        struct ScreenSpaceSettings final
        {
            float ssao_radius = 0.75f;
            float ssao_intensity = 1.0f;
            float ssao_power = 1.6f;
            float ssao_thin_occluder = 1.0f;
            int ssao_slice_count = 4;
            int ssao_step_count = 8;
            float ssao_fade_start = 60.0f;
            float ssao_fade_end = 140.0f;
            float ssao_normal_bias = 0.35f;
            float ssao_blur_sharpness = 1.0f;
            bool ssao_blur_enabled = true;
            float ssr_max_distance = 40.0f;
            float ssr_thickness = 0.55f;
            float ssr_stride = 3.0f;
            int ssr_max_step = 32;
            int ssr_refine_step = 4;
            float ssr_max_roughness = 0.6f;
            float ssr_intensity = 1.0f;
            float ssr_edge_fade = 0.08f;
            float ssr_ray_bias = 0.001f;
            float ssr_resolve_radius = 0.0f;
            int ssr_resolve_tap_count = 1;
            float taa_blend = 0.88f;
            float taa_variance_gamma = 1.0f;
            float taa_sharpness = 0.0f;
            float taa_max_velocity = 48.0f;
        };

        const ScreenSpaceSettings& ScreenSpace() const noexcept { return screen_space_; }
        ScreenSpaceSettings& MutableScreenSpace() noexcept { return screen_space_; }
        void SetScreenSpace(ScreenSpaceSettings settings) noexcept
        {
            screen_space_ = settings;
        }

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

        // ---- Loading Screen Scene --------------------------------------------
        const std::string& LoadingSceneGuid() const noexcept
        {
            return loading_scene_guid_;
        }

        void SetLoadingSceneGuid(std::string guid)
        {
            loading_scene_guid_ = std::move(guid);
        }

        void ClearLoadingScene() noexcept { loading_scene_guid_.clear(); }

        bool HasLoadingScene() const noexcept { return !loading_scene_guid_.empty(); }

        AssetReferenceStatus ResolveLoadingScene(
            const Assets::AssetDatabase& database) const;

        // ---- Active Scene Flow ----------------------------------------------
        const std::string& SceneFlowGuid() const noexcept { return scene_flow_guid_; }
        void SetSceneFlowGuid(std::string guid) { scene_flow_guid_ = std::move(guid); }
        void ClearSceneFlow() noexcept { scene_flow_guid_.clear(); }
        bool HasSceneFlow() const noexcept { return !scene_flow_guid_.empty(); }
        AssetReferenceStatus ResolveSceneFlow(
            const Assets::AssetDatabase& database) const;

        // ---- Localization --------------------------------------------------
        const std::string& LocalizationTableGuid() const noexcept
        {
            return localization_table_guid_;
        }
        void SetLocalizationTableGuid(std::string guid)
        {
            localization_table_guid_ = std::move(guid);
        }
        void ClearLocalizationTable() noexcept { localization_table_guid_.clear(); }
        const std::string& DefaultLanguage() const noexcept { return default_language_; }
        void SetDefaultLanguage(std::string language)
        {
            default_language_ = language.empty() ? std::string("ja") : std::move(language);
        }

        // ---- Input Action Asset -------------------------------------------
        const std::string& InputActionAssetGuid() const noexcept { return input_action_asset_guid_; }
        void SetInputActionAssetGuid(std::string guid) { input_action_asset_guid_ = std::move(guid); }
        void ClearInputActionAsset() noexcept { input_action_asset_guid_.clear(); }

        // ---- Runtime UI Focus Style ---------------------------------------
        bool FocusOutlineEnabled() const noexcept { return focus_outline_enabled_; }
        void SetFocusOutlineEnabled(bool value) noexcept { focus_outline_enabled_ = value; }
        const DirectX::XMFLOAT4& FocusOutlineColor() const noexcept { return focus_outline_color_; }
        void SetFocusOutlineColor(const DirectX::XMFLOAT4& value) noexcept { focus_outline_color_ = value; }
        float FocusOutlineWidth() const noexcept { return focus_outline_width_; }
        void SetFocusOutlineWidth(float value) noexcept { focus_outline_width_ = value; }
        float FocusCornerRadius() const noexcept { return focus_corner_radius_; }
        void SetFocusCornerRadius(float value) noexcept { focus_corner_radius_ = value; }

        // ---- Rendering Toggles ---------------------------------------------
        //
        // 【なぜ Scene ではなく Project へ置くか】
        //   どれも「このプロジェクトをどう描くか」の設定で、Scene の内容ではない。
        //   Scene ごとに変えられると、シーンを切り替えるたびに描画が変わって
        //   前後比較が成立しなくなる。
        //
        // 【なぜ保存するか】
        //   最適な値は中身によって変わる。深度プリパスは実測で、
        //   キャラ 1 体 400 万三角形のシーンでは切った方が 1.32ms 速く、
        //   地形シーンでは差が出なかった。LOD を入れれば逆転しうる。
        //   ハードコードのままだと、測って切り替えることができない。
        bool SsaoEnabled() const noexcept { return ssao_enabled_; }
        void SetSsaoEnabled(bool value) noexcept { ssao_enabled_ = value; }
        bool SsrEnabled() const noexcept { return ssr_enabled_; }
        void SetSsrEnabled(bool value) noexcept { ssr_enabled_ = value; }
        bool TaaEnabled() const noexcept { return taa_enabled_; }
        void SetTaaEnabled(bool value) noexcept { taa_enabled_ = value; }
        bool DepthPrepassEnabled() const noexcept { return depth_prepass_enabled_; }
        void SetDepthPrepassEnabled(bool value) noexcept { depth_prepass_enabled_ = value; }
        bool LuminanceEnabled() const noexcept { return luminance_enabled_; }
        void SetLuminanceEnabled(bool value) noexcept { luminance_enabled_ = value; }
        float LuminanceThreshold() const noexcept { return luminance_threshold_; }
        void SetLuminanceThreshold(float value) noexcept { luminance_threshold_ = value; }
        bool FinalPassEnabled() const noexcept { return final_pass_enabled_; }
        void SetFinalPassEnabled(bool value) noexcept { final_pass_enabled_ = value; }

        // ---- Editor Component Visibility ----------------------------------
        bool ShowGameTemplateComponents() const noexcept
        {
            return show_game_template_components_;
        }
        void SetShowGameTemplateComponents(bool value) noexcept
        {
            show_game_template_components_ = value;
        }

        // ---- 既定値へ戻す --------------------------------------------------

        void Reset() noexcept
        {
            ssao_enabled_ = true;
            ssr_enabled_ = true;
            taa_enabled_ = true;
            depth_prepass_enabled_ = false;
            luminance_enabled_ = true;
            luminance_threshold_ = 1.0f;
            final_pass_enabled_ = true;
            default_character_prefab_guid_.clear();
            startup_scene_guid_.clear();
            loading_scene_guid_.clear();
            scene_flow_guid_.clear();
            localization_table_guid_.clear();
            input_action_asset_guid_.clear();
            default_language_ = "ja";
            focus_outline_enabled_ = true;
            focus_outline_color_ = { 0.25f, 0.78f, 1.0f, 1.0f };
            focus_outline_width_ = 2.0f;
            focus_corner_radius_ = 4.0f;
            show_game_template_components_ = false;
            screen_space_ = ScreenSpaceSettings{};
        }

    private:
        std::string default_character_prefab_guid_;
        std::string startup_scene_guid_;
        std::string loading_scene_guid_;
        std::string scene_flow_guid_;
        std::string localization_table_guid_;
        std::string input_action_asset_guid_;
        std::string default_language_{ "ja" };
        bool focus_outline_enabled_ = true;
        DirectX::XMFLOAT4 focus_outline_color_{ 0.25f, 0.78f, 1.0f, 1.0f };
        float focus_outline_width_ = 2.0f;
        float focus_corner_radius_ = 4.0f;
        bool ssao_enabled_ = true;
        bool ssr_enabled_ = true;
        bool taa_enabled_ = true;
        // 既定 false は実測に基づく。Docs 参照。LOD 導入後は測り直すこと。
        bool depth_prepass_enabled_ = false;
        bool luminance_enabled_ = true;
        float luminance_threshold_ = 1.0f;
        bool final_pass_enabled_ = true;
        // Game Template は汎用プロジェクトでは既定非表示。Registry からは消さない。
        bool show_game_template_components_ = false;
        ScreenSpaceSettings screen_space_{};
    };
}
