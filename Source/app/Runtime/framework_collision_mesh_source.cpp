// 衝突用の三角形をどこから取ってくるかを決める場所。
//
// Cook キャッシュ（CookedMeshCollisionCache）は「三角形をどう速く引くか」だけを担当し、
// 「三角形をどこから読むか」は知らない。その接続をここで行う。
//
// 【ローカル座標で返すこと】
//   ここが返す三角形は必ず「モデルのローカル座標」。
//   ワールドへ変換して返すと、同じ Asset を別の場所へ置いた 2 体で
//   Cook 結果を共有できなくなる。Transform は MeshCollider 側が持つ。

#include "framework.h"

#include "skinned_mesh.h"

#include <cctype>
#include <sstream>

namespace
{
    // Cook キーを、ディスクキャッシュのファイル名に使える文字列へ畳む。
    //
    // AssetGUID だけをファイル名にすると、cell size や double sided を変えても
    // 同じファイルを読んでしまう。キーの全要素を混ぜた名前にする。
    std::string MakeCacheIdentity(const ReplayEngine::Physics::CookKey& key)
    {
        const std::size_t hash = ReplayEngine::Physics::CookKeyHash{}(key);

        std::string identity = key.asset_guid;
        for (char& character : identity)
        {
            const unsigned char raw = static_cast<unsigned char>(character);
            if (!std::isalnum(raw) && character != '_' && character != '-') character = '_';
        }

        std::ostringstream stream;
        stream << identity << '_' << std::hex << hash;
        return stream.str();
    }
}

std::string framework::resolve_asset_revision(const std::string& asset_guid) const
{
    // 「実体が変われば必ず変わる文字列」を返す。
    // 更新時刻とサイズの組み合わせで足りる。content hash まではまだ取らない
    // （大きな FBX を毎回全走査すると、起動が目に見えて遅くなるため）。
    const ReplayEngine::Assets::AssetRecord* record = asset_database.FindByGuid(asset_guid);
    if (record == nullptr) return std::string();

    std::filesystem::path source = record->source_path;
    std::filesystem::path cache = source;
    cache.replace_extension(L".cereal");

    std::error_code filesystem_error;

    // 実際に読むのは .cereal なので、そちらを優先して見る。
    const std::filesystem::path& target =
        std::filesystem::exists(cache, filesystem_error) && !filesystem_error ? cache : source;

    const auto write_time = std::filesystem::last_write_time(target, filesystem_error);
    if (filesystem_error) return std::string();

    const auto size = std::filesystem::file_size(target, filesystem_error);
    if (filesystem_error) return std::string();

    std::ostringstream stream;
    stream << write_time.time_since_epoch().count() << ':' << size;
    return stream.str();
}

bool framework::load_collision_triangles(const ReplayEngine::Physics::CookKey& key,
    std::vector<ReplayEngine::Physics::Triangle>& out_local_triangles)
{
    using ReplayEngine::Physics::Triangle;

    out_local_triangles.clear();
    if (key.asset_guid.empty()) return false;

    const std::string identity = MakeCacheIdentity(key);

    // 1) ディスクの Cook キャッシュがあればそれを使う。
    //    ファイル名にキー全体のハッシュが入っているので、
    //    設定違い・再インポート後の古い結果を読むことはない。
    {
        ReplayEngine::Physics::CollisionCookResult result{};
        std::string error;
        const std::filesystem::path cache_path =
            std::filesystem::path("resources") / ".replay_cache" / "collisions" /
            (identity + "_v1.replaycollision");

        std::error_code filesystem_error;
        if (std::filesystem::exists(cache_path, filesystem_error) && !filesystem_error &&
            collision_cooker.Load(cache_path, out_local_triangles, result, error) &&
            !out_local_triangles.empty())
        {
            return true;
        }
        out_local_triangles.clear();
    }

    // 2) メッシュ本体から取り出す。
    //    resolve_object_mesh は失敗を内部で記録し、同じ Asset を何度も試さない。
    skinned_mesh* mesh = resolve_object_mesh(key.asset_guid);
    if (mesh == nullptr)
    {
        if (object_collision_failures.insert(key.asset_guid).second)
        {
            const std::string message =
                "[Collision] 衝突メッシュを読み込めません (GUID: " + key.asset_guid + ")";
            OutputDebugStringA((message + "\n").c_str());
            object_editor_context.SetStatus(message);
        }
        return false;
    }

    using namespace DirectX;

    std::size_t triangle_count = 0;
    for (const auto& sub_mesh : mesh->meshes) triangle_count += sub_mesh.indices.size() / 3;
    out_local_triangles.reserve(triangle_count);

    int sub_mesh_index = 0;
    for (const auto& sub_mesh : mesh->meshes)
    {
        const int current = sub_mesh_index++;

        // sub_mesh_index が指定されていれば、その 1 つだけを使う。
        if (key.settings.sub_mesh_index >= 0 && key.settings.sub_mesh_index != current)
        {
            continue;
        }

        // default_global_transform はサブメッシュをモデル空間へ置く行列。
        // ここまでで「モデルのローカル座標」になる。
        // Stage のワールド行列は掛けない（掛けると共有できなくなる）。
        const XMMATRIX to_model = XMLoadFloat4x4(&sub_mesh.default_global_transform);

        for (std::size_t index = 0; index + 2 < sub_mesh.indices.size(); index += 3)
        {
            const std::uint32_t indices[3]{
                sub_mesh.indices[index],
                sub_mesh.indices[index + 1],
                sub_mesh.indices[index + 2] };

            if (indices[0] >= sub_mesh.vertices.size() ||
                indices[1] >= sub_mesh.vertices.size() ||
                indices[2] >= sub_mesh.vertices.size())
            {
                continue;
            }

            Triangle triangle{};
            for (int vertex = 0; vertex < 3; ++vertex)
            {
                XMStoreFloat3(&triangle.vertices[vertex], XMVector3TransformCoord(
                    XMLoadFloat3(&sub_mesh.vertices[indices[vertex]].position), to_model));
            }
            out_local_triangles.push_back(triangle);
        }
    }

    if (out_local_triangles.empty())
    {
        if (object_collision_failures.insert(key.asset_guid).second)
        {
            const std::string message =
                "[Collision] 衝突三角形が 0 件でした (GUID: " + key.asset_guid + ")";
            OutputDebugStringA((message + "\n").c_str());
            object_editor_context.SetStatus(message);
        }
        return false;
    }

    // 3) 次回のために書き出す。失敗しても衝突は成立するので、
    //    ここでは false を返さない。
    ReplayEngine::Physics::CollisionCookResult result{};
    std::string error;
    if (!collision_cooker.Cook(identity, out_local_triangles, result, error))
    {
        OutputDebugStringA(("[Collision] Cook キャッシュを書き出せません: " +
            error + "\n").c_str());
    }
    return true;
}
