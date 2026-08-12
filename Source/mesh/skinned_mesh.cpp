// skinned_mesh の責務を、生成・FBX取込・アニメーション・GPU資源・描画へ分ける。
//
//   skinned_mesh.cpp          … モデル生成と破棄（このファイル）
//   skinned_meshImport.cpp    … FBXメッシュ・骨格・マテリアルの取込
//   skinned_meshAnimation.cpp … FBXアニメーション取込と再生補助
//   skinned_meshDevice.cpp    … スキニング用GPU資源の作成と解放
//   skinned_meshRender.cpp    … スキニングメッシュ描画（モーションベクターを含む）
//   skinned_meshInternal.h    … FBX変換ヘルパの分割内部宣言

#include "misc.h"
#include "skinned_mesh.h"
#include<fstream>
#include <sstream>
#include <functional>
#include <algorithm>
#include"shader.h"
#include"static_mesh.h"
#include"sprite_batch.h"

#include"texture.h"
#include"../render/motion_vector_context.h"
#include"../../RePlayEngine/Rendering/RenderStats.h"
#include <cstring>
#include <filesystem>
#include <stdexcept>
using namespace DirectX;

skinned_mesh::skinned_mesh(ID3D11Device* device, const char* fbx_filename, bool triangulate, float sampling_rate)
{
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
        throw std::runtime_error("実行用モデルキャッシュが見つかりません: " + cereal_filename.string());
#endif
    }

    // Direct3Dリソース（バッファ・シェーダー）の作成（共通処理） [cite: 350]
    create_com_objects(device, fbx_filename);
}
