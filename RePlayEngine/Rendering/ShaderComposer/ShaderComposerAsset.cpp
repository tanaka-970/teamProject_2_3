#include "ShaderComposerAsset.h"

#include "../Shaders/ShaderSource.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <unordered_set>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace ReplayEngine::Rendering
{
    const char* ToString(ShaderComposerNodeKind kind) noexcept
    {
        switch (kind)
        {
        case ShaderComposerNodeKind::UV: return "uv";
        case ShaderComposerNodeKind::Time: return "time";
        case ShaderComposerNodeKind::Normal: return "normal";
        case ShaderComposerNodeKind::ViewDirection: return "view_direction";
        case ShaderComposerNodeKind::Float: return "float";
        case ShaderComposerNodeKind::Color: return "color";
        case ShaderComposerNodeKind::FloatProperty: return "float_property";
        case ShaderComposerNodeKind::ColorProperty: return "color_property";
        case ShaderComposerNodeKind::TextureProperty: return "texture_property";
        case ShaderComposerNodeKind::Add: return "add";
        case ShaderComposerNodeKind::Subtract: return "subtract";
        case ShaderComposerNodeKind::Multiply: return "multiply";
        case ShaderComposerNodeKind::Divide: return "divide";
        case ShaderComposerNodeKind::Lerp: return "lerp";
        case ShaderComposerNodeKind::Saturate: return "saturate";
        case ShaderComposerNodeKind::Power: return "power";
        case ShaderComposerNodeKind::Fresnel: return "fresnel";
        case ShaderComposerNodeKind::UVScroll: return "uv_scroll";
        case ShaderComposerNodeKind::Noise: return "noise";
        case ShaderComposerNodeKind::Dissolve: return "dissolve";
        case ShaderComposerNodeKind::SurfaceOutput: return "surface_output";
        case ShaderComposerNodeKind::LayerOutput: return "layer_output";
        default: return "float";
        }
    }

    bool TryParseShaderComposerNodeKind(const std::string& text,
        ShaderComposerNodeKind& out) noexcept
    {
#define REPLAY_NODE_PARSE(value, name) if (text == name) { out = ShaderComposerNodeKind::value; return true; }
        REPLAY_NODE_PARSE(UV, "uv")
        REPLAY_NODE_PARSE(Time, "time")
        REPLAY_NODE_PARSE(Normal, "normal")
        REPLAY_NODE_PARSE(ViewDirection, "view_direction")
        REPLAY_NODE_PARSE(Float, "float")
        REPLAY_NODE_PARSE(Color, "color")
        REPLAY_NODE_PARSE(FloatProperty, "float_property")
        REPLAY_NODE_PARSE(ColorProperty, "color_property")
        REPLAY_NODE_PARSE(TextureProperty, "texture_property")
        REPLAY_NODE_PARSE(Add, "add")
        REPLAY_NODE_PARSE(Subtract, "subtract")
        REPLAY_NODE_PARSE(Multiply, "multiply")
        REPLAY_NODE_PARSE(Divide, "divide")
        REPLAY_NODE_PARSE(Lerp, "lerp")
        REPLAY_NODE_PARSE(Saturate, "saturate")
        REPLAY_NODE_PARSE(Power, "power")
        REPLAY_NODE_PARSE(Fresnel, "fresnel")
        REPLAY_NODE_PARSE(UVScroll, "uv_scroll")
        REPLAY_NODE_PARSE(Noise, "noise")
        REPLAY_NODE_PARSE(Dissolve, "dissolve")
        REPLAY_NODE_PARSE(SurfaceOutput, "surface_output")
        REPLAY_NODE_PARSE(LayerOutput, "layer_output")
#undef REPLAY_NODE_PARSE
        return false;
    }

    namespace
    {
        bool ReplaceAtomic(const std::filesystem::path& temporary,
            const std::filesystem::path& destination, std::string& error)
        {
#ifdef _WIN32
            if (MoveFileExW(temporary.c_str(), destination.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE)
                return true;
#endif
            std::error_code ec;
            std::filesystem::remove(destination, ec);
            ec.clear();
            std::filesystem::rename(temporary, destination, ec);
            if (!ec) return true;
            std::filesystem::remove(temporary, ec);
            error = "Shader Composer Asset を確定できません: " + destination.generic_u8string();
            return false;
        }

        bool IsFinite(const ShaderComposerNode& node)
        {
            const float values[] = { node.x, node.y, node.value, node.minimum, node.maximum,
                node.vector2.x, node.vector2.y, node.color.x, node.color.y, node.color.z, node.color.w };
            for (float value : values) if (!std::isfinite(value)) return false;
            return true;
        }

        std::uint32_t InputCount(ShaderComposerNodeKind kind)
        {
            switch (kind)
            {
            case ShaderComposerNodeKind::TextureProperty: return 1;
            case ShaderComposerNodeKind::Add:
            case ShaderComposerNodeKind::Subtract:
            case ShaderComposerNodeKind::Multiply:
            case ShaderComposerNodeKind::Divide: return 2;
            case ShaderComposerNodeKind::Lerp: return 3;
            case ShaderComposerNodeKind::Saturate: return 1;
            case ShaderComposerNodeKind::Power: return 2;
            case ShaderComposerNodeKind::Fresnel: return 3;
            case ShaderComposerNodeKind::UVScroll: return 3;
            case ShaderComposerNodeKind::Noise: return 2;
            case ShaderComposerNodeKind::Dissolve: return 3;
            case ShaderComposerNodeKind::SurfaceOutput: return 3;
            case ShaderComposerNodeKind::LayerOutput: return 1;
            default: return 0;
            }
        }
    }

    ShaderComposerNode& ShaderComposerAsset::AddNode(ShaderComposerNodeKind kind,
        float x, float y)
    {
        ShaderComposerNode node;
        node.id = next_node_id++;
        node.kind = kind;
        node.x = x;
        node.y = y;
        switch (kind)
        {
        case ShaderComposerNodeKind::FloatProperty:
            node.name = "Value" + std::to_string(node.id); node.display_name = "Value"; node.category = "Composer";
            node.value = 1.0f; node.minimum = 0.0f; node.maximum = 1.0f; break;
        case ShaderComposerNodeKind::ColorProperty:
            node.name = "Color" + std::to_string(node.id); node.display_name = "Color"; node.category = "Composer";
            node.color = { 1,1,1,1 }; break;
        case ShaderComposerNodeKind::TextureProperty:
            node.name = "Texture" + std::to_string(node.id); node.display_name = "Texture"; node.category = "Composer";
            node.default_texture = "white"; break;
        case ShaderComposerNodeKind::Float:
            node.value = 1.0f; break;
        case ShaderComposerNodeKind::Color:
            node.color = { 1,1,1,1 }; break;
        case ShaderComposerNodeKind::Fresnel:
            node.value = 2.0f; break;
        case ShaderComposerNodeKind::UVScroll:
            node.vector2 = { 0.1f, 0.0f }; break;
        case ShaderComposerNodeKind::Noise:
            node.value = 4.0f; break;
        case ShaderComposerNodeKind::Dissolve:
            node.value = 0.5f; node.minimum = 0.02f; break;
        default: break;
        }
        nodes.push_back(std::move(node));
        return nodes.back();
    }

    bool ShaderComposerAsset::RemoveNode(std::uint64_t id)
    {
        const auto found = std::find_if(nodes.begin(), nodes.end(),
            [id](const ShaderComposerNode& node) { return node.id == id; });
        if (found == nodes.end()) return false;
        nodes.erase(found);
        connections.erase(std::remove_if(connections.begin(), connections.end(),
            [id](const ShaderComposerConnection& edge)
            { return edge.from_node == id || edge.to_node == id; }), connections.end());
        return true;
    }

    ShaderComposerNode* ShaderComposerAsset::FindNode(std::uint64_t id) noexcept
    {
        for (ShaderComposerNode& node : nodes) if (node.id == id) return &node;
        return nullptr;
    }

    const ShaderComposerNode* ShaderComposerAsset::FindNode(std::uint64_t id) const noexcept
    {
        for (const ShaderComposerNode& node : nodes) if (node.id == id) return &node;
        return nullptr;
    }

    bool ShaderComposerAsset::Connect(std::uint64_t from_node, std::uint64_t to_node,
        std::uint32_t to_pin)
    {
        if (from_node == 0 || to_node == 0 || from_node == to_node) return false;
        const ShaderComposerNode* from = FindNode(from_node);
        const ShaderComposerNode* to = FindNode(to_node);
        if (from == nullptr || to == nullptr || to_pin >= InputCount(to->kind)) return false;
        DisconnectInput(to_node, to_pin);
        connections.push_back({ from_node, 0, to_node, to_pin });
        return true;
    }

    bool ShaderComposerAsset::DisconnectInput(std::uint64_t to_node, std::uint32_t to_pin)
    {
        const std::size_t before = connections.size();
        connections.erase(std::remove_if(connections.begin(), connections.end(),
            [to_node, to_pin](const ShaderComposerConnection& edge)
            { return edge.to_node == to_node && edge.to_pin == to_pin; }), connections.end());
        return connections.size() != before;
    }

    const ShaderComposerConnection* ShaderComposerAsset::FindInput(std::uint64_t to_node,
        std::uint32_t to_pin) const noexcept
    {
        for (const ShaderComposerConnection& edge : connections)
            if (edge.to_node == to_node && edge.to_pin == to_pin) return &edge;
        return nullptr;
    }

    ShaderComposerAsset ShaderComposerAsset::CreateDefault(ShaderDomain domain,
        const std::string& display_name, const std::filesystem::path& generated_hlsl)
    {
        ShaderComposerAsset asset;
        asset.shader_id = ShaderSource::GenerateID();
        asset.display_name = display_name.empty() ? "New Composer Shader" : display_name;
        asset.category = "Project/Composer";
        asset.domain = domain == ShaderDomain::Layer ? ShaderDomain::Layer :
            domain == ShaderDomain::PostProcess ? ShaderDomain::PostProcess :
            ShaderDomain::Surface;
        asset.lighting_model = ShaderLightingModel::Unlit;
        asset.generated_hlsl = generated_hlsl;

        const std::uint64_t uv_id = asset.AddNode(ShaderComposerNodeKind::UV, 40, 80).id;
        ShaderComposerNode& texture_node = asset.AddNode(ShaderComposerNodeKind::TextureProperty, 250, 60);
        const std::uint64_t tex_id = texture_node.id;
        texture_node.name = "BaseMap";
        texture_node.display_name = "Base Map";
        texture_node.category = asset.domain == ShaderDomain::Layer ? "Layer" :
            asset.domain == ShaderDomain::PostProcess ? "UI Effect" : "Surface";

        ShaderComposerNode& color_node = asset.AddNode(ShaderComposerNodeKind::ColorProperty, 250, 250);
        const std::uint64_t color_id = color_node.id;
        color_node.name = asset.domain == ShaderDomain::Layer ||
            asset.domain == ShaderDomain::PostProcess ? "Tint" : "BaseColor";
        color_node.display_name = asset.domain == ShaderDomain::Layer ||
            asset.domain == ShaderDomain::PostProcess ? "Tint" : "Base Color";
        color_node.category = asset.domain == ShaderDomain::Layer ? "Layer" :
            asset.domain == ShaderDomain::PostProcess ? "UI Effect" : "Surface";

        const std::uint64_t mul_id = asset.AddNode(ShaderComposerNodeKind::Multiply, 500, 150).id;
        const std::uint64_t out_id = asset.AddNode(
            asset.domain == ShaderDomain::Layer ? ShaderComposerNodeKind::LayerOutput
                                                : ShaderComposerNodeKind::SurfaceOutput,
            760, 150).id;
        asset.Connect(uv_id, tex_id, 0);
        asset.Connect(tex_id, mul_id, 0);
        asset.Connect(color_id, mul_id, 1);
        asset.Connect(mul_id, out_id, 0);
        return asset;
    }

    bool ShaderComposerAsset::Save(const ShaderComposerAsset& asset,
        const std::filesystem::path& path, std::string& error)
    {
        error.clear();
        if (path.empty() || !asset.shader_id.IsValid())
        {
            error = "Shader Composer Asset の path または ShaderGUID が無効です";
            return false;
        }
        if (asset.domain != ShaderDomain::Surface &&
            asset.domain != ShaderDomain::Layer &&
            asset.domain != ShaderDomain::PostProcess)
        {
            error = "Shader Composer v1 は surface / layer のみ対応です";
            return false;
        }
        if (asset.nodes.empty() || asset.nodes.size() > 512 || asset.connections.size() > 2048)
        {
            error = "Shader Composer graph size が不正です";
            return false;
        }

        std::unordered_set<std::uint64_t> ids;
        for (const ShaderComposerNode& node : asset.nodes)
        {
            if (node.id == 0 || !ids.insert(node.id).second || !IsFinite(node))
            {
                error = "Shader Composer node ID/value が不正です";
                return false;
            }
        }
        for (const ShaderComposerConnection& edge : asset.connections)
        {
            const ShaderComposerNode* to = asset.FindNode(edge.to_node);
            if (asset.FindNode(edge.from_node) == nullptr || to == nullptr ||
                edge.to_pin >= InputCount(to->kind))
            {
                error = "Shader Composer connection が不正です";
                return false;
            }
        }

        std::error_code ec;
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) { error = "Shader Composer folder を作成できません"; return false; }

        std::filesystem::path temporary = path;
        temporary += L".tmp";
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) { error = "Shader Composer 一時ファイルを作成できません"; return false; }

        stream << std::setprecision(std::numeric_limits<float>::max_digits10);
        stream << "REPLAY_SHADER_COMPOSER " << current_version << '\n';
        stream << "SHADER_GUID " << std::quoted(asset.shader_id.ToString()) << '\n';
        stream << "DISPLAY_NAME " << std::quoted(asset.display_name) << '\n';
        stream << "CATEGORY " << std::quoted(asset.category) << '\n';
        stream << "DOMAIN " << ToString(asset.domain) << '\n';
        stream << "LIGHTING " << ToString(asset.lighting_model) << '\n';
        stream << "GENERATED_HLSL " << std::quoted(asset.generated_hlsl.generic_u8string()) << '\n';
        stream << "NEXT_NODE_ID " << asset.next_node_id << '\n';
        stream << "NODE_COUNT " << asset.nodes.size() << '\n';
        for (const ShaderComposerNode& node : asset.nodes)
        {
            stream << "NODE " << node.id << ' ' << ToString(node.kind) << ' '
                << node.x << ' ' << node.y << ' ' << node.value << ' '
                << node.minimum << ' ' << node.maximum << ' '
                << node.vector2.x << ' ' << node.vector2.y << ' '
                << node.color.x << ' ' << node.color.y << ' ' << node.color.z << ' ' << node.color.w << ' '
                << std::quoted(node.name) << ' ' << std::quoted(node.display_name) << ' '
                << std::quoted(node.category) << ' ' << std::quoted(node.tooltip) << ' '
                << std::quoted(node.default_texture) << '\n';
        }
        stream << "CONNECTION_COUNT " << asset.connections.size() << '\n';
        for (const ShaderComposerConnection& edge : asset.connections)
            stream << "CONNECTION " << edge.from_node << ' ' << edge.from_pin << ' '
                << edge.to_node << ' ' << edge.to_pin << '\n';
        stream << "END_SHADER_COMPOSER\n";
        stream.flush();
        if (!stream)
        {
            stream.close(); std::filesystem::remove(temporary, ec);
            error = "Shader Composer の書き込みに失敗しました"; return false;
        }
        stream.close();
        return ReplaceAtomic(temporary, path, error);
    }

    bool ShaderComposerAsset::Load(const std::filesystem::path& path,
        ShaderComposerAsset& asset, std::string& error)
    {
        error.clear();
        std::ifstream stream(path, std::ios::binary);
        if (!stream) { error = "Shader Composer Asset を開けません"; return false; }

        std::string magic, token, guid_text, domain_text, lighting_text, generated;
        int version = 0;
        ShaderComposerAsset loaded;
        if (!(stream >> magic >> version) || magic != "REPLAY_SHADER_COMPOSER" ||
            version != current_version ||
            !(stream >> token) || token != "SHADER_GUID" || !(stream >> std::quoted(guid_text)) ||
            !(stream >> token) || token != "DISPLAY_NAME" || !(stream >> std::quoted(loaded.display_name)) ||
            !(stream >> token) || token != "CATEGORY" || !(stream >> std::quoted(loaded.category)) ||
            !(stream >> token) || token != "DOMAIN" || !(stream >> domain_text) ||
            !(stream >> token) || token != "LIGHTING" || !(stream >> lighting_text) ||
            !(stream >> token) || token != "GENERATED_HLSL" || !(stream >> std::quoted(generated)) ||
            !(stream >> token) || token != "NEXT_NODE_ID" || !(stream >> loaded.next_node_id))
        {
            error = "Shader Composer header が不正です"; return false;
        }
        if (!ShaderID::TryParse(guid_text, loaded.shader_id) || !loaded.shader_id.IsValid() ||
            !TryParseShaderDomain(domain_text, loaded.domain) ||
            !TryParseShaderLightingModel(lighting_text, loaded.lighting_model) ||
            (loaded.domain != ShaderDomain::Surface &&
                loaded.domain != ShaderDomain::Layer &&
                loaded.domain != ShaderDomain::PostProcess))
        {
            error = "Shader Composer metadata が不正です"; return false;
        }
        loaded.generated_hlsl = std::filesystem::path(generated);

        std::size_t node_count = 0;
        if (!(stream >> token) || token != "NODE_COUNT" || !(stream >> node_count) || node_count > 512)
        { error = "Shader Composer node count が不正です"; return false; }
        loaded.nodes.reserve(node_count);
        std::unordered_set<std::uint64_t> ids;
        for (std::size_t index = 0; index < node_count; ++index)
        {
            ShaderComposerNode node;
            std::string kind_text;
            if (!(stream >> token) || token != "NODE" ||
                !(stream >> node.id >> kind_text >> node.x >> node.y >> node.value >>
                    node.minimum >> node.maximum >> node.vector2.x >> node.vector2.y >>
                    node.color.x >> node.color.y >> node.color.z >> node.color.w >>
                    std::quoted(node.name) >> std::quoted(node.display_name) >>
                    std::quoted(node.category) >> std::quoted(node.tooltip) >>
                    std::quoted(node.default_texture)) ||
                !TryParseShaderComposerNodeKind(kind_text, node.kind) || node.id == 0 ||
                !ids.insert(node.id).second || !IsFinite(node))
            {
                error = "Shader Composer node が不正です"; return false;
            }
            loaded.nodes.push_back(std::move(node));
        }

        std::size_t connection_count = 0;
        if (!(stream >> token) || token != "CONNECTION_COUNT" ||
            !(stream >> connection_count) || connection_count > 2048)
        { error = "Shader Composer connection count が不正です"; return false; }
        for (std::size_t index = 0; index < connection_count; ++index)
        {
            ShaderComposerConnection edge;
            if (!(stream >> token) || token != "CONNECTION" ||
                !(stream >> edge.from_node >> edge.from_pin >> edge.to_node >> edge.to_pin))
            { error = "Shader Composer connection が不正です"; return false; }
            const ShaderComposerNode* to = loaded.FindNode(edge.to_node);
            if (loaded.FindNode(edge.from_node) == nullptr || to == nullptr ||
                edge.to_pin >= InputCount(to->kind))
            { error = "Shader Composer connection target が不正です"; return false; }
            // Duplicate input edges are normalized to the last one.
            loaded.DisconnectInput(edge.to_node, edge.to_pin);
            loaded.connections.push_back(edge);
        }
        if (!(stream >> token) || token != "END_SHADER_COMPOSER")
        { error = "Shader Composer end marker がありません"; return false; }

        std::uint64_t maximum_id = 0;
        for (const ShaderComposerNode& node : loaded.nodes) maximum_id = (std::max)(maximum_id, node.id);
        loaded.next_node_id = (std::max)(loaded.next_node_id, maximum_id + 1);
        asset = std::move(loaded);
        return true;
    }
}
