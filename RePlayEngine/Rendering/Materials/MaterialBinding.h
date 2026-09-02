#pragma once

#include "MaterialAsset.h"
#include "../Shaders/ShaderAsset.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ReplayEngine::Rendering
{
    class ShaderCatalog;

    // 1 つの #pragma property texture を、描画時に解決するための情報。
    // GPU リソースは持たず、AssetGUID と既定テクスチャ名だけを運ぶ。
    struct ResolvedMaterialTexture final
    {
        std::string property_name;  // "BaseMap"
        std::uint32_t slot = 0;     // t40 以降
        std::string asset_guid;     // 空なら default_texture を使う
        std::string default_texture; // white / black / gray / bump

        bool UsesDefault() const noexcept { return asset_guid.empty(); }
    };

    // MaterialAsset と ShaderCatalog から作る、1 draw 分の CPU 側バインド情報。
    //
    // RenderItem にコピーしても安全なものだけを所有する。
    // ShaderPropertySchema は shared_ptr で保持し、ホットリロードで Catalog 側の
    // Schema が差し替わっても描画中の参照がぶら下がらないようにする。
    struct ResolvedMaterialBinding final
    {
        ShaderID requested_shader; // Material に保存されていた GUID
        ShaderID shader;           // 実際に描画へ使う GUID。Missing 時は Unlit fallback
        ShaderVariant variant = ShaderVariant::Static;
        ShaderPropertySchemaRef schema;

        std::vector<std::uint8_t> constants;
        std::vector<ResolvedMaterialTexture> textures;

        // MaterialAsset が所有する。RenderItem の寿命は 1 frame で、Material cache は
        // frame 中に破棄しないため借用でよい。配列を毎 draw コピーしない。
        const ShaderLayerStack* layers = nullptr;

        ShaderLightingModel lighting_model = ShaderLightingModel::Pbr;
        bool missing_shader = false;
        bool usable_shader = false;
        std::string diagnostic;

        bool HasConstants() const noexcept { return !constants.empty(); }
        bool HasTextures() const noexcept { return !textures.empty(); }

        // GBuffer の互換ブリッジで使う固定 semantic mask。
        // Schema のスロット番号ではなくプロパティ名で判定するため、
        // Toon の t41(RampMap) を NormalMap と誤認しない。
        enum TextureSemantic : std::uint32_t
        {
            BaseMapSemantic      = 1u << 0,
            NormalMapSemantic    = 1u << 1,
            MetallicMapSemantic  = 1u << 2,
            RoughnessMapSemantic = 1u << 3,
            EmissiveMapSemantic  = 1u << 4,
            OcclusionMapSemantic = 1u << 5,
            // 1u << 6 は glTF の packed ORM が描画側で使用中。
            RampMapSemantic      = 1u << 7,
        };

        std::uint32_t TextureSemanticMask() const noexcept;

        // Deferred GBufferの固定bridge slot。Shader propertyの宣言順とは独立。
        static bool TryGetGBufferBridgeSlot(const std::string& property_name,
            std::uint32_t& out_slot) noexcept;
    };

    class MaterialBindingResolver final
    {
    public:
        // Material を Catalog の surface shader へ解決する。
        //
        // 成功の意味:
        //   ・要求された Shader が使える、または
        //   ・Missing Shader を Unlit + magenta へ安全にフォールバックできる
        //
        // false は fallback さえ作れず、旧描画経路へ戻す必要がある場合だけ。
        static bool Resolve(const MaterialAsset& material,
            const ShaderCatalog& catalog, ShaderVariant variant,
            ResolvedMaterialBinding& out);

        // PropertyBag の値を ShaderConstantPacker 用 float4 へ変換する。
        // Validation からも使うため公開している。
        static bool LookupConstant(const std::string& saved_name,
            DirectX::XMFLOAT4& out, void* user);

    private:
        static bool ResolveEntry(const MaterialAsset& material,
            const ShaderCatalog& catalog, ShaderID requested, ShaderVariant variant,
            ResolvedMaterialBinding& out);
        static void BuildTextures(const MaterialAsset& material,
            const ShaderPropertySchema& schema,
            std::vector<ResolvedMaterialTexture>& out);
        static void MakeMissingFallback(const MaterialAsset& material,
            const ShaderCatalog& catalog, ShaderVariant variant,
            ShaderID requested, const std::string& reason,
            ResolvedMaterialBinding& out);
    };
}
