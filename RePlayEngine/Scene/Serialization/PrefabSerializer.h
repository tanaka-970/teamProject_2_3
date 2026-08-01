#pragma once

#include "SceneData.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Scene::Serialization
{
    struct PrefabOverrideSummary
    {
        bool missing_source = false;
        bool unsupported_nested_prefab = false;
        bool has_overrides = false;
        std::vector<std::string> details;
    };

    // GameObject の部分木を Prefab として保存・復元する。
    //
    // 旧 PrefabSerializer は SceneDocument の SceneEntity 1 件を
    // 旧 SceneSerializer で書き出すだけのものだった。
    // こちらは v7 の SceneData をそのまま使うため、次がすべて自動で付いてくる。
    //
    //   - Component の型名で保存し、ComponentRegistry で復元する
    //   - プロパティは PropertyRegistry の定義に従って保存・復元する
    //   - 子 GameObject を含む階層をそのまま保存できる
    //   - 未登録 Component / 未知プロパティは警告してスキップする
    //
    // 型ごとの分岐は 1 つも無い。Component を増やしても、このファイルは変更不要。
    //
    // ファイル形式は Scene と同じ v7 テキスト。拡張子だけ .replayprefab に分ける。
    // 中身が同形式なので、Prefab をそのまま Scene として開くこともできる。
    class PrefabSerializer final
    {
    public:
        PrefabSerializer() = delete;

        static constexpr const char* file_extension = ".replayprefab";

        // 指定した GameObject とその子孫を Prefab として保存する。
        //
        // 保存される ID は Prefab 内で閉じた相対的なものとして扱う。
        // 起点の GameObject は必ず親なし（PARENT 0）として書き出すため、
        // どの階層のオブジェクトを保存しても、読み込み側で単独の部分木になる。
        static bool Save(const Scene& scene, Core::ObjectID root,
            const std::filesystem::path& path, std::string& error);

        // Prefab を Scene へ配置する。
        //
        // ObjectID は必ず新しく採番し直す。保存されていた ID をそのまま使うと、
        // 同じ Prefab を 2 回置いたときに衝突するため。
        // 成功したら起点 GameObject の新しい ObjectID を返し、失敗なら無効 ID を返す。
        static Core::ObjectID Instantiate(Scene& scene,
            const std::filesystem::path& path, std::string& error,
            SceneLoadReport* report = nullptr,
            const std::string& source_asset_guid = {});

        // Saving an ordinary hierarchy creates an asset; LinkInstance attaches the
        // resulting AssetGUID to the existing Scene objects without name tracking.
        static bool LinkInstance(Scene& scene, Core::ObjectID root,
            const std::string& source_asset_guid, std::string& error);

        static bool ApplyOverrides(Scene& scene, Core::ObjectID root,
            const std::filesystem::path& path, const std::string& source_asset_guid,
            std::string& error);
        static bool RevertOverrides(Scene& scene, Core::ObjectID root,
            const std::filesystem::path& path, const std::string& source_asset_guid,
            std::string& error, SceneLoadReport* report = nullptr);
        static bool Unpack(Scene& scene, Core::ObjectID root, std::string& error);

        static PrefabOverrideSummary InspectOverrides(const Scene& scene,
            Core::ObjectID root, const std::filesystem::path& path,
            const std::string& source_asset_guid);
    };
}
