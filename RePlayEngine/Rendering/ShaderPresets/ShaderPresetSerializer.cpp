#include "ShaderPresetSerializer.h"

#include <fstream>
#include <iomanip>
#include <locale>
// 共有可能なシェーダープリセットだけを安全なデータ形式へ変換する。
#include <algorithm>
#include <utility>

namespace ReplayEngine::Rendering
{
    namespace
    {
        bool Expect(std::istream& stream, const char* expected, std::string& error)
        {
            std::string token;
            if (!(stream >> token) || token != expected)
            {
                error = std::string("シェーダープリセット形式が不正です: ") + expected;
                return false;
            }
            return true;
        }

        void WriteColor(std::ostream& stream, const DirectX::XMFLOAT4& color)
        {
            stream << color.x << ' ' << color.y << ' ' << color.z << ' ' << color.w;
        }

        bool ReadColor(std::istream& stream, DirectX::XMFLOAT4& color)
        {
            return static_cast<bool>(stream >> color.x >> color.y >> color.z >> color.w);
        }
    }

    bool ShaderPresetSerializer::Save(const ShaderPreset& preset,
        const std::filesystem::path& path, std::string& error)
    {
        std::error_code filesystem_error;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), filesystem_error);
        if (filesystem_error)
        {
            error = "プリセット保存フォルダーを作成できません";
            return false;
        }
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            error = "シェーダープリセットを作成できません";
            return false;
        }
        stream.imbue(std::locale::classic());
        const auto& value = preset.character;
        stream << "REPLAY_SHADER_PRESET 3\n";
        stream << "NAME " << std::quoted(preset.name) << '\n';
        stream << "SURFACE " << preset.base_shader << ' ' << preset.outline << ' '
            << preset.pixelate_grid << ' ' << preset.pixelate_strength << '\n';
        stream << "GENERAL " << std::quoted(value.name) << ' ' << value.toon_threshold << ' '
            << value.toon_softness << ' ' << value.shadow_strength << ' ' << value.saturation << '\n';
        stream << "ARTISTIC "; WriteColor(stream, value.artistic.top_color); stream << ' ';
        WriteColor(stream, value.artistic.bottom_color);
        stream << ' ' << value.artistic.shadow_bands << ' ' << value.artistic.contrast << ' '
            << value.artistic.hue_shift << ' ' << value.artistic.gradient_strength << ' '
            << value.artistic.gradient_scale << ' ' << value.artistic.gradient_offset << '\n';
        stream << "SPECULAR "; WriteColor(stream, value.specular.color);
        stream << ' ' << value.specular.power << ' ' << value.specular.threshold << ' '
            << value.specular.intensity << ' ' << value.specular.enabled << '\n';
        stream << "SKIN "; WriteColor(stream, value.skin.tint); stream << ' ';
        WriteColor(stream, value.skin.shadow_tint);
        stream << ' ' << value.skin.wrap << ' ' << value.skin.scatter << ' '
            << value.skin.softness << ' ' << value.skin.enabled << '\n';
        stream << "FACE "; WriteColor(stream, value.face.shadow_tint);
        stream << ' ' << value.face.light_bias << ' ' << value.face.shadow_softness << ' '
            << value.face.front_fill << ' ' << value.face.enabled << '\n';
        stream << "HAIR "; WriteColor(stream, value.hair.highlight_color);
        stream << ' ' << value.hair.power << ' ' << value.hair.intensity << ' '
            << value.hair.anisotropy << ' ' << value.hair.enabled << '\n';
        stream << "RIM "; WriteColor(stream, value.rim.color);
        stream << ' ' << value.rim.power << ' ' << value.rim.threshold << ' '
            << value.rim.intensity << ' ' << value.rim.enabled << '\n';
        stream << "CRYSTAL "; WriteColor(stream, value.crystal.tint);
        stream << ' ' << value.crystal.transparency << ' ' << value.crystal.fresnel_power << ' '
            << value.crystal.dispersion << ' ' << value.crystal.internal_emission << ' '
            << value.crystal.enabled << '\n';
        stream << "LAYER_COUNT " << preset.layers.Layers().size() << '\n';
        for (const auto& layer : preset.layers.Layers())
        {
            stream << "LAYER " << static_cast<std::uint32_t>(layer.type) << ' '
                << static_cast<std::uint32_t>(layer.blend) << ' ' << layer.enabled << ' '
                << layer.opacity << ' ' << layer.strength << ' ' << layer.parameter << ' ';
            WriteColor(stream, layer.tint);
            stream << '\n';
        }
        return static_cast<bool>(stream);
    }

    bool ShaderPresetSerializer::Load(ShaderPreset& preset,
        const std::filesystem::path& path, std::string& error)
    {
        std::error_code filesystem_error;
        const auto file_size = std::filesystem::file_size(path, filesystem_error);
        if (!filesystem_error && file_size > 1024 * 1024)
        {
            error = "シェーダープリセットが大きすぎます";
            return false;
        }
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            error = "シェーダープリセットを開けません";
            return false;
        }
        stream.imbue(std::locale::classic());
        if (!Expect(stream, "REPLAY_SHADER_PRESET", error)) return false;
        int version = 0;
        if (!(stream >> version) || version < 1 || version > 3)
        {
            error = "未対応のシェーダープリセットです";
            return false;
        }

        ShaderPreset loaded{};
        auto& value = loaded.character;
        if (!Expect(stream, "NAME", error) || !(stream >> std::quoted(loaded.name)) ||
            loaded.name.size() > 128) return false;
        if (!Expect(stream, "SURFACE", error) ||
            !(stream >> loaded.base_shader >> loaded.outline)) return false;
        if (version >= 3 &&
            !(stream >> loaded.pixelate_grid >> loaded.pixelate_strength)) return false;
        if (!Expect(stream, "GENERAL", error) ||
            !(stream >> std::quoted(value.name) >> value.toon_threshold >> value.toon_softness
                >> value.shadow_strength >> value.saturation) || value.name.size() > 128) return false;
        if (version >= 2)
        {
            if (!Expect(stream, "ARTISTIC", error) ||
                !ReadColor(stream, value.artistic.top_color) ||
                !ReadColor(stream, value.artistic.bottom_color) ||
                !(stream >> value.artistic.shadow_bands >> value.artistic.contrast
                    >> value.artistic.hue_shift >> value.artistic.gradient_strength
                    >> value.artistic.gradient_scale >> value.artistic.gradient_offset)) return false;
            if (!Expect(stream, "SPECULAR", error) || !ReadColor(stream, value.specular.color) ||
                !(stream >> value.specular.power >> value.specular.threshold
                    >> value.specular.intensity >> value.specular.enabled)) return false;
        }
        if (!Expect(stream, "SKIN", error) || !ReadColor(stream, value.skin.tint) ||
            !ReadColor(stream, value.skin.shadow_tint) ||
            !(stream >> value.skin.wrap >> value.skin.scatter >> value.skin.softness
                >> value.skin.enabled)) return false;
        if (!Expect(stream, "FACE", error) || !ReadColor(stream, value.face.shadow_tint) ||
            !(stream >> value.face.light_bias >> value.face.shadow_softness
                >> value.face.front_fill >> value.face.enabled)) return false;
        if (!Expect(stream, "HAIR", error) || !ReadColor(stream, value.hair.highlight_color) ||
            !(stream >> value.hair.power >> value.hair.intensity >> value.hair.anisotropy
                >> value.hair.enabled)) return false;
        if (!Expect(stream, "RIM", error) || !ReadColor(stream, value.rim.color) ||
            !(stream >> value.rim.power >> value.rim.threshold >> value.rim.intensity
                >> value.rim.enabled)) return false;
        if (!Expect(stream, "CRYSTAL", error) || !ReadColor(stream, value.crystal.tint) ||
            !(stream >> value.crystal.transparency >> value.crystal.fresnel_power
                >> value.crystal.dispersion >> value.crystal.internal_emission
                >> value.crystal.enabled)) return false;
        if (!Expect(stream, "LAYER_COUNT", error)) return false;
        std::size_t layer_count = 0;
        if (!(stream >> layer_count) || layer_count > 16)
        {
            error = "追加シェーダーパス数が不正です";
            return false;
        }
        for (std::size_t index = 0; index < layer_count; ++index)
        {
            if (!Expect(stream, "LAYER", error)) return false;
            std::uint32_t type = 0;
            std::uint32_t blend = 0;
            ShaderLayer layer{};
            if (!(stream >> type >> blend >> layer.enabled >> layer.opacity >> layer.strength
                >> layer.parameter) || !ReadColor(stream, layer.tint)) return false;
            if (type > static_cast<std::uint32_t>(ShaderLayerType::StylizedCharacter) ||
                blend > static_cast<std::uint32_t>(ShaderLayerBlend::Multiply))
            {
                error = "許可されていないシェーダーパスです";
                return false;
            }
            layer.opacity = std::clamp(layer.opacity, 0.0f, 1.0f);
            layer.strength = std::clamp(layer.strength, 0.0f, 1.0f);
            if (type == static_cast<std::uint32_t>(ShaderLayerType::Pixelate))
                layer.parameter = std::clamp(layer.parameter, 1.0f, 24.0f);
            else
                layer.parameter = std::clamp(layer.parameter, 1.0f, 512.0f);
            auto& destination = loaded.layers.Add(static_cast<ShaderLayerType>(type));
            destination.blend = static_cast<ShaderLayerBlend>(blend);
            destination.enabled = layer.enabled;
            destination.opacity = layer.opacity;
            destination.strength = layer.strength;
            destination.parameter = layer.parameter;
            destination.tint = layer.tint;
        }
        loaded.base_shader = std::clamp(loaded.base_shader, 0, 4);
        loaded.pixelate_grid = std::clamp(loaded.pixelate_grid, 1.0f, 24.0f);
        loaded.pixelate_strength = std::clamp(loaded.pixelate_strength, 0.0f, 1.0f);
        value.toon_threshold = std::clamp(value.toon_threshold, 0.0f, 1.0f);
        value.toon_softness = std::clamp(value.toon_softness, 0.001f, 0.5f);
        value.shadow_strength = std::clamp(value.shadow_strength, 0.0f, 1.0f);
        value.saturation = std::clamp(value.saturation, 0.0f, 2.0f);
        value.artistic.shadow_bands = std::clamp(value.artistic.shadow_bands, 1.0f, 8.0f);
        value.artistic.contrast = std::clamp(value.artistic.contrast, 0.25f, 2.5f);
        value.artistic.hue_shift = std::clamp(value.artistic.hue_shift, -0.5f, 0.5f);
        value.artistic.gradient_strength = std::clamp(value.artistic.gradient_strength, 0.0f, 1.0f);
        value.specular.power = std::clamp(value.specular.power, 1.0f, 256.0f);
        value.specular.threshold = std::clamp(value.specular.threshold, 0.0f, 1.0f);
        value.specular.intensity = std::clamp(value.specular.intensity, 0.0f, 4.0f);
        preset = std::move(loaded);
        return true;
    }
}
