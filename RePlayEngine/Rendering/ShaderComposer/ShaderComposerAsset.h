#pragma once

#include "../Shaders/ShaderAsset.h"

#include <DirectXMath.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Rendering
{
    enum class ShaderComposerNodeKind : std::int32_t
    {
        UV = 0,
        Time,
        Normal,
        ViewDirection,
        Float,
        Color,
        FloatProperty,
        ColorProperty,
        TextureProperty,
        Add,
        Subtract,
        Multiply,
        Divide,
        Lerp,
        Saturate,
        Power,
        Fresnel,
        UVScroll,
        Noise,
        Dissolve,
        SurfaceOutput,
        LayerOutput,
    };

    const char* ToString(ShaderComposerNodeKind kind) noexcept;
    bool TryParseShaderComposerNodeKind(const std::string& text,
        ShaderComposerNodeKind& out) noexcept;

    enum class ShaderComposerValueType : std::int32_t
    {
        Invalid = 0,
        Float,
        Float2,
        Float3,
        Float4,
    };

    struct ShaderComposerNode final
    {
        std::uint64_t id = 0;
        ShaderComposerNodeKind kind = ShaderComposerNodeKind::Float;
        float x = 0.0f;
        float y = 0.0f;

        // Constant / fallback value / property range.
        float value = 0.0f;
        float minimum = 0.0f;
        float maximum = 1.0f;
        DirectX::XMFLOAT2 vector2{ 1.0f, 0.0f };
        DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };

        // Property nodes only. name is HLSL identifier, display_name/category are UI metadata.
        std::string name;
        std::string display_name;
        std::string category;
        std::string tooltip;
        std::string default_texture{ "white" };
    };

    struct ShaderComposerConnection final
    {
        std::uint64_t from_node = 0;
        std::uint32_t from_pin = 0; // v1 nodes have one output, reserved for future expansion.
        std::uint64_t to_node = 0;
        std::uint32_t to_pin = 0;
    };

    class ShaderComposerAsset final
    {
    public:
        static constexpr int current_version = 1;
        static constexpr const char* file_extension = ".replayshadergraph";

        ShaderID shader_id;
        std::string display_name{ "New Composer Shader" };
        std::string category{ "Project/Composer" };
        ShaderDomain domain = ShaderDomain::Surface;
        ShaderLightingModel lighting_model = ShaderLightingModel::Unlit;
        std::filesystem::path generated_hlsl;

        std::vector<ShaderComposerNode> nodes;
        std::vector<ShaderComposerConnection> connections;
        std::uint64_t next_node_id = 1;

        ShaderComposerNode& AddNode(ShaderComposerNodeKind kind,
            float x = 0.0f, float y = 0.0f);
        bool RemoveNode(std::uint64_t id);
        ShaderComposerNode* FindNode(std::uint64_t id) noexcept;
        const ShaderComposerNode* FindNode(std::uint64_t id) const noexcept;

        // One connection per input socket. Reconnecting an input replaces its old edge.
        bool Connect(std::uint64_t from_node, std::uint64_t to_node,
            std::uint32_t to_pin);
        bool DisconnectInput(std::uint64_t to_node, std::uint32_t to_pin);
        const ShaderComposerConnection* FindInput(std::uint64_t to_node,
            std::uint32_t to_pin) const noexcept;

        static ShaderComposerAsset CreateDefault(ShaderDomain domain,
            const std::string& display_name,
            const std::filesystem::path& generated_hlsl);

        static bool Save(const ShaderComposerAsset& asset,
            const std::filesystem::path& path, std::string& error);
        static bool Load(const std::filesystem::path& path,
            ShaderComposerAsset& asset, std::string& error);
    };
}
