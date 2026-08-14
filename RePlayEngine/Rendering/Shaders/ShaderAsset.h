#pragma once

#include "../../Reflection/Registry/TypeGUID.h"

#include <DirectXMath.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ReplayEngine::Rendering
{
    // シェーダの永続 ID。
    //
    // TypeGUID をそのまま使う。別の型を作らない理由:
    //   ・保存形式（32 文字 16 進）を揃えられる
    //   ・Editor の GUID 表示・コピー処理を使い回せる
    //   ・C# の [ReplayGuid] と同じ考え方だとチームへ説明しやすい
    using ShaderID = Reflection::TypeGUID;

    // シェーダの用途。走査先フォルダと、書くべき関数が変わる。
    enum class ShaderDomain : std::int32_t
    {
        // 個々のメッシュを描く。ReplaySurface を書く。
        Surface = 0,

        // マテリアルの層として重ねる。ReplayLayer を書く。
        Layer = 1,

        // 画面全体へ後からかける。ReplayPostProcess を書く。
        PostProcess = 2,
    };

    const char* ToString(ShaderDomain domain) noexcept;
    bool TryParseShaderDomain(std::string_view text, ShaderDomain& out) noexcept;

    // 遅延描画で使う照明式。
    //
    // ShaderID や旧 shading_model の番号とは別物。
    // GBuffer に保存するのは「どのシェーダか」ではなく、
    // 最終照明を PBR / Toon / Unlit のどれで評価するかだけ。
    //
    // 数値は HLSL の REPLAY_LIGHTING_* と必ず一致させること。
    enum class ShaderLightingModel : std::int32_t
    {
        Pbr = 0,
        Toon = 1,
        Unlit = 2,
    };

    inline constexpr std::uint32_t shader_lighting_model_count = 3;

    const char* ToString(ShaderLightingModel model) noexcept;
    bool TryParseShaderLightingModel(std::string_view text,
        ShaderLightingModel& out) noexcept;

    // 同じシェーダを、頂点の入り方の違いで 2 通りコンパイルする。
    //
    // 【なぜ必要か】
    //   このエンジンは静的メッシュとスキンメッシュで VS_OUT の中身が違う。
    //     static  … 接線を持たない。法線から擬似接線を作る
    //     skinned … world_tangent を持つ
    //   そのため今も static_mesh_pbr_ps.hlsl と skinned_mesh_pbr_ps.hlsl が
    //   別ファイルで存在する。中身はほぼ同じで、接線の作り方だけが違う。
    //
    //   マテリアル側から見れば「PBR」は 1 つであるべきで、
    //   付ける相手がスキンかどうかを人が選ぶのはおかしい。
    //   だから 1 枚の .hlsl を 2 通りにコンパイルして、
    //   描画時にメッシュの種類で選ぶ。
    //
    // 【なぜ 2 つに固定するのか】
    //   Unity の multi_compile のようにキーワードを自由に足せるようにすると、
    //   組み合わせが指数で増えてコンパイル時間とメモリが破綻する。
    //   ここは「増やせない軸」として固定する。
    //   増やしたくなったら、そのときに設計し直すこと。安易に足さない。
    enum class ShaderVariant : std::int32_t
    {
        Static = 0,
        Skinned = 1,
    };

    inline constexpr int shader_variant_count = 2;

    const char* ToString(ShaderVariant variant) noexcept;

    // Shader-owned additional pass. Material Layer とは別物。
    // Layer はユーザーが順番を変えるが、Pass は Shader 作者が宣言した順に固定する。
    enum class ShaderPassBlend : std::int32_t
    {
        Inherit = 0,
        Alpha,
        Additive,
        Multiply,
    };

    const char* ToString(ShaderPassBlend blend) noexcept;
    bool TryParseShaderPassBlend(std::string_view text, ShaderPassBlend& out) noexcept;

    struct ShaderPassInfo final
    {
        std::string name;
        std::string entry_point;
        ShaderPassBlend blend = ShaderPassBlend::Inherit;
    };

    // HLSL へ渡す define 名。#if REPLAY_SKINNED で分岐する。
    inline constexpr const char* shader_variant_define = "REPLAY_SKINNED";

    // このドメインがその変種を使うか。
    //
    // surface と layer はメッシュの Pixel Shader 入力を受けるため 2 通り要る。
    // postprocess だけは画面全体にかけ、頂点の入り方に依存しないので Static のみ。
    constexpr bool ShaderDomainUsesVariant(ShaderDomain domain,
        ShaderVariant variant) noexcept
    {
        if (variant == ShaderVariant::Static) return true;
        return domain == ShaderDomain::Surface || domain == ShaderDomain::Layer;
    }

    // Inspector に出す 1 項目の種別。
    //
    // Unity の Properties ブロックにある型と対応させてある。
    //   Color   -> Color
    //   Range   -> Range(min, max)
    //   Texture -> 2D
    //   Toggle  -> [Toggle] Float
    //   Enum    -> [Enum(...)] Float
    enum class ShaderPropertyKind : std::int32_t
    {
        Float = 0,
        Range,
        Float2,
        Float3,
        Float4,
        Color,
        Texture,
        Toggle,
        Enum,
    };

    const char* ToString(ShaderPropertyKind kind) noexcept;
    bool TryParseShaderPropertyKind(std::string_view text,
        ShaderPropertyKind& out) noexcept;

    // 定数バッファに載る大きさ（バイト）。Texture は 0。
    std::uint32_t ShaderPropertySize(ShaderPropertyKind kind) noexcept;

    // #pragma property から作られる 1 項目。
    struct ShaderProperty final
    {
        // HLSL の変数名。"BaseColor"
        std::string name;

        // Inspector の表示名。"基本色"。空なら name を使う。
        std::string display_name;

        // Inspector の見出し。空なら "Shader Properties" へ入る。
        // Shader 固有 UI を C++ へ書かず、宣言側だけで整理できるようにする。
        std::string category;

        // 項目にマウスを重ねたときの説明。空なら何も出さない。
        std::string tooltip;

        ShaderPropertyKind kind = ShaderPropertyKind::Float;

        // Range のときだけ意味を持つ。
        float minimum = 0.0f;
        float maximum = 1.0f;

        DirectX::XMFLOAT4 default_value{ 0.0f, 0.0f, 0.0f, 0.0f };

        // Texture のときの既定。"white" / "black" / "gray" / "bump"
        std::string default_texture;

        // Enum の選択肢。
        std::vector<std::string> enum_names;

        // ---- ShaderConstantPacker が埋める ----
        std::uint32_t constant_offset = 0;
        std::uint32_t constant_size = 0;

        // Texture のときの t レジスタ番号。
        std::uint32_t texture_slot = 0;

        // Inspector 表示名。display_name が空なら name。
        const std::string& DisplayName() const noexcept
        {
            return display_name.empty() ? name : display_name;
        }

        // 保存名。"prop.BaseColor"
        //
        // ScriptComponent の "field.Speed" と同じ規則にしてある。
        // 規則を揃えておくと、PropertyBag の扱いと Inspector の
        // 描画経路をそのまま流用できる。
        std::string SavedName() const;
    };

    // 1 枚のシェーダが公開するプロパティの並び。
    //
    // ScriptFieldSchema と同じ役割・同じ形にしてある。
    // Inspector の DynamicProperties() 経路をそのまま使えるようにするため。
    // ここで独自の描画経路を作らないこと。
    class ShaderPropertySchema final
    {
    public:
        ShaderPropertySchema() = default;
        ShaderPropertySchema(ShaderID id, std::vector<ShaderProperty> properties,
            std::uint32_t revision);

        ShaderID TypeID() const noexcept { return id_; }
        std::uint32_t Revision() const noexcept { return revision_; }

        const std::vector<ShaderProperty>& Properties() const noexcept
        {
            return properties_;
        }

        // "prop.BaseColor" で引く。
        const ShaderProperty* FindBySavedName(const std::string& saved) const noexcept;

        // "BaseColor" で引く。
        const ShaderProperty* FindByName(const std::string& name) const noexcept;

        std::uint32_t ConstantBufferSize() const noexcept
        {
            return constant_buffer_size_;
        }

        std::uint32_t TextureCount() const noexcept { return texture_count_; }

        bool Empty() const noexcept { return properties_.empty(); }

    private:
        ShaderID id_;
        std::uint32_t revision_ = 0;
        std::vector<ShaderProperty> properties_;
        std::uint32_t constant_buffer_size_ = 0;
        std::uint32_t texture_count_ = 0;
    };

    using ShaderPropertySchemaRef = std::shared_ptr<const ShaderPropertySchema>;

    // .hlsl 1 枚から読み取ったメタデータ。
    //
    // バイトコードは持たない。ここは「何が書いてあるか」だけ。
    // 実際のコンパイル結果は ShaderProgram が持つ。
    struct ShaderSourceInfo final
    {
        std::filesystem::path source_path;

        ShaderID id;
        std::string name;          // "Standard Lit"
        std::string category;      // "Lit/Standard"
        ShaderDomain domain = ShaderDomain::Surface;

        // #pragma replay_lighting。未指定は PBR。
        // 不明な名前を読んだときは lighting_model_valid=false になり、
        // ShaderLibrary は Catalog へ登録しない。勝手に PBR へ丸めない。
        ShaderLightingModel lighting_model = ShaderLightingModel::Pbr;
        bool lighting_model_valid = true;

        std::vector<ShaderProperty> properties;

        // #pragma replay_pass "Glow" GlowPass additive
        // 宣言順が描画順。Material 側から並べ替えない。
        std::vector<ShaderPassInfo> passes;

        // Inspector のドロップダウンに出す表示名。
        // category が空なら name だけ、あれば "category/name"。
        std::string MenuPath() const;

        // 表示用。name が空ならファイル名を使う。
        std::string DisplayName() const;
    };
}
