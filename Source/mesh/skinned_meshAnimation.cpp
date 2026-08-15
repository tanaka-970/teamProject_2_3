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
#include "skinned_meshInternal.h"

using namespace DirectX;
#if REPLAY_ENABLE_FBX_IMPORTER
using namespace skinned_mesh_detail;
#endif

#if REPLAY_ENABLE_FBX_IMPORTER
void skinned_mesh::fetch_animations(FbxScene* fbx_scene, std::vector<animation>& animation_clips,
    float sampling_rate /*If this value is 0, the animation data will be sampled at the default frame rate. */)
{
    FbxArray<FbxString*> animation_stack_names;
    fbx_scene->FillAnimStackNameArray(animation_stack_names);
    const int animation_stack_count{ animation_stack_names.GetCount() };
    for (int animation_stack_index = 0; animation_stack_index < animation_stack_count; ++animation_stack_index)
    {
        animation& animation_clip{ animation_clips.emplace_back() };
        animation_clip.name = animation_stack_names[animation_stack_index]->Buffer();

        FbxAnimStack* animation_stack{ fbx_scene->FindMember<FbxAnimStack>(animation_clip.name.c_str()) };
        fbx_scene->SetCurrentAnimationStack(animation_stack);

        const FbxTime::EMode time_mode{ fbx_scene->GetGlobalSettings().GetTimeMode() };
        FbxTime one_second;
        one_second.SetTime(0, 0, 1, 0, 0, time_mode);
        animation_clip.sampling_rate = sampling_rate > 0 ?
            sampling_rate : static_cast<float>(one_second.GetFrameRate(time_mode));
        const FbxTime sampling_interval
        { static_cast<FbxLongLong>(one_second.Get() / animation_clip.sampling_rate) };
        const FbxTakeInfo* take_info{ fbx_scene->GetTakeInfo(animation_clip.name.c_str()) };
        const FbxTime start_time{ take_info->mLocalTimeSpan.GetStart() };
        const FbxTime stop_time{ take_info->mLocalTimeSpan.GetStop() };
        for (FbxTime time = start_time; time < stop_time; time += sampling_interval)
        {
            animation::keyframe& keyframe{ animation_clip.sequence.emplace_back() };

            const size_t node_count{ scene_view.nodes.size() };
            keyframe.nodes.resize(node_count);
            for (size_t node_index = 0; node_index < node_count; ++node_index)
            {
                FbxNode* fbx_node{ fbx_scene->FindNodeByName(scene_view.nodes.at(node_index).name.c_str()) };
                if (fbx_node)
                {
                    animation::keyframe::node& node{ keyframe.nodes.at(node_index) };
                  
                    node.global_transform = to_xmfloat4x4(fbx_node->EvaluateGlobalTransform(time));



                    const FbxAMatrix& local_transform{ fbx_node->EvaluateLocalTransform(time) };
                    node.scaling = to_xmfloat3(local_transform.GetS());     // スケーリング取得 [cite: 394]
                    node.rotation = to_xmfloat4(local_transform.GetQ());   // 回転取得 [cite: 400]
                    node.translation = to_xmfloat3(local_transform.GetT()); // 平行移動取得 [cite: 400]
                }
            }
        }
    }
    for (int animation_stack_index = 0; animation_stack_index < animation_stack_count; ++animation_stack_index)
    {
        delete animation_stack_names[animation_stack_index];
    }

}
#endif
bool skinned_mesh::append_animations(const char* animation_filename, float sampling_rate)
{
#if REPLAY_ENABLE_FBX_IMPORTER
    FbxManager* fbx_manager{ FbxManager::Create() };
    FbxScene* fbx_scene{ FbxScene::Create(fbx_manager, "") };
    FbxImporter* fbx_importer{ FbxImporter::Create(fbx_manager, "") };

    bool import_status{ fbx_importer->Initialize(animation_filename) };
    _ASSERT_EXPR_A(import_status, fbx_importer->GetStatus().GetErrorString());

    import_status = fbx_importer->Import(fbx_scene);
    _ASSERT_EXPR_A(import_status, fbx_importer->GetStatus().GetErrorString());

    // 既存の animation_clips にアニメーションを追加
    fetch_animations(fbx_scene, animation_clips, sampling_rate);

    fbx_manager->Destroy();
    return true;
#else
    (void)animation_filename;
    (void)sampling_rate;
    return false;
#endif
}
void skinned_mesh::blend_animations(const animation::keyframe* keyframes[2], float factor, animation::keyframe& keyframe)
{
    size_t node_count{ keyframes[0]->nodes.size() };
    keyframe.nodes.resize(node_count);

    for (size_t node_index = 0; node_index < node_count; ++node_index)
    {
        // スケーリングの線形補完
        XMVECTOR S[2]{ XMLoadFloat3(&keyframes[0]->nodes.at(node_index).scaling),
                       XMLoadFloat3(&keyframes[1]->nodes.at(node_index).scaling) };
        XMStoreFloat3(&keyframe.nodes.at(node_index).scaling, XMVectorLerp(S[0], S[1], factor));

        // 回転の球面線形補完
        XMVECTOR R[2]{ XMLoadFloat4(&keyframes[0]->nodes.at(node_index).rotation),
                       XMLoadFloat4(&keyframes[1]->nodes.at(node_index).rotation) };
        XMStoreFloat4(&keyframe.nodes.at(node_index).rotation, XMQuaternionSlerp(R[0], R[1], factor));

        // 平行移動の線形補完
        XMVECTOR T[2]{ XMLoadFloat3(&keyframes[0]->nodes.at(node_index).translation),
                       XMLoadFloat3(&keyframes[1]->nodes.at(node_index).translation) };
        XMStoreFloat3(&keyframe.nodes.at(node_index).translation, XMVectorLerp(T[0], T[1], factor));

        keyframe.nodes.at(node_index).morph_weight =
            keyframes[0]->nodes.at(node_index).morph_weight +
            (keyframes[1]->nodes.at(node_index).morph_weight -
                keyframes[0]->nodes.at(node_index).morph_weight) * factor;
    }
}
void skinned_mesh::update_animation(animation::keyframe& keyframe)
{
    size_t node_count{ keyframe.nodes.size() };
        for (size_t node_index = 0; node_index < node_count; ++node_index)
        {
            animation::keyframe::node& node{ keyframe.nodes.at(node_index) };

                // SRTから行列を作成 [cite: 432]
                XMMATRIX S{ XMMatrixScaling(node.scaling.x, node.scaling.y, node.scaling.z) };
                XMMATRIX R{ XMMatrixRotationQuaternion(XMLoadFloat4(&node.rotation)) };
                XMMATRIX T{ XMMatrixTranslation(node.translation.x, node.translation.y, node.translation.z) };

                // 親ノードの行列を掛けてグローバル行列を求める [cite: 438]
                int64_t parent_index{ scene_view.nodes.at(node_index).parent_index };
                XMMATRIX P{ parent_index < 0 ? XMMatrixIdentity() :
                            XMLoadFloat4x4(&keyframe.nodes.at(parent_index).global_transform) };

                XMStoreFloat4x4(&node.global_transform, S* R* T* P);
        }
}
