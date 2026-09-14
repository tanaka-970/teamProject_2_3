#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Assets
{
    struct VertexColorRgba8 final
    {
        std::uint8_t r = 255;
        std::uint8_t g = 255;
        std::uint8_t b = 255;
        std::uint8_t a = 255;
    };

    static_assert(sizeof(VertexColorRgba8) == 4);

    struct VertexColorFingerprint final
    {
        std::uint32_t vertex_count = 0;
        std::uint32_t index_count = 0;
        std::uint64_t position_hash = 1469598103934665603ull;

        bool operator==(const VertexColorFingerprint& other) const noexcept;
    };

    struct VertexColorMeshBlock final
    {
        std::uint32_t mesh_index = 0;
        VertexColorFingerprint fingerprint;
        std::vector<VertexColorRgba8> colors;
    };

    class VertexColorAsset final
    {
    public:
        static constexpr const char* file_extension = ".replayvcolor";
        static constexpr int current_version = 1;
        std::vector<VertexColorMeshBlock> meshes;

        static std::filesystem::path SidecarPath(const std::filesystem::path& model_path);
        static VertexColorFingerprint Fingerprint(const void* positions,
            std::uint32_t vertex_count, std::size_t vertex_stride,
            std::uint32_t index_count) noexcept;
        const VertexColorMeshBlock* FindMesh(std::uint32_t mesh_index,
            const VertexColorFingerprint& fingerprint) const noexcept;
        static bool LoadFromFile(const std::filesystem::path& path,
            const std::vector<VertexColorFingerprint>& expected,
            VertexColorAsset& out, std::string& error);
        static bool SaveToFile(const std::filesystem::path& path,
            const VertexColorAsset& asset, std::string& error);
    };
}
