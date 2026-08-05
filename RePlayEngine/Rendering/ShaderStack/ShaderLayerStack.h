#pragma once

#include <DirectXMath.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ReplayEngine::Rendering
{
    enum class ShaderLayerType : std::uint32_t
    {
        Pbr,
        Toon,
        Unlit,
        Pixelate,
        Wireframe,
        Outline,
        StylizedCharacter
    };

    enum class ShaderLayerBlend : std::uint32_t
    {
        Alpha,
        Additive,
        Multiply
    };

    struct ShaderLayer
    {
        std::uint64_t id = 0;
        ShaderLayerType type = ShaderLayerType::Pixelate;
        ShaderLayerBlend blend = ShaderLayerBlend::Alpha;
        bool enabled = true;
        float opacity = 0.45f;
        float strength = 1.0f;
        float parameter = 6.0f;
        DirectX::XMFLOAT4 tint{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    class ShaderLayerStack final
    {
    public:
        // 重ね掛けの上限。
        //
        // GPU 側に固定長の配列は無く、1 層につき 1 パス描くだけなので
        // 構造上の制限は無い。それでも上限を残すのは、
        // 誤って何百層も積んで気付かないうちにフレームが落ちるのを防ぐため。
        // 実用上まず届かない値にしてある。
        static constexpr std::size_t MaxLayers = 64;

        ShaderLayer& Add(ShaderLayerType type)
        {
            if (layers_.size() >= MaxLayers) return layers_.back();
            ShaderLayer layer{};
            layer.id = next_id_++;
            layer.type = type;
            layers_.push_back(layer);
            return layers_.back();
        }

        void Remove(std::size_t index)
        {
            if (index < layers_.size()) layers_.erase(layers_.begin() + index);
        }

        void Move(std::size_t source, std::size_t destination)
        {
            if (source >= layers_.size() || destination >= layers_.size() || source == destination) return;
            ShaderLayer moving = layers_[source];
            layers_.erase(layers_.begin() + source);
            layers_.insert(layers_.begin() + destination, moving);
        }

        void Clear() noexcept
        {
            layers_.clear();
            next_id_ = 1;
        }

        bool HasEnabledLayers() const noexcept
        {
            return std::any_of(layers_.begin(), layers_.end(),
                [](const ShaderLayer& layer) { return layer.enabled; });
        }

        bool Contains(ShaderLayerType type) const noexcept
        {
            return std::any_of(layers_.begin(), layers_.end(),
                [type](const ShaderLayer& layer) { return layer.type == type; });
        }

        std::vector<ShaderLayer>& Layers() noexcept { return layers_; }
        const std::vector<ShaderLayer>& Layers() const noexcept { return layers_; }
        bool CanAdd() const noexcept { return layers_.size() < MaxLayers; }

    private:
        std::vector<ShaderLayer> layers_;
        std::uint64_t next_id_ = 1;
    };
}
