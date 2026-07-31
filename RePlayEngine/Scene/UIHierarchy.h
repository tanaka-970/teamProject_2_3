#pragma once

#include "SceneDocument.h"

#include <DirectXMath.h>
#include <cstdint>
#include <vector>

namespace ReplayEngine::Scene
{
    // UI要素の階層を解決し、描画に必要な最終値を求める。
    //
    // なぜ別クラスにするか:
    //   SceneDocumentはデータの入れ物に徹させ、計算はここへ寄せる。
    //   こうしておくと、後からキーフレーム評価を挟むときに
    //   「時間で値を差し替えてから解決する」だけで済む。
    //
    // 参照の持ち方:
    //   親子はEntityIdで辿る。ポインタや添字を保持しないのは、
    //   SceneDocumentのvectorが再確保されても壊れないようにするため。
    //   既存のUIクラスは std::vector<UI> の参照を外へ返しており、
    //   要素追加のたびに参照が無効化される問題があった。
    class UIHierarchy final
    {
    public:
        // 1要素分の解決結果。描画側はこれだけ見ればよい。
        struct Resolved
        {
            EntityId id = 0;
            // 画面上の矩形(左上基準)。アンカーと親の変形を適用済み。
            DirectX::XMFLOAT2 screen_position{ 0.0f, 0.0f };
            DirectX::XMFLOAT2 screen_size{ 0.0f, 0.0f };
            // 累積した回転(degree)と不透明度。
            float rotation = 0.0f;
            float opacity = 1.0f;
            DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT4 uv_rect{ 0.0f, 0.0f, 0.0f, 0.0f };
            int alpha_mode = 0;
            const UIElementData* source = nullptr;
            const SceneEntity* entity = nullptr;
        };

        // 文書からUI要素を集め、親子と描画順を解決する。
        // 戻り値は描画順(奥→手前)に並ぶ。
        void Build(const SceneDocument& document);

        const std::vector<Resolved>& Elements() const noexcept { return resolved_; }
        // 循環参照を検出した数。0以外ならデータが壊れている。
        int CycleCount() const noexcept { return cycle_count_; }

    private:
        struct Node
        {
            EntityId id = 0;
            EntityId parent = 0;
            int order = 0;
            const SceneEntity* entity = nullptr;
            const UIElementData* ui = nullptr;
            std::vector<std::size_t> children;
            bool visited = false;
        };

        void Traverse(std::size_t index, const Resolved& parent_state);

        std::vector<Node> nodes_;
        std::vector<Resolved> resolved_;
        int cycle_count_ = 0;
    };
}
