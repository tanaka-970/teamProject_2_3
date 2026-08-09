#pragma once

#include "../../Object/Component/Component.h"
#include "../../Rendering/Adapter/IRenderSubmitter.h"

#include <DirectXMath.h>

#include <string>

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
        DirectX::XMFLOAT4 tint{ 1.0f, 1.0f, 1.0f, 1.0f };
        int shading_model = 1;
        bool outline = false;
        bool cast_shadow = true;
        bool receive_shadow = true;
        bool visible = true;
    };
}
