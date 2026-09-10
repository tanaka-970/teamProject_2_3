#pragma once

#include "../../Object/Component/Component.h"
#include "../../Landscape/LandscapeData.h"

namespace ReplayEngine::Components
{
    // 普通の GameObject を「編集可能な地形」にする Component。
    //
    // 描画・衝突をここへ詰め込まない。
    //   LandscapeComponent          = geometry / topology data
    //   LandscapeRendererComponent  = 見た目
    //   LandscapeColliderComponent  = 衝突
    // という責務分離にすることで、Ground を特殊 World Object にしない。
    class LandscapeComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(LandscapeComponent)

    public:
        LandscapeComponent();

        Landscape::LandscapeData& Data() noexcept { return data_; }
        const Landscape::LandscapeData& Data() const noexcept { return data_; }

        bool GenerateFlat(int width, int height, float cell_size,
            float height_value = 0.0f);

        // Scene serializer へ任意 topology を保存する。
        void OnSerialize(Reflection::PropertyBag& output) const override;
        void OnDeserialize(const Reflection::PropertyBag& input) override;

        // Inspector に出す生成 preset 値。GenerateFlat を押すまでは既存 mesh を壊さない。
        int default_resolution = 33;
        float default_cell_size = 2.0f;
        // この地形を作ったモデルの Asset GUID。手で作った地形は空。
        std::string source_model_asset;

        const std::string& LastDeserializeError() const noexcept { return deserialize_error_; }

    private:
        Landscape::LandscapeData data_;
        std::string deserialize_error_;
    };
}
