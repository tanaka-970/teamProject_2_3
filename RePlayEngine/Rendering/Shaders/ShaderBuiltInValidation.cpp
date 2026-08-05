#include "ShaderBuiltInValidation.h"

#include "BuiltInShaders.h"
#include "ShaderLibrary.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
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

            int Report(const char* title) const
            {
                if (first_failure_ == 0)
                {
                    std::fprintf(stderr, "%s OK: %d checks passed\n", title, total_);
                    return 0;
                }
                std::fprintf(stderr, "%s FAILED: %d/%d checks failed (first=%d)\n",
                    title, failures_, total_, first_failure_);
                return first_failure_;
            }

        private:
            int next_code_ = 0;
            int first_failure_ = 0;
            int total_ = 0;
            int failures_ = 0;
        };

        // 各組み込みが最低限持っていなければならない property。
        //
        // ここを緩めないこと。
        // フェーズ 5 で MaterialAsset の固定フィールドをこの名前へ
        // 詰め替えるので、1 つ欠けるとその値が移行で落ちる。
        // 落ちても画面には何も出ないため、検査で止めるしかない。
        struct RequiredProperties final
        {
            ShaderID id;
            std::vector<const char*> names;
        };

        std::vector<RequiredProperties> Required()
        {
            return {
                { BuiltInShaders::FbxDefault, { "BaseColor", "BaseMap" } },
                { BuiltInShaders::Pbr, {
                    "BaseColor", "BaseMap", "NormalMap",
                    "Metallic", "Roughness",
                    "Emissive", "EmissiveStrength",
                    "AmbientOcclusion", "AlphaCutoff", "DoubleSided" } },
                { BuiltInShaders::Toon, { "BaseColor", "BaseMap", "RampMap" } },
                { BuiltInShaders::Unlit, { "BaseColor", "BaseMap" } },
                { BuiltInShaders::Pixelate, {
                    "PixelSize", "PixelateStrength", "PixelateOpacity" } },
            };
        }
    }

    int RunShaderBuiltInValidation()
    {
        Checker check(1200);

        const std::filesystem::path root = std::filesystem::current_path();

        // ---- 1. ファイルが置いてある -------------------------------------
        for (const BuiltInShaders::Definition& definition : BuiltInShaders::All())
        {
            const std::filesystem::path path =
                root / "Shader" / definition.relative_path;
            std::error_code error;
            check.Expect(std::filesystem::exists(path, error) && !error,
                std::string("組み込みシェーダのファイルがある: ") +
                definition.relative_path);
        }

        // ---- 2. GUID の決め打ちが崩れていない ------------------------------
        //
        // 「.hlsl 側を書き換えたが C++ 側を直し忘れた」を検出する。
        // 崩れると全マテリアルの参照が切れるので、必ずここで止める。
        check.Expect(BuiltInShaders::All().size() == 5, "組み込みは 5 種");
        check.Expect(BuiltInShaders::FbxDefault.ToString() ==
            "00000000000000000000000000000001", "FbxDefault の GUID が固定値");
        check.Expect(BuiltInShaders::Pbr.ToString() ==
            "00000000000000000000000000000002", "Pbr の GUID が固定値");
        check.Expect(BuiltInShaders::Toon.ToString() ==
            "00000000000000000000000000000003", "Toon の GUID が固定値");
        check.Expect(BuiltInShaders::Unlit.ToString() ==
            "00000000000000000000000000000004", "Unlit の GUID が固定値");
        check.Expect(BuiltInShaders::Pixelate.ToString() ==
            "00000000000000000000000000000005", "Pixelate の GUID が固定値");

        // ---- 3. shading_model の番号 → GUID -------------------------------
        check.Expect(BuiltInShaders::FromShadingModel(0) ==
            BuiltInShaders::FbxDefault, "shading_model 0 は FbxDefault");
        check.Expect(BuiltInShaders::FromShadingModel(1) ==
            BuiltInShaders::Pbr, "shading_model 1 は Pbr");
        check.Expect(BuiltInShaders::FromShadingModel(2) ==
            BuiltInShaders::Toon, "shading_model 2 は Toon");
        check.Expect(BuiltInShaders::FromShadingModel(3) ==
            BuiltInShaders::Unlit, "shading_model 3 は Unlit");
        check.Expect(BuiltInShaders::FromShadingModel(4) ==
            BuiltInShaders::Pixelate, "shading_model 4 は Pixelate");

        // 知らない番号を勝手に丸めない。
        check.Expect(!BuiltInShaders::FromShadingModel(99).IsValid(),
            "知らない shading_model は無効な ID を返す（勝手に丸めない）");
        check.Expect(!BuiltInShaders::FromShadingModel(-1).IsValid(),
            "負の shading_model も無効な ID");

        check.Expect(BuiltInShaders::IsBuiltIn(BuiltInShaders::Toon),
            "組み込みを組み込みと判定する");
        check.Expect(!BuiltInShaders::IsBuiltIn(ShaderID{}),
            "無効な ID を組み込みと判定しない");

        // ---- 4. 走査してコンパイルできる -----------------------------------
        ShaderLibrary library;
        std::vector<std::string> compile_errors;
        library.SetLogSink(
            [&compile_errors](const std::string& severity,
                const std::string& message,
                const std::filesystem::path& file, int line)
            {
                if (severity != "Error") return;
                compile_errors.push_back(
                    file.filename().u8string() + "(" + std::to_string(line) +
                    "): " + message);
            });

        const ShaderLibrary::ScanReport report = library.ScanAll(root);

        check.Expect(report.scanned >= 5, "組み込み 5 種を含めて走査できる");
        check.Expect(report.duplicate_ids == 0,
            "GUID の重複が無い（組み込みと見本がぶつかっていない）");

        if (!compile_errors.empty())
        {
            std::fprintf(stderr, "  --- コンパイルエラー ---\n");
            for (const std::string& text : compile_errors)
            {
                std::fprintf(stderr, "    %s\n", text.c_str());
            }
        }

        // ---- 5. 5 種が Catalog に載り、両方の変種が通る ---------------------
        for (const BuiltInShaders::Definition& definition : BuiltInShaders::All())
        {
            const std::string label = definition.display_name;
            const ShaderCatalog::Entry* entry =
                library.Catalog().Find(definition.id);

            check.Expect(entry != nullptr, label + ": Catalog に載る");
            if (entry == nullptr) continue;

            check.Expect(entry->info.domain == ShaderDomain::Surface,
                label + ": domain が surface");

            // 変種の両方が通ること。
            //
            // 片方だけ通った状態を通過させないこと。
            // Skinned だけ落ちていると、キャラだけ描けない状態になり、
            // しかも「PBR は動いている」ように見えてしまう。
            check.Expect(entry->At(ShaderVariant::Static).compiled,
                label + ": Static 変種がコンパイルできる");
            check.Expect(entry->At(ShaderVariant::Skinned).compiled,
                label + ": Skinned 変種がコンパイルできる");
            check.Expect(entry->AllCompiled(),
                label + ": 使う変種が全部通る");
            check.Expect(entry->ErrorCount() == 0,
                label + ": エラーが 0 件");

            check.Expect(entry->At(ShaderVariant::Static).bytecode != nullptr,
                label + ": Static のバイトコードがある");
            check.Expect(entry->At(ShaderVariant::Skinned).bytecode != nullptr,
                label + ": Skinned のバイトコードがある");

            check.Expect(entry->schema != nullptr, label + ": Schema がある");
        }

        // ---- 6. Schema が期待どおりの property を持つ -----------------------
        for (const RequiredProperties& required : Required())
        {
            const ShaderCatalog::Entry* entry =
                library.Catalog().Find(required.id);
            if (entry == nullptr || !entry->schema) continue;

            const std::string label = entry->info.DisplayName();
            for (const char* name : required.names)
            {
                check.Expect(entry->schema->FindByName(name) != nullptr,
                    label + ": property " + name + " がある");
            }

            // 保存名の規則が ScriptComponent と揃っていること。
            const ShaderProperty* first =
                entry->schema->FindByName(required.names.front());
            if (first != nullptr)
            {
                check.Expect(first->SavedName() ==
                    std::string("prop.") + required.names.front(),
                    label + ": 保存名が prop.<名前> になる");
                check.Expect(entry->schema->FindBySavedName(first->SavedName()) != nullptr,
                    label + ": 保存名で引ける");
            }
        }

        // ---- 7. テクスチャのスロットが既存と衝突しない -----------------------
        //
        // t12 は csm_common の影マップが使っている。
        // ここに材質のテクスチャを重ねると影が壊れるが、
        // エラーは出ず絵だけおかしくなる。数字で止める。
        for (const BuiltInShaders::Definition& definition : BuiltInShaders::All())
        {
            const ShaderCatalog::Entry* entry =
                library.Catalog().Find(definition.id);
            if (entry == nullptr || !entry->schema) continue;

            bool all_safe = true;
            for (const ShaderProperty& property : entry->schema->Properties())
            {
                if (property.kind != ShaderPropertyKind::Texture) continue;
                if (property.texture_slot < 40) all_safe = false;
            }
            check.Expect(all_safe,
                std::string(definition.display_name) +
                ": テクスチャが t40 以降に置かれる（t12 の影マップを侵さない）");
        }

        return check.Report("shader-builtin");
    }
}
