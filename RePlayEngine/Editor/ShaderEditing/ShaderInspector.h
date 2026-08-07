#pragma once

#include "../../Rendering/Materials/CharacterMaterialProfile.h"
#include "../../Rendering/ShaderStack/ShaderLayerStack.h"

#include <DirectXMath.h>
#include <Windows.h>

#include <string>

namespace ReplayEngine::Editor
{
    // シェーダを編集する唯一の入口。
    //
    // 【なぜ 1 つにするか】
    //   以前は同じマテリアルを 6 箇所から編集できた。
    //     Inspector の Component Card      … 本番の GameObject
    //     「シェーダー調整」タブ            … デバッグメッシュだけ（偽物）
    //     ShaderStackEditor                … 呼ばれた文脈による
    //     ShaderPresetEditor               … 同上
    //     CharacterMaterialEditor          … 同上
    //     「描画確認」のチェックボックス    … 全マテリアルを無言で上書き
    //   編集した結果がどこへ効くのかを画面から判断できなかった。
    //
    // 【何を変えないか】
    //   層構造（ShaderLayerStack）もキャラ材質（CharacterMaterialProfile）も
    //   プリセットもデータとしてはそのまま残す。あれは表現力であって
    //   分裂ではない。既存の 3 エディタの描画コードもそのまま流用する。
    //   変えるのは「呼ぶ場所を 1 箇所へ集める」ことだけ。
    //
    // 【オブジェクトごとに違うシェーダを掛けられること】
    //   Target は参照の束であり、このクラスは所有しない。
    //   呼び出し側が「今選んでいる GameObject の Renderer / Material」を
    //   渡せば、その 1 体だけが編集される。
    //   グローバルな状態をこのクラスの中に持たないのが要点。

    struct ShaderInspectorTarget final
    {
        // 表示名。「Material : wall_concrete」のように出す。
        std::string label;

        // 0=FBX標準 / 1=PBR / 2=トゥーン / 3=アンリット / 4=ピクセル化
        int* base_shader = nullptr;

        bool* outline_pass = nullptr;

        // 層構造。nullptr なら「レイヤ」欄を出さない。
        Rendering::ShaderLayerStack* layers = nullptr;

        // キャラ材質。nullptr なら「キャラクター材質」欄を出さない。
        Rendering::CharacterMaterialProfile* character = nullptr;

        float* pixel_grid = nullptr;
        float* pixelate_strength = nullptr;

        DirectX::XMFLOAT4* outline_color = nullptr;
        DirectX::XMFLOAT4* outline_parameters = nullptr;

        // 詳細表示の ON/OFF。呼び出し側が保持する。
        bool* advanced_mode = nullptr;

        // プリセットの読み書き結果を出す先。nullptr なら
        // 「プリセット」欄を出さない。
        std::string* preset_status = nullptr;
    };

    struct ShaderInspectorResult final
    {
        bool changed = false;

        // このマテリアルが必要とする描画パス。
        // 呼び出し側が「そのパスが無効なら警告を出す」ために使う。
        // ここで描画を止めてはいけない。
        bool requires_pbr = false;
        bool requires_toon = false;
        bool requires_unlit = false;
        bool requires_outline = false;
    };

    class ShaderInspector final
    {
    public:
        // id は ImGui の識別子。対象ごとに別の文字列を渡すこと。
        // 同じ id を使い回すと、別オブジェクトを選んだときに
        // 開閉状態や入力中の値が混ざる。
        static ShaderInspectorResult Draw(const char* id, HWND owner,
            const ShaderInspectorTarget& target);
    };
}
