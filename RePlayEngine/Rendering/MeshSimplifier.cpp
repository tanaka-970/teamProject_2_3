#include "MeshSimplifier.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>
#include <unordered_set>

using namespace DirectX;

namespace ReplayEngine::Rendering
{
    namespace
    {
        // 4x4対称行列を上三角の10要素で保持する。
        // 並びは m11 m12 m13 m14 m22 m23 m24 m33 m34 m44。
        struct Quadric
        {
            double m[10]{};

            void AddPlane(double a, double b, double c, double d)
            {
                m[0] += a * a; m[1] += a * b; m[2] += a * c; m[3] += a * d;
                m[4] += b * b; m[5] += b * c; m[6] += b * d;
                m[7] += c * c; m[8] += c * d;
                m[9] += d * d;
            }

            void Add(const Quadric& other)
            {
                for (int i = 0; i < 10; ++i) m[i] += other.m[i];
            }

            // v = (x, y, z, 1) に対する vᵀ Q v。平面群からの二乗距離の和になる。
            double Evaluate(double x, double y, double z) const
            {
                return m[0] * x * x + 2.0 * m[1] * x * y + 2.0 * m[2] * x * z + 2.0 * m[3] * x
                     + m[4] * y * y + 2.0 * m[5] * y * z + 2.0 * m[6] * y
                     + m[7] * z * z + 2.0 * m[8] * z
                     + m[9];
            }
        };

        // 誤差が最小になる縮約位置を解く。
        // ∂(vᵀQv)/∂v = 0 を満たす3x3の線形系を解く。行列が退化していたら false。
        bool SolveOptimalPosition(const Quadric& q, XMFLOAT3& out_position)
        {
            // | m11 m12 m13 |   |x|     |-m14|
            // | m12 m22 m23 | * |y|  =  |-m24|
            // | m13 m23 m33 |   |z|     |-m34|
            const double a11 = q.m[0], a12 = q.m[1], a13 = q.m[2];
            const double a22 = q.m[4], a23 = q.m[5];
            const double a33 = q.m[7];

            const double determinant =
                a11 * (a22 * a33 - a23 * a23) -
                a12 * (a12 * a33 - a23 * a13) +
                a13 * (a12 * a23 - a22 * a13);

            // 平面が縮退している(直線状/点状)場合は解が一意に定まらない。
            if (std::abs(determinant) < 1.0e-10) return false;

            const double b1 = -q.m[3], b2 = -q.m[6], b3 = -q.m[8];
            const double inverse_determinant = 1.0 / determinant;

            // 余因子行列で逆行列を作って解く。
            const double c11 = (a22 * a33 - a23 * a23) * inverse_determinant;
            const double c12 = -(a12 * a33 - a13 * a23) * inverse_determinant;
            const double c13 = (a12 * a23 - a13 * a22) * inverse_determinant;
            const double c22 = (a11 * a33 - a13 * a13) * inverse_determinant;
            const double c23 = -(a11 * a23 - a13 * a12) * inverse_determinant;
            const double c33 = (a11 * a22 - a12 * a12) * inverse_determinant;

            out_position.x = static_cast<float>(c11 * b1 + c12 * b2 + c13 * b3);
            out_position.y = static_cast<float>(c12 * b1 + c22 * b2 + c23 * b3);
            out_position.z = static_cast<float>(c13 * b1 + c23 * b2 + c33 * b3);
            return true;
        }

        struct Triangle
        {
            std::uint32_t v[3]{};
            bool removed = false;
        };

        // 優先度キューの要素。cost が小さいものから取り出す。
        struct EdgeCandidate
        {
            double cost = 0.0;
            std::uint32_t a = 0;
            std::uint32_t b = 0;
            // 取り出したときに頂点が更新済みなら破棄する(遅延削除)。
            std::uint32_t version_a = 0;
            std::uint32_t version_b = 0;
            XMFLOAT3 position{};

            bool operator>(const EdgeCandidate& other) const { return cost > other.cost; }
        };

        std::uint64_t MakeEdgeKey(std::uint32_t a, std::uint32_t b)
        {
            if (a > b) std::swap(a, b);
            return (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint64_t>(b);
        }

        XMFLOAT3 TriangleNormal(const XMFLOAT3& p0, const XMFLOAT3& p1, const XMFLOAT3& p2)
        {
            XMFLOAT3 normal{};
            XMStoreFloat3(&normal, XMVector3Normalize(XMVector3Cross(
                XMVectorSubtract(XMLoadFloat3(&p1), XMLoadFloat3(&p0)),
                XMVectorSubtract(XMLoadFloat3(&p2), XMLoadFloat3(&p0)))));
            return normal;
        }
    }

    MeshSimplifier::Result MeshSimplifier::Simplify(
        const std::vector<Vertex>& input_vertices,
        const std::vector<std::uint32_t>& input_indices,
        const Options& options)
    {
        Result result{};
        result.vertices = input_vertices;
        result.indices = input_indices;
        result.source_triangles = static_cast<std::uint32_t>(input_indices.size() / 3);
        result.result_triangles = result.source_triangles;

        if (input_indices.size() < 3 || input_indices.size() % 3 != 0) return result;
        if (options.target_ratio >= 1.0f) return result;

        const std::uint32_t target_triangles = (std::max)(1u,
            static_cast<std::uint32_t>(result.source_triangles * (std::max)(options.target_ratio, 0.0f)));
        if (target_triangles >= result.source_triangles) return result;

        std::vector<Vertex> vertices = input_vertices;
        std::vector<Triangle> triangles(input_indices.size() / 3);
        for (size_t i = 0; i < triangles.size(); ++i)
        {
            triangles[i].v[0] = input_indices[i * 3 + 0];
            triangles[i].v[1] = input_indices[i * 3 + 1];
            triangles[i].v[2] = input_indices[i * 3 + 2];
        }

        // --- 頂点ごとのquadricを積む ------------------------------------------
        std::vector<Quadric> quadrics(vertices.size());
        // 頂点→隣接三角形。縮約のたびに更新する。
        std::vector<std::vector<std::uint32_t>> vertex_triangles(vertices.size());
        // エッジの参照回数。1なら境界エッジ。
        std::unordered_map<std::uint64_t, int> edge_reference;
        edge_reference.reserve(triangles.size() * 3);

        for (std::uint32_t index = 0; index < triangles.size(); ++index)
        {
            const Triangle& triangle = triangles[index];
            if (triangle.v[0] >= vertices.size() ||
                triangle.v[1] >= vertices.size() ||
                triangle.v[2] >= vertices.size()) continue;

            const XMFLOAT3& p0 = vertices[triangle.v[0]].position;
            const XMFLOAT3& p1 = vertices[triangle.v[1]].position;
            const XMFLOAT3& p2 = vertices[triangle.v[2]].position;
            const XMFLOAT3 normal = TriangleNormal(p0, p1, p2);
            // 退化三角形は平面が定義できないので飛ばす。
            if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z))
                continue;

            const double d = -(static_cast<double>(normal.x) * p0.x +
                               static_cast<double>(normal.y) * p0.y +
                               static_cast<double>(normal.z) * p0.z);
            Quadric plane{};
            plane.AddPlane(normal.x, normal.y, normal.z, d);

            for (int corner = 0; corner < 3; ++corner)
            {
                quadrics[triangle.v[corner]].Add(plane);
                vertex_triangles[triangle.v[corner]].push_back(index);
            }
            for (int corner = 0; corner < 3; ++corner)
            {
                ++edge_reference[MakeEdgeKey(triangle.v[corner], triangle.v[(corner + 1) % 3])];
            }
        }

        // 境界に接する頂点は動かさない(穴が開くのを防ぐ)。
        std::vector<bool> boundary_vertex(vertices.size(), false);
        if (options.preserve_boundary)
        {
            for (const auto& [key, count] : edge_reference)
            {
                if (count != 1) continue;
                boundary_vertex[static_cast<std::uint32_t>(key >> 32)] = true;
                boundary_vertex[static_cast<std::uint32_t>(key & 0xFFFFFFFFull)] = true;
            }
        }

        std::vector<std::uint32_t> vertex_version(vertices.size(), 0);
        std::vector<bool> vertex_removed(vertices.size(), false);

        // 縮約候補を作る。位置と誤差もここで決める。
        const auto make_candidate = [&](std::uint32_t a, std::uint32_t b) -> EdgeCandidate
        {
            EdgeCandidate candidate{};
            candidate.a = a;
            candidate.b = b;
            candidate.version_a = vertex_version[a];
            candidate.version_b = vertex_version[b];

            Quadric combined = quadrics[a];
            combined.Add(quadrics[b]);

            const XMFLOAT3& pa = vertices[a].position;
            const XMFLOAT3& pb = vertices[b].position;

            XMFLOAT3 best{};
            // 境界頂点は動かさない。片方が境界ならそちらへ寄せる。
            const bool lock_a = options.preserve_boundary && boundary_vertex[a];
            const bool lock_b = options.preserve_boundary && boundary_vertex[b];
            if (lock_a && lock_b)
            {
                // 両端が境界。潰すと縁が壊れるのでコストを無限大にして除外する。
                candidate.cost = 1.0e30;
                candidate.position = pa;
                return candidate;
            }
            if (lock_a) best = pa;
            else if (lock_b) best = pb;
            else if (!SolveOptimalPosition(combined, best))
            {
                // 解が定まらないときは端点と中点から誤差最小を選ぶ。
                const XMFLOAT3 midpoint{
                    (pa.x + pb.x) * 0.5f, (pa.y + pb.y) * 0.5f, (pa.z + pb.z) * 0.5f };
                const double cost_a = combined.Evaluate(pa.x, pa.y, pa.z);
                const double cost_b = combined.Evaluate(pb.x, pb.y, pb.z);
                const double cost_m = combined.Evaluate(midpoint.x, midpoint.y, midpoint.z);
                best = (cost_a <= cost_b && cost_a <= cost_m) ? pa
                     : (cost_b <= cost_m ? pb : midpoint);
            }

            candidate.position = best;
            candidate.cost = (std::max)(0.0, combined.Evaluate(best.x, best.y, best.z));
            return candidate;
        };

        std::priority_queue<EdgeCandidate, std::vector<EdgeCandidate>,
            std::greater<EdgeCandidate>> queue;
        {
            std::unordered_set<std::uint64_t> seen;
            seen.reserve(edge_reference.size());
            for (const auto& [key, count] : edge_reference)
            {
                (void)count;
                if (!seen.insert(key).second) continue;
                const std::uint32_t a = static_cast<std::uint32_t>(key >> 32);
                const std::uint32_t b = static_cast<std::uint32_t>(key & 0xFFFFFFFFull);
                if (a >= vertices.size() || b >= vertices.size()) continue;
                queue.push(make_candidate(a, b));
            }
        }

        std::uint32_t live_triangles = result.source_triangles;

        // --- 縮約ループ --------------------------------------------------------
        while (live_triangles > target_triangles && !queue.empty())
        {
            const EdgeCandidate candidate = queue.top();
            queue.pop();

            // 遅延削除: 頂点が既に潰された/更新されたエントリは無効。
            if (candidate.a >= vertices.size() || candidate.b >= vertices.size()) continue;
            if (vertex_removed[candidate.a] || vertex_removed[candidate.b]) continue;
            if (vertex_version[candidate.a] != candidate.version_a) continue;
            if (vertex_version[candidate.b] != candidate.version_b) continue;
            if (candidate.cost >= 1.0e29) break; // 潰せるエッジが尽きた
            if (options.error_limit > 0.0 && candidate.cost > options.error_limit) break;

            const std::uint32_t keep = candidate.a;
            const std::uint32_t drop = candidate.b;

            // 法線が裏返る縮約は棄却する。薄い部分が反転して黒くなるのを防ぐ。
            if (options.prevent_normal_flip)
            {
                bool flipped = false;
                for (const std::uint32_t triangle_index : vertex_triangles[drop])
                {
                    const Triangle& triangle = triangles[triangle_index];
                    if (triangle.removed) continue;
                    // 縮約で消える三角形(keepとdropを両方含む)は判定不要。
                    bool has_keep = false;
                    for (int corner = 0; corner < 3; ++corner)
                        if (triangle.v[corner] == keep) has_keep = true;
                    if (has_keep) continue;

                    XMFLOAT3 before[3];
                    XMFLOAT3 after[3];
                    for (int corner = 0; corner < 3; ++corner)
                    {
                        before[corner] = vertices[triangle.v[corner]].position;
                        after[corner] = (triangle.v[corner] == drop)
                            ? candidate.position : before[corner];
                    }
                    const XMFLOAT3 normal_before = TriangleNormal(before[0], before[1], before[2]);
                    const XMFLOAT3 normal_after = TriangleNormal(after[0], after[1], after[2]);
                    const float dot = normal_before.x * normal_after.x +
                        normal_before.y * normal_after.y + normal_before.z * normal_after.z;
                    if (!std::isfinite(dot) || dot < 0.0f) { flipped = true; break; }
                }
                if (flipped) continue;
            }

            // keep を最適位置へ動かし、属性は2頂点の平均で近似する。
            vertices[keep].position = candidate.position;
            vertices[keep].normal = {
                (vertices[keep].normal.x + vertices[drop].normal.x) * 0.5f,
                (vertices[keep].normal.y + vertices[drop].normal.y) * 0.5f,
                (vertices[keep].normal.z + vertices[drop].normal.z) * 0.5f };
            XMStoreFloat3(&vertices[keep].normal,
                XMVector3Normalize(XMLoadFloat3(&vertices[keep].normal)));
            vertices[keep].texcoord = {
                (vertices[keep].texcoord.x + vertices[drop].texcoord.x) * 0.5f,
                (vertices[keep].texcoord.y + vertices[drop].texcoord.y) * 0.5f };
            quadrics[keep].Add(quadrics[drop]);
            if (boundary_vertex[drop]) boundary_vertex[keep] = true;

            // drop を参照する三角形を keep へ付け替える。
            for (const std::uint32_t triangle_index : vertex_triangles[drop])
            {
                Triangle& triangle = triangles[triangle_index];
                if (triangle.removed) continue;
                for (int corner = 0; corner < 3; ++corner)
                    if (triangle.v[corner] == drop) triangle.v[corner] = keep;

                // 同じ頂点を2つ以上持つ三角形は面積0なので削除する。
                if (triangle.v[0] == triangle.v[1] ||
                    triangle.v[1] == triangle.v[2] ||
                    triangle.v[2] == triangle.v[0])
                {
                    triangle.removed = true;
                    if (live_triangles > 0) --live_triangles;
                }
                else
                {
                    vertex_triangles[keep].push_back(triangle_index);
                }
            }
            vertex_triangles[drop].clear();
            vertex_removed[drop] = true;
            ++vertex_version[keep];
            ++vertex_version[drop];
            result.max_error = (std::max)(result.max_error, candidate.cost);

            // keep の周りのエッジを作り直す。重複はキューが遅延削除で吸収する。
            std::unordered_set<std::uint32_t> neighbours;
            for (const std::uint32_t triangle_index : vertex_triangles[keep])
            {
                const Triangle& triangle = triangles[triangle_index];
                if (triangle.removed) continue;
                for (int corner = 0; corner < 3; ++corner)
                {
                    const std::uint32_t other = triangle.v[corner];
                    if (other != keep && !vertex_removed[other]) neighbours.insert(other);
                }
            }
            for (const std::uint32_t other : neighbours) queue.push(make_candidate(keep, other));
        }

        // --- 生き残った三角形で詰め直す ----------------------------------------
        std::vector<std::uint32_t> remap(vertices.size(), UINT32_MAX);
        std::vector<Vertex> output_vertices;
        std::vector<std::uint32_t> output_indices;
        output_vertices.reserve(vertices.size());
        output_indices.reserve(live_triangles * 3);

        for (const Triangle& triangle : triangles)
        {
            if (triangle.removed) continue;
            if (triangle.v[0] == triangle.v[1] ||
                triangle.v[1] == triangle.v[2] ||
                triangle.v[2] == triangle.v[0]) continue;
            for (int corner = 0; corner < 3; ++corner)
            {
                const std::uint32_t source = triangle.v[corner];
                if (remap[source] == UINT32_MAX)
                {
                    remap[source] = static_cast<std::uint32_t>(output_vertices.size());
                    output_vertices.push_back(vertices[source]);
                }
                output_indices.push_back(remap[source]);
            }
        }

        if (output_indices.empty()) return result; // 減らしすぎた場合は元を返す

        result.vertices = std::move(output_vertices);
        result.indices = std::move(output_indices);
        result.result_triangles = static_cast<std::uint32_t>(result.indices.size() / 3);
        return result;
    }
}
