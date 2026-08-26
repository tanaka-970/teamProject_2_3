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
    // 描画したいメッシュと見た目の設定を保持する Component。
    //
    // 重要な制約:
    //   このクラスは Direct3D に一切触れない。
    //   GPU API オブジェクトを持たず、Draw も Map も定数バッファ更新も行わない。
    //   保持するのは「どの Asset を」「どんな見た目で」描くかという情報だけで、
    //   実際の GPU 操作は既存の Renderer がメインスレッド上で行う。
    //
    //   Scene から描画対象を集める処理は SceneRenderCollector が担当し、
    //   そこで RenderItem の一覧を作って既存の描画パスへ渡す。
    //   この分離により、GameObject や Gameplay Component が
    //   Shader や RenderState を直接いじる構造にならないようにしている。
    //
    // Asset の持ち方:
    //   GPU リソースのポインタではなく AssetDatabase の GUID を保持する。
    //   Scene ファイルへ保存されるのもこの GUID で、
    //   読み込み時に AssetDatabase から実体を引き直す。
    class MeshRendererComponent final
        : public Core::Component
        , public Rendering::IRenderSubmitter
    {
        REPLAY_COMPONENT_BODY(MeshRendererComponent)

    public:
        MeshRendererComponent() = default;

        // IRenderSubmitter。GameObject のワールド行列をそのまま提出する。
        // 見た目の姿勢補正やアニメーションは扱わない
        // （それが必要なら SkinnedMeshRendererComponent を使う）。
        bool BuildRenderItem(const Core::GameObject& owner,
            Rendering::RenderItem& out) const override;
        const std::vector<Reflection::PropertyDesc>* DynamicProperties()
            const noexcept override;
        void OnMotionPropertyApplied(const char* property_name) override;
        void PrepareMaterialMotion(const Rendering::MaterialAsset* material,
            const Rendering::ShaderPropertySchema* schema);

        // 実際に描くべきか。Component の有効状態と visible の両方を見る。
        bool ShouldRender() const noexcept
        {
            return visible && !mesh_asset.empty() && ActiveInHierarchy();
        }

        // AssetDatabase の GUID。空なら描画対象にならない。
        std::string mesh_asset;

        std::string material_asset;

        // trueならRenderer側の色・描画方式をMaterial Assetより優先する。
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

        // 既存の描画方式の番号に合わせる。0=FBX標準 / 1=PBR / 2=トゥーン / 3=アンリット
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

        DirectX::XMFLOAT3 local_position_offset{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 local_rotation_offset{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 local_scale_multiplier{ 1.0f, 1.0f, 1.0f };
    };
}
