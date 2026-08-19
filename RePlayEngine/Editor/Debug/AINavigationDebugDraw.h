#pragma once

#include "ColliderDebugDraw.h"
#include "../../Core/ObjectID/ObjectID.h"

#include <DirectXMath.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ReplayEngine::Scene { class Scene; }

namespace ReplayEngine::Editor
{
    // ImGui そのものには依存せず、AI / Navigation の「何を描くか」だけを
    // ワールド空間データへ変換する。実描画は既存 framework_collider_debug.cpp の
    // ImGui overlay 経路へ委譲する。
    struct DebugFilledPolygon
    {
        std::vector<DirectX::XMFLOAT3> points;
        std::uint32_t color = 0xffffffffu;
    };

    struct DebugWorldLabel
    {
        DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
        std::string text;
        std::uint32_t color = 0xffffffffu;
    };

    struct AINavigationDebugFrame
    {
        std::vector<DebugLine> lines;
        std::vector<DebugFilledPolygon> fills;
        std::vector<DebugWorldLabel> labels;

        void Clear()
        {
            lines.clear();
            fills.clear();
            labels.clear();
        }
    };

    namespace AINavigationDebugColors
    {
        inline constexpr std::uint32_t detection = 0xb020d8ffu;       // 黄
        inline constexpr std::uint32_t detection_faint = 0x1820d8ffu;
        inline constexpr std::uint32_t attack = 0xc04040ffu;          // 赤
        inline constexpr std::uint32_t attack_active = 0x704040ffu;
        inline constexpr std::uint32_t attack_recovery = 0x90808080u;
        inline constexpr std::uint32_t sight_clear = 0xd040ff40u;     // 緑
        inline constexpr std::uint32_t sight_blocked = 0xd04040ffu;   // 赤
        inline constexpr std::uint32_t patrol = 0xc0ff8040u;          // 青系
        inline constexpr std::uint32_t path = 0xd0ffff40u;            // シアン
        inline constexpr std::uint32_t trail = 0x6040ffffu;
        inline constexpr std::uint32_t label = 0xffffffffu;
    }

    class AINavigationDebugDraw final
    {
    public:
        AINavigationDebugDraw() = delete;

        static void Build(const Scene::Scene& scene, Core::ObjectID selected_object,
            AINavigationDebugFrame& out);
    };
}
