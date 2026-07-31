#pragma once

#include "../SceneDocument.h"

#include <filesystem>
#include <string>

namespace ReplayEngine::Scene::Legacy
{
    // 旧 SceneDocument 形式（v1〜v6）の読み書き。
    //
    // ---- このクラスの位置づけ ----------------------------------------------
    //
    // Scene の正式な保存方式は v7 の
    //   ReplayEngine::Scene::Serialization::SceneSerializer
    // であり、GameObject / Component 構成はすべてそちらが扱う。
    //
    // このクラスが残っているのは、SceneDocument がまだ次の情報を持っており、
    // それらが新しい Component へ移行できていないため。
    //
    //   - ステージ素材の配置記録（AssetGUID / Transform）
    //   - ShaderLayerStack（追加シェーダーパスの積み重ね）
    //   - CharacterMaterialProfile（キャラクター表現の詳細パラメータ）
    //   - クック済み衝突メッシュのキャッシュパスとセルサイズ
    //
    // これらを扱う Component（ShaderLayer / CharacterMaterial / MeshCollider）が
    // 用意できた時点で、このクラスと SceneDocument はまとめて廃止する。
    // 廃止予定は実装報告の「次に Component 化すべき処理」を参照。
    //
    // ---- 新形式と混ぜないための取り決め ------------------------------------
    //
    //  1. クラス名を分けている。
    //     新形式へ SceneDocument 用のオーバーロードを足して曖昧にしない。
    //  2. 名前空間を分けている（ReplayEngine::Scene::Legacy）。
    //  3. ファイル名を分けている。
    //     以前は SceneSerializer.cpp が新旧 2 つ存在し、MSVC が既定で
    //     全 obj を同一フォルダへ出すため SceneSerializer.obj が衝突して
    //     片方の定義が消え、LNK2001 になっていた。
    //  4. 保存拡張子を分けている。
    //     旧: .replaystage / 新: .replayscene
    //     同じ拡張子だと、v7 リーダーが旧ファイルを開いて
    //     「旧形式のため非対応」と拒否する事故が起きるため。
    class LegacySceneDocumentSerializer final
    {
    public:
        LegacySceneDocumentSerializer() = delete;

        // 旧ステージ配置記録の拡張子。新形式の .replayscene とは別物。
        static constexpr const char* file_extension = ".replaystage";

        static bool Save(const SceneDocument& scene,
            const std::filesystem::path& path, std::string& error);

        static bool Load(SceneDocument& scene,
            const std::filesystem::path& path, std::string& error);
    };
}
