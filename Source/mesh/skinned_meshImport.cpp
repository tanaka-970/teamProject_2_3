#include "misc.h"
#include "skinned_mesh.h"
#include<fstream>
#include <sstream>
#include <functional>
#include <algorithm>
#include"static_mesh.h"

#include"../render/motion_vector_context.h"
#include <cstring>
#include <filesystem>
#include <stdexcept>
using namespace DirectX;
#include "skinned_meshInternal.h"

using namespace DirectX;
#if REPLAY_ENABLE_FBX_IMPORTER
using namespace skinned_mesh_detail;

namespace
{
struct bone_influence
{
    uint32_t bone_index;
    float bone_weight;
};

using bone_influences_per_control_point = std::vector<bone_influence>;

void fetch_bone_influences(const FbxMesh* fbx_mesh,
    std::vector<bone_influences_per_control_point>& bone_influences)
{
    const int control_points_count{ fbx_mesh->GetControlPointsCount() };
    bone_influences.resize(control_points_count);

    const int skin_count{ fbx_mesh->GetDeformerCount(FbxDeformer::eSkin) };
    for (int skin_index = 0; skin_index < skin_count; ++skin_index)
    {
        const FbxSkin* fbx_skin
        { static_cast<FbxSkin*>(fbx_mesh->GetDeformer(skin_index, FbxDeformer::eSkin)) };

        const int cluster_count{ fbx_skin->GetClusterCount() };
        for (int cluster_index = 0; cluster_index < cluster_count; ++cluster_index)
        {
            const FbxCluster* fbx_cluster{ fbx_skin->GetCluster(cluster_index) };

            const int control_point_indices_count{ fbx_cluster->GetControlPointIndicesCount() };
            for (int control_point_indices_index = 0; control_point_indices_index < control_point_indices_count;
                ++control_point_indices_index)
            {
                int control_point_index{ fbx_cluster->GetControlPointIndices()[control_point_indices_index] };
                double control_point_weight
                { fbx_cluster->GetControlPointWeights()[control_point_indices_index] };
                bone_influence& bone_influence{ bone_influences.at(control_point_index).emplace_back() };
                bone_influence.bone_index = static_cast<uint32_t>(cluster_index);
                bone_influence.bone_weight = static_cast<float>(control_point_weight);
            }
        }
    }
}
}

#if REPLAY_ENABLE_FBX_IMPORTER
void skinned_mesh::fetch_meshes(FbxScene* fbx_scene, std::vector<mesh>& meshes)
{
    for (const scene::node& node : scene_view.nodes)
    {
        if (node.attribute != static_cast<int32_t>(FbxNodeAttribute::EType::eMesh))
        {
            continue;
        }

        FbxNode* fbx_node{ fbx_scene->FindNodeByName(node.name.c_str()) };
        FbxMesh* fbx_mesh{ fbx_node->GetMesh() };

        mesh& mesh{ meshes.emplace_back() };
        mesh.unique_id = fbx_node->GetUniqueID();
        mesh.name = fbx_mesh->GetNode()->GetName();
        mesh.node_index = scene_view.indexof(mesh.unique_id);
        //ユニット２１
        mesh.default_global_transform = to_xmfloat4x4(fbx_node->EvaluateGlobalTransform());

        // UNIT22 手順5: ボーン影響度の取得呼び出し
        std::vector<bone_influences_per_control_point> bone_influences;
        fetch_bone_influences(fbx_mesh, bone_influences);
		// UNIT24 手順4: skeleton 情報の抽出呼び出し
        fetch_skeleton(fbx_mesh, mesh.bind_pose);
        std::vector<mesh::subset>& subsets{ mesh.subsets };
        const int material_count{ fbx_mesh->GetNode()->GetMaterialCount() };
        subsets.resize(material_count > 0 ? material_count : 1);

        for (int material_index = 0; material_index < material_count; ++material_index)
        {
            const FbxSurfaceMaterial* fbx_material{ fbx_mesh->GetNode()->GetMaterial(material_index) };
            subsets.at(material_index).material_name = fbx_material->GetName();
            subsets.at(material_index).material_unique_id = fbx_material->GetUniqueID();
        }

        const int polygon_count{ fbx_mesh->GetPolygonCount() };

        if (material_count > 0)
        {
            for (int polygon_index = 0; polygon_index < polygon_count; ++polygon_index)
            {
                // 各ポリゴンがどのマテリアルを使用しているか取得
                const int material_index{ fbx_mesh->GetElementMaterial()->GetIndexArray().GetAt(polygon_index) };
                subsets.at(material_index).index_count += 3;
            }

            uint32_t offset{ 0 };
            for (mesh::subset& subset : subsets)
            {
                // マテリアルごとの開始位置を確定
                subset.start_index_location = offset;
                offset += subset.index_count;

                // 次のループで書き込み用カウンタとして使うため、一度 0 に戻す
                subset.index_count = 0;
            }
        }

        mesh.vertices.resize(polygon_count * 3LL);
        mesh.indices.resize(polygon_count * 3LL);

        FbxStringList uv_names;
        fbx_mesh->GetUVSetNames(uv_names);
        const FbxVector4* control_points{ fbx_mesh->GetControlPoints() };

        for (int polygon_index = 0; polygon_index < polygon_count; ++polygon_index)
        {
            // マテリアルインデックスの決定
            const int material_index{ material_count > 0 ?
                fbx_mesh->GetElementMaterial()->GetIndexArray().GetAt(polygon_index) : 0 };

            mesh::subset& subset{ subsets.at(material_index) };

            // このサブセットが書き込むべきインデックス配列上の位置
            const uint32_t offset{ subset.start_index_location + subset.index_count };

            for (int position_in_polygon = 0; position_in_polygon < 3; ++position_in_polygon)
            {
                const int vertex_index{ polygon_index * 3 + position_in_polygon };
                const int polygon_vertex
                { fbx_mesh->GetPolygonVertex(polygon_index, position_in_polygon) };

                vertex vertex;

                bone_influences_per_control_point influences_per_control_point
                { bone_influences.at(polygon_vertex) };
                std::sort(influences_per_control_point.begin(), influences_per_control_point.end(),
                    [](const bone_influence& lhs, const bone_influence& rhs)
                    {
                        return lhs.bone_weight > rhs.bone_weight;
                    });

                float total_weight = 0.0f;
                const size_t influence_count = std::min<size_t>(influences_per_control_point.size(), MAX_BONE_INFLUENCES);
                for (size_t influence_index = 0; influence_index < influence_count; ++influence_index)
                {
                    total_weight += influences_per_control_point.at(influence_index).bone_weight;
                }

                if (total_weight > 0.0f)
                {
                    for (size_t influence_index = 0; influence_index < MAX_BONE_INFLUENCES; ++influence_index)
                    {
                        vertex.bone_weights[influence_index] = 0.0f;
                        vertex.bone_indices[influence_index] = 0;
                    }
                    for (size_t influence_index = 0; influence_index < influence_count; ++influence_index)
                    {
                        vertex.bone_weights[influence_index] =
                            influences_per_control_point.at(influence_index).bone_weight / total_weight;
                        vertex.bone_indices[influence_index] =
                            influences_per_control_point.at(influence_index).bone_index;
                    }
                }

                // 座標
                vertex.position.x = static_cast<float>(control_points[polygon_vertex][0]);
                vertex.position.y = static_cast<float>(control_points[polygon_vertex][1]);
                vertex.position.z = static_cast<float>(control_points[polygon_vertex][2]);

                // 法線
                if (fbx_mesh->GetElementNormalCount() > 0)
                {
                    FbxVector4 normal;
                    fbx_mesh->GetPolygonVertexNormal(polygon_index, position_in_polygon, normal);
                    vertex.normal.x = static_cast<float>(normal[0]);
                    vertex.normal.y = static_cast<float>(normal[1]);
                    vertex.normal.z = static_cast<float>(normal[2]);
                }

                // UV
                if (fbx_mesh->GetElementUVCount() > 0)
                {
                    FbxVector2 uv;
                    bool unmapped_uv;
                    fbx_mesh->GetPolygonVertexUV(polygon_index, position_in_polygon, uv_names[0], uv, unmapped_uv);

                    vertex.texcoord.x = static_cast<float>(uv[0]);
                    vertex.texcoord.y = 1.0f - static_cast<float>(uv[1]);
                }

                if (fbx_mesh->GenerateTangentsData(0, false))
                {
                    const FbxGeometryElementTangent* tangent = fbx_mesh->GetElementTangent(0); 
                        vertex.tangent.x = static_cast<float>(tangent->GetDirectArray().GetAt(vertex_index)[0]); 
                        vertex.tangent.y = static_cast<float>(tangent->GetDirectArray().GetAt(vertex_index)[1]); 
                        vertex.tangent.z = static_cast<float>(tangent->GetDirectArray().GetAt(vertex_index)[2]); 
                        vertex.tangent.w = static_cast<float>(tangent->GetDirectArray().GetAt(vertex_index)[3]); 
                }
               
                // 頂点配列への格納
                mesh.vertices.at(vertex_index) = std::move(vertex);

                // インデックス配列への格納 (マテリアルごとのオフセット位置から配置)
                mesh.indices.at(static_cast<size_t>(offset) + position_in_polygon) = vertex_index;

                // カウンタを進める
                subset.index_count++;
            }

        }
        for (const vertex& v : mesh.vertices)
        {
            mesh.bounding_box[0].x = std::min<float>(mesh.bounding_box[0].x, v.position.x);
            mesh.bounding_box[0].y = std::min<float>(mesh.bounding_box[0].y, v.position.y);
            mesh.bounding_box[0].z = std::min<float>(mesh.bounding_box[0].z, v.position.z);
            mesh.bounding_box[1].x = std::max<float>(mesh.bounding_box[1].x, v.position.x);
            mesh.bounding_box[1].y = std::max<float>(mesh.bounding_box[1].y, v.position.y);
            mesh.bounding_box[1].z = std::max<float>(mesh.bounding_box[1].z, v.position.z);
        }
    }

}
void skinned_mesh::fetch_skeleton(FbxMesh* fbx_mesh, skeleton& bind_pose)
{
    const int deformer_count = fbx_mesh->GetDeformerCount(FbxDeformer::eSkin);
    for (int deformer_index = 0; deformer_index < deformer_count; ++deformer_index)
    {
        FbxSkin* skin = static_cast<FbxSkin*>(fbx_mesh->GetDeformer(deformer_index, FbxDeformer::eSkin));
        const int cluster_count = skin->GetClusterCount();
        bind_pose.bones.resize(cluster_count);
        for (int cluster_index = 0; cluster_index < cluster_count; ++cluster_index)
        {
            FbxCluster* cluster = skin->GetCluster(cluster_index);

            skeleton::bone& bone{ bind_pose.bones.at(cluster_index) };
            bone.name = cluster->GetLink()->GetName();
            bone.unique_id = cluster->GetLink()->GetUniqueID();
            bone.parent_index = bind_pose.indexof(cluster->GetLink()->GetParent()->GetUniqueID());

            bone.node_index = scene_view.indexof(bone.unique_id);

            // 'reference_global_init_position' is used to convert from local space of model(mesh) to
            // global space of scene.
            FbxAMatrix reference_global_init_position;
            cluster->GetTransformMatrix(reference_global_init_position);

            // 'cluster_global_init_position' is used to convert from local space of bone to
            // global space of scene.
            FbxAMatrix cluster_global_init_position;
            cluster->GetTransformLinkMatrix(cluster_global_init_position);

            // Matrices are defined using the Column Major scheme. When a FbxAMatrix represents a transformation
            // (translation, rotation and scale), the last row of the matrix represents the translation part of
            // the transformation.

            // Compose 'bone.offset_transform' matrix that transforms position from mesh space to bone space.
            // This matrix is called the offset matrix.
            bone.offset_transform
                = to_xmfloat4x4(cluster_global_init_position.Inverse() * reference_global_init_position);
        }
    }
}
#endif

#if REPLAY_ENABLE_FBX_IMPORTER
void skinned_mesh::fetch_materials(FbxScene* fbx_scene,
    std::unordered_map<uint64_t, material>& materials)
{
    const size_t node_count{ scene_view.nodes.size() };

    for (size_t node_index = 0; node_index < node_count; ++node_index)
    {
        const scene::node& node{ scene_view.nodes.at(node_index) };
        const FbxNode* fbx_node{ fbx_scene->FindNodeByName(node.name.c_str()) };
        const int material_count{ fbx_node->GetMaterialCount() };

        for (int material_index = 0; material_index < material_count; ++material_index)
        {
            const FbxSurfaceMaterial* fbx_material{ fbx_node->GetMaterial(material_index) };

            material material;
            material.name = fbx_material->GetName();
            material.unique_id = fbx_material->GetUniqueID();

            FbxProperty fbx_property;
            fbx_property = fbx_material->FindProperty(FbxSurfaceMaterial::sDiffuse);
            if (fbx_property.IsValid())
            {
                const FbxDouble3 color{ fbx_property.Get<FbxDouble3>() };
                material.Kd.x = static_cast<float>(color[0]);
                material.Kd.y = static_cast<float>(color[1]);
                material.Kd.z = static_cast<float>(color[2]);
                material.Kd.w = 1.0f;

                const FbxFileTexture* fbx_texture{ fbx_property.GetSrcObject<FbxFileTexture>() };
                material.texture_filenames[0] = fbx_texture ? fbx_texture->GetRelativeFileName() : "";
            }
            FbxProperty normal_property = fbx_material->FindProperty(FbxSurfaceMaterial::sNormalMap);
            if (normal_property.IsValid())
            {
                const FbxFileTexture* file_texture{ normal_property.GetSrcObject<FbxFileTexture>() };
                material.texture_filenames[1] = file_texture ? file_texture->GetRelativeFileName() : "";
            }

            materials.emplace(material.unique_id, std::move(material));
        }
        if (materials.empty())
        {
            material dummy;
            dummy.unique_id = 0;
            dummy.name = "dummy";
            
            dummy.Kd = { 1.0f, 1.0f, 1.0f, 1.0f };
            materials.emplace(dummy.unique_id, std::move(dummy));
        }
    }

}
#endif
#endif
