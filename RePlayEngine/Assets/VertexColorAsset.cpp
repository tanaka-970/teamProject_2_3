#include "VertexColorAsset.h"

#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>

namespace ReplayEngine::Assets
{
    bool VertexColorFingerprint::operator==(const VertexColorFingerprint& other) const noexcept
    {
        return vertex_count == other.vertex_count && index_count == other.index_count &&
            position_hash == other.position_hash;
    }

    std::filesystem::path VertexColorAsset::SidecarPath(const std::filesystem::path& model_path)
    {
        auto path = model_path;
        path += file_extension;
        return path;
    }

    VertexColorFingerprint VertexColorAsset::Fingerprint(const void* positions,
        std::uint32_t vertex_count, std::size_t vertex_stride,
        std::uint32_t index_count) noexcept
    {
        VertexColorFingerprint result;
        result.vertex_count = vertex_count;
        result.index_count = index_count;
        if ((vertex_count != 0 && positions == nullptr) || vertex_stride < 12 ||
            (vertex_count != 0 && vertex_stride >
                (std::numeric_limits<std::size_t>::max)() / vertex_count))
        {
            result.vertex_count = 0;
            result.position_hash = 0;
            return result;
        }
        const auto* bytes = static_cast<const std::uint8_t*>(positions);
        for (std::uint32_t vertex = 0; vertex < vertex_count; ++vertex)
        {
            for (std::size_t component = 0; component < 3; ++component)
            {
                std::uint32_t bits = 0;
                std::memcpy(&bits, bytes + vertex * vertex_stride + component * 4, 4);
                for (unsigned shift = 0; shift < 32; shift += 8)
                {
                    result.position_hash ^= (bits >> shift) & 0xffu;
                    result.position_hash *= 1099511628211ull;
                }
            }
        }
        return result;
    }

    const VertexColorMeshBlock* VertexColorAsset::FindMesh(std::uint32_t mesh_index,
        const VertexColorFingerprint& fingerprint) const noexcept
    {
        for (const auto& mesh : meshes)
            if (mesh.mesh_index == mesh_index && mesh.fingerprint == fingerprint &&
                mesh.colors.size() == fingerprint.vertex_count) return &mesh;
        return nullptr;
    }

    bool VertexColorAsset::LoadFromFile(const std::filesystem::path& path,
        const std::vector<VertexColorFingerprint>& expected,
        VertexColorAsset& out, std::string& error)
    {
        out.meshes.clear();
        error.clear();
        std::ifstream file(path, std::ios::binary);
        if (!file) { error = "頂点カラーを開けません"; return false; }
        VertexColorAsset asset;
        const auto read_line = [&file](std::istringstream& input)
        {
            std::string line;
            if (!std::getline(file, line) || file.eof()) return false;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            input.clear();
            input.str(line);
            input.imbue(std::locale::classic());
            return true;
        };
        const auto ended = [](std::istringstream& input)
        {
            input >> std::ws;
            return input.eof();
        };
        std::istringstream input;
        std::string tag;
        unsigned version = 0;
        std::uint64_t count = 0;
        if (!read_line(input) || !(input >> tag >> version) ||
            tag != "VERTEX_COLOR_VERSION" || version != current_version || !ended(input) ||
            !read_line(input) || !(input >> tag >> count) || tag != "MESH_COUNT" ||
            count > expected.size() || !ended(input))
        { error = "頂点カラーのヘッダが不正です"; return false; }
        std::uint64_t total_bytes = 0;
        constexpr std::uint64_t maximum_bytes = 1ull << 30;
        for (std::uint64_t i = 0; i < count; ++i)
        {
            VertexColorMeshBlock mesh;
            std::string hash;
            if (!read_line(input) || !(input >> tag >> mesh.mesh_index >>
                mesh.fingerprint.vertex_count >> mesh.fingerprint.index_count >> hash) ||
                tag != "MESH" || !ended(input) || hash.size() != 16 ||
                hash.find_first_not_of("0123456789abcdef") != std::string::npos ||
                mesh.mesh_index >= expected.size() ||
                (!asset.meshes.empty() && mesh.mesh_index <= asset.meshes.back().mesh_index))
            { error = "頂点カラーのメッシュヘッダが不正です"; return false; }
            std::istringstream hash_input(hash);
            hash_input.imbue(std::locale::classic());
            hash_input >> std::hex >> mesh.fingerprint.position_hash;
            if (!(mesh.fingerprint == expected[mesh.mesh_index]))
            { error = "頂点カラーの指紋がモデルと一致しません"; return false; }
            total_bytes += static_cast<std::uint64_t>(mesh.fingerprint.vertex_count) * 4;
            if (total_bytes > maximum_bytes)
            { error = "頂点カラーが大きすぎます"; return false; }
            asset.meshes.push_back(std::move(mesh));
        }
        std::uint64_t declared_bytes = 0;
        if (!read_line(input) || !(input >> tag >> declared_bytes) || tag != "COLORS" ||
            declared_bytes != total_bytes || !ended(input))
        { error = "頂点カラーのバイト数が不正です"; return false; }
        const auto payload_start = file.tellg();
        file.seekg(0, std::ios::end);
        const auto file_end = file.tellg();
        if (payload_start < 0 || file_end < payload_start ||
            static_cast<std::uint64_t>(file_end - payload_start) != total_bytes)
        { error = "頂点カラーのファイル長が不正です"; return false; }
        file.seekg(payload_start);
        // 全メッシュの指紋が一致してから初めて色を読む。
        for (auto& mesh : asset.meshes)
        {
            mesh.colors.resize(mesh.fingerprint.vertex_count);
            const auto bytes = static_cast<std::streamsize>(mesh.colors.size() * 4);
            if (bytes != 0 && !file.read(reinterpret_cast<char*>(mesh.colors.data()), bytes))
            { error = "頂点カラーが途中で切れています"; return false; }
        }
        out = std::move(asset);
        return true;
    }

    bool VertexColorAsset::SaveToFile(const std::filesystem::path& path,
        const VertexColorAsset& asset, std::string& error)
    {
        error.clear();
        std::uint64_t total_bytes = 0;
        for (std::size_t i = 0; i < asset.meshes.size(); ++i)
        {
            const auto& mesh = asset.meshes[i];
            total_bytes += static_cast<std::uint64_t>(mesh.colors.size()) * 4;
            if (mesh.colors.size() != mesh.fingerprint.vertex_count ||
                (i != 0 && mesh.mesh_index <= asset.meshes[i - 1].mesh_index) ||
                total_bytes > (1ull << 30))
            { error = "頂点カラーのメッシュまたは色数が不正です"; return false; }
        }
        std::ofstream file(path, std::ios::binary);
        if (!file) { error = "頂点カラーを書き込めません"; return false; }
        file.imbue(std::locale::classic());
        file << "VERTEX_COLOR_VERSION " << current_version << '\n';
        file << "MESH_COUNT " << asset.meshes.size() << '\n';
        for (const auto& mesh : asset.meshes)
        {
            file << "MESH " << mesh.mesh_index << ' ' << mesh.fingerprint.vertex_count << ' '
                << mesh.fingerprint.index_count << ' ' << std::hex << std::setfill('0') <<
                std::setw(16) << mesh.fingerprint.position_hash << std::dec << '\n';
        }
        file << "COLORS " << total_bytes << '\n';
        for (const auto& mesh : asset.meshes)
            if (!mesh.colors.empty()) file.write(reinterpret_cast<const char*>(mesh.colors.data()),
                static_cast<std::streamsize>(mesh.colors.size() * 4));
        file.flush();
        if (!file) { error = "頂点カラーの書き込みに失敗しました"; return false; }
        return true;
    }
}
