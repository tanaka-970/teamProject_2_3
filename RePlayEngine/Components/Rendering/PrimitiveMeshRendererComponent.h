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
    // Engine 内蔵の基本形状を描画する Component。
    //
    // 外部 Model Asset を参照する MeshRendererComponent とは責務を分離する。
    // Primitive はファイル/GUIDを持たず、Component が形状種別だけを保持し、
    // Renderer へ engine-internal の builtin:* ID を提出する。
    //
    // GameObject 自体は特別扱いしない。Plane / Cube 等も
    // 「普通の GameObject + Primitive Mesh Renderer」で構成される。
    class PrimitiveMeshRendererComponent final
        : public Core::Component
        , public Rendering::IRenderSubmitter
    {
        REPLAY_COMPONENT_BODY(PrimitiveMeshRendererComponent)

    public:
        enum PrimitiveType : int
        {
            Plane = 0,
            Cube,
            Sphere,
            Capsule,
            Cylinder,
            Quad,
            PrimitiveTypeCount
        };

        PrimitiveMeshRendererComponent() = default;

        bool BuildRenderItem(const Core::GameObject& owner,
            Rendering::RenderItem& out) const override;
        const std::vector<Reflection::PropertyDesc>* DynamicProperties()
            const noexcept override;
        void OnMotionPropertyApplied(const char* property_name) override;
        void PrepareMaterialMotion(const Rendering::MaterialAsset* material,
            const Rendering::ShaderPropertySchema* schema);

        bool ShouldRender() const noexcept
        {
            return visible && ActiveInHierarchy() && BuiltinAssetId() != nullptr;
        }

        // Renderer / MeshCollider が共有して使う engine-internal ID。
        // Scene へ外部ファイルパスを保存するためのものではない。
        const char* BuiltinAssetId() const noexcept;

        int primitive_type = static_cast<int>(Plane);

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
        int shading_model = 1;
        bool outline = false;
        bool cast_shadow = true;
        bool receive_shadow = true;
        bool visible = true;
    };
}
