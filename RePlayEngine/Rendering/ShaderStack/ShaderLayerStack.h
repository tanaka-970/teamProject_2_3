#pragma once

#include "BuiltInShaderLayers.h"
#include "../../Reflection/Property/PropertyBag.h"

#include <DirectXMath.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ReplayEngine::Rendering
{
    // v2/v3 Material / ShaderPreset の移行用番号。
    // 新しい Layer の種類を増やすための enum ではない。
    // 新規 Layer は ShaderID だけで追加できる。
    enum class ShaderLayerType : std::uint32_t
    {
        Pbr = 0,
        Toon = 1,
        Unlit = 2,
        Pixelate = 3,
        Wireframe = 4,
        Outline = 5,
        StylizedCharacter = 6,
        Custom = 0xFFFFFFFFu
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

        // ---- Phase 10: 正本 -------------------------------------------------
        // Layer domain の Shader Asset GUID。これが種類そのもの。
        ShaderID shader;
        Reflection::PropertyBag properties;
        ShaderLayerBlend blend = ShaderLayerBlend::Alpha;
        bool enabled = true;

        // ---- v2/v3 / 既存特殊パス互換 -------------------------------------
        // これらは新しい Layer Shader を追加するためには使わない。
        // 既存 7 種の見た目を移行期間中そのまま維持する bridge。
        ShaderLayerType type = ShaderLayerType::Custom;
        float opacity = 0.45f;
        float strength = 1.0f;
        float parameter = 6.0f;
        DirectX::XMFLOAT4 tint{ 1.0f, 1.0f, 1.0f, 1.0f };

        // GUID が正本。v2/v3 由来で空の場合だけ legacy enum から補う。
        ShaderID EffectiveShader() const noexcept
        {
            if (shader.IsValid()) return shader;
            if (type == ShaderLayerType::Custom) return ShaderID{};
            return BuiltInShaderLayers::FromLegacyType(
                static_cast<std::uint32_t>(type));
        }

        bool Is(ShaderID id_value) const noexcept
        {
            return id_value.IsValid() && EffectiveShader() == id_value;
        }

        // 旧 7 layer の専用描画コードへ値を渡す互換 bridge。
        // 新しい custom layer は PropertyBag だけを使い、この値には依存しない。
        void SyncLegacyFieldsToProperties()
        {
            using Reflection::PropertyValue;
            const ShaderID effective = EffectiveShader();

            // Custom Layer の PropertyBag に互換用の謎プロパティを混ぜない。
            // legacy bridge は固定 7 種だけに限定する。
            if (!BuiltInShaderLayers::IsBuiltIn(effective)) return;

            properties.Set("prop.Opacity", PropertyValue::MakeFloat(opacity));
            properties.Set("prop.Strength", PropertyValue::MakeFloat(strength));
            properties.Set("prop.Parameter", PropertyValue::MakeFloat(parameter));
            properties.Set("prop.Tint", PropertyValue::MakeColor(tint));

            if (effective == BuiltInShaderLayers::Pixelate)
            {
                properties.Set("prop.PixelSize", PropertyValue::MakeFloat(parameter));
                properties.Set("prop.Strength", PropertyValue::MakeFloat(strength));
                properties.Set("prop.Opacity", PropertyValue::MakeFloat(opacity));
            }
            else if (effective == BuiltInShaderLayers::Outline)
            {
                properties.Set("prop.Color", PropertyValue::MakeColor(tint));
                properties.Set("prop.Width", PropertyValue::MakeFloat(parameter));
                properties.Set("prop.Opacity", PropertyValue::MakeFloat(opacity));
            }
        }

        void SyncPropertiesToLegacyFields()
        {
            const auto read_float = [this](const char* name, float& out)
            {
                if (const Reflection::PropertyValue* value = properties.Find(name))
                    out = value->AsFloat(out);
            };
            const auto read_color = [this](const char* name, DirectX::XMFLOAT4& out)
            {
                if (const Reflection::PropertyValue* value = properties.Find(name))
                {
                    if (value->Type() == Reflection::PropertyType::Color ||
                        value->Type() == Reflection::PropertyType::Vector4)
                        out = value->AsVector4();
                }
            };

            read_float("prop.Opacity", opacity);
            read_float("prop.Strength", strength);
            read_float("prop.Parameter", parameter);
            read_color("prop.Tint", tint);

            const ShaderID effective = EffectiveShader();
            if (effective == BuiltInShaderLayers::Pixelate)
            {
                read_float("prop.PixelSize", parameter);
                read_float("prop.Strength", strength);
                read_float("prop.Opacity", opacity);
            }
            else if (effective == BuiltInShaderLayers::Outline)
            {
                read_color("prop.Color", tint);
                read_float("prop.Width", parameter);
                read_float("prop.Opacity", opacity);
            }
        }
    };

    class ShaderLayerStack final
    {
    public:
        static constexpr std::size_t MaxLayers = 64;

        ShaderLayer& Add(ShaderID shader)
        {
            return AddWithID(shader, next_id_);
        }

        // MaterialAsset v4 の永続 Layer ID を復元する入口。
        // 同じ Shader を複数重ねても個々の Layer を後から識別できるよう、
        // ID は Save/Reload をまたいで維持する。
        ShaderLayer& AddWithID(ShaderID shader, std::uint64_t persistent_id)
        {
            if (layers_.size() >= MaxLayers) return layers_.back();
            if (persistent_id == 0) persistent_id = next_id_;

            ShaderLayer layer{};
            layer.id = persistent_id;
            layer.shader = shader;

            if (persistent_id >= next_id_)
            {
                // UINT64_MAX は loader 側で拒否する。通常経路では +1 が安全。
                next_id_ = persistent_id + 1;
            }

            std::uint32_t legacy = 0;
            if (BuiltInShaderLayers::TryGetLegacyType(shader, legacy))
                layer.type = static_cast<ShaderLayerType>(legacy);
            layer.SyncLegacyFieldsToProperties();

            layers_.push_back(std::move(layer));
            return layers_.back();
        }

        // v2/v3 と古い Editor 呼び出しの移行入口。
        ShaderLayer& Add(ShaderLayerType type)
        {
            const std::uint32_t legacy = static_cast<std::uint32_t>(type);
            ShaderLayer& layer = Add(BuiltInShaderLayers::FromLegacyType(legacy));
            layer.type = type;
            return layer;
        }

        void Remove(std::size_t index)
        {
            if (index < layers_.size()) layers_.erase(layers_.begin() + index);
        }

        void Move(std::size_t source, std::size_t destination)
        {
            if (source >= layers_.size() || destination >= layers_.size() || source == destination) return;
            ShaderLayer moving = std::move(layers_[source]);
            layers_.erase(layers_.begin() + source);
            layers_.insert(layers_.begin() + destination, std::move(moving));
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

        bool Contains(ShaderID shader) const noexcept
        {
            return std::any_of(layers_.begin(), layers_.end(),
                [shader](const ShaderLayer& layer) { return layer.Is(shader); });
        }

        bool ContainsID(std::uint64_t id) const noexcept
        {
            return id != 0 && std::any_of(layers_.begin(), layers_.end(),
                [id](const ShaderLayer& layer) { return layer.id == id; });
        }

        // 互換 API。判定は enum ではなく固定 GUID へ変換して行う。
        bool Contains(ShaderLayerType type) const noexcept
        {
            return Contains(BuiltInShaderLayers::FromLegacyType(
                static_cast<std::uint32_t>(type)));
        }

        std::vector<ShaderLayer>& Layers() noexcept { return layers_; }
        const std::vector<ShaderLayer>& Layers() const noexcept { return layers_; }
        bool CanAdd() const noexcept { return layers_.size() < MaxLayers; }

    private:
        std::vector<ShaderLayer> layers_;
        std::uint64_t next_id_ = 1;
    };
}
