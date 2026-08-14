#pragma once

#include "../../Core/ObjectID/ObjectID.h"
#include "../../Reflection/Property/PropertyBag.h"
#include "../Shaders/ShaderAsset.h"
#include "../Materials/MaterialBinding.h"

#include <DirectXMath.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ReplayEngine::Rendering
{
    // Scene が「これを描いてほしい」と提出する 1 件ぶんの情報。
    //
    // ここに GPU リソースは一切入らない。
    //   保持するのは Asset の GUID とワールド行列と見た目のパラメータだけ。
    //   実際の Mesh / Texture / Shader の解決と描画は、
    //   既存のレンダラーがメインスレッド上で行う。
    //
    // この分離により、GameObject や Gameplay Component が
    // ID3D11DeviceContext や Shader へ直接触る構造にならない。
    struct RenderItem
    {
        // どの GameObject から出たか。ピッキングやデバッグ表示に使う。
        Core::ObjectID owner;

        // AssetDatabase の GUID。空なら描画しない。
        std::string mesh_asset;

        // MaterialAssetのAssetGUID。空ならRendererのプロパティだけを使う。
        std::string material_asset;

        // 旧 Scene 互換。Material が割り当てられているときは Shader と値を
        // MaterialAsset から解決し、この値は追加 tint の有無だけに使う。
        bool material_override = false;

        // Motion がこのフレームだけ Material に重ねる値。
        // Material Asset 本体を書き換えないため、描画提出にだけ運ぶ。
        std::uint32_t material_motion_fixed_mask = 0;
        Reflection::PropertyBag material_motion_properties;

        DirectX::XMFLOAT4 override_material_base_color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float override_material_metallic = 0.0f;
        float override_material_roughness = 0.55f;
        float override_material_ambient_occlusion = 1.0f;
        DirectX::XMFLOAT3 override_material_emissive_color{ 0.0f, 0.0f, 0.0f };
        float override_material_emissive_strength = 0.0f;
        bool override_material_double_sided = false;

        // ワールド行列。親子階層を合成済みの最終値。
        DirectX::XMFLOAT4X4 world{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f };

        // Catalog shader 使用時に頂点色へ掛ける追加 tint。
        DirectX::XMFLOAT4 tint{ 1.0f, 1.0f, 1.0f, 1.0f };

        // Catalog shader を作れなかった場合の旧 .cso fallback 用 tint。
        DirectX::XMFLOAT4 legacy_tint{ 1.0f, 1.0f, 1.0f, 1.0f };

        // 旧 Scene / Material の移行用番号。描画の主経路は material_binding.shader。
        // Material が無い Object だけが互換 fallback として使う。
        int shading_model = 1;

        // MaterialAsset -> ShaderCatalog -> PropertyBag を解決した結果。
        // GPU リソースは持たない。
        ResolvedMaterialBinding material_binding;

        // 遅延照明だけは ShaderAsset の #pragma replay_lighting から解決する。
        // ShaderID と旧 shading_model の番号を GBuffer へ直接書かない。
        ShaderLightingModel lighting_model = ShaderLightingModel::Pbr;

        bool outline = false;
        bool cast_shadow = true;
        bool receive_shadow = true;

        // Material Assetから解決され、GBuffer材質定数へ渡す値。
        DirectX::XMFLOAT4 material_base_color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float metallic = 0.0f;
        float roughness = 0.55f;
        float ambient_occlusion = 1.0f;
        DirectX::XMFLOAT3 emissive_color{ 0.0f, 0.0f, 0.0f };
        float emissive_strength = 0.0f;
        bool double_sided = false;

        // Deferred互換ブリッジ。Pixelateは照明モデルではなく追加設定。
        bool pixelate_enabled = false;
        float pixelate_size = 6.0f;
        float pixelate_strength = 1.0f;

        // ---- スキンメッシュ用 ----------------------------------------------
        //
        // AnimatorComponent が決めたクリップと再生位置。
        // 実際のキーフレーム抽出とボーン行列の計算は既存 Renderer が行う。
        // ここは「どのクリップを、どの時刻で描くか」という指示だけを運ぶ。

        // true ならスキンメッシュとして扱う。false は静的メッシュ。
        bool skinned = false;

        // 再生するクリップ番号。-1 なら Renderer 側の現在値を維持する。
        int clip_index = -1;

        // クリップ先頭からの経過秒。
        float animation_time = 0.0f;

        // false なら時間を進めず、その姿勢で止める。
        bool animation_playing = true;

        // data-driven Animator の遷移元。-1 ならブレンドしない。
        int previous_clip_index = -1;
        float previous_animation_time = 0.0f;
        float animation_blend_factor = 1.0f;
        bool animation_loop = true;
        bool previous_animation_loop = true;
    };

    // 1 フレーム分の描画提出リスト。
    //
    // 毎フレーム Clear() してから作り直す。
    // vector を使い回すので確保の回数は増えない。
    class RenderItemList final
    {
    public:
        void Clear() noexcept { items_.clear(); }
        void Add(RenderItem item) { items_.push_back(std::move(item)); }

        bool Empty() const noexcept { return items_.empty(); }
        std::size_t Size() const noexcept { return items_.size(); }

        const std::vector<RenderItem>& Items() const noexcept { return items_; }

        std::vector<RenderItem>::const_iterator begin() const noexcept { return items_.begin(); }
        std::vector<RenderItem>::const_iterator end() const noexcept { return items_.end(); }

    private:
        std::vector<RenderItem> items_;
    };
}
