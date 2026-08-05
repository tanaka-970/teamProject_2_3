#include "MaterialAsset.h"

#include <windows.h>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>

namespace ReplayEngine::Rendering
{
    namespace
    {
        bool Expect(std::istream& stream, const char* expected, std::string& error)
        {
            std::string token;
            if (!(stream >> token) || token != expected)
            {
                error = std::string("Materialの項目が不正です: ") + expected;
                return false;
            }
            return true;
        }

        bool Finite(const MaterialAsset& value) noexcept
        {
            const float values[]{ value.base_color.x, value.base_color.y,
                value.base_color.z, value.base_color.w, value.metallic, value.roughness,
                value.emissive.x, value.emissive.y, value.emissive.z,
                value.emissive_strength, value.ambient_occlusion, value.alpha_cutoff };
            for (const float item : values) if (!std::isfinite(item)) return false;
            return value.metallic >= 0.0f && value.metallic <= 1.0f &&
                value.roughness >= 0.0f && value.roughness <= 1.0f &&
                value.ambient_occlusion >= 0.0f && value.ambient_occlusion <= 1.0f &&
                value.alpha_cutoff >= 0.0f && value.alpha_cutoff <= 1.0f &&
                value.emissive_strength >= 0.0f &&
                value.shading_model >= 0 && value.shading_model <= 4;
        }
    }

    bool MaterialAsset::Save(const MaterialAsset& material,
        const std::filesystem::path& path, std::string& error)
    {
        error.clear();
        if (path.empty())
        {
            error = "Materialの保存先が空です";
            return false;
        }
        if (!Finite(material))
        {
            error = "Materialに範囲外または非有限の値があります";
            return false;
        }

        std::error_code filesystem_error;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), filesystem_error);
        if (filesystem_error)
        {
            error = "Material保存フォルダーを作成できません";
            return false;
        }

        std::filesystem::path temporary = path;
        temporary += L".tmp";
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            error = "Material一時ファイルを作成できません";
            return false;
        }
        stream << std::setprecision(std::numeric_limits<float>::max_digits10);
        stream << "REPLAY_MATERIAL " << current_version << '\n';
        stream << "BASE_COLOR " << material.base_color.x << ' ' << material.base_color.y
            << ' ' << material.base_color.z << ' ' << material.base_color.w << '\n';
        stream << "BASE_COLOR_TEXTURE " << std::quoted(material.base_color_texture) << '\n';
        stream << "NORMAL_TEXTURE " << std::quoted(material.normal_texture) << '\n';
        stream << "METALLIC " << material.metallic << '\n';
        stream << "METALLIC_TEXTURE " << std::quoted(material.metallic_texture) << '\n';
        stream << "ROUGHNESS " << material.roughness << '\n';
        stream << "ROUGHNESS_TEXTURE " << std::quoted(material.roughness_texture) << '\n';
        stream << "EMISSIVE " << material.emissive.x << ' ' << material.emissive.y
            << ' ' << material.emissive.z << ' ' << material.emissive_strength << '\n';
        stream << "EMISSIVE_TEXTURE " << std::quoted(material.emissive_texture) << '\n';
        stream << "AMBIENT_OCCLUSION " << material.ambient_occlusion << '\n';
        stream << "AMBIENT_OCCLUSION_TEXTURE "
            << std::quoted(material.ambient_occlusion_texture) << '\n';
        stream << "ALPHA " << static_cast<int>(material.alpha_mode) << ' '
            << material.alpha_cutoff << '\n';
        stream << "DOUBLE_SIDED " << (material.double_sided ? 1 : 0) << '\n';
        stream << "SHADING_MODEL " << material.shading_model << '\n';

        // ---- version 2 で追加 -------------------------------------------
        //
        // 層構造は Material 固有。並び順がそのまま描画順になる。
        // id は保存しない。読み込み時に振り直す。
        // 保存された id を復元しても意味が無く、
        // 別 Material 間で衝突したときに追いにくくなるだけのため。
        stream << "PIXELATE " << material.pixelate_grid << ' '
            << material.pixelate_strength << '\n';
        stream << "OUTLINE_PASS " << (material.outline_pass ? 1 : 0) << '\n';
        stream << "LAYER_COUNT " << material.layers.Layers().size() << '\n';
        for (const ShaderLayer& layer : material.layers.Layers())
        {
            stream << "LAYER "
                << static_cast<int>(layer.type) << ' '
                << static_cast<int>(layer.blend) << ' '
                << (layer.enabled ? 1 : 0) << ' '
                << layer.opacity << ' '
                << layer.strength << ' '
                << layer.parameter << ' '
                << layer.tint.x << ' ' << layer.tint.y << ' '
                << layer.tint.z << ' ' << layer.tint.w << '\n';
        }
        stream << "END_MATERIAL\n";
        stream.flush();
        if (!stream)
        {
            stream.close();
            std::filesystem::remove(temporary, filesystem_error);
            error = "Materialを書き込み中に失敗しました";
            return false;
        }
        stream.close();

        if (std::filesystem::exists(path, filesystem_error) && !filesystem_error)
        {
            std::filesystem::path backup = path;
            backup += L".bak";
            std::filesystem::copy_file(path, backup,
                std::filesystem::copy_options::overwrite_existing, filesystem_error);
            if (filesystem_error)
            {
                std::filesystem::remove(temporary, filesystem_error);
                error = "Materialバックアップを作成できません";
                return false;
            }
        }

        if (!MoveFileExW(temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::filesystem::remove(temporary, filesystem_error);
            error = "Material一時ファイルを置換できません";
            return false;
        }
        return true;
    }

    bool MaterialAsset::Load(const std::filesystem::path& path,
        MaterialAsset& material, std::string& error)
    {
        error.clear();
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            error = "Materialを開けません";
            return false;
        }

        std::string magic;
        int version = 0;
        MaterialAsset loaded;
        int alpha = 0;
        int double_sided_value = 0;
        // version 1 も読む。層構造が無いだけで、それ以外は同じ並び。
        // 古い .replaymaterial を開けなくすると、
        // 既に作ってあるアセットが全部失われる。
        if (!(stream >> magic >> version) || magic != "REPLAY_MATERIAL" ||
            version < 1 || version > current_version ||
            !Expect(stream, "BASE_COLOR", error) ||
            !(stream >> loaded.base_color.x >> loaded.base_color.y >>
                loaded.base_color.z >> loaded.base_color.w) ||
            !Expect(stream, "BASE_COLOR_TEXTURE", error) ||
            !(stream >> std::quoted(loaded.base_color_texture)) ||
            !Expect(stream, "NORMAL_TEXTURE", error) ||
            !(stream >> std::quoted(loaded.normal_texture)) ||
            !Expect(stream, "METALLIC", error) || !(stream >> loaded.metallic) ||
            !Expect(stream, "METALLIC_TEXTURE", error) ||
            !(stream >> std::quoted(loaded.metallic_texture)) ||
            !Expect(stream, "ROUGHNESS", error) || !(stream >> loaded.roughness) ||
            !Expect(stream, "ROUGHNESS_TEXTURE", error) ||
            !(stream >> std::quoted(loaded.roughness_texture)) ||
            !Expect(stream, "EMISSIVE", error) ||
            !(stream >> loaded.emissive.x >> loaded.emissive.y >>
                loaded.emissive.z >> loaded.emissive_strength) ||
            !Expect(stream, "EMISSIVE_TEXTURE", error) ||
            !(stream >> std::quoted(loaded.emissive_texture)) ||
            !Expect(stream, "AMBIENT_OCCLUSION", error) ||
            !(stream >> loaded.ambient_occlusion) ||
            !Expect(stream, "AMBIENT_OCCLUSION_TEXTURE", error) ||
            !(stream >> std::quoted(loaded.ambient_occlusion_texture)) ||
            !Expect(stream, "ALPHA", error) ||
            !(stream >> alpha >> loaded.alpha_cutoff) ||
            !Expect(stream, "DOUBLE_SIDED", error) || !(stream >> double_sided_value) ||
            !Expect(stream, "SHADING_MODEL", error) || !(stream >> loaded.shading_model))
        {
            if (error.empty()) error = "Materialの内容を読み取れません";
            return false;
        }

        // ---- version 2 の追加分 -------------------------------------------
        if (version >= 2)
        {
            int outline_value = 0;
            std::size_t layer_count = 0;
            if (!Expect(stream, "PIXELATE", error) ||
                !(stream >> loaded.pixelate_grid >> loaded.pixelate_strength) ||
                !Expect(stream, "OUTLINE_PASS", error) || !(stream >> outline_value) ||
                !Expect(stream, "LAYER_COUNT", error) || !(stream >> layer_count))
            {
                if (error.empty()) error = "Materialの層構造を読み取れません";
                return false;
            }
            loaded.outline_pass = outline_value != 0;

            if (layer_count > ShaderLayerStack::MaxLayers)
            {
                error = "Materialの層数が上限を超えています";
                return false;
            }

            for (std::size_t index = 0; index < layer_count; ++index)
            {
                int type = 0;
                int blend = 0;
                int enabled = 0;
                ShaderLayer source{};
                if (!Expect(stream, "LAYER", error) ||
                    !(stream >> type >> blend >> enabled >> source.opacity >>
                        source.strength >> source.parameter >>
                        source.tint.x >> source.tint.y >>
                        source.tint.z >> source.tint.w))
                {
                    if (error.empty()) error = "Materialの層を読み取れません";
                    return false;
                }
                if (type < static_cast<int>(ShaderLayerType::Pbr) ||
                    type > static_cast<int>(ShaderLayerType::StylizedCharacter) ||
                    blend < static_cast<int>(ShaderLayerBlend::Alpha) ||
                    blend > static_cast<int>(ShaderLayerBlend::Multiply))
                {
                    error = "Materialの層の列挙値が不正です";
                    return false;
                }

                // id は Add が振り直す。保存された id は使わない。
                ShaderLayer& added =
                    loaded.layers.Add(static_cast<ShaderLayerType>(type));
                added.blend = static_cast<ShaderLayerBlend>(blend);
                added.enabled = enabled != 0;
                added.opacity = source.opacity;
                added.strength = source.strength;
                added.parameter = source.parameter;
                added.tint = source.tint;
            }
        }

        if (!Expect(stream, "END_MATERIAL", error))
        {
            if (error.empty()) error = "Materialの終端が見つかりません";
            return false;
        }
        if (alpha < static_cast<int>(MaterialAlphaMode::Opaque) ||
            alpha > static_cast<int>(MaterialAlphaMode::Blend) ||
            (double_sided_value != 0 && double_sided_value != 1))
        {
            error = "Materialの列挙値が不正です";
            return false;
        }
        loaded.alpha_mode = static_cast<MaterialAlphaMode>(alpha);
        loaded.double_sided = double_sided_value != 0;
        if (!Finite(loaded))
        {
            error = "Materialに範囲外または非有限の値があります";
            return false;
        }
        material = std::move(loaded);
        return true;
    }
}
