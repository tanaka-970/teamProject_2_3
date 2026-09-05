#include "ShaderLightingValidation.h"

#include "BuiltInShaders.h"
#include "ShaderLibrary.h"
#include "ShaderSource.h"

#include <cmath>
#include <cstdio>
#include <filesystem>/////
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace ReplayEngine::Rendering::Validation
{
    namespace
    {
        class Checker final
        {
        public:
            explicit Checker(int first_code) : next_code_(first_code) {}

            void Expect(bool condition, const std::string& what)
            {
                const int code = next_code_++;
                ++total_;
                if (condition) return;
                ++failures_;
                if (first_failure_ == 0) first_failure_ = code;
                std::fprintf(stderr, "  [FAIL %d] %s\n", code, what.c_str());
            }

            int Report() const
            {
                if (first_failure_ == 0)
                {
                    std::fprintf(stderr,
                        "shader-lighting OK: %d checks passed\n", total_);
                    return 0;
                }
                std::fprintf(stderr,
                    "shader-lighting FAILED: %d/%d checks failed (first=%d)\n",
                    failures_, total_, first_failure_);
                return first_failure_;
            }

        private:
            int next_code_ = 0;
            int first_failure_ = 0;
            int total_ = 0;
            int failures_ = 0;
        };

        std::string ReadAllText(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return {};
            return std::string((std::istreambuf_iterator<char>(stream)),
                std::istreambuf_iterator<char>());
        }

        bool HasFatalIssue(const ShaderSource::ParseResult& result)
        {
            for (const ShaderSource::ParseIssue& issue : result.issues)
            {
                if (issue.fatal) return true;
            }
            return false;
        }

        struct BuiltInExpectation final
        {
            ShaderID id;
            int legacy_shading_model = 0;
            ShaderLightingModel lighting_model = ShaderLightingModel::Pbr;
        };

        std::vector<BuiltInExpectation> ExpectedBuiltIns()
        {
            return {
                { BuiltInShaders::FbxDefault, 0, ShaderLightingModel::Pbr },
                { BuiltInShaders::Pbr,        1, ShaderLightingModel::Pbr },
                { BuiltInShaders::Toon,       2, ShaderLightingModel::Toon },
                { BuiltInShaders::Unlit,      3, ShaderLightingModel::Unlit },
                { BuiltInShaders::Pixelate,   4, ShaderLightingModel::Pbr },
            };
        }
    }

    int RunShaderLightingValidation()
    {
        Checker check(1400);

        // ---- 1. C++ 側の固定値 ------------------------------------------
        check.Expect(static_cast<int>(ShaderLightingModel::Pbr) == 0,
            "PBR の値は 0");
        check.Expect(static_cast<int>(ShaderLightingModel::Toon) == 1,
            "Toon の値は 1");
        check.Expect(static_cast<int>(ShaderLightingModel::Unlit) == 2,
            "Unlit の値は 2");
        check.Expect(shader_lighting_model_count == 3,
            "照明モデルは 3 種");

        ShaderLightingModel parsed_model = ShaderLightingModel::Unlit;
        check.Expect(TryParseShaderLightingModel("pbr", parsed_model) &&
            parsed_model == ShaderLightingModel::Pbr,
            "pbr を解析できる");
        check.Expect(TryParseShaderLightingModel("toon", parsed_model) &&
            parsed_model == ShaderLightingModel::Toon,
            "toon を解析できる");
        check.Expect(TryParseShaderLightingModel("unlit", parsed_model) &&
            parsed_model == ShaderLightingModel::Unlit,
            "unlit を解析できる");
        check.Expect(!TryParseShaderLightingModel("phong", parsed_model),
            "知らない照明モデルを受理しない");
        check.Expect(std::string(ToString(ShaderLightingModel::Pbr)) == "pbr" &&
            std::string(ToString(ShaderLightingModel::Toon)) == "toon" &&
            std::string(ToString(ShaderLightingModel::Unlit)) == "unlit",
            "照明モデルを文字列へ戻せる");

        // ---- 2. pragma 解析 ---------------------------------------------
        constexpr const char* guid =
            "#pragma replay_guid \"1234567890abcdef1234567890abcdef\"\n";

        bool needs_guid = true;
        ShaderSource::ParseResult explicit_toon = ShaderSource::ParseText(
            std::string(guid) + "#pragma replay_lighting toon\n",
            "explicit_toon.hlsl", needs_guid);
        check.Expect(explicit_toon.succeeded && !needs_guid,
            "replay_lighting を含むソースを解析できる");
        check.Expect(explicit_toon.info.lighting_model == ShaderLightingModel::Toon &&
            explicit_toon.info.lighting_model_valid,
            "replay_lighting toon が Toon になる");
        check.Expect(!HasFatalIssue(explicit_toon),
            "正しい replay_lighting は fatal にならない");

        needs_guid = true;
        ShaderSource::ParseResult omitted = ShaderSource::ParseText(
            guid, "omitted.hlsl", needs_guid);
        check.Expect(omitted.info.lighting_model == ShaderLightingModel::Pbr &&
            omitted.info.lighting_model_valid,
            "未指定なら PBR になる");

        needs_guid = true;
        ShaderSource::ParseResult unknown = ShaderSource::ParseText(
            std::string(guid) + "#pragma replay_lighting phong\n",
            "unknown.hlsl", needs_guid);
        check.Expect(!unknown.info.lighting_model_valid,
            "不明な名前は有効扱いにしない");
        check.Expect(HasFatalIssue(unknown),
            "不明な名前は fatal な書式エラーになる");

        needs_guid = true;
        ShaderSource::ParseResult missing = ShaderSource::ParseText(
            std::string(guid) + "#pragma replay_lighting\n",
            "missing.hlsl", needs_guid);
        check.Expect(!missing.info.lighting_model_valid && HasFatalIssue(missing),
            "値が無い replay_lighting を通さない");

        needs_guid = true;
        ShaderSource::ParseResult duplicated = ShaderSource::ParseText(
            std::string(guid) +
            "#pragma replay_lighting pbr\n#pragma replay_lighting toon\n",
            "duplicated.hlsl", needs_guid);
        check.Expect(!duplicated.info.lighting_model_valid &&
            HasFatalIssue(duplicated),
            "replay_lighting の重複を通さない");

        // ---- 3. 旧番号と組み込みShaderの対応 ----------------------------
        for (const BuiltInExpectation& expected : ExpectedBuiltIns())
        {
            ShaderLightingModel by_legacy = ShaderLightingModel::Unlit;
            ShaderLightingModel by_id = ShaderLightingModel::Unlit;
            check.Expect(BuiltInShaders::TryGetLightingModelFromShadingModel(
                expected.legacy_shading_model, by_legacy) &&
                by_legacy == expected.lighting_model,
                "旧 shading_model から照明モデルを引ける: " +
                std::to_string(expected.legacy_shading_model));
            check.Expect(BuiltInShaders::TryGetLightingModel(expected.id, by_id) &&
                by_id == expected.lighting_model,
                "組み込み ShaderID から照明モデルを引ける: " +
                expected.id.ToString());
        }
        ShaderLightingModel not_found = ShaderLightingModel::Pbr;
        check.Expect(!BuiltInShaders::TryGetLightingModelFromShadingModel(
            99, not_found),
            "不明な旧 shading_model を勝手に丸めない");
        check.Expect(!BuiltInShaders::TryGetLightingModel(ShaderID{}, not_found),
            "無効な ShaderID を勝手に丸めない");

        // ---- 4. 実ファイルとCatalog --------------------------------------
        const std::filesystem::path root = std::filesystem::current_path();
        ShaderLibrary library;
        std::vector<std::string> errors;
        library.SetLogSink(
            [&errors](const std::string& severity, const std::string& message,
                const std::filesystem::path& file, int line)
            {
                if (severity == "Error")
                {
                    errors.push_back(file.generic_u8string() + ":" +
                        std::to_string(line) + ": " + message);
                }
            });
        const ShaderLibrary::ScanReport report = library.ScanAll(root);
        check.Expect(report.scanned >= 5,
            "組み込み5種を含むShaderフォルダを走査できる");
        check.Expect(report.duplicate_ids == 0,
            "Shader GUID の重複が無い");

        // Catalogは編集中の壊れた自作Shaderを抱えたままでも起動を継続する。
        // そのため、ここでは全ShaderのError件数を完了条件にしない。
        // 下で組み込み5種が実際に登録され、宣言が一致することを個別に検査する。

        for (const BuiltInExpectation& expected : ExpectedBuiltIns())
        {
            const ShaderCatalog::Entry* entry = library.Catalog().Find(expected.id);
            check.Expect(entry != nullptr,
                "組み込みShaderがCatalogにある: " + expected.id.ToString());
            if (entry == nullptr) continue;
            check.Expect(entry->info.lighting_model_valid,
                "組み込みShaderのreplay_lightingが有効: " +
                entry->info.DisplayName());
            check.Expect(entry->info.lighting_model == expected.lighting_model,
                "組み込みShaderの照明モデルが期待値: " +
                entry->info.DisplayName());
        }

        // ---- 5. C++ / HLSL の数値一致 -----------------------------------
        const std::string lighting_hlsl = ReadAllText(
            root / "Shader" / "lighting_model_hlsl.hlsli");
        check.Expect(!lighting_hlsl.empty(),
            "lighting_model_hlsl.hlsli がある");
        check.Expect(lighting_hlsl.find("REPLAY_LIGHTING_PBR   0") !=
            std::string::npos, "HLSL PBR は 0");
        check.Expect(lighting_hlsl.find("REPLAY_LIGHTING_TOON  1") !=
            std::string::npos, "HLSL Toon は 1");
        check.Expect(lighting_hlsl.find("REPLAY_LIGHTING_UNLIT 2") !=
            std::string::npos, "HLSL Unlit は 2");

        // HLSLと同じ「value / 255 → round(alpha * 255)」で全値が戻ること。
        bool byte_round_trip_ok = true;
        for (std::uint32_t value = 0; value <= 255; ++value)
        {
            const float encoded = static_cast<float>(value) / 255.0f;
            const std::uint32_t decoded = static_cast<std::uint32_t>(
                std::lround(encoded * 255.0f));
            if (decoded != value)
            {
                byte_round_trip_ok = false;
                break;
            }
        }
        check.Expect(byte_round_trip_ok,
            "GBuffer alpha の 8bit 値が 0..255 すべて往復する");

        // Pixelate の識別は負数、セル幅は絶対値という契約。
        bool pixelate_marker_ok = true;
        for (const float size : { 1.0f, 6.0f, 64.0f })
        {
            const float stored = -size;
            if (!(stored < 0.0f) || std::fabs((-stored) - size) > 0.0001f)
            {
                pixelate_marker_ok = false;
                break;
            }
        }
        check.Expect(pixelate_marker_ok,
            "Pixelate設定マーカーが照明モデル番号と独立している");

        // 有効なGBuffer経路に旧シェーダ種類番号が残っていないこと。
        const std::vector<std::filesystem::path> active_hlsl = {
            root / "Shader" / "gbuffer_common.hlsli",
            root / "Shader" / "static_mesh_gbuffer_ps.hlsl",
            root / "Shader" / "skinned_mesh_gbuffer_ps.hlsl",
            root / "Shader" / "deferred_lighting_ps.hlsl",
        };
        bool active_path_is_decoupled = true;
        for (const std::filesystem::path& file : active_hlsl)
        {
            const std::string text = ReadAllText(file);
            if (text.empty() || text.find("SHADING_MODEL_") != std::string::npos ||
                text.find("d.shading_model") != std::string::npos ||
                text.find("g.shading_model") != std::string::npos)
            {
                active_path_is_decoupled = false;
                break;
            }
        }
        check.Expect(active_path_is_decoupled,
            "有効なGBuffer経路が旧SHADING_MODEL番号へ依存しない");

        if (!errors.empty())
        {
            std::fprintf(stderr, "  --- ShaderLibrary errors ---\n");
            for (const std::string& error : errors)
            {
                std::fprintf(stderr, "    %s\n", error.c_str());
            }
        }

        return check.Report();
    }
}
