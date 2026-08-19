#include "AINavigationDebugDraw.h"

#include "../../Components/Gameplay/EnemyBehaviourComponent.h"
#include "../../Components/Navigation/NavAgentComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ReplayEngine::Editor
{
    namespace
    {
        constexpr int CircleSegments = 48;
        constexpr float ArrowLength = 0.45f;

        DirectX::XMFLOAT3 Add(const DirectX::XMFLOAT3& a,
            const DirectX::XMFLOAT3& b) noexcept
        {
            return { a.x + b.x, a.y + b.y, a.z + b.z };
        }

        DirectX::XMFLOAT3 Subtract(const DirectX::XMFLOAT3& a,
            const DirectX::XMFLOAT3& b) noexcept
        {
            return { a.x - b.x, a.y - b.y, a.z - b.z };
        }

        DirectX::XMFLOAT3 Scale(const DirectX::XMFLOAT3& value, float scale) noexcept
        {
            return { value.x * scale, value.y * scale, value.z * scale };
        }

        float PlanarLength(const DirectX::XMFLOAT3& value) noexcept
        {
            return std::sqrt(value.x * value.x + value.z * value.z);
        }

        void AppendCircle(const DirectX::XMFLOAT3& center, float radius,
            std::uint32_t color, std::vector<DebugLine>& lines)
        {
            radius = (std::max)(0.0f, radius);
            if (radius <= 0.0f) return;
            for (int index = 0; index < CircleSegments; ++index)
            {
                const float a0 = DirectX::XM_2PI * static_cast<float>(index) /
                    static_cast<float>(CircleSegments);
                const float a1 = DirectX::XM_2PI * static_cast<float>(index + 1) /
                    static_cast<float>(CircleSegments);
                lines.push_back({
                    { center.x + std::sin(a0) * radius, center.y, center.z + std::cos(a0) * radius },
                    { center.x + std::sin(a1) * radius, center.y, center.z + std::cos(a1) * radius },
                    color });
            }
        }

        void AppendDisk(const DirectX::XMFLOAT3& center, float radius,
            std::uint32_t color, std::vector<DebugFilledPolygon>& fills)
        {
            radius = (std::max)(0.0f, radius);
            if (radius <= 0.0f) return;
            for (int index = 0; index < CircleSegments; ++index)
            {
                const float a0 = DirectX::XM_2PI * static_cast<float>(index) /
                    static_cast<float>(CircleSegments);
                const float a1 = DirectX::XM_2PI * static_cast<float>(index + 1) /
                    static_cast<float>(CircleSegments);
                DebugFilledPolygon triangle;
                triangle.color = color;
                triangle.points = {
                    center,
                    { center.x + std::sin(a0) * radius, center.y, center.z + std::cos(a0) * radius },
                    { center.x + std::sin(a1) * radius, center.y, center.z + std::cos(a1) * radius } };
                fills.push_back(std::move(triangle));
            }
        }

        float WorldYaw(const Core::GameObject& object)
        {
            const DirectX::XMFLOAT4 rotation = object.GetTransform().WorldRotationQuaternion();
            const DirectX::XMVECTOR forward_vector = DirectX::XMVector3Rotate(
                DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), DirectX::XMLoadFloat4(&rotation));
            DirectX::XMFLOAT3 forward{};
            DirectX::XMStoreFloat3(&forward, forward_vector);
            return std::atan2(forward.x, forward.z);
        }

        void AppendFov(const Core::GameObject& object, float radius, float degrees,
            bool selected, std::vector<DebugLine>& lines,
            std::vector<DebugFilledPolygon>& fills)
        {
            radius = (std::max)(0.0f, radius);
            degrees = (std::max)(0.0f, (std::min)(360.0f, degrees));
            if (radius <= 0.0f || degrees <= 0.0f) return;

            DirectX::XMFLOAT3 center = object.GetTransform().WorldPosition();
            center.y += 0.035f;
            const float yaw = WorldYaw(object);
            const float half = degrees * 0.5f * (DirectX::XM_PI / 180.0f);
            const int segments = (std::max)(2, static_cast<int>(std::ceil(degrees / 8.0f)));

            const std::uint32_t line_color = selected
                ? AINavigationDebugColors::detection : 0x5020d8ffu;
            DirectX::XMFLOAT3 previous{
                center.x + std::sin(yaw - half) * radius,
                center.y,
                center.z + std::cos(yaw - half) * radius };
            lines.push_back({ center, previous, line_color });

            for (int index = 1; index <= segments; ++index)
            {
                const float t = static_cast<float>(index) / static_cast<float>(segments);
                const float angle = yaw - half + (half * 2.0f) * t;
                DirectX::XMFLOAT3 point{
                    center.x + std::sin(angle) * radius,
                    center.y,
                    center.z + std::cos(angle) * radius };
                lines.push_back({ previous, point, line_color });
                if (selected)
                {
                    DebugFilledPolygon triangle;
                    triangle.color = AINavigationDebugColors::detection_faint;
                    triangle.points = { center, previous, point };
                    fills.push_back(std::move(triangle));
                }
                previous = point;
            }
            lines.push_back({ center, previous, line_color });
        }

        Core::GameObject* ResolveReference(const Scene::Scene& scene,
            const Reflection::ObjectReference& reference)
        {
            if (!reference.IsAssigned()) return nullptr;
            Core::GameObject* object = scene.FindGameObjectByID(reference.object);
            if (object == nullptr || object->PendingDestroy()) return nullptr;
            return object;
        }

        void AppendArrow(const DirectX::XMFLOAT3& start, const DirectX::XMFLOAT3& end,
            std::uint32_t color, std::vector<DebugLine>& lines)
        {
            DirectX::XMFLOAT3 direction = Subtract(end, start);
            const float length = PlanarLength(direction);
            if (length <= 0.0001f) return;
            direction.x /= length;
            direction.z /= length;
            const DirectX::XMFLOAT3 midpoint{
                (start.x + end.x) * 0.5f,
                (start.y + end.y) * 0.5f + 0.04f,
                (start.z + end.z) * 0.5f };
            const DirectX::XMFLOAT3 side{ -direction.z, 0.0f, direction.x };
            const DirectX::XMFLOAT3 back = Scale(direction, -ArrowLength);
            lines.push_back({ midpoint, Add(Add(midpoint, back), Scale(side, ArrowLength * 0.45f)), color });
            lines.push_back({ midpoint, Add(Add(midpoint, back), Scale(side, -ArrowLength * 0.45f)), color });
        }

        void AppendCross(const DirectX::XMFLOAT3& center, float size,
            std::uint32_t color, std::vector<DebugLine>& lines)
        {
            lines.push_back({ {center.x - size, center.y, center.z - size},
                {center.x + size, center.y, center.z + size}, color });
            lines.push_back({ {center.x - size, center.y, center.z + size},
                {center.x + size, center.y, center.z - size}, color });
        }

        void AppendEnemy(const Scene::Scene& scene, const Core::GameObject& object,
            const Components::EnemyBehaviourComponent& enemy, bool selected,
            AINavigationDebugFrame& out)
        {
            if (!enemy.debug_draw) return;

            DirectX::XMFLOAT3 center = object.GetTransform().WorldPosition();
            center.y += 0.02f;
            AppendCircle(center, enemy.detection_range, selected
                ? AINavigationDebugColors::detection : 0x6020d8ffu, out.lines);
            AppendFov(object, enemy.detection_range, enemy.field_of_view_degrees,
                selected, out.lines, out.fills);

            std::string state_label = enemy.CurrentStateName();
            if (enemy.CurrentState() == Components::EnemyState::Attack &&
                enemy.attack_controls_damage_area)
            {
                state_label += " / ";
                state_label += enemy.CurrentAttackPhaseName();
            }
            out.labels.push_back({
                { center.x, center.y + 2.2f, center.z }, state_label,
                AINavigationDebugColors::label });

            if (!selected) return;

            // 索敵円は FOV 以外の方向も距離として分かるよう、ごく薄く塗る。
            AppendDisk(center, enemy.detection_range, 0x0820d8ffu, out.fills);

            std::uint32_t attack_color = AINavigationDebugColors::attack;
            if (enemy.attack_controls_damage_area &&
                enemy.CurrentState() == Components::EnemyState::Attack)
            {
                if (enemy.CurrentAttackPhase() == Components::EnemyAttackPhase::Active)
                {
                    attack_color = AINavigationDebugColors::attack;
                    AppendDisk(center, enemy.attack_range,
                        AINavigationDebugColors::attack_active, out.fills);
                }
                else if (enemy.CurrentAttackPhase() == Components::EnemyAttackPhase::Recovery)
                {
                    attack_color = AINavigationDebugColors::attack_recovery;
                    AppendDisk(center, enemy.attack_range, 0x18808080u, out.fills);
                }
                else if (enemy.CurrentAttackPhase() == Components::EnemyAttackPhase::Windup)
                {
                    // Windup はまだ当たらないことが分かるよう、二重の細い円で示す。
                    AppendCircle(center, enemy.attack_range * 0.96f,
                        0x804040ffu, out.lines);
                }
            }
            AppendCircle(center, enemy.attack_range, attack_color, out.lines);

            if (enemy.LastLineOfSightTested())
            {
                const std::uint32_t los_color = enemy.LastLineOfSightClear()
                    ? AINavigationDebugColors::sight_clear
                    : AINavigationDebugColors::sight_blocked;
                const DirectX::XMFLOAT3 los_end = enemy.LastLineOfSightClear()
                    ? enemy.LastLineOfSightEnd() : enemy.LastLineOfSightHit();
                out.lines.push_back({ enemy.LastLineOfSightStart(), los_end, los_color });
                if (!enemy.LastLineOfSightClear())
                    AppendCross(enemy.LastLineOfSightHit(), 0.18f, los_color, out.lines);
            }

            // 巡回点を配列順に結び、進行方向を矢印で示す。
            std::vector<Core::GameObject*> waypoints;
            waypoints.reserve(enemy.patrol_waypoints.size());
            for (const Reflection::ObjectReference& reference : enemy.patrol_waypoints)
            {
                if (Core::GameObject* waypoint = ResolveReference(scene, reference))
                    waypoints.push_back(waypoint);
            }
            if (!waypoints.empty())
            {
                for (std::size_t index = 0; index < waypoints.size(); ++index)
                {
                    const std::size_t next = (index + 1) % waypoints.size();
                    DirectX::XMFLOAT3 a = waypoints[index]->GetTransform().WorldPosition();
                    DirectX::XMFLOAT3 b = waypoints[next]->GetTransform().WorldPosition();
                    a.y += 0.08f;
                    b.y += 0.08f;
                    out.lines.push_back({ a, b, AINavigationDebugColors::patrol });
                    AppendArrow(a, b, AINavigationDebugColors::patrol, out.lines);
                    const float radius = index == enemy.CurrentWaypointIndex() ? 0.32f : 0.18f;
                    AppendCircle(a, radius, AINavigationDebugColors::patrol, out.lines);
                }
            }

            const Components::NavAgentComponent* agent =
                object.GetComponent<Components::NavAgentComponent>();
            if (agent == nullptr) return;

            std::vector<DirectX::XMFLOAT3> path;
            agent->BuildDebugPath(path);
            for (std::size_t index = 1; index < path.size(); ++index)
            {
                DirectX::XMFLOAT3 a = path[index - 1];
                DirectX::XMFLOAT3 b = path[index];
                a.y += 0.12f;
                b.y += 0.12f;
                out.lines.push_back({ a, b, AINavigationDebugColors::path });
                AppendArrow(a, b, AINavigationDebugColors::path, out.lines);
            }

            if (agent->HasDestination())
            {
                DirectX::XMFLOAT3 destination = agent->Destination();
                destination.y += 0.13f;
                AppendCross(destination, 0.24f, AINavigationDebugColors::path, out.lines);
                AppendCircle(destination, agent->stopping_distance,
                    AINavigationDebugColors::path, out.lines);
            }

            const std::vector<DirectX::XMFLOAT3>& trail = agent->RecentTrail();
            for (std::size_t index = 1; index < trail.size(); ++index)
            {
                DirectX::XMFLOAT3 a = trail[index - 1];
                DirectX::XMFLOAT3 b = trail[index];
                a.y += 0.06f;
                b.y += 0.06f;
                out.lines.push_back({ a, b, AINavigationDebugColors::trail });
            }
        }
    }

    void AINavigationDebugDraw::Build(const Scene::Scene& scene,
        Core::ObjectID selected_object, AINavigationDebugFrame& out)
    {
        out.Clear();
        for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
        {
            Core::GameObject* object = scene.GameObjectAt(index);
            if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy())
                continue;
            const Components::EnemyBehaviourComponent* enemy =
                object->GetComponent<Components::EnemyBehaviourComponent>();
            if (enemy == nullptr || !enemy->ActiveInHierarchy()) continue;
            AppendEnemy(scene, *object, *enemy, object->ID() == selected_object, out);
        }
    }
}
