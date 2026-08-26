#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/References.h"
#include "../../Reflection/Property/PropertyDesc.h"

#include <DirectXMath.h>

#include <vector>

namespace ReplayEngine::Components
{
    class UIMaskComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UIMaskComponent)

    public:
        enum MaskMode : int
        {
            Rectangle = 0,
            Image = 1,
            Shape = 2,
            ObjectAlpha = 3,
            ObjectLuma = 4,
        };

        enum ShapeMaskKind : int
        {
            ShapeRectangle = 0,
            ShapeCircle = 1,
            ShapePolygon = 2,
            ShapeStar = 3,
            ShapeRoundedRectangle = 4,
        };

        UIMaskComponent() = default;

        void OnAttach() override;
        void OnPropertyChanged(const char* property_name) override;
        const std::vector<Reflection::PropertyDesc>* DynamicProperties()
            const noexcept override;

        bool enabled_mask = true;
        bool show_mask_graphic = true;
        int mask_mode = Rectangle;
        int shape_kind = ShapeRectangle;
        int shape_sides = 5;
        float shape_inner_radius = 0.5f;
        float shape_corner_radius = 0.0f;
        float shape_rotation = 0.0f;
        DirectX::XMFLOAT2 group_scale{ 1.0f, 1.0f };
        Reflection::AssetReference mask_image;
        // ObjectAlpha / ObjectLuma では、この Scene 内 GameObject の描画結果を Track Matte に使う。
        // 生ポインタは保持せず ObjectReference のまま保存する。
        Reflection::ObjectReference mask_object;

        enum MatteOperation : int
        {
            MatteAdd = 0,
            MatteSubtract = 1,
            MatteIntersect = 2,
        };
        // 2個目以降の Track Matte。matte_operations[i] は matte_objects[i] を
        // 現在の合成Matteへどう足すかを表す。旧Sceneは空配列のため従来どおり。
        std::vector<Reflection::ObjectReference> matte_objects;
        std::vector<int> matte_operations;

        bool invert = false;
        float softness = 0.0f;

    private:
        void NormalizeMatteOperations();
        void RebuildDynamicProperties() const;
        mutable std::vector<Reflection::PropertyDesc> dynamic_properties_;

        // Rectangle は従来どおり RectTransform の resolved_rect を D3D11 scissor に渡す。
        // Image / Shape は既存 Effect Stack の Mask pass 用 RT へ逃がし、別の描画経路や
        // 新しい GPU リソース所有者を増やさない。softness は shader 側の境界幅に使う。
    };
}
