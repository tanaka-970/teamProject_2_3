#include "framework.h"
#include "texture.h"
#include "../../RePlayEngine/Assets/AssetCache.h"
#include "../../RePlayEngine/Editor/Style/EditorStyle.h"
#include "../../RePlayEngine/Motion/CompositionAsset.h"
#include "../../RePlayEngine/Motion/MotionAsset.h"
#include "../../RePlayEngine/Rendering/Materials/MaterialAsset.h"
#include "../../RePlayEngine/Rendering/Shaders/ShaderAssetFactory.h"
#include "../../RePlayEngine/Rendering/ShaderComposer/ShaderComposerAsset.h"
#include "../../RePlayEngine/Rendering/ShaderComposer/ShaderComposerGenerator.h"
#include "../../RePlayEngine/Runtime/Scene/SceneFlowAsset.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpProject.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

// =============================================================================
//  Project ブラウザ
//
//  Unity / Explorer 型の 2 ペイン構成。
//    左  : フォルダ + ファイルを展開できる完全 Project Tree
//    右  : 現在フォルダの中身（アイコン / サムネイル付き）
//  Rename/Delete/Duplicate/Move/Open は左右どちらからでも同一処理を使う。
//
//  ここに集約した理由:
//    以前は framework_editor.cpp の draw_project_panel が
//    「フォームで名前を打って作る」形になっており、
//    作る場所と作られた物が出る場所が違っていた。
//    フォルダを持ち、その場で作り、その場で改名できるようにしたほうがかんりしやすいでしょ
// =============================================================================

// 分割一覧（framework_project_browser.cpp）:
//   framework_project_browserInternal.h          Project 一覧・表示用の共通補助
//   framework_project_browser_assets.cpp          種別判定・基本操作・通常アセット作成
//   framework_project_browser_shader_creation.cpp シェーダー／Composer 作成
//   framework_project_browser_rename.cpp          エントリ改名
//   framework_project_browser_view.cpp            フォルダツリーとフォルダ内容表示
//   framework_project_browser_entry.cpp           Project Browser 入口
