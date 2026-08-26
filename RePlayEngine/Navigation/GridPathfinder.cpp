#include "GridPathfinder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_map>
#include <utility>

namespace ReplayEngine::Navigation
{
    namespace
    {
        constexpr float PositionEpsilon = 0.0001f;
        constexpr float DiagonalCost = 1.41421356237f;

        struct SanitizedSettings
        {
            float grid_size = 1.0f;
            float maximum_range = 24.0f;
            std::size_t maximum_search_cells = 4096;
            float agent_radius = 0.38f;
            float walkable_normal_y = 0.25f;
            float ground_probe_up = 0.4f;
            float ground_probe_down = 1.4f;
            DirectX::XMFLOAT3 ground_offset{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 wall_offset{ 0.0f, 0.0f, 0.0f };
            Scene::CollisionQueryFilter filter;
        };

        struct GridNode
        {
            int x = 0;
            int z = 0;
            bool sampled = false;
            bool walkable = false;
            bool opened = false;
            bool closed = false;
            DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
            float g = (std::numeric_limits<float>::max)();
            int parent_x = 0;
            int parent_z = 0;
            bool has_parent = false;
        };

        struct OpenEntry
        {
            float f = 0.0f;
            float g = 0.0f;
            int x = 0;
            int z = 0;
        };

        struct OpenEntryGreater
        {
            bool operator()(const OpenEntry& left, const OpenEntry& right) const noexcept
            {
                if (left.f != right.f) return left.f > right.f;
                return left.g > right.g;
            }
        };

        bool FinitePositive(float value) noexcept
        {
            return std::isfinite(value) && value > 0.0f;
        }

        bool FiniteNonNegative(float value) noexcept
        {
            return std::isfinite(value) && value >= 0.0f;
        }

        float ClampFloat(float value, float minimum, float maximum) noexcept
        {
            if (!std::isfinite(value)) return minimum;
            if (value < minimum) return minimum;
            if (value > maximum) return maximum;
            return value;
        }

        SanitizedSettings Sanitize(const GridPathfinderSettings& source) noexcept
        {
            SanitizedSettings result;
            result.grid_size = FinitePositive(source.grid_size)
                ? ClampFloat(source.grid_size, 0.05f, 1000.0f) : 1.0f;
            result.maximum_range = FinitePositive(source.maximum_range)
                ? ClampFloat(source.maximum_range, result.grid_size, 100000.0f) : 24.0f;
            result.maximum_search_cells = (std::max)(static_cast<std::size_t>(16),
                (std::min)(source.maximum_search_cells, static_cast<std::size_t>(65536)));
            result.agent_radius = FinitePositive(source.agent_radius)
                ? ClampFloat(source.agent_radius, 0.001f, 10000.0f) : 0.38f;
            result.walkable_normal_y = ClampFloat(source.walkable_normal_y, -1.0f, 1.0f);
            result.ground_probe_up = FiniteNonNegative(source.ground_probe_up)
                ? ClampFloat(source.ground_probe_up, 0.0f, 10000.0f) : 0.4f;
            result.ground_probe_down = FinitePositive(source.ground_probe_down)
                ? ClampFloat(source.ground_probe_down, result.ground_probe_up, 100000.0f) : 1.4f;
            result.ground_offset = source.ground_offset;
            result.wall_offset = source.wall_offset;
            result.filter = source.filter;
            return result;
        }

        std::uint64_t NodeKey(int x, int z) noexcept
        {
            return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
                static_cast<std::uint32_t>(z);
        }

        float PlanarDistance(const DirectX::XMFLOAT3& a,
            const DirectX::XMFLOAT3& b) noexcept
        {
            const float dx = b.x - a.x;
            const float dz = b.z - a.z;
            return std::sqrt(dx * dx + dz * dz);
        }

        DirectX::XMFLOAT3 AddOffset(const DirectX::XMFLOAT3& point,
            const DirectX::XMFLOAT3& offset) noexcept
        {
            return DirectX::XMFLOAT3{
                point.x + offset.x,
                point.y + offset.y,
                point.z + offset.z };
        }

        bool QueryWalkablePoint(const Scene::IPhysicsQueryService& physics,
            float x, float z, float reference_owner_y,
            const SanitizedSettings& settings,
            DirectX::XMFLOAT3& out_owner_position) noexcept
        {
            const DirectX::XMFLOAT3 origin{
                x + settings.ground_offset.x,
                reference_owner_y + settings.ground_offset.y,
                z + settings.ground_offset.z };

            Scene::GroundHit hit{};
            if (!physics.QueryGroundFiltered(origin, settings.agent_radius,
                settings.ground_probe_up, settings.ground_probe_down,
                settings.walkable_normal_y, settings.filter, hit))
            {
                return false;
            }

            out_owner_position = DirectX::XMFLOAT3{
                x,
                hit.position.y + settings.agent_radius - settings.ground_offset.y,
                z };
            return std::isfinite(out_owner_position.y);
        }

        bool SweepClear(const Scene::IPhysicsQueryService& physics,
            const DirectX::XMFLOAT3& from_owner,
            const DirectX::XMFLOAT3& to_owner,
            const SanitizedSettings& settings) noexcept
        {
            Scene::SphereSweepHit hit{};
            const DirectX::XMFLOAT3 start = AddOffset(from_owner, settings.wall_offset);
            const DirectX::XMFLOAT3 end = AddOffset(to_owner, settings.wall_offset);
            return !physics.SweepSphereFiltered(start, end, settings.agent_radius,
                settings.walkable_normal_y - 0.001f, settings.filter, hit);
        }

        bool WalkStraight(const Scene::IPhysicsQueryService& physics,
            const DirectX::XMFLOAT3& from_owner,
            const DirectX::XMFLOAT3& to_owner,
            const SanitizedSettings& settings) noexcept
        {
            const float planar_distance = PlanarDistance(from_owner, to_owner);
            if (planar_distance <= PositionEpsilon) return true;

            const int sample_count = (std::max)(1,
                static_cast<int>(std::ceil(planar_distance / settings.grid_size)));
            DirectX::XMFLOAT3 previous = from_owner;

            for (int sample = 1; sample <= sample_count; ++sample)
            {
                const float t = static_cast<float>(sample) / static_cast<float>(sample_count);
                const float x = from_owner.x + (to_owner.x - from_owner.x) * t;
                const float z = from_owner.z + (to_owner.z - from_owner.z) * t;

                DirectX::XMFLOAT3 next{};
                if (!QueryWalkablePoint(physics, x, z, previous.y, settings, next)) return false;

                // QueryGround は下方向へ深く探せるので、上りだけは Motor の step 高さを
                // 越えないことをここで明示する。これをしないと A* だけが段差を登れてしまう。
                if (next.y - previous.y > settings.ground_probe_up + 0.05f) return false;
                if (!SweepClear(physics, previous, next, settings)) return false;
                previous = next;
            }
            return true;
        }

        float Heuristic(const DirectX::XMFLOAT3& point,
            const DirectX::XMFLOAT3& goal) noexcept
        {
            return PlanarDistance(point, goal);
        }

        bool ReconstructPath(const std::unordered_map<std::uint64_t, GridNode>& nodes,
            int goal_x, int goal_z, const DirectX::XMFLOAT3& exact_start,
            const DirectX::XMFLOAT3& exact_goal,
            std::vector<DirectX::XMFLOAT3>& out_path)
        {
            out_path.clear();
            int x = goal_x;
            int z = goal_z;

            for (std::size_t guard = 0; guard <= nodes.size(); ++guard)
            {
                const auto it = nodes.find(NodeKey(x, z));
                if (it == nodes.end()) return false;

                out_path.push_back(it->second.position);
                if (!it->second.has_parent) break;
                x = it->second.parent_x;
                z = it->second.parent_z;
            }

            if (out_path.empty()) return false;
            std::reverse(out_path.begin(), out_path.end());
            out_path.front() = exact_start;
            out_path.back() = exact_goal;
            return out_path.size() >= 2;
        }

        bool PullString(const Scene::IPhysicsQueryService& physics,
            const std::vector<DirectX::XMFLOAT3>& raw_path,
            const SanitizedSettings& settings,
            std::vector<DirectX::XMFLOAT3>& out_path)
        {
            out_path.clear();
            if (raw_path.size() < 2) return false;

            out_path.push_back(raw_path.front());
            std::size_t anchor = 0;
            while (anchor + 1 < raw_path.size())
            {
                std::size_t farthest = anchor + 1;
                for (std::size_t candidate = anchor + 2;
                    candidate < raw_path.size(); ++candidate)
                {
                    if (!WalkStraight(physics, raw_path[anchor], raw_path[candidate], settings))
                        break;
                    farthest = candidate;
                }

                out_path.push_back(raw_path[farthest]);
                anchor = farthest;
            }
            return out_path.size() >= 2;
        }

        bool FindGridPathImpl(const Scene::IPhysicsQueryService& physics,
            const DirectX::XMFLOAT3& start, const DirectX::XMFLOAT3& goal,
            const GridPathfinderSettings& source_settings,
            std::vector<DirectX::XMFLOAT3>& out_path)
        {
            out_path.clear();
            if (!physics.CollisionAvailable()) return false;

            const SanitizedSettings settings = Sanitize(source_settings);
            const float delta_x = goal.x - start.x;
            const float delta_z = goal.z - start.z;
            if (std::fabs(delta_x) > settings.maximum_range ||
                std::fabs(delta_z) > settings.maximum_range)
            {
                return false;
            }

            // 障害物が無い大多数のケースは A* を作らずここで終える。
            if (WalkStraight(physics, start, goal, settings))
            {
                out_path.push_back(start);
                out_path.push_back(goal);
                return true;
            }

            const int range_cells = (std::max)(1,
                static_cast<int>(std::floor(settings.maximum_range / settings.grid_size)));
            int goal_x = static_cast<int>(std::lround(delta_x / settings.grid_size));
            int goal_z = static_cast<int>(std::lround(delta_z / settings.grid_size));
            if (goal_x == 0 && goal_z == 0)
            {
                // 同じ升目の中でも薄い壁を挟んでいる場合は、開始ノードと目的ノードを
                // 同一視すると迂回探索そのものができない。仮想的に隣接升目へ割り当て、
                // ノード位置だけは exact goal を使う。
                if (std::fabs(delta_x) >= std::fabs(delta_z) &&
                    std::fabs(delta_x) > PositionEpsilon)
                {
                    goal_x = delta_x > 0.0f ? 1 : -1;
                }
                else if (std::fabs(delta_z) > PositionEpsilon)
                {
                    goal_z = delta_z > 0.0f ? 1 : -1;
                }
                else
                {
                    return false;
                }
            }
            if (std::abs(goal_x) > range_cells || std::abs(goal_z) > range_cells)
                return false;

            std::unordered_map<std::uint64_t, GridNode> nodes;
            nodes.reserve((std::min)(settings.maximum_search_cells,
                static_cast<std::size_t>(4096)));

            std::priority_queue<OpenEntry, std::vector<OpenEntry>, OpenEntryGreater> open;
            bool limit_exceeded = false;

            auto get_or_create = [&](int x, int z) -> GridNode*
            {
                if (std::abs(x) > range_cells || std::abs(z) > range_cells) return nullptr;
                const std::uint64_t key = NodeKey(x, z);
                const auto found = nodes.find(key);
                if (found != nodes.end()) return &found->second;
                if (nodes.size() >= settings.maximum_search_cells)
                {
                    limit_exceeded = true;
                    return nullptr;
                }
                GridNode node;
                node.x = x;
                node.z = z;
                return &nodes.emplace(key, node).first->second;
            };

            auto sample_node = [&](GridNode& node, float reference_y) -> bool
            {
                if (node.sampled) return node.walkable;
                node.sampled = true;

                float x = start.x + static_cast<float>(node.x) * settings.grid_size;
                float z = start.z + static_cast<float>(node.z) * settings.grid_size;
                if (node.x == goal_x && node.z == goal_z)
                {
                    x = goal.x;
                    z = goal.z;
                }

                node.walkable = QueryWalkablePoint(physics, x, z, reference_y,
                    settings, node.position);
                return node.walkable;
            };

            GridNode* start_node = get_or_create(0, 0);
            if (start_node == nullptr || !sample_node(*start_node, start.y)) return false;
            start_node->position = start;
            start_node->g = 0.0f;
            start_node->opened = true;
            open.push(OpenEntry{ Heuristic(start, goal), 0.0f, 0, 0 });

            static constexpr int offsets[8][2] = {
                { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
                { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 }
            };

            bool found_goal = false;
            while (!open.empty())
            {
                const OpenEntry entry = open.top();
                open.pop();

                const auto current_it = nodes.find(NodeKey(entry.x, entry.z));
                if (current_it == nodes.end()) continue;
                GridNode& current = current_it->second;
                if (current.closed || entry.g > current.g + PositionEpsilon) continue;
                current.closed = true;

                if (current.x == goal_x && current.z == goal_z)
                {
                    found_goal = true;
                    break;
                }

                for (const auto& offset : offsets)
                {
                    GridNode* next = get_or_create(current.x + offset[0], current.z + offset[1]);
                    if (limit_exceeded) return false;
                    if (next == nullptr || next->closed) continue;
                    if (!sample_node(*next, current.position.y)) continue;

                    if (next->position.y - current.position.y >
                        settings.ground_probe_up + 0.05f)
                    {
                        continue;
                    }
                    if (!SweepClear(physics, current.position, next->position, settings)) continue;

                    const bool diagonal = offset[0] != 0 && offset[1] != 0;
                    const float step_cost = settings.grid_size *
                        (diagonal ? DiagonalCost : 1.0f);
                    const float tentative_g = current.g + step_cost;
                    if (next->opened && tentative_g >= next->g - PositionEpsilon) continue;

                    next->opened = true;
                    next->g = tentative_g;
                    next->parent_x = current.x;
                    next->parent_z = current.z;
                    next->has_parent = true;
                    const float f = tentative_g + Heuristic(next->position, goal);
                    open.push(OpenEntry{ f, tentative_g, next->x, next->z });
                }
            }

            if (!found_goal) return false;

            std::vector<DirectX::XMFLOAT3> raw_path;
            if (!ReconstructPath(nodes, goal_x, goal_z, start, goal, raw_path)) return false;

            // A* の 8 方向グリッドをそのまま返すと階段状に見える。
            // 既存 Physics Query で直進可能な区間だけをまとめ、折れ点だけ残す。
            if (!PullString(physics, raw_path, settings, out_path)) return false;
            return true;
        }
    }

    bool FindGridPath(const Scene::IPhysicsQueryService& physics,
        const DirectX::XMFLOAT3& start, const DirectX::XMFLOAT3& goal,
        const GridPathfinderSettings& settings,
        std::vector<DirectX::XMFLOAT3>& out_path) noexcept
    {
        // MoveTo は公開 API 上 noexcept。探索途中の一時コンテナ確保に失敗しても
        // Editor 全体を止めず、呼び出し側が Phase 1 の直進へフォールバックできるようにする。
        try
        {
            return FindGridPathImpl(physics, start, goal, settings, out_path);
        }
        catch (...)
        {
            out_path.clear();
            return false;
        }
    }
}
