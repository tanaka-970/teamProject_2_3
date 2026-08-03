#pragma once

#include "SceneData.h"

#include <filesystem>
#include <iosfwd>
#include <string>

namespace ReplayEngine::Scene::Serialization
{
    // SceneData とファイルの相互変換だけを担当する。
    //
    // Scene クラスへは一切依存しない。
    // 受け渡すのは SceneData だけなので、
    //   Scene -> SceneData -> ファイル
    //   ファイル -> SceneData -> Scene
    // という一方向の流れになり、Scene と保存処理が密結合しない。
    //
    // 将来 JSON 出力を足す場合は、WriteText / ReadText と同じ形の
    // WriteJson / ReadJson を並べるだけでよく、SceneData 側は変更不要。
    //
    // 対応バージョン:
    //   v7 / v8 / v9。v1〜v6は構造が根本的に違うため非対応。
    //   v8 で「操作対象 ObjectID」、v9 で「衝突の設定」を追加した。
    //   旧形式を読み込もうとした場合はクラッシュさせず、
    //   作り直しを促す明確なエラーメッセージを返す。
    //   将来 v10 へ移行できるよう、バージョン判定の入口自体は必ず通す設計にしてある。
    //
    //   v8 / v9 の SCENE_STATE 行には旧 Player の移行状態が並んでいたが、
    //   旧 Player 経路の撤去にともない書き出さなくなった。読み取りは行単位なので、
    //   その項目が並んでいる既存ファイルもそのまま読める。
    class SceneSerializer final
    {
    public:
        SceneSerializer() = delete;

        // 保存に使う拡張子。
        static constexpr const char* file_extension = ".replayscene";

        // ---- ファイル入出力 ------------------------------------------------

        // 失敗した場合、既存ファイルを壊さないよう一時ファイルへ書いてから差し替える。
        static bool SaveToFile(const SceneData& data,
            const std::filesystem::path& path, std::string& error);

        static bool LoadFromFile(SceneData& data,
            const std::filesystem::path& path, std::string& error);

        // ---- ストリーム入出力 (テスト・将来の非同期化用) --------------------

        static bool WriteText(const SceneData& data, std::ostream& stream, std::string& error);
        static bool ReadText(SceneData& data, std::istream& stream, std::string& error);

        // ---- バージョン確認 ------------------------------------------------

        // ファイル先頭のバージョンだけを読む。読めない場合は 0 を返す。
        // 旧形式かどうかを開く前に判定したい場合に使う。
        static int PeekVersion(const std::filesystem::path& path, std::string& error);

        static bool IsSupportedVersion(int version) noexcept
        {
            // v7 も読める。v7 には Scene 単位の状態が無いので、既定値として扱う。
            return version >= SceneData::minimum_supported_version &&
                version <= SceneData::current_version;
        }

        // 旧バージョンを読もうとしたときのメッセージ。Editor の表示と揃えるため関数化する。
        static std::string UnsupportedVersionMessage(int version);
    };
}
