// スキンメッシュの CPU データとアニメーションを読み込む。
#include "misc.h"
#include "skinned_mesh.h"
#include<fstream>
#include <sstream>
#include <functional>
#include <algorithm>

#include <cstring>
#include <filesystem>
#include <stdexcept>
using namespace DirectX;

skinned_mesh::skinned_mesh(const char* fbx_filename, bool triangulate, float sampling_rate)
    : skinned_mesh(std::filesystem::path(fbx_filename ? fbx_filename : ""), triangulate, sampling_rate)
{
}

skinned_mesh::skinned_mesh(const std::filesystem::path& source_filename,
    bool triangulate, float sampling_rate)
{
    std::string source_extension = source_filename.extension().string();
    std::transform(source_extension.begin(), source_extension.end(), source_extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (source_extension == ".glb" || source_extension == ".gltf")
    {
        // glTF を別 Renderer へ再実装せず、既存の骨・Animator・影・TAA 経路が
        // 読める skinned_mesh の CPU 表現へ一度だけ変換する。
        if (!import_gltf(source_filename, sampling_rate))
            throw std::runtime_error("glTFモデルをスキンメッシュへ変換できません: " +
                source_filename.u8string());
        return;
    }

    // string() は ACP へ落とせない文字で例外を投げる。u8string なら安全。
    const std::string narrow_source = source_filename.u8string();
    const char* fbx_filename = narrow_source.c_str();

    // UNIT30 手順5: シリアライズファイルのパスを作成 [cite: 316, 323]
    std::filesystem::path cereal_filename(fbx_filename);
    cereal_filename.replace_extension("cereal");

    // シリアライズファイルが存在する場合 [cite: 319, 321]
    if (std::filesystem::exists(cereal_filename))
    {
        // 既存の .cereal ファイルからロードする [cite: 325, 326, 327]
        std::ifstream ifs(cereal_filename.c_str(), std::ios::binary);
        cereal::BinaryInputArchive deserialization(ifs);
        deserialization(scene_view, meshes, materials, animation_clips); //
    }
    else
    {
#if REPLAY_ENABLE_FBX_IMPORTER
        // ファイルが存在しない場合は、従来通り FBX からインポートする 
        FbxManager* fbx_manager{ FbxManager::Create() };
        FbxScene* fbx_scene{ FbxScene::Create(fbx_manager, "") };

        FbxImporter* fbx_importer{ FbxImporter::Create(fbx_manager, "") };
        bool import_status{ fbx_importer->Initialize(fbx_filename) };
        _ASSERT_EXPR_A(import_status, fbx_importer->GetStatus().GetErrorString());

        import_status = fbx_importer->Import(fbx_scene);
        _ASSERT_EXPR_A(import_status, fbx_importer->GetStatus().GetErrorString());

        FbxGeometryConverter fbx_converter(fbx_manager);
        if (triangulate)
        {
            fbx_converter.Triangulate(fbx_scene, true/*replace*/, false/*legacy*/);
            fbx_converter.RemoveBadPolygonsFromMeshes(fbx_scene);
        }

        // シーン階層の構築
        std::function<void(FbxNode*)> traverse{ [&](FbxNode* fbx_node) {
            scene::node& node{ scene_view.nodes.emplace_back() };
            node.attribute = fbx_node->GetNodeAttribute() ?
                static_cast<int32_t>(fbx_node->GetNodeAttribute()->GetAttributeType()) : 0;
            node.name = fbx_node->GetName();
            node.unique_id = fbx_node->GetUniqueID();
            node.parent_index = scene_view.indexof(fbx_node->GetParent() ?
                fbx_node->GetParent()->GetUniqueID() : 0);
            for (int child_index = 0; child_index < fbx_node->GetChildCount(); ++child_index)
            {
                traverse(fbx_node->GetChild(child_index));
            }
        } };
        traverse(fbx_scene->GetRootNode());

        fetch_materials(fbx_scene, materials);
        fetch_meshes(fbx_scene, meshes);
        fetch_animations(fbx_scene, animation_clips, sampling_rate);

        fbx_importer->Destroy();
        fbx_manager->Destroy(); // [cite: 345]

        // 次回以降のためにシリアライズして保存する [cite: 345, 346]
        std::ofstream ofs(cereal_filename.c_str(), std::ios::binary);
        cereal::BinaryOutputArchive serialization(ofs);
        serialization(scene_view, meshes, materials, animation_clips); // [cite: 346]
#else
        (void)triangulate;
        (void)sampling_rate;
        throw std::runtime_error("実行用モデルキャッシュが見つかりません: " + cereal_filename.u8string());
#endif
    }

}
