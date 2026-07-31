#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

#include <string>

namespace ReplayEngine::Components
{
    // 描画したいメッシュと見た目の設定を保持する Component。
    //
    // 重要な制約:
    //   このクラスは Direct3D に一切触れない。
    //   ID3D11* を持たず、Draw も Map も定数バッファ更新も行わない。
    //   保持するのは「どの Asset を」「どんな見た目で」描くかという情報だけで、
    //   実際の GPU 操作は既存の Renderer がメインスレッド上で行う。
    //
    //   Scene から描画対象を集める処理は SceneRenderCollector が担当し、
    //   そこで RenderItem の一覧を作って既存の描画パスへ渡す。
    //   この分離により、GameObject や Gameplay Component が
    //   Shader や RenderState を直接いじる構造にならないようにしている。
    //
    // Asset の持ち方:
    //   GPU リソースのポインタではなく AssetDatabase の GUID を保持する。
    //   Scene ファイルへ保存されるのもこの GUID で、
    //   読み込み時に AssetDatabase から実体を引き直す。
    class MeshRendererComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(MeshRendererComponent)

    public:
        MeshRendererComponent() = default;

        // 実際に描くべきか。Component の有効状態と visible の両方を見る。
        bool ShouldRender() const noexcept
        {
            return visible && !mesh_asset.empty() && ActiveInHierarchy();
        }

        // AssetDatabase の GUID。空なら描画対象にならない。
        std::string mesh_asset;

        // 将来 Material を分離するための枠。現時点では未使用。
        std::string material_asset;

        DirectX::XMFLOAT4 tint{ 1.0f, 1.0f, 1.0f, 1.0f };

        // 既存の描画方式の番号に合わせる。0=FBX標準 / 1=PBR / 2=トゥーン / 3=アンリット
        int shading_model = 1;

        bool outline = false;
        bool cast_shadow = true;
        bool visible = true;
    };
}
