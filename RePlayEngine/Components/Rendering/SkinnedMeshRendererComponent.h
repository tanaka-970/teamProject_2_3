#pragma once

#include "MaterialOverrideDynamicProperties.h"
#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/PropertyDesc.h"
#include "../../Rendering/Adapter/IRenderSubmitter.h"

#include <DirectXMath.h>

#include <string>
#include <vector>

namespace ReplayEngine::Components
{
    // スキンメッシュの描画情報を提出する。
    //
    // MeshRendererComponent と分けた理由:
    //   静的メッシュには不要な「アニメーションクリップと再生時刻の提出」と
    //   「FBX 座標系の補正・見た目の姿勢補正」を持つため。
    //   1 つのクラスへ詰めると、静的メッシュ側に使わない設定が並んで責任が曖昧になる。
    //
    // 持たないもの:
    //   入力・移動・HP・当たり判定・GPU リソース・Player 固有ロジック。
    //
    // 見た目の姿勢補正について:
    //   旧 Player は position/rotation とは別に
    //   visual_pitch(90) / visual_yaw_offset(188.5) / visual_roll(180) を持ち、
    //   FBX の基準姿勢を正立させていた。
    //   これは論理的な向きではなく「このモデルの描き方」なので、
    //   GameObject の Transform ではなく描画 Component 側の設定として持つ。
    class SkinnedMeshRendererComponent final
        : public Core::Component
        , public Rendering::IRenderSubmitter
    {
        REPLAY_COMPONENT_BODY(SkinnedMeshRendererComponent)

    public:
        SkinnedMeshRendererComponent() = default;

        // IRenderSubmitter
        bool BuildRenderItem(const Core::GameObject& owner,
            Rendering::RenderItem& out) const override;
        const std::vector<Reflection::PropertyDesc>* DynamicProperties()
            const noexcept override;
        void OnMotionPropertyApplied(const char* property_name) override;
        void PrepareMaterialMotion(const Rendering::MaterialAsset* material,
            const Rendering::ShaderPropertySchema* schema);

        // 描画すべきか。Asset 未指定・非表示・無効ならいずれも false。
        bool ShouldRender() const noexcept
        {
            return visible && !mesh_asset.empty() && ActiveInHierarchy();
        }

        // ---- 保存される設定 -------------------------------------------------

        // AssetDatabase の GUID。空なら描画しない（クラッシュさせない）。
        std::string mesh_asset;

        std::string material_asset;
        bool material_override = false;

        // Motion の Material Track 用一時値。Scene/Prefab の正本にはしない。
        mutable MaterialMotionOverrideState material_motion_state;
        mutable std::vector<Reflection::PropertyDesc> material_dynamic_properties_cache;

        DirectX::XMFLOAT4 tint{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 material_base_color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float material_metallic = 0.0f;
        float material_roughness = 0.55f;
        float material_ambient_occlusion = 1.0f;
        DirectX::XMFLOAT3 material_emissive_color{ 0.0f, 0.0f, 0.0f };
        float material_emissive_strength = 0.0f;
        bool material_double_sided = false;

        // 0=FBX標準 / 1=PBR / 2=トゥーン / 3=アンリット
        int shading_model = 1;

        bool outline = false;
        bool cast_shadow = true;
        bool receive_shadow = true;
        // Material Asset を使わない形式でも影をアルファで抜くか。既定は抜かない。
        bool shadow_alpha_clip = false;
        float shadow_alpha_cutoff = 0.5f;
        // Screen Effect Stack の Rendering Layer mask。0..31。
        int rendering_layer = 0;
        bool visible = true;

        // ---- モデル座標系の補正 --------------------------------------------
        //
        // これは「このモデルをどう描くか」の設定であり、GameObject の論理的な
        // 位置・向き・大きさとは別物。Player 固有の処理としてハードコードしない。
        //
        // 旧 Player は position/angle とは別に visual_pitch(90) / yaw(188.5) / roll(180)
        // を持ち、さらに scale 0.01 を GameObject 側の縮尺として使っていた。
        // 縮尺をここへ移すことで、GameObject の Scale は 1.0 のまま扱える
        // （Collider の半径やギズモが直感的な単位になる）。

        // 姿勢補正（度）。旧 Player の既定値を引き継ぐ。
        DirectX::XMFLOAT3 visual_rotation_offset{ 90.0f, 188.5f, 180.0f };

        // モデル座標系での位置ずらし。原点がモデルの足元でない場合に使う。
        DirectX::XMFLOAT3 local_position_offset{ 0.0f, 0.0f, 0.0f };

        // GameObject の Scale へ掛ける倍率。
        // 旧 Player の 0.01 相当をここへ持たせると、GameObject 側は 1.0 で済む。
        DirectX::XMFLOAT3 local_scale_multiplier{ 1.0f, 1.0f, 1.0f };

        // FBX の座標系補正を掛けるか。
        // 既存 Renderer の fbx_coordinate_transform と同じ行列を提出前に適用する。
        bool apply_fbx_coordinate_transform = true;
    };
}
