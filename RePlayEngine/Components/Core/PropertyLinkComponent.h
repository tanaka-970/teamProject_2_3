#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/References.h"

#include <string>

namespace ReplayEngine::Scene { class Scene; }

namespace ReplayEngine::Components
{
    // ある Component の数値プロパティを別の Component へ写す。
    //
    // 接続の値は Scene に保存するが、平滑化の途中値は実行時だけ保持する。
    // 式や条件分岐を持たせず、Motion Mixer の後に一度だけ評価することで、
    // 既存の PropertyRegistry と保存形式をそのまま利用する。
    class PropertyLinkComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(PropertyLinkComponent)

    public:
        Reflection::ComponentReference source_object;
        std::string source_property;
        Reflection::ComponentReference target_object;
        std::string target_property;

        float source_min = 0.0f;
        float source_max = 1.0f;
        float target_min = 0.0f;
        float target_max = 1.0f;
        bool invert = false;
        bool clamp = true;
        int easing = 0;
        float smoothing = 0.0f;

        void OnRuntimeAwake() override;
        void OnRuntimeDestroy() override;
        void OnPropertyChanged(const char* property_name) override;

        // Scene::Update と Motion Mixer が終わった後に framework から呼ぶ。
        static void EvaluateAll(Scene::Scene& scene, float delta_time);

    private:
        bool smoothing_initialized_ = false;
        float smoothed_value_ = 0.0f;
        bool cycle_disabled_ = false;
    };
}
