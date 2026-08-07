#include "framework.h"
#include "../../../RePlayEngine/Editor/ShaderEditing/ShaderInspector.h"

// シェーダ編集の唯一の入口。
//
// 以前はここと framework_character_material.cpp の 2 本があり、
// それぞれが別のエディタを直接呼んでいた。
// 同じマテリアルを別々の場所から編集できる状態だったため、
// 編集結果がどこへ効くのか画面から判断できなかった。
//
// 今は ShaderInspector 1 つへ集約し、この関数はその呼び出しだけを行う。
//
// 【オブジェクトごとに違うシェーダを掛けられること】
//   引数はすべて参照。呼び出し側が「今選んでいる GameObject の
//   Renderer / Material が持つ値」を渡せば、その 1 体だけが変わる。
//   ここでグローバルな状態を触らないのが要点。
void framework::draw_shader_inspector(const char* id, const std::string& label,
    int& base_shader, bool& outline_pass,
    ReplayEngine::Rendering::ShaderLayerStack& layers,
    ReplayEngine::Rendering::CharacterMaterialProfile& profile,
    float& pixel_grid, float& pixelate_strength)
{
    ReplayEngine::Editor::ShaderInspectorTarget target;
    target.label = label;
    target.base_shader = &base_shader;
    target.outline_pass = &outline_pass;
    target.layers = &layers;
    target.character = &profile;
    target.pixel_grid = &pixel_grid;
    target.pixelate_strength = &pixelate_strength;
    target.outline_color = &toon.outline.outline_color;
    target.outline_parameters = &toon.outline.outline_params;
    target.advanced_mode = &shader_stack_advanced_mode;
    target.preset_status = &shader_preset_status;

    const auto result =
        ReplayEngine::Editor::ShaderInspector::Draw(id, hwnd, target);

    // このマテリアルが必要とする描画パスを有効にする。
    //
    // フラグ側で描画を止めていると、マテリアルで選んだ絵柄が
    // 無言で Unlit へ落ちる。選んだ本人には理由が分からないので、
    // 選んだ時点で必要なパスを立てておく。
    if (result.requires_pbr)     use_pbr_skin = true;
    if (result.requires_toon)    enable_toon_shader = true;
    if (result.requires_unlit)   enable_unlit_shader = true;
    if (result.requires_outline) enable_outline_shader = true;
}
