#pragma once

#include <DirectXMath.h>

#include <cstdint>
#include <vector>

namespace ReplayEngine::Scene { class SceneCollisionWorld; }
namespace ReplayEngine::Core { struct ObjectID; }

namespace ReplayEngine::Editor
{
    // Collider の形をワールド空間の線分として書き出す。
    //
    // 【D3D も ImGui も触らない理由】
    //   ここが描画 API に触ると、g++ の検証ハーネスで動かせなくなる。
    //   「どんな線を引くか」だけを決めて線分の配列を返し、
    //   実際の描画は framework 側（ImGui のオーバーレイ）が担当する。
    //   線の本数や位置の正しさは、描画なしで検証できる。
    struct DebugLine
    {
        DirectX::XMFLOAT3 start{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 end{ 0.0f, 0.0f, 0.0f };

        // 0xAABBGGRR。ImGui の ImU32 とそのまま同じ並び。
        std::uint32_t color = 0xffffffffu;
    };

    // 色の決め方。見た目で状態が分かるようにする。
    namespace ColliderDebugColors
    {
        inline constexpr std::uint32_t blocking = 0xff40ff40u;   // 緑   … 通常の当たり判定
        inline constexpr std::uint32_t trigger = 0xffffc040u;    // 水色 … Trigger
        inline constexpr std::uint32_t disabled = 0xff808080u;   // 灰   … 無効
        inline constexpr std::uint32_t primary = 0xff40a0ffu;    // 橙   … Motor の移動用
        inline constexpr std::uint32_t cook_failed = 0xff4040ffu;// 赤   … Mesh の Cook 失敗
        inline constexpr std::uint32_t bounds = 0x60ffffffu;     // 半透明の白 … 境界ボックス
    }

    class ColliderDebugDraw final
    {
    public:
        struct Options
        {
            // 境界ボックスを描くか。
            bool draw_bounds = true;

            // 形そのものを描くか。
            bool draw_shapes = true;

            // Mesh Collider の三角形を描くか。
            // 既定は false（Bounds だけ）。三角形は本数が多く重いため、
            // Collider 側の debug_draw_wireframe が true のものだけ描く。
            bool draw_mesh_wireframe = false;

            // 球・カプセルの円を何分割で描くか。
            int circle_segments = 24;
        };

        ColliderDebugDraw() = delete;

        // 登録表を走査して線分を作る。out は先にクリアされる。
        //
        // Character Motor の移動用 Collider は別の色になる。
        // 「どれで動いているのか」を画面から確かめられるようにするため。
        static void Build(const Scene::SceneCollisionWorld& world,
            const Options& options, std::vector<DebugLine>& out);

        // 個別の形状。テストからも直接呼べるようにしてある。
        static void AppendBox(const DirectX::XMFLOAT3& center,
            const DirectX::XMFLOAT3& half_extents, const DirectX::XMFLOAT4X4& rotation,
            std::uint32_t color, std::vector<DebugLine>& out);

        static void AppendAxisAlignedBox(const DirectX::XMFLOAT3& minimum,
            const DirectX::XMFLOAT3& maximum, std::uint32_t color,
            std::vector<DebugLine>& out);

        static void AppendSphere(const DirectX::XMFLOAT3& center, float radius,
            int segments, std::uint32_t color, std::vector<DebugLine>& out);

        static void AppendCapsule(const DirectX::XMFLOAT3& segment_start,
            const DirectX::XMFLOAT3& segment_end, float radius, int segments,
            std::uint32_t color, std::vector<DebugLine>& out);
    };
}
