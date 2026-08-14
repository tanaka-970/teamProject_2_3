#include "ShaderAssetValidation.h"

#include "ShaderCatalog.h"
#include "ShaderConstantPacker.h"
#include "ShaderLibrary.h"
#include "ShaderSource.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory>
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

            void Expect(bool condition, const char* what)
            {
                const int code = next_code_++;
                ++total_;
                if (condition) return;
                ++failures_;
                if (first_failure_ == 0) first_failure_ = code;
                std::fprintf(stderr, "  [FAIL %d] %s\n", code, what);
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

        bool WriteText(const std::filesystem::path& path, const std::string& text)
        {
            std::error_code error;
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path(), error);
            }
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream) return false;
            stream << text;
            return static_cast<bool>(stream);
        }

        std::string ReadText(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return std::string();
            return std::string((std::istreambuf_iterator<char>(stream)),
                std::istreambuf_iterator<char>());
        }

        bool Close(float a, float b) noexcept
        {
            const float diff = a - b;
            return (diff < 0.0f ? -diff : diff) < 0.0001f;
        }

        std::string FullSource()
        {
            return
                "#pragma replay_guid     \"8f2c1a4b9d3e4f5a8b7c6d5e4f3a2b1c\"\n"
                "#pragma replay_name     \"Standard Lit\"\n"
                "#pragma replay_category \"Lit/Standard\"\n"
                "#pragma replay_domain   surface\n"
                "\n"
                "#pragma property color   BaseColor   \"基本色\"       = (1, 1, 1, 1)\n"
                "#pragma property texture BaseMap     \"基本色マップ\"  default white\n"
                "#pragma property range   Metallic    \"金属度\"  0..1  = 0.25\n"
                "#pragma property range   Roughness   \"粗さ\"    0..1  = 0.55\n"
                "#pragma property float3  Emissive    \"発光色\"       = (0.1, 0.2, 0.3)\n"
                "#pragma property float2  TileScale   \"タイル\"       = (2, 4)\n"
                "#pragma property float   Height      \"高さ\"         = 1.5\n"
                "#pragma property toggle  DoubleSided \"両面描画\"      = true\n"
                "#pragma property enum    CullMode    \"カリング\" { Off, Front, Back } = Back\n"
                "#pragma property float4  Custom      \"任意\"         = (1, 2, 3, 4)\n"
                "\n"
                "float4 main() : SV_TARGET { return 0; }\n";
        }
    }

    int RunShaderAssetValidation()
    {
        Checker check(950);

        const std::filesystem::path folder =
            std::filesystem::path("Saved") / "Validation" / "Shader";
        std::error_code folder_error;
        std::filesystem::create_directories(folder, folder_error);

        // ---- 1. 全種別の pragma を解析できる -----------------------------
        const std::filesystem::path full_path = folder / "AssetFull.hlsl";
        check.Expect(WriteText(full_path, FullSource()),
            "検証用シェーダを書き出せる");

        bool needs_guid = true;
        const ShaderSource::ParseResult parsed =
            ShaderSource::ParseFile(full_path, needs_guid);

        check.Expect(parsed.succeeded, "解析が成功する");
        check.Expect(!needs_guid, "replay_guid があれば採番不要と判定する");
        check.Expect(parsed.issues.empty(), "正しい書式で issue が出ない");

        const ShaderSourceInfo& info = parsed.info;
        check.Expect(info.id.ToString() == "8f2c1a4b9d3e4f5a8b7c6d5e4f3a2b1c",
            "replay_guid を読める");
        check.Expect(info.name == "Standard Lit", "replay_name を読める");
        check.Expect(info.category == "Lit/Standard", "replay_category を読める");
        check.Expect(info.domain == ShaderDomain::Surface, "replay_domain を読める");
        check.Expect(info.MenuPath() == "Lit/Standard/Standard Lit",
            "MenuPath を組み立てられる");
        check.Expect(info.properties.size() == 10,
            "property を 10 個すべて読める");

        // ---- 2. 各 property の中身 ---------------------------------------
        const auto find = [&info](const char* name) -> const ShaderProperty*
        {
            for (const ShaderProperty& item : info.properties)
            {
                if (item.name == name) return &item;
            }
            return nullptr;
        };

        const ShaderProperty* base_color = find("BaseColor");
        check.Expect(base_color != nullptr, "BaseColor がある");
        check.Expect(base_color && base_color->kind == ShaderPropertyKind::Color,
            "BaseColor が Color 型");
        check.Expect(base_color && base_color->DisplayName() == "基本色",
            "表示名を読める");
        check.Expect(base_color && Close(base_color->default_value.w, 1.0f),
            "Color の既定値を読める");
        check.Expect(base_color && base_color->SavedName() == "prop.BaseColor",
            "保存名が prop.<名前> になる");

        const ShaderProperty* base_map = find("BaseMap");
        check.Expect(base_map && base_map->kind == ShaderPropertyKind::Texture,
            "BaseMap が Texture 型");
        check.Expect(base_map && base_map->default_texture == "white",
            "Texture の既定を読める");

        const ShaderProperty* metallic = find("Metallic");
        check.Expect(metallic && metallic->kind == ShaderPropertyKind::Range,
            "Metallic が Range 型");
        check.Expect(metallic && Close(metallic->minimum, 0.0f) &&
            Close(metallic->maximum, 1.0f), "Range の範囲を読める");
        check.Expect(metallic && Close(metallic->default_value.x, 0.25f),
            "Range の既定値を読める");

        const ShaderProperty* emissive = find("Emissive");
        check.Expect(emissive && emissive->kind == ShaderPropertyKind::Float3,
            "Emissive が Float3 型");
        check.Expect(emissive && Close(emissive->default_value.x, 0.1f) &&
            Close(emissive->default_value.z, 0.3f),
            "Float3 の既定値を読める");

        const ShaderProperty* tile = find("TileScale");
        check.Expect(tile && tile->kind == ShaderPropertyKind::Float2,
            "TileScale が Float2 型");
        check.Expect(tile && Close(tile->default_value.y, 4.0f),
            "Float2 の既定値を読める");

        const ShaderProperty* height = find("Height");
        check.Expect(height && Close(height->default_value.x, 1.5f),
            "Float の既定値を読める");

        const ShaderProperty* double_sided = find("DoubleSided");
        check.Expect(double_sided && double_sided->kind == ShaderPropertyKind::Toggle,
            "DoubleSided が Toggle 型");
        check.Expect(double_sided && Close(double_sided->default_value.x, 1.0f),
            "Toggle の true を 1 として読める");

        const ShaderProperty* cull = find("CullMode");
        check.Expect(cull && cull->kind == ShaderPropertyKind::Enum,
            "CullMode が Enum 型");
        check.Expect(cull && cull->enum_names.size() == 3,
            "Enum の選択肢を 3 個読める");
        check.Expect(cull && cull->enum_names.size() > 2 &&
            cull->enum_names[2] == "Back", "Enum の名前を読める");
        check.Expect(cull && Close(cull->default_value.x, 2.0f),
            "Enum の既定を名前から番号へ解決できる");

        // ---- 3. サイズ表 --------------------------------------------------
        check.Expect(ShaderPropertySize(ShaderPropertyKind::Float) == 4,
            "Float は 4 バイト");
        check.Expect(ShaderPropertySize(ShaderPropertyKind::Float2) == 8,
            "Float2 は 8 バイト");
        check.Expect(ShaderPropertySize(ShaderPropertyKind::Float3) == 12,
            "Float3 は 12 バイト");
        check.Expect(ShaderPropertySize(ShaderPropertyKind::Color) == 16,
            "Color は 16 バイト");
        check.Expect(ShaderPropertySize(ShaderPropertyKind::Texture) == 0,
            "Texture は定数バッファに載らない");

        // ---- 4. GUID の自動採番 ------------------------------------------
        const std::filesystem::path no_guid_path = folder / "AssetNoGuid.hlsl";
        const std::string no_guid_source =
            "#pragma replay_name \"No Guid\"\n"
            "float4 main() : SV_TARGET { return 0; }\n";
        check.Expect(WriteText(no_guid_path, no_guid_source),
            "GUID 無しシェーダを書き出せる");

        bool needs_guid2 = false;
        ShaderSource::ParseFile(no_guid_path, needs_guid2);
        check.Expect(needs_guid2, "GUID が無いことを検出する");

        ShaderID assigned;
        std::string assign_error;
        check.Expect(ShaderSource::AssignGuid(no_guid_path, assigned, assign_error),
            "GUID を採番して書き戻せる");
        check.Expect(assigned.IsValid(), "採番した GUID が有効");
        check.Expect(ReadText(no_guid_path).find("#pragma replay_guid") !=
            std::string::npos, "ファイルへ書き戻されている");
        check.Expect(ReadText(no_guid_path).find("No Guid") != std::string::npos,
            "元の内容が失われていない");

        // 【最重要】再スキャンで GUID が変わらないこと。
        //
        // 変わると、そのシェーダを使っている全マテリアルの参照が切れる。
        bool needs_guid3 = true;
        const ShaderSource::ParseResult reparsed =
            ShaderSource::ParseFile(no_guid_path, needs_guid3);
        check.Expect(!needs_guid3, "再スキャンで採番不要になる");
        check.Expect(reparsed.info.id == assigned,
            "再スキャンしても GUID が変わらない");

        ShaderID second_assign;
        std::string second_error;
        check.Expect(ShaderSource::AssignGuid(no_guid_path, second_assign, second_error),
            "2 回目の AssignGuid も成功する");
        check.Expect(second_assign == assigned,
            "2 回目の AssignGuid で GUID を書き換えない");

        // ---- 5. 壊れた pragma ---------------------------------------------
        {
            const std::string broken =
                "#pragma replay_guid \"これは16進ではない\"\n"
                "#pragma replay_domain いいかげんな値\n"
                "#pragma property ふしぎな型 Foo\n"
                "#pragma property float\n"
                "#pragma property color Good \"良い\" = (1,1,1,1)\n"
                "float4 main() : SV_TARGET { return 0; }\n";
            bool needs = true;
            const ShaderSource::ParseResult result =
                ShaderSource::ParseText(broken, "Broken.hlsl", needs);

            check.Expect(result.succeeded,
                "壊れた pragma があっても解析自体は続行する");
            check.Expect(result.issues.size() >= 3,
                "壊れた pragma が issue として残る");
            check.Expect(result.info.properties.size() == 1,
                "壊れた行を飛ばして正しい property だけ読む");
            check.Expect(needs, "壊れた GUID は未採番として扱う");
        }

        // ---- 6. 名前の重複 -------------------------------------------------
        {
            const std::string duplicated =
                "#pragma property float Same \"1つ目\" = 1\n"
                "#pragma property float Same \"2つ目\" = 2\n"
                "float4 main() : SV_TARGET { return 0; }\n";
            bool needs = true;
            const ShaderSource::ParseResult result =
                ShaderSource::ParseText(duplicated, "Dup.hlsl", needs);
            check.Expect(result.info.properties.size() == 1,
                "同名の property は 1 つだけ採用する");
            check.Expect(!result.issues.empty(),
                "同名の property を issue として残す");
        }

        // ---- 7. domain の判別 ----------------------------------------------
        {
            bool needs = true;
            const ShaderSource::ParseResult layer = ShaderSource::ParseText(
                "#pragma replay_domain layer\nfloat4 main():SV_TARGET{return 0;}\n",
                "L.hlsl", needs);
            check.Expect(layer.info.domain == ShaderDomain::Layer,
                "layer ドメインを読める");

            const ShaderSource::ParseResult post = ShaderSource::ParseText(
                "#pragma replay_domain postprocess\nfloat4 main():SV_TARGET{return 0;}\n",
                "P.hlsl", needs);
            check.Expect(post.info.domain == ShaderDomain::PostProcess,
                "postprocess ドメインを読める");

            const ShaderSource::ParseResult none = ShaderSource::ParseText(
                "float4 main():SV_TARGET{return 0;}\n", "N.hlsl", needs);
            check.Expect(none.info.domain == ShaderDomain::Surface,
                "domain 未指定は surface になる");
        }

        // ---- 8. Schema --------------------------------------------------
        {
            std::vector<ShaderProperty> properties = info.properties;
            // オフセットはフェーズ 3 で入れる。ここでは 0 のまま集計を見る。
            const ShaderPropertySchema schema(info.id, properties, 1);

            check.Expect(schema.TypeID() == info.id, "Schema が ID を保持する");
            check.Expect(schema.Revision() == 1, "Schema が revision を保持する");
            check.Expect(schema.Properties().size() == 10, "Schema が全項目を持つ");
            check.Expect(schema.FindByName("Metallic") != nullptr,
                "名前で引ける");
            check.Expect(schema.FindBySavedName("prop.Metallic") != nullptr,
                "保存名で引ける");
            check.Expect(schema.FindByName("NotExist") == nullptr,
                "無い名前は nullptr");
            check.Expect(schema.TextureCount() == 1,
                "テクスチャ数を数えられる");
        }

        // ---- 9. Catalog ---------------------------------------------------
        {
            ShaderCatalog catalog;
            check.Expect(catalog.Count() == 0, "初期状態は空");

            ShaderCatalog::Entry entry;
            entry.info = info;
            entry.schema = std::make_shared<ShaderPropertySchema>(
                info.id, info.properties, 1);
            entry.At(ShaderVariant::Static).compiled = true;
            catalog.Register(entry);

            check.Expect(catalog.Count() == 1, "登録できる");
            check.Expect(catalog.Find(info.id) != nullptr, "ID で引ける");
            check.Expect(catalog.FindSchema(info.id) != nullptr,
                "Schema を引ける");

            // 同じファイルの再登録は更新であって重複ではない。
            catalog.Register(entry);
            check.Expect(catalog.Count() == 1, "同じ ID の再登録は上書き");
            check.Expect(catalog.DuplicateIdCount() == 0,
                "同じファイルの再登録は重複と数えない");

            // 別ファイルで同じ GUID は重複。コピペの消し忘れ。
            ShaderCatalog::Entry copied = entry;
            copied.info.source_path = "Another.hlsl";
            catalog.Register(copied);
            check.Expect(catalog.DuplicateIdCount() == 1,
                "別ファイルの同一 GUID を重複として数える");

            // 別 ID を足す。
            ShaderCatalog::Entry other;
            other.info.id = ShaderSource::GenerateID();
            other.info.name = "Other";
            other.info.category = "Unlit";
            catalog.Register(other);
            check.Expect(catalog.Count() == 2, "別 ID は別項目になる");

            const std::vector<std::string> paths = catalog.MenuPaths();
            check.Expect(paths.size() == 2, "MenuPath が項目数ぶん出る");
            check.Expect(std::find(paths.begin(), paths.end(), "Unlit/Other") !=
                paths.end(), "MenuPath が category/name になる");

            ShaderID invalid;
            check.Expect(catalog.Find(invalid) == nullptr,
                "無効な ID で引くと nullptr");

            catalog.Clear();
            check.Expect(catalog.Count() == 0, "Clear で空になる");
            check.Expect(catalog.DuplicateIdCount() == 0,
                "Clear で重複数もリセットされる");
        }

        // ---- 10. 定数バッファのパッキング -----------------------------------
        //
        // HLSL の 16 バイト境界規則。ここを間違えると値が 1 つずれ、
        // 絵が「なんとなく変」になるだけでエラーにならない。
        // 原因の特定が非常に難しくなるので、境界跨ぎを重点的に見る。
        {
            const auto make = [](std::initializer_list<ShaderPropertyKind> kinds)
            {
                std::vector<ShaderProperty> properties;
                int index = 0;
                for (ShaderPropertyKind kind : kinds)
                {
                    ShaderProperty property;
                    property.kind = kind;
                    property.name = "P" + std::to_string(index++);
                    properties.push_back(property);
                }
                return properties;
            };

            std::uint32_t size = 0;

            // float4 だけ。素直に 16 の倍数。
            auto only_float4 = make({ ShaderPropertyKind::Float4,
                ShaderPropertyKind::Float4 });
            ShaderConstantPacker::AssignOffsets(only_float4, size);
            check.Expect(size == 32, "float4 x2 は 32 バイト");
            check.Expect(only_float4[1].constant_offset == 16,
                "2 つ目の float4 が 16 から始まる");

            // float3 + float は 16 に収まる。
            auto three_one = make({ ShaderPropertyKind::Float3,
                ShaderPropertyKind::Float });
            ShaderConstantPacker::AssignOffsets(three_one, size);
            check.Expect(size == 16, "float3 + float は 16 バイトに収まる");
            check.Expect(three_one[1].constant_offset == 12,
                "float3 の直後に float が入る");

            // float2 x2 は 16 に収まる。
            auto two_two = make({ ShaderPropertyKind::Float2,
                ShaderPropertyKind::Float2 });
            ShaderConstantPacker::AssignOffsets(two_two, size);
            check.Expect(size == 16, "float2 x2 は 16 バイトに収まる");
            check.Expect(two_two[1].constant_offset == 8,
                "2 つ目の float2 が 8 から始まる");

            // float3 x2 は境界を跨ぐので 32。ここが一番間違えやすい。
            auto three_three = make({ ShaderPropertyKind::Float3,
                ShaderPropertyKind::Float3 });
            ShaderConstantPacker::AssignOffsets(three_three, size);
            check.Expect(size == 32, "float3 x2 は 24 ではなく 32 バイト");
            check.Expect(three_three[1].constant_offset == 16,
                "2 つ目の float3 が次の境界へ送られる");

            // float + float3 は同じ 16 バイトレジスタにちょうど収まる。
            // float が先頭 4 バイト、float3 が残り 12 バイトを使う。
            auto one_three = make({ ShaderPropertyKind::Float,
                ShaderPropertyKind::Float3 });
            ShaderConstantPacker::AssignOffsets(one_three, size);
            check.Expect(size == 16, "float + float3 は 16 バイトに収まる");
            check.Expect(one_three[1].constant_offset == 4,
                "float3 が float の直後から始まる");

            // float + float2 は残り 12 なので入る。
            auto one_two = make({ ShaderPropertyKind::Float,
                ShaderPropertyKind::Float2 });
            ShaderConstantPacker::AssignOffsets(one_two, size);
            check.Expect(size == 16, "float + float2 は 16 バイトに収まる");
            check.Expect(one_two[1].constant_offset == 4,
                "float2 が 4 から始まる");

            // float x3 + float2 は残り 4 なので送られる。
            auto three_ones_two = make({ ShaderPropertyKind::Float,
                ShaderPropertyKind::Float, ShaderPropertyKind::Float,
                ShaderPropertyKind::Float2 });
            ShaderConstantPacker::AssignOffsets(three_ones_two, size);
            check.Expect(three_ones_two[3].constant_offset == 16,
                "残り 4 のとき float2 は次の境界へ送られる");
            check.Expect(size == 32, "float x3 + float2 は 32 バイト");

            // Color は float4 と同じ扱い。
            auto color_float = make({ ShaderPropertyKind::Color,
                ShaderPropertyKind::Float });
            ShaderConstantPacker::AssignOffsets(color_float, size);
            check.Expect(color_float[0].constant_size == 16,
                "Color は 16 バイト");
            check.Expect(color_float[1].constant_offset == 16,
                "Color の次は 16 から");

            // Toggle / Enum / Range は float 1 個ぶん。
            auto small_kinds = make({ ShaderPropertyKind::Toggle,
                ShaderPropertyKind::Enum, ShaderPropertyKind::Range,
                ShaderPropertyKind::Float });
            ShaderConstantPacker::AssignOffsets(small_kinds, size);
            check.Expect(size == 16,
                "Toggle / Enum / Range / Float の 4 個で 16 バイト");

            // テクスチャは定数バッファに載らず、スロットが順に振られる。
            auto with_textures = make({ ShaderPropertyKind::Texture,
                ShaderPropertyKind::Float, ShaderPropertyKind::Texture });
            ShaderConstantPacker::AssignOffsets(with_textures, size);
            check.Expect(size == 16, "テクスチャは定数バッファを消費しない");
            check.Expect(with_textures[0].constant_size == 0,
                "テクスチャの定数サイズは 0");
            check.Expect(with_textures[0].texture_slot ==
                ShaderConstantPacker::material_texture_base_slot,
                "テクスチャのスロットが基準値から始まる");
            check.Expect(with_textures[2].texture_slot ==
                ShaderConstantPacker::material_texture_base_slot + 1,
                "2 枚目のテクスチャが次のスロットになる");
            check.Expect(with_textures[1].constant_offset == 0,
                "テクスチャを挟んでも定数の位置は詰まる");

            // 空でも落ちない。
            std::vector<ShaderProperty> empty;
            ShaderConstantPacker::AssignOffsets(empty, size);
            check.Expect(size == 0, "property が無ければ 0 バイト");
        }

        // ---- 11. 値の詰め込みと読み戻し -------------------------------------
        {
            std::vector<ShaderProperty> properties;
            {
                ShaderProperty color;
                color.kind = ShaderPropertyKind::Color;
                color.name = "Tint";
                color.default_value = DirectX::XMFLOAT4{ 0.1f, 0.2f, 0.3f, 0.4f };
                properties.push_back(color);

                ShaderProperty scalar;
                scalar.kind = ShaderPropertyKind::Float;
                scalar.name = "Power";
                scalar.default_value = DirectX::XMFLOAT4{ 7.5f, 0, 0, 0 };
                properties.push_back(scalar);

                ShaderProperty vector3;
                vector3.kind = ShaderPropertyKind::Float3;
                vector3.name = "Axis";
                vector3.default_value = DirectX::XMFLOAT4{ 1.0f, 2.0f, 3.0f, 0 };
                properties.push_back(vector3);
            }
            std::uint32_t size = 0;
            ShaderConstantPacker::AssignOffsets(properties, size);

            const ShaderPropertySchema schema(
                ShaderSource::GenerateID(), properties, 1);
            check.Expect(schema.ConstantBufferSize() == size,
                "Schema が定数バッファの大きさを引き継ぐ");

            std::vector<std::uint8_t> bytes;
            ShaderConstantPacker::PackDefaults(schema, bytes);
            check.Expect(bytes.size() == schema.ConstantBufferSize(),
                "詰めたバイト数が定数バッファの大きさと一致する");

            const auto read_float = [&bytes](std::uint32_t offset)
            {
                float value = 0.0f;
                if (offset + sizeof(float) <= bytes.size())
                {
                    std::memcpy(&value, bytes.data() + offset, sizeof(float));
                }
                return value;
            };

            check.Expect(Close(read_float(properties[0].constant_offset), 0.1f),
                "Color の既定値が詰まる");
            check.Expect(Close(read_float(properties[0].constant_offset + 12), 0.4f),
                "Color の alpha が詰まる");
            check.Expect(Close(read_float(properties[1].constant_offset), 7.5f),
                "Float の既定値が詰まる");
            check.Expect(Close(read_float(properties[2].constant_offset + 8), 3.0f),
                "Float3 の 3 要素目が詰まる");
        }

        // ---- 12. HLSL 宣言の生成 --------------------------------------------
        {
            std::vector<ShaderProperty> properties;
            {
                ShaderProperty a;
                a.kind = ShaderPropertyKind::Float3;
                a.name = "Alpha";
                properties.push_back(a);

                ShaderProperty b;
                b.kind = ShaderPropertyKind::Float3;
                b.name = "Beta";
                properties.push_back(b);

                ShaderProperty t;
                t.kind = ShaderPropertyKind::Texture;
                t.name = "MainTex";
                properties.push_back(t);
            }
            std::uint32_t size = 0;
            ShaderConstantPacker::AssignOffsets(properties, size);
            const ShaderPropertySchema schema(
                ShaderSource::GenerateID(), properties, 1);

            const std::string hlsl =
                ShaderConstantPacker::GenerateHlslDeclaration(schema);

            check.Expect(hlsl.find("cbuffer REPLAY_MATERIAL_CB : register(b9)") !=
                std::string::npos, "cbuffer を b9 へ置く");
            check.Expect(hlsl.find("float3 Alpha;") != std::string::npos,
                "float3 を宣言する");
            check.Expect(hlsl.find("float3 Beta;") != std::string::npos,
                "2 つ目の float3 を宣言する");
            check.Expect(hlsl.find("_replay_pad") != std::string::npos,
                "境界跨ぎの隙間を埋める");
            check.Expect(hlsl.find("Texture2D MainTex : register(t40);") !=
                std::string::npos, "テクスチャを t40 へ置く");
        }

        // ---- 13. GenerateID -----------------------------------------------
        {
            bool all_unique = true;
            std::vector<std::string> seen;
            for (int index = 0; index < 200; ++index)
            {
                const ShaderID id = ShaderSource::GenerateID();
                if (!id.IsValid()) { all_unique = false; break; }
                const std::string text = id.ToString();
                if (std::find(seen.begin(), seen.end(), text) != seen.end())
                {
                    all_unique = false;
                    break;
                }
                seen.push_back(text);
            }
            check.Expect(all_unique, "200 回連続で一意な ID を作れる");
        }

        // ---- 14. 走査から D3DCompile まで通す --------------------------------
        //
        // ここが今回の本題。
        // 「#pragma property を書くと、それが cbuffer になって、
        //   HLSL 側からその名前で参照できる」ことを機械で確かめる。
        //
        // 目で見て確かめる作りにすると、確かめない日が必ず来る。
        {
            const std::filesystem::path root = folder / "Lib";
            const std::filesystem::path materials = root / "Shader" / "Materials";
            std::error_code clean_error;
            std::filesystem::remove_all(root, clean_error);

            const std::filesystem::path good = materials / "LibGood.hlsl";
            const std::filesystem::path bare = materials / "LibBare.hlsl";

            // 宣言した名前を本体から参照する。
            //
            // これが要点。cbuffer の自動生成が効いていなければ
            // "undeclared identifier 'LibTint'" で落ちる。
            // つまりこの 1 枚が通ることが、生成が効いている証拠になる。
            check.Expect(WriteText(good,
                "#pragma replay_guid     \"00000000000000000000000000009001\"\n"
                "#pragma replay_name     \"Lib Good\"\n"
                "#pragma replay_domain   surface\n"
                "#pragma property color LibTint  \"色\"   = (1, 1, 1, 1)\n"
                "#pragma property range LibPower \"強さ\" 0..4 = 1\n"
                "float4 main() : SV_TARGET { return LibTint * LibPower; }\n"),
                "検証用シェーダを書ける");

            // property が 1 つも無い場合。空 cbuffer で落ちないこと。
            check.Expect(WriteText(bare,
                "#pragma replay_guid     \"00000000000000000000000000009002\"\n"
                "#pragma replay_name     \"Lib Bare\"\n"
                "#pragma replay_domain   surface\n"
                "float4 main() : SV_TARGET { return 1; }\n"),
                "property の無いシェーダを書ける");

            ShaderLibrary library;
            const ShaderLibrary::ScanReport report = library.ScanAll(root);

            check.Expect(report.scanned == 2, "2 枚見つける");
            check.Expect(report.registered == 2, "2 枚とも登録する");
            check.Expect(report.compiled == 2,
                "宣言した名前を本体から参照しても通る（cbuffer 自動生成が効いている）");
            check.Expect(report.compile_failed == 0, "失敗が 0 件");

            const ShaderID good_id =
                Reflection::MakeTypeGUID("00000000000000000000000000009001");
            const ShaderCatalog::Entry* entry = library.Catalog().Find(good_id);
            check.Expect(entry != nullptr, "コンパイル後も ID で引ける");

            if (entry != nullptr)
            {
                check.Expect(entry->AllCompiled(), "使う変種が全部通る");
                check.Expect(entry->EverCompiled(), "ever_compiled が立つ");
                check.Expect(entry->At(ShaderVariant::Static).bytecode != nullptr,
                    "Static のバイトコードが入る");
                check.Expect(entry->At(ShaderVariant::Skinned).bytecode != nullptr,
                    "surface は Skinned もコンパイルされる");
                check.Expect(entry->ErrorCount() == 0, "エラーが 0 件");
            }

            // ---- 壊しても直前のバイトコードを捨てない --------------------
            //
            // ここを守らないと、構文エラーを 1 文字書いた瞬間に
            // 絵が消える。編集中は常に壊れているので、
            // 「壊れている間は前のもので描き続ける」が要る。
            // 参照を握ったまま比べる。
            //
            // 握らずにアドレスだけ覚えると、解放されたあと同じ番地が
            // 再利用されて「別物なのに同じ」に見えることがある。
            // 検査がたまに通ってしまう作りにしない。
            Microsoft::WRL::ComPtr<ID3DBlob> previous_blob;
            if (entry != nullptr)
            {
                previous_blob = entry->At(ShaderVariant::Static).bytecode;
            }
            const void* previous = previous_blob
                ? previous_blob->GetBufferPointer() : nullptr;

            check.Expect(WriteText(good,
                "#pragma replay_guid     \"00000000000000000000000000009001\"\n"
                "#pragma replay_name     \"Lib Good\"\n"
                "#pragma replay_domain   surface\n"
                "#pragma property color LibTint \"色\" = (1, 1, 1, 1)\n"
                "float4 main() : SV_TARGET { return NotDeclaredAtAll; }\n"),
                "壊したソースを書ける");

            const bool broke = library.CompileOne(good_id, false);
            check.Expect(!broke, "壊れたソースはコンパイルに失敗する");

            const ShaderCatalog::Entry* after = library.Catalog().Find(good_id);
            check.Expect(after != nullptr, "失敗しても Entry を消さない");
            if (after != nullptr)
            {
                const ShaderCatalog::VariantResult& still =
                    after->At(ShaderVariant::Static);
                check.Expect(!still.compiled, "失敗したら compiled は下りる");
                check.Expect(still.ever_compiled,
                    "一度成功していれば ever_compiled は立ったまま");
                check.Expect(still.bytecode != nullptr,
                    "失敗してもバイトコードを捨てない");
                check.Expect(still.bytecode &&
                    still.bytecode->GetBufferPointer() == previous,
                    "失敗時のバイトコードは直前に成功したものと同一");
                check.Expect(after->schema != nullptr,
                    "失敗しても Schema を捨てない");
                check.Expect(after->ErrorCount() != 0, "エラーが記録される");

                // 行番号が元ソースのものであること。
                //
                // 自動生成した cbuffer を先頭へ差し込んでいるので、
                // #line で戻していなければ 10 行以上ずれる。
                // ずれると「エラー行をクリックしたら別の行が開く」。
                const ShaderDiagnostic* first = after->FirstError();
                check.Expect(first != nullptr, "最初のエラーを取れる");
                if (first != nullptr)
                {
                    check.Expect(first->line == 5,
                        "行番号が元ソースの 5 行目を指す（#line で戻している）");
                    check.Expect(first->file.filename() == good.filename(),
                        "ファイル名が元ソースを指す");
                }
            }

            // ---- 直せば戻る ------------------------------------------------
            check.Expect(WriteText(good,
                "#pragma replay_guid     \"00000000000000000000000000009001\"\n"
                "#pragma replay_name     \"Lib Good\"\n"
                "#pragma replay_domain   surface\n"
                "#pragma property color LibTint  \"色\"   = (1, 1, 1, 1)\n"
                "#pragma property range LibPower \"強さ\" 0..4 = 1\n"
                "#pragma property float3 LibAdded \"追加\" = (0, 0, 0)\n"
                "float4 main() : SV_TARGET\n"
                "{ return LibTint * LibPower + float4(LibAdded, 0); }\n"),
                "直したソースを書ける");

            // 更新時刻を確実に進める。
            // 同じ秒に書くと保存検出が拾えないことがある。
            {
                std::error_code touch_error;
                std::filesystem::last_write_time(good,
                    std::filesystem::file_time_type::clock::now() +
                    std::chrono::seconds(2), touch_error);
            }

            const std::size_t recompiled = library.PollSourceChanges(false);
            check.Expect(recompiled >= 1, "保存を検出して再コンパイルする");

            const ShaderCatalog::Entry* fixed = library.Catalog().Find(good_id);
            check.Expect(fixed != nullptr, "直したあとも ID で引ける");
            if (fixed != nullptr)
            {
                check.Expect(fixed->AllCompiled(), "直せばコンパイルが通る");
                check.Expect(fixed->ErrorCount() == 0, "エラーが消える");
                check.Expect(fixed->schema != nullptr, "Schema が付いている");

                if (fixed->schema)
                {
                    // 足した property が Schema に出ること。
                    // ここが「.hlsl に 1 行足すと Inspector の欄が増える」の実体。
                    check.Expect(fixed->schema->Properties().size() == 3,
                        "保存し直すと足した property が増える");
                    check.Expect(
                        fixed->schema->FindByName("LibAdded") != nullptr,
                        "足した property を名前で引ける");
                    check.Expect(fixed->schema->Revision() >= 2,
                        "宣言が変わったら Revision が上がる");
                }

                const ShaderCatalog::VariantResult& renewed =
                    fixed->At(ShaderVariant::Static);
                check.Expect(renewed.bytecode != nullptr &&
                    renewed.bytecode->GetBufferPointer() != previous,
                    "直したら新しいバイトコードに差し替わる");
            }

            // 何も触っていなければ再コンパイルしない。
            // 毎フレーム全部コンパイルし直すと編集どころではなくなる。
            check.Expect(library.PollSourceChanges(false) == 0,
                "変更が無ければ再コンパイルしない");

            std::filesystem::remove_all(root, clean_error);
        }

        return check.Report("shader-asset");
    }
}
