#include "SceneSerializer.h"

#include "../Rendering/ShaderStack/ShaderLayerStack.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <locale>
#include <utility>

namespace ReplayEngine::Scene
{
    namespace
    {
        constexpr int scene_format_version = 6;
        constexpr std::size_t max_scene_entities = 100000;

        bool Expect(std::istream& stream, const char* expected, std::string& error)
        {
            std::string token;
            if (!(stream >> token) || token != expected)
            {
                error = std::string("シーン形式が不正です: ") + expected;
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

    bool SceneSerializer::Save(const SceneDocument& scene,
        const std::filesystem::path& path, std::string& error)
    {
        std::error_code filesystem_error;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), filesystem_error);
        if (filesystem_error)
        {
            error = "シーン保存フォルダーを作成できません";
            return false;
        }

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            error = "シーンファイルを作成できません";
            return false;
        }
        // 小数点表記を実行環境のロケールに依存させない。
        stream.imbue(std::locale::classic());
        stream << "REPLAY_SCENE " << scene_format_version << '\n';
        stream << "SCENE " << std::quoted(scene.SceneIdentifier()) << ' '
            << std::quoted(scene.SceneName()) << '\n';
        stream << "ENTITY_COUNT " << scene.Entities().size() << '\n';
        for (const SceneEntity& entity : scene.Entities())
        {
            stream << "ENTITY " << entity.id << ' ' << std::quoted(entity.identifier)
                << ' ' << std::quoted(entity.name)
                << ' ' << entity.active << '\n';
            if (entity.transform)
            {
                const auto& value = *entity.transform;
                stream << "TRANSFORM " << value.position.x << ' ' << value.position.y << ' ' << value.position.z
                    << ' ' << value.rotation.x << ' ' << value.rotation.y << ' ' << value.rotation.z
                    << ' ' << value.scale.x << ' ' << value.scale.y << ' ' << value.scale.z << '\n';
            }
            if (entity.model_renderer)
            {
                const auto& value = *entity.model_renderer;
                stream << "MODEL_RENDERER " << std::quoted(value.asset_guid)
                    << ' ' << std::quoted(value.asset_name)
                    << ' ' << value.tint.x << ' ' << value.tint.y << ' ' << value.tint.z << ' ' << value.tint.w
                    << ' ' << value.shading_model << ' ' << value.outline << ' ' << value.visible << '\n';
                for (const auto& layer : value.shader_layers)
                {
                    stream << "SHADER_LAYER " << layer.type << ' ' << layer.blend << ' '
                        << layer.enabled << ' ' << layer.opacity << ' ' << layer.strength << ' '
                        << layer.parameter << ' ' << layer.tint.x << ' ' << layer.tint.y << ' '
                        << layer.tint.z << ' ' << layer.tint.w << '\n';
                }
                if (value.character_material)
                {
                    const auto& profile = *value.character_material;
                    stream << "CHARACTER_PROFILE " << std::quoted(profile.name) << ' '
                        << profile.toon_threshold << ' ' << profile.toon_softness << ' '
                        << profile.shadow_strength << ' ' << profile.saturation << ' ';
                    WriteColor(stream, profile.skin.tint); stream << ' ';
                    WriteColor(stream, profile.skin.shadow_tint);
                    stream << ' ' << profile.skin.wrap << ' ' << profile.skin.scatter << ' '
                        << profile.skin.softness << ' ' << profile.skin.enabled << ' ';
                    WriteColor(stream, profile.face.shadow_tint);
                    stream << ' ' << profile.face.light_bias << ' ' << profile.face.shadow_softness
                        << ' ' << profile.face.front_fill << ' ' << profile.face.enabled << ' ';
                    WriteColor(stream, profile.hair.highlight_color);
                    stream << ' ' << profile.hair.power << ' ' << profile.hair.intensity << ' '
                        << profile.hair.anisotropy << ' ' << profile.hair.enabled << ' ';
                    WriteColor(stream, profile.rim.color);
                    stream << ' ' << profile.rim.power << ' ' << profile.rim.threshold << ' '
                        << profile.rim.intensity << ' ' << profile.rim.enabled << ' ';
                    WriteColor(stream, profile.crystal.tint);
                    stream << ' ' << profile.crystal.transparency << ' '
                        << profile.crystal.fresnel_power << ' ' << profile.crystal.dispersion << ' '
                        << profile.crystal.internal_emission << ' ' << profile.crystal.enabled << '\n';
                    stream << "CHARACTER_ARTISTIC ";
                    WriteColor(stream, profile.artistic.top_color); stream << ' ';
                    WriteColor(stream, profile.artistic.bottom_color);
                    stream << ' ' << profile.artistic.shadow_bands << ' '
                        << profile.artistic.contrast << ' ' << profile.artistic.hue_shift << ' '
                        << profile.artistic.gradient_strength << ' ' << profile.artistic.gradient_scale
                        << ' ' << profile.artistic.gradient_offset << '\n';
                    stream << "CHARACTER_SPECULAR ";
                    WriteColor(stream, profile.specular.color);
                    stream << ' ' << profile.specular.power << ' ' << profile.specular.threshold << ' '
                        << profile.specular.intensity << ' ' << profile.specular.enabled << '\n';
                }
            }
            if (entity.mesh_collider)
            {
                const auto& value = *entity.mesh_collider;
                stream << "MESH_COLLIDER " << std::quoted(value.cooked_path)
                    << ' ' << value.triangle_count << ' ' << value.cell_size << ' ' << value.enabled << '\n';
            }
            if (entity.gravity)
            {
                const auto& value = *entity.gravity;
                stream << "GRAVITY " << value.direction.x << ' ' << value.direction.y << ' ' << value.direction.z
                    << ' ' << value.strength << ' ' << value.scale << ' ' << value.terminal_speed
                    << ' ' << value.use_terminal_speed << ' ' << value.enabled << '\n';
            }
            if (entity.animation)
            {
                const auto& value = *entity.animation;
                stream << "ANIMATION " << value.clip_index << ' ' << value.speed
                    << ' ' << value.loop << ' ' << value.playing << '\n';
            }
            stream << "END_ENTITY\n";
        }
        if (!stream)
        {
            error = "シーンデータの書き込みに失敗しました";
            return false;
        }
        return true;
    }

    bool SceneSerializer::Load(SceneDocument& scene,
        const std::filesystem::path& path, std::string& error)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            error = "シーンファイルを開けません";
            return false;
        }
        stream.imbue(std::locale::classic());
        if (!Expect(stream, "REPLAY_SCENE", error)) return false;
        int version = 0;
        if (!(stream >> version) || version < 1 || version > scene_format_version)
        {
            error = "未対応のシーンバージョンです";
            return false;
        }
        // 途中で失敗しても呼び出し元のシーンを壊さないよう一時領域へ読み込む。
        SceneDocument loaded;
        if (version >= 3)
        {
            if (!Expect(stream, "SCENE", error)) return false;
            std::string scene_identifier;
            std::string scene_name;
            if (!(stream >> std::quoted(scene_identifier) >> std::quoted(scene_name)))
            {
                error = "シーン名を読み取れません";
                return false;
            }
            loaded.SetSceneName(scene_name.empty() ? scene_identifier : scene_name);
        }
        else
        {
            loaded.SetSceneName(path.stem().u8string());
        }
        if (!Expect(stream, "ENTITY_COUNT", error)) return false;
        std::size_t count = 0;
        if (!(stream >> count) || count > max_scene_entities)
        {
            error = "Entity数が不正です";
            return false;
        }

        loaded.Entities().reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            if (!Expect(stream, "ENTITY", error)) return false;
            SceneEntity entity{};
            if (version >= 2)
            {
                if (!(stream >> entity.id >> std::quoted(entity.identifier) >>
                    std::quoted(entity.name) >> entity.active))
                {
                    error = "Entity情報を読み取れません";
                    return false;
                }
            }
            else
            {
                entity.identifier.clear();
                if (!(stream >> entity.id >> std::quoted(entity.name) >> entity.active))
                {
                    error = "Entity情報を読み取れません";
                    return false;
                }
            }

            // END_ENTITYまでコンポーネント名を読み、存在する項目だけを復元する。
            std::string token;
            while (stream >> token)
            {
                if (token == "END_ENTITY") break;
                if (token == "TRANSFORM")
                {
                    TransformData value{};
                    if (!(stream >> value.position.x >> value.position.y >> value.position.z
                        >> value.rotation.x >> value.rotation.y >> value.rotation.z
                        >> value.scale.x >> value.scale.y >> value.scale.z)) return false;
                    entity.transform = value;
                }
                else if (token == "MODEL_RENDERER")
                {
                    ModelRendererData value{};
                    if (!(stream >> std::quoted(value.asset_guid))) return false;
                    if (version >= 2 && !(stream >> std::quoted(value.asset_name))) return false;
                    if (!(stream
                        >> value.tint.x >> value.tint.y >> value.tint.z >> value.tint.w
                        >> value.shading_model >> value.outline >> value.visible)) return false;
                    entity.model_renderer = value;
                }
                else if (token == "MESH_COLLIDER")
                {
                    MeshColliderData value{};
                    if (!(stream >> std::quoted(value.cooked_path) >> value.triangle_count
                        >> value.cell_size >> value.enabled)) return false;
                    entity.mesh_collider = value;
                }
                else if (token == "SHADER_LAYER" && version >= 4)
                {
                    if (!entity.model_renderer) entity.model_renderer.emplace();
                    ModelRendererData::ShaderLayerData value{};
                    if (!(stream >> value.type >> value.blend >> value.enabled >> value.opacity
                        >> value.strength >> value.parameter >> value.tint.x >> value.tint.y
                        >> value.tint.z >> value.tint.w)) return false;
                    if (value.type > static_cast<std::uint32_t>(
                            ReplayEngine::Rendering::ShaderLayerType::StylizedCharacter) ||
                        value.blend > static_cast<std::uint32_t>(
                            ReplayEngine::Rendering::ShaderLayerBlend::Multiply) ||
                        entity.model_renderer->shader_layers.size() >=
                            ReplayEngine::Rendering::ShaderLayerStack::MaxLayers)
                    {
                        error = "許可されていないシェーダーパスです";
                        return false;
                    }
                    value.opacity = std::clamp(value.opacity, 0.0f, 1.0f);
                    value.strength = std::clamp(value.strength, 0.0f, 1.0f);
                    value.parameter = std::clamp(value.parameter, 1.0f, 512.0f);
                    entity.model_renderer->shader_layers.push_back(value);
                }
                else if (token == "CHARACTER_PROFILE" && version >= 5)
                {
                    if (!entity.model_renderer) entity.model_renderer.emplace();
                    ReplayEngine::Rendering::CharacterMaterialProfile value{};
                    if (!(stream >> std::quoted(value.name) >> value.toon_threshold
                        >> value.toon_softness >> value.shadow_strength >> value.saturation) ||
                        !ReadColor(stream, value.skin.tint) || !ReadColor(stream, value.skin.shadow_tint) ||
                        !(stream >> value.skin.wrap >> value.skin.scatter >> value.skin.softness
                            >> value.skin.enabled) || !ReadColor(stream, value.face.shadow_tint) ||
                        !(stream >> value.face.light_bias >> value.face.shadow_softness
                            >> value.face.front_fill >> value.face.enabled) ||
                        !ReadColor(stream, value.hair.highlight_color) ||
                        !(stream >> value.hair.power >> value.hair.intensity >> value.hair.anisotropy
                            >> value.hair.enabled) || !ReadColor(stream, value.rim.color) ||
                        !(stream >> value.rim.power >> value.rim.threshold >> value.rim.intensity
                            >> value.rim.enabled) || !ReadColor(stream, value.crystal.tint) ||
                        !(stream >> value.crystal.transparency >> value.crystal.fresnel_power
                            >> value.crystal.dispersion >> value.crystal.internal_emission
                            >> value.crystal.enabled)) return false;
                    entity.model_renderer->character_material = std::move(value);
                }
                else if (token == "CHARACTER_ARTISTIC" && version >= 6)
                {
                    if (!entity.model_renderer) entity.model_renderer.emplace();
                    if (!entity.model_renderer->character_material)
                        entity.model_renderer->character_material.emplace();
                    auto& value = entity.model_renderer->character_material->artistic;
                    if (!ReadColor(stream, value.top_color) || !ReadColor(stream, value.bottom_color) ||
                        !(stream >> value.shadow_bands >> value.contrast >> value.hue_shift
                            >> value.gradient_strength >> value.gradient_scale
                            >> value.gradient_offset)) return false;
                    value.shadow_bands = std::clamp(value.shadow_bands, 1.0f, 8.0f);
                    value.contrast = std::clamp(value.contrast, 0.25f, 2.5f);
                    value.hue_shift = std::clamp(value.hue_shift, -0.5f, 0.5f);
                    value.gradient_strength = std::clamp(value.gradient_strength, 0.0f, 1.0f);
                }
                else if (token == "CHARACTER_SPECULAR" && version >= 6)
                {
                    if (!entity.model_renderer) entity.model_renderer.emplace();
                    if (!entity.model_renderer->character_material)
                        entity.model_renderer->character_material.emplace();
                    auto& value = entity.model_renderer->character_material->specular;
                    if (!ReadColor(stream, value.color) ||
                        !(stream >> value.power >> value.threshold >> value.intensity
                            >> value.enabled)) return false;
                    value.power = std::clamp(value.power, 1.0f, 256.0f);
                    value.threshold = std::clamp(value.threshold, 0.0f, 1.0f);
                    value.intensity = std::clamp(value.intensity, 0.0f, 4.0f);
                }
                else if (token == "GRAVITY")
                {
                    GravityData value{};
                    if (!(stream >> value.direction.x >> value.direction.y >> value.direction.z
                        >> value.strength >> value.scale >> value.terminal_speed
                        >> value.use_terminal_speed >> value.enabled)) return false;
                    entity.gravity = value;
                }
                else if (token == "ANIMATION")
                {
                    AnimationData value{};
                    if (!(stream >> value.clip_index >> value.speed >> value.loop >> value.playing)) return false;
                    entity.animation = value;
                }
                else
                {
                    error = "未知のコンポーネントです: " + token;
                    return false;
                }
            }
            loaded.Entities().push_back(std::move(entity));
        }
        // 読み込んだIDと識別子を検査し、次回追加用のIDも再構築する。
        loaded.RebuildNextId();
        scene = std::move(loaded);
        return true;
    }
}
