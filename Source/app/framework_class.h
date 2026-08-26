#pragma once

#include "framework_application.h"

#include "static_mesh.h"
#include "pbr_renderer.h"
#include "toon_renderer.h"
#include "csm_renderer.h"
#include "lights_manager.h"
#include "shading_model.h"
#include "../game/game_scene.h"
#include "../game/game_input.h"
#include "../../RePlayEngine/Scene/SceneManager.h"
#include "../../RePlayEngine/Rendering/Passes/PostProcessPass.h"
#include "../../RePlayEngine/Rendering/Passes/BloomPass.h"
#include "../../RePlayEngine/Rendering/Passes/SsaoPass.h"
#include "../../RePlayEngine/Rendering/Passes/SsrPass.h"
#include "../../RePlayEngine/Rendering/Passes/TaaPass.h"
#include "../../RePlayEngine/Rendering/Deferred/TiledDeferredPass.h"
#include "../../RePlayEngine/Rendering/Shadows/LocalShadowAtlas.h"
#include "../../RePlayEngine/Rendering/FrameConstants.h"
#include "../../RePlayEngine/Rendering/RenderStats.h"
#include "../../RePlayEngine/Rendering/DX12/D3D12DeviceContext.h"
#include "../../RePlayEngine/Rendering/Frustum.h"
#include "../render/motion_vector_context.h"
#include "../../RePlayEngine/Rendering/RenderGraph/RenderGraph.h"
#include "../../RePlayEngine/Rendering/ShaderStack/ShaderLayerStack.h"
#include "../../RePlayEngine/Rendering/Materials/CharacterMaterialProfile.h"
#include "../../RePlayEngine/Rendering/Materials/CharacterMaterialGpuData.h"
#include "../../RePlayEngine/Rendering/Materials/MaterialAsset.h"
#include "../../RePlayEngine/Rendering/Shaders/ShaderLibrary.h"
#include "../../RePlayEngine/Rendering/Capture/GoldenImage.h"
#include "../../RePlayEngine/Assets/AssetDatabase.h"
#include "../../RePlayEngine/Assets/SpriteAtlasAsset.h"
#include "../../RePlayEngine/Assets/AsyncAssetManager.h"
#include "../../RePlayEngine/Assets/ConcurrentResourceCache.h"
#include "../../RePlayEngine/Core/ObjectID/RuntimeIdentity.h"
#include "../../RePlayEngine/Motion/CompositionAsset.h"
#include "../../RePlayEngine/Motion/MotionAsset.h"
#include "../../RePlayEngine/Motion/MotionMixer.h"
#include "../../RePlayEngine/Reflection/Property/PropertyBag.h"
#include "../../RePlayEngine/UI/FontAtlas.h"
#include "../../RePlayEngine/UI/UIInputFieldSystem.h"
#include "../../RePlayEngine/Audio/AudioSystem.h"
#include "../../RePlayEngine/Runtime/API/RuntimeSaveGameService.h"
#include "../../RePlayEngine/Project/ProjectSettings.h"
#include "../../RePlayEngine/Editor/Gizmo/TransformGizmo.h"
#include "../../RePlayEngine/Editor/Gizmo/ViewportPicker.h"
#include "../../RePlayEngine/Components/Editor/EditorNoteComponent.h"
#include "../../RePlayEngine/Components/Rendering/LineRendererComponent.h"
#include "../../RePlayEngine/Physics/MeshCollisionCooker.h"
#include "../../RePlayEngine/Landscape/LandscapeBrush.h"
#include "../../RePlayEngine/Landscape/LandscapeEditorTool.h"

// --- GameObject / Component 基盤 -------------------------------------------
// SceneManager とは責任が違う。
//   SceneManager  … 起動ロゴ / ロード画面 / ゲームという画面遷移
//   Scene (下記)  … GameObject の入れ物。今回の新基盤。
#include "../../RePlayEngine/Scene/Runtime/Scene.h"
#include "../../RePlayEngine/Runtime/API/RuntimeContext.h"
#include "../../RePlayEngine/Runtime/Events/CollisionEventDispatcher.h"
#include "../../RePlayEngine/Runtime/Scene/RuntimeSceneService.h"
#include "../../RePlayEngine/Scripting/Core/ScriptRuntime.h"
#include "../../RePlayEngine/Runtime/Scene/SceneFlowService.h"
#include "../../RePlayEngine/Editor/Core/EditorContext.h"
#include "../../RePlayEngine/Editor/Commands/MotionEditHistory.h"
#include "../../RePlayEngine/Editor/Commands/FileEditHistory.h"
#include "../../RePlayEngine/Editor/Hierarchy/HierarchyPanel.h"
#include "../../RePlayEngine/Editor/Inspector/InspectorPanel.h"
#include "../../RePlayEngine/Rendering/Adapter/RenderItem.h"
#include "../../RePlayEngine/Scene/Services/PlayerControlSystem.h"
#include "../../RePlayEngine/Scene/Services/SceneCollisionWorld.h"
#include "../../RePlayEngine/Physics/PhysicsDynamicsWorld.h"
#include "../../RePlayEngine/Editor/Debug/ColliderDebugDraw.h"
#include "../../RePlayEngine/Editor/Validation/ValidationPanel.h"
#include "../../RePlayEngine/Editor/ShaderEditing/ShaderComposerEditor.h"
#include "../../RePlayEngine/Editor/Viewport/EditorViewportCamera.h"
#include "../../RePlayEngine/Editor/Viewport/EditorCameraController.h"
#include "../../RePlayEngine/Editor/Viewport/EditorCameraPreset.h"
#include "../../RePlayEngine/Editor/Viewport/EditorCameraStateStore.h"
#include "../game/camera_basis_provider.h"

#include <unordered_map>
#include <unordered_set>

// Runtime World の所有と診断で使う。
// 推移的な include に頼ると、上流のヘッダーを整理した瞬間に壊れる。
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>


namespace ReplayEngine::Editor { struct GoldenImageState; }

class framework
{
    friend struct ReplayEngine::Editor::GoldenImageState;

    // framework_class.h 分割一覧:
    //   framework_class_render_state.inl     … DX12 描画状態と基礎状態
    //   framework_class_runtime_scene.inl    … Runtime World/衝突/編集カメラ
    //   framework_class_application_loop.inl … 起動設定と run() メインループ
    //   framework_class_editor_commands.inl  … Window/Editor/制作機能の宣言
    //   framework_class_scene_services.inl   … Scene/Runtime/描画/衝突サービス接続
    //   framework_class_editor_state.inl     … Editor の一時状態・Workspace・各ツール状態
    //
    // すべて class framework の内部へテキスト展開するため、メンバ宣言順は元と完全に同一。
#include "framework_class_render_state.inl"
#include "framework_class_runtime_scene.inl"
#include "framework_class_application_loop.inl"
#include "framework_class_editor_commands.inl"
#include "framework_class_scene_services.inl"
#include "framework_class_editor_state.inl"
};
