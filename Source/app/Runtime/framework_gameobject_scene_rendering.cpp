// GameObject / Component 基盤のうち「Mesh / Material 解決」を持つ。
//
//   framework_gameobject_scene_rendering.cpp           … Primitive生成、Mesh / Material 解決（このファイル）
//   framework_gameobject_scene_rendering_draw.cpp      … Object / Landscape 描画
//   framework_gameobject_scene_rendering_animation.cpp … Mesh cache / Animation 解決
//
// 関数本体は分割前のまま移動し、Material fallback と描画経路の選択は変更しない。
#include "framework.h"

#include "gltf_model.h"
#include "skinned_mesh.h"

#include "../../RePlayEngine/Components/Camera/CameraComponent.h"
#include "../../RePlayEngine/Components/Camera/CameraTargetComponent.h"
#include "../../RePlayEngine/Components/Camera/FollowTargetComponent.h"
#include "../../RePlayEngine/Components/Motion/MotionPlayerComponent.h"
#include "../../RePlayEngine/Components/Core/PropertyLinkComponent.h"
#include "../../RePlayEngine/Components/UI/UIEffectStackComponent.h"
#include "../../RePlayEngine/Components/UI/UISpriteAnimatorComponent.h"
#include "../../RePlayEngine/Components/UI/UITextComponent.h"
#include "../../RePlayEngine/Components/Rendering/LightComponents.h"
#include "../../RePlayEngine/Components/Rendering/MeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/MaterialOverrideDynamicProperties.h"
#include "../../RePlayEngine/Components/Rendering/PrimitiveMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/SkinnedMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeRendererComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeColliderComponent.h"
#include "../../RePlayEngine/Rendering/Shaders/BuiltInShaders.h"
#include "../../RePlayEngine/Rendering/ShaderStack/BuiltInShaderLayers.h"
#include "../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../RePlayEngine/Project/ProjectSettingsSerializer.h"
#include "../../RePlayEngine/Rendering/Adapter/SceneRenderCollector.h"
#include "../../RePlayEngine/Motion/MotionBindingResolver.h"
#include "../../RePlayEngine/Motion/MotionEvaluator.h"
#include "../../RePlayEngine/UI/UILayout.h"
#include "../../RePlayEngine/Runtime/Events/EventBus.h"
#include "../../RePlayEngine/Scene/Serialization/PrefabSerializer.h"
#include "../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../RePlayEngine/Scene/Serialization/SceneSerializer.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpScriptBackend.h"
#include "../../RePlayEngine/Scripting/Core/ScriptComponent.h"
#include "../../RePlayEngine/Scripting/Core/ScriptRuntime.h"
#include "../../RePlayEngine/Scripting/Core/ScriptTypeCatalog.h"
#include "../../RePlayEngine/Scripting/Core/ScriptTypes.h"
#include "../../game/Behaviours/ValidationBehaviours.h"

#include <commdlg.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using PrimitiveVertex = static_mesh::vertex;

    void AppendPrimitiveQuad(std::vector<PrimitiveVertex>& vertices,
        std::vector<std::uint32_t>& indices,
        const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b,
        const DirectX::XMFLOAT3& c, const DirectX::XMFLOAT3& d,
        const DirectX::XMFLOAT3& normal)
    {
        const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back({ a, normal, { 0.0f, 1.0f } });
        vertices.push_back({ b, normal, { 0.0f, 0.0f } });
        vertices.push_back({ c, normal, { 1.0f, 0.0f } });
        vertices.push_back({ d, normal, { 1.0f, 1.0f } });
        indices.insert(indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }

    bool BuildBuiltinPrimitive(const std::string& id,
        std::vector<PrimitiveVertex>& vertices, std::vector<std::uint32_t>& indices)
    {
        using namespace DirectX;
        vertices.clear();
        indices.clear();

        if (id == "builtin:plane")
        {
            AppendPrimitiveQuad(vertices, indices,
                { -0.5f, 0.0f, -0.5f }, { -0.5f, 0.0f, 0.5f },
                { 0.5f, 0.0f, 0.5f }, { 0.5f, 0.0f, -0.5f },
                { 0.0f, 1.0f, 0.0f });
            return true;
        }
        if (id == "builtin:quad")
        {
            AppendPrimitiveQuad(vertices, indices,
                { -0.5f, -0.5f, 0.0f }, { -0.5f, 0.5f, 0.0f },
                { 0.5f, 0.5f, 0.0f }, { 0.5f, -0.5f, 0.0f },
                { 0.0f, 0.0f, -1.0f });
            return true;
        }
        if (id == "builtin:cube")
        {
            const float h = 0.5f;
            AppendPrimitiveQuad(vertices, indices, { -h,-h,-h }, { -h, h,-h }, { h, h,-h }, { h,-h,-h }, { 0,0,-1 });
            AppendPrimitiveQuad(vertices, indices, { h,-h, h }, { h, h, h }, { -h, h, h }, { -h,-h, h }, { 0,0,1 });
            AppendPrimitiveQuad(vertices, indices, { -h,-h, h }, { -h, h, h }, { -h, h,-h }, { -h,-h,-h }, { -1,0,0 });
            AppendPrimitiveQuad(vertices, indices, { h,-h,-h }, { h, h,-h }, { h, h, h }, { h,-h, h }, { 1,0,0 });
            AppendPrimitiveQuad(vertices, indices, { -h, h,-h }, { -h, h, h }, { h, h, h }, { h, h,-h }, { 0,1,0 });
            AppendPrimitiveQuad(vertices, indices, { -h,-h, h }, { -h,-h,-h }, { h,-h,-h }, { h,-h, h }, { 0,-1,0 });
            return true;
        }

        const bool sphere = id == "builtin:sphere";
        const bool capsule = id == "builtin:capsule";
        if (sphere || capsule)
        {
            constexpr int slices = 24;
            constexpr int stacks = 16;
            const float radius = 0.5f;
            const float capsule_half_cylinder = capsule ? 0.5f : 0.0f;
            for (int stack = 0; stack <= stacks; ++stack)
            {
                const float v = static_cast<float>(stack) / stacks;
                const float latitude = -XM_PIDIV2 + v * XM_PI;
                const float cos_lat = std::cos(latitude);
                const float sin_lat = std::sin(latitude);
                for (int slice = 0; slice <= slices; ++slice)
                {
                    const float u = static_cast<float>(slice) / slices;
                    const float longitude = u * XM_2PI;
                    XMFLOAT3 normal{ cos_lat * std::cos(longitude), sin_lat,
                        cos_lat * std::sin(longitude) };
                    XMFLOAT3 position{ normal.x * radius, normal.y * radius,
                        normal.z * radius };
                    if (capsule)
                        position.y += normal.y >= 0.0f ? capsule_half_cylinder : -capsule_half_cylinder;
                    vertices.push_back({ position, normal, { u, 1.0f - v } });
                }
            }
            const int stride = slices + 1;
            for (int stack = 0; stack < stacks; ++stack)
            {
                for (int slice = 0; slice < slices; ++slice)
                {
                    const std::uint32_t a = static_cast<std::uint32_t>(stack * stride + slice);
                    const std::uint32_t b = a + 1;
                    const std::uint32_t c = a + stride;
                    const std::uint32_t d = c + 1;
                    indices.insert(indices.end(), { a, c, b, b, c, d });
                }
            }
            return true;
        }

        if (id == "builtin:cylinder")
        {
            constexpr int slices = 24;
            const float radius = 0.5f;
            const float half_height = 0.5f;
            for (int slice = 0; slice <= slices; ++slice)
            {
                const float u = static_cast<float>(slice) / slices;
                const float angle = u * XM_2PI;
                const XMFLOAT3 normal{ std::cos(angle), 0.0f, std::sin(angle) };
                vertices.push_back({ { normal.x * radius, -half_height, normal.z * radius }, normal, { u,1 } });
                vertices.push_back({ { normal.x * radius, half_height, normal.z * radius }, normal, { u,0 } });
            }
            for (int slice = 0; slice < slices; ++slice)
            {
                const std::uint32_t a = static_cast<std::uint32_t>(slice * 2);
                const std::uint32_t b = a + 1;
                const std::uint32_t c = a + 2;
                const std::uint32_t d = a + 3;
                indices.insert(indices.end(), { a,b,c, c,b,d });
            }
            const std::uint32_t bottom_center = static_cast<std::uint32_t>(vertices.size());
            vertices.push_back({ {0,-half_height,0}, {0,-1,0}, {0.5f,0.5f} });
            const std::uint32_t top_center = static_cast<std::uint32_t>(vertices.size());
            vertices.push_back({ {0,half_height,0}, {0,1,0}, {0.5f,0.5f} });
            const std::uint32_t cap_start = static_cast<std::uint32_t>(vertices.size());
            for (int slice = 0; slice < slices; ++slice)
            {
                const float angle = static_cast<float>(slice) / slices * XM_2PI;
                const float x = std::cos(angle) * radius, z = std::sin(angle) * radius;
                vertices.push_back({ {x,-half_height,z},{0,-1,0},{x+0.5f,z+0.5f} });
                vertices.push_back({ {x, half_height,z},{0, 1,0},{x+0.5f,z+0.5f} });
            }
            for (int slice = 0; slice < slices; ++slice)
            {
                const int next = (slice + 1) % slices;
                const std::uint32_t b0 = cap_start + static_cast<std::uint32_t>(slice * 2);
                const std::uint32_t b1 = cap_start + static_cast<std::uint32_t>(next * 2);
                const std::uint32_t t0 = b0 + 1, t1 = b1 + 1;
                indices.insert(indices.end(), { bottom_center,b1,b0, top_center,t0,t1 });
            }
            return true;
        }
        return false;
    }
}
// ---------------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------------


gltf_model* framework::resolve_object_gltf(const std::string& asset_guid)
{
    if (asset_guid.empty() || !device) return nullptr;

    const ReplayEngine::Assets::AssetRecord* record = asset_database.FindByGuid(asset_guid);
    if (record == nullptr) return nullptr;

    const std::filesystem::path source = content_path(record->source_path);
    std::string extension = source.extension().string();
    for (char& character : extension)
    {
        character = static_cast<char>(::tolower(static_cast<unsigned char>(character)));
    }
    if (extension != ".glb" && extension != ".gltf") return nullptr;
    if (object_mesh_failures.find(asset_guid) != object_mesh_failures.end()) return nullptr;

    const auto give_up = [this, &asset_guid](const std::string& reason) -> gltf_model*
    {
        object_mesh_failures.insert(asset_guid);
        const std::string message = "[Mesh] " + reason + " (GUID: " + asset_guid + ")";
        OutputDebugStringA((message + "\n").c_str());
        object_editor_context.SetStatus(message);
        return nullptr;
    };

    std::error_code filesystem_error;
    if (!std::filesystem::exists(source, filesystem_error) || filesystem_error)
    {
        return give_up("glTF/GLB ファイルが見つかりません: " + source.generic_string());
    }

    try
    {
        // Asset Browser のプレビューと GameObject は同じキャッシュを共有する。
        // 参考実装の Repository と同じく、モデル本体を Object ごとに複製しない。
        const auto loaded = gltf_model_cache.Load(source, [this, source]
        {
            return std::make_shared<gltf_model>(device.Get(), source.string());
        });
        if (!loaded || !loaded->IsLoaded())
        {
            return give_up(loaded && !loaded->Error().empty()
                ? "glTF/GLB の読み込みに失敗しました: " + loaded->Error()
                : "glTF/GLB を構築できませんでした: " + source.generic_string());
        }
        return loaded.get();
    }
    catch (const std::exception& exception)
    {
        return give_up("glTF/GLB の読み込みに失敗しました: " +
            std::string(exception.what()));
    }
    catch (...)
    {
        return give_up("glTF/GLB の読み込み中に不明なエラーが発生しました: " +
            source.generic_string());
    }
}

skinned_mesh* framework::resolve_object_mesh(const std::string& asset_guid)
{
    // 1) Asset 未指定。Editor で MeshRenderer を付けただけの状態はこれになる。
    //    正常な状態なので警告も出さず、静かに描画対象から外す。
    if (asset_guid.empty()) return nullptr;
    if (!device) return nullptr;

    // 2) 読み込み済みならそれを返す。キャッシュには有効なメッシュしか入らない。
    const auto cached = object_mesh_cache.find(asset_guid);
    if (cached != object_mesh_cache.end()) return cached->second.get();

    // 3) 一度失敗した Asset は再試行しない。ログも一度きりで済む。
    if (object_mesh_failures.find(asset_guid) != object_mesh_failures.end()) return nullptr;

    // 失敗を記録してログへ出す。以降このフレームでは何も返さない。
    const auto give_up = [this, &asset_guid](const std::string& reason) -> skinned_mesh*
    {
        object_mesh_failures.insert(asset_guid);
        const std::string message = "[Mesh] " + reason + " (GUID: " + asset_guid + ")";
        OutputDebugStringA((message + "\n").c_str());
        // Editor のステータス欄にも出して、原因が画面から分かるようにする。
        object_editor_context.SetStatus(message);
        return nullptr;
    };

    // 4) GUID が AssetDatabase で解決できるか。
    //    古い Scene ファイルや EditorSession に残った GUID はここで弾かれる。
    const ReplayEngine::Assets::AssetRecord* record = asset_database.FindByGuid(asset_guid);
    if (record == nullptr)
    {
        return give_up("Asset がプロジェクトに登録されていません");
    }

    // 5) 対応拡張子かどうか。GLB/glTF は内容に Skin がある場合だけ
    //    skinned_mesh 表現へ変換し、既存 Animator/影/TAA を再利用する。
    const std::filesystem::path source = content_path(record->source_path);
    std::string extension = source.extension().string();
    for (char& character : extension)
    {
        character = static_cast<char>(::tolower(static_cast<unsigned char>(character)));
    }
    const bool gltf_source = extension == ".glb" || extension == ".gltf";
    if (!gltf_source && extension != ".fbx" && extension != ".cereal")
    {
        return give_up("この形式は GameObject の描画へ接続していません（" +
            (extension.empty() ? std::string("拡張子なし") : extension) + "）: " +
            source.generic_string());
    }

    // FBXだけは従来どおり隣接.cereal必須。GLB/glTFは本体から直接変換する。
    if (!gltf_source)
    {
        std::filesystem::path cache = source;
        cache.replace_extension(L".cereal");
        std::error_code filesystem_error;
        if (!std::filesystem::exists(cache, filesystem_error) || filesystem_error)
            return give_up("実行用の .cereal キャッシュが見つかりません: " + cache.generic_string());
    }

    // 7) ここまで通ってから構築する。
    std::unique_ptr<skinned_mesh> loaded;
    try
    {
        loaded = std::make_unique<skinned_mesh>(device.Get(), source.string().c_str());
    }
    catch (...)
    {
        // 既存プロジェクトは例外を前提にしていないため、ここで受け止めて
        // 「描けない Asset」として扱う。Scene 全体の描画は継続する。
        return give_up("メッシュの読み込みに失敗しました: " + source.generic_string());
    }

    if (!loaded)
    {
        return give_up("メッシュを構築できませんでした: " + source.generic_string());
    }

    // 8) 成功したものだけをキャッシュへ入れる。
    skinned_mesh* raw = loaded.get();
    object_mesh_cache.emplace(asset_guid, std::move(loaded));
    return raw;
}
static_mesh* framework::resolve_builtin_primitive_mesh(const std::string& builtin_id)
{
    if (!device || builtin_id.rfind("builtin:", 0) != 0) return nullptr;
    const auto cached = builtin_primitive_mesh_cache.find(builtin_id);
    if (cached != builtin_primitive_mesh_cache.end()) return cached->second.get();

    std::vector<static_mesh::vertex> vertices;
    std::vector<std::uint32_t> indices;
    if (!BuildBuiltinPrimitive(builtin_id, vertices, indices)) return nullptr;
    auto mesh = std::make_unique<static_mesh>(device.Get(), vertices, indices);
    if (!mesh || !mesh->is_loaded()) return nullptr;
    static_mesh* raw = mesh.get();
    builtin_primitive_mesh_cache.emplace(builtin_id, std::move(mesh));
    return raw;
}

const ReplayEngine::Rendering::MaterialAsset* framework::resolve_object_material(
    const std::string& asset_guid)
{
    using ReplayEngine::Assets::AssetKind;
    using ReplayEngine::Rendering::MaterialAsset;

    if (asset_guid.empty()) return nullptr;
    const ReplayEngine::Assets::AssetRecord* record = asset_database.FindByGuid(asset_guid);
    if (record == nullptr || record->kind != AssetKind::Material) return nullptr;

    const std::filesystem::path material_path = content_path(record->source_path);
    std::error_code filesystem_error;
    const auto write_time = std::filesystem::last_write_time(
        material_path, filesystem_error);
    if (filesystem_error)
    {
        object_material_failures.insert(asset_guid);
        return nullptr;
    }

    const auto cached = object_material_cache.find(asset_guid);
    if (cached != object_material_cache.end() && cached->second.write_time == write_time)
        return &cached->second.material;

    MaterialAsset loaded;
    std::string error;
    if (!MaterialAsset::Load(material_path, loaded, error))
    {
        if (object_material_failures.insert(asset_guid).second)
            OutputDebugStringA(("[Material] " + error + " (GUID: " + asset_guid + ")\n").c_str());
        return nullptr;
    }

    object_material_failures.erase(asset_guid);
    cached_material_asset entry;
    entry.material = std::move(loaded);
    entry.write_time = write_time;
    auto inserted = object_material_cache.insert_or_assign(asset_guid, std::move(entry));
    return &inserted.first->second.material;
}
