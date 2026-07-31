#pragma once

#include "../../Core/ObjectID/ObjectID.h"

#include <DirectXMath.h>

#include <cstddef>
#include <string>
#include <vector>

namespace ReplayEngine::Rendering
{
    // Scene が「これを描いてほしい」と提出する 1 件ぶんの情報。
    //
    // ここに GPU リソースは一切入らない。
    //   保持するのは Asset の GUID とワールド行列と見た目のパラメータだけ。
    //   実際の Mesh / Texture / Shader の解決と描画は、
    //   既存のレンダラーがメインスレッド上で行う。
    //
    // この分離により、GameObject や Gameplay Component が
    // ID3D11DeviceContext や Shader へ直接触る構造にならない。
    struct RenderItem
    {
        // どの GameObject から出たか。ピッキングやデバッグ表示に使う。
        Core::ObjectID owner;

        // AssetDatabase の GUID。空なら描画しない。
        std::string mesh_asset;

        // 将来 Material を分離するための枠。現時点では未使用。
        std::string material_asset;

        // ワールド行列。親子階層を合成済みの最終値。
        DirectX::XMFLOAT4X4 world{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f };

        DirectX::XMFLOAT4 tint{ 1.0f, 1.0f, 1.0f, 1.0f };

        // 既存の描画方式番号。0=FBX標準 / 1=PBR / 2=トゥーン / 3=アンリット
        int shading_model = 1;

        bool outline = false;
        bool cast_shadow = true;
    };

    // 1 フレーム分の描画提出リスト。
    //
    // 毎フレーム Clear() してから作り直す。
    // vector を使い回すので確保の回数は増えない。
    class RenderItemList final
    {
    public:
        void Clear() noexcept { items_.clear(); }
        void Add(RenderItem item) { items_.push_back(std::move(item)); }

        bool Empty() const noexcept { return items_.empty(); }
        std::size_t Size() const noexcept { return items_.size(); }

        const std::vector<RenderItem>& Items() const noexcept { return items_; }

        std::vector<RenderItem>::const_iterator begin() const noexcept { return items_.begin(); }
        std::vector<RenderItem>::const_iterator end() const noexcept { return items_.end(); }

    private:
        std::vector<RenderItem> items_;
    };
}
