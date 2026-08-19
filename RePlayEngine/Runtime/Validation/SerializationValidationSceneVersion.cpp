#include "SerializationValidationInternal.h"

namespace ReplayEngine::Runtime::Validation
{
    using namespace Detail::SerializationValidation;

    // =====================================================================
    // Scene Version
    // =====================================================================


    int RunSceneVersionValidation()
    {
        Core::RegisterBuiltInComponents();
        Checker check(210);

        check.Expect(Serialization::SceneData::current_version == 11,
            "現行の Scene バージョンは 11");
        check.Expect(Serialization::SceneData::minimum_supported_version == 7,
            "最低対応バージョンは 7 のまま");

        // ---- v7 〜 v11 を読める --------------------------------------------

        for (int version = 7; version <= 11; ++version)
        {
            const std::string legacy = MakeLegacyScene(version);
            std::istringstream in(legacy);
            Serialization::SceneData data;
            std::string error;

            const bool ok = Serialization::SceneSerializer::ReadText(data, in, error);
            const std::string label = "v" + std::to_string(version);

            check.Expect(ok, ("旧形式 " + label + " を読み込める").c_str());
            if (!ok)
            {
                std::fprintf(stderr, "  %s read error: %s\n", label.c_str(), error.c_str());
                continue;
            }

            check.Expect(data.version == version,
                ("読み取ったバージョンが " + label + " と一致する").c_str());
            check.Expect(data.objects.size() == 2,
                (label + " の GameObject を 2 体読める").c_str());
            check.Expect(data.scene_name == "レガシー Scene",
                (label + " の日本語 Scene 名を読める").c_str());

            // v8 以降は操作対象が読める。v7 は無効のまま。
            const bool controlled_expected = version >= 8;
            check.Expect(data.controlled_object.Valid() == controlled_expected,
                (label + " の操作対象 ObjectID の有無が正しい").c_str());

            if (data.objects.size() == 2 && !data.objects[0].components.empty())
            {
                const Serialization::ComponentData& component = data.objects[0].components[0];
                check.Expect(component.type_name == "RotatorComponent",
                    (label + " の Component 型名を読める").c_str());
                check.Expect(component.properties.Contains("degrees_per_second"),
                    (label + " の Component プロパティを読める").c_str());

                // v11 未満は StableID が未記録 (0) であること。
                const bool has_stable_id = version >= 11;
                check.Expect((component.stable_id != 0) == has_stable_id,
                    (label + " の StableID の有無が正しい").c_str());
            }
        }

        // ---- 旧形式 -> 現行形式への移行と往復 --------------------------------

        for (int version = 7; version <= 10; ++version)
        {
            const std::string label = "v" + std::to_string(version);
            const std::string legacy = MakeLegacyScene(version);

            std::istringstream in(legacy);
            Serialization::SceneData loaded;
            std::string error;
            if (!Serialization::SceneSerializer::ReadText(loaded, in, error)) continue;

            // 実 Scene へ流し込んでから保存し直す。
            // Editor で「開いて保存した」ときと同じ経路を通す。
            ReplayEngine::Scene::Scene world("MigrationWorld");
            Serialization::SceneLoadReport report;
            check.Expect(Serialization::ApplySceneData(loaded, world, report),
                (label + " を実 Scene へ流し込める").c_str());
            check.Expect(report.missing_components == 0,
                (label + " の移行で Missing Component が発生しない").c_str());

            Serialization::SceneData migrated;
            Serialization::CaptureScene(world, migrated);

            Serialization::SceneData restored;
            std::string text;
            const bool ok = RoundTrip(migrated, restored, text, error);
            check.Expect(ok, (label + " を現行形式で保存して読み戻せる").c_str());
            if (!ok) continue;

            check.Expect(restored.version == Serialization::SceneData::current_version,
                (label + " から保存すると現行バージョンになる").c_str());
            check.Expect(restored.objects.size() == 2,
                (label + " の GameObject 数が移行後も保たれる").c_str());

            // 移行が冪等であること。もう一度通しても内容が変わらない。
            ReplayEngine::Scene::Scene second_world("MigrationWorld2");
            Serialization::SceneLoadReport second_report;
            Serialization::ApplySceneData(restored, second_world, second_report);
            Serialization::SceneData second_migrated;
            Serialization::CaptureScene(second_world, second_migrated);

            Serialization::SceneData ignored;
            std::string second_text;
            check.Expect(RoundTrip(second_migrated, ignored, second_text, error) &&
                second_text == text,
                (label + " の移行が冪等（2 回通しても同じ結果）").c_str());
        }

        // ---- 新しすぎるバージョンは拒否する ---------------------------------

        {
            const std::string too_new = MakeLegacyScene(Serialization::SceneData::current_version + 1);
            std::istringstream in(too_new);
            Serialization::SceneData data;
            std::string error;

            const bool ok = Serialization::SceneSerializer::ReadText(data, in, error);
            check.Expect(!ok, "現行より新しいバージョンは読み込みを拒否する");
            check.Expect(!error.empty(), "拒否時に理由のメッセージが入る");
            check.Expect(data.objects.empty(),
                "拒否時に途中まで読んだ内容を返さない（半端なデータで上書きしない）");
        }

        // ---- 対応範囲外の旧形式 --------------------------------------------

        {
            const std::string ancient = MakeLegacyScene(6);
            std::istringstream in(ancient);
            Serialization::SceneData data;
            std::string error;
            check.Expect(!Serialization::SceneSerializer::ReadText(data, in, error),
                "v6 以前は読み込みを拒否する");
        }

        // ---- Scene ファイルでないものを渡された場合 --------------------------

        {
            std::istringstream in("これは Scene ファイルではありません");
            Serialization::SceneData data;
            std::string error;
            check.Expect(!Serialization::SceneSerializer::ReadText(data, in, error),
                "Scene ファイルでない入力を拒否する（クラッシュしない）");
        }

        return check.Report("Scene version validation", 210);
    }
}
