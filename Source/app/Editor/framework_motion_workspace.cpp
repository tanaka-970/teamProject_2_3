#include "framework.h"

#include "../../RePlayEngine/Components/UI/UIImageComponent.h"
#include "../../RePlayEngine/Motion/MotionBindingResolver.h"
#include "../../RePlayEngine/Motion/MotionEvaluator.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Object/Registry/ComponentRegistry.h"
#include "../../RePlayEngine/Reflection/Registry/PropertyRegistry.h"
#include "../../RePlayEngine/Scene/Runtime/Scene.h"

#include "imgui/imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>


// 分割一覧（framework_motion_workspace.cpp）:
//   framework_motion_workspaceInternal.h       共通のモーション編集補助
//   framework_motion_workspace_edit.cpp        アセット入出力・編集履歴・プレビュー制御
//   framework_motion_workspace_layers.cpp      Motion Layer 編集
//   framework_motion_workspace_preview.cpp     プレビュー描画
//   framework_motion_workspace_inspector.cpp   プロパティ／イベント インスペクタ
//   framework_motion_workspace_timeline.cpp    タイムラインとグラフエディタ
