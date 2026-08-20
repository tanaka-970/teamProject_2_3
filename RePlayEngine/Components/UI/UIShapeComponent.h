#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/PropertyDesc.h"

#include <DirectXMath.h>

#include <vector>

namespace ReplayEngine::Components
{
    class UIShapeComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UIShapeComponent)

    public:
        enum Shape : int
        {
            Rectangle = 0,
            Circle = 1,
            Line = 2,
            Polygon = 3,
            BezierPath = 4,
            Superellipse = 5,
            PolarFormula = 6,
            CustomBezierPath = 7,
        };

        enum FillMode : int
        {
            Solid = 0,
            LinearGradient = 1,
            RadialGradient = 2,
        };

        enum StrokeMode : int
        {
            StrokeSolid = 0,
            StrokeAlongLength = 1,
        };

        UIShapeComponent() = default;

        const std::vector<Reflection::PropertyDesc>*
            DynamicProperties() const noexcept override;
        void OnPropertyChanged(const char* property_name) override;
        void SetPathPointCount(int count);

        int shape = Rectangle;
        DirectX::XMFLOAT4 fill_color{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 fill_color_2{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 fill_color_3{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 fill_color_4{ 1.0f, 1.0f, 1.0f, 1.0f };
        float fill_stop_2 = 1.0f;
        // 負値は未設定。古い Scene は 2 色の意味をそのまま維持する。
        float fill_stop_3 = -1.0f;
        float fill_stop_4 = -1.0f;
        int fill_mode = Solid;
        float fill_angle = 0.0f;
        DirectX::XMFLOAT2 fill_center{ 0.5f, 0.5f };
        DirectX::XMFLOAT4 stroke_color{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 stroke_color_2{ 1.0f, 1.0f, 1.0f, 1.0f };
        int stroke_mode = StrokeSolid;
        float stroke_width = 0.0f;
        float corner_radius = 0.0f;
        float arc_curvature = 1.0f;
        int sides = 5;
        float superellipse_exponent = 2.0f;
        float polar_base_radius = 1.0f;
        float polar_amplitude = 0.0f;
        float polar_lobes = 5.0f;
        float polar_rotation = 0.0f;
        float trim_start = 0.0f;
        float trim_end = 1.0f;
        float trim_offset = 0.0f;
        float dash_length = 0.0f;
        float dash_gap = 0.0f;
        float dash_offset = 0.0f;

        // CustomBezierPath は正規化座標 0..1 の anchor / tangent を使う。
        // 配列自体が Scene 保存を担当し、個々の point[n].* は Dynamic Property として
        // Motion からアニメーションできる。
        bool path_closed = true;
        std::vector<DirectX::XMFLOAT2> path_points;
        std::vector<DirectX::XMFLOAT2> path_in_handles;
        std::vector<DirectX::XMFLOAT2> path_out_handles;

    private:
        void NormalizePathArrays();
        void RebuildDynamicProperties() const;
        mutable std::vector<Reflection::PropertyDesc> dynamic_properties_;
    };
}
