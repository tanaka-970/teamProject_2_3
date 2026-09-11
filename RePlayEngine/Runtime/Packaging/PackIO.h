#pragma once

#include "../../../tinygltf-release/json.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ReplayEngine::Runtime::Packaging
{
    using Json = nlohmann::json;

    inline void Require(bool condition, const std::string& message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    inline std::uint64_t HashBytes(const std::uint8_t* bytes, std::size_t size,
        std::uint64_t hash = 14695981039346656037ull) noexcept
    {
        for (std::size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    inline std::string FileFingerprint(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        Require(static_cast<bool>(stream), "Cannot read: " + path.generic_u8string());
        std::uint64_t hash = 14695981039346656037ull;
        std::uint64_t size = 0;
        char buffer[65536];
        while (stream.read(buffer, sizeof(buffer)) || stream.gcount() != 0)
        {
            const auto count = static_cast<std::size_t>(stream.gcount());
            hash = HashBytes(reinterpret_cast<const std::uint8_t*>(buffer), count, hash);
            size += count;
        }
        Require(stream.eof() && !stream.bad(), "Read failed: " + path.generic_u8string());
        return std::to_string(size) + ":" + std::to_string(hash);
    }

    inline void WritePack(const std::filesystem::path& path, const Json& data)
    {
        const auto bytes = Json::to_cbor(data);
        Require(bytes.size() <= 512ull * 1024 * 1024 - 16, "Pack exceeds the supported size: " + path.generic_u8string());
        const std::uint64_t checksum = HashBytes(bytes.data(), bytes.size());
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        Require(static_cast<bool>(stream), "Cannot create pack: " + path.generic_u8string());
        stream.write("RPACK001", 8);
        stream.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));
        stream.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        stream.flush();
        Require(static_cast<bool>(stream), "Cannot write pack: " + path.generic_u8string());
        stream.close();
        Require(!stream.fail(), "Cannot close pack: " + path.generic_u8string());
    }

    inline Json ReadPack(const std::filesystem::path& path, const char* kind)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        Require(static_cast<bool>(stream), "Missing pack: " + path.generic_u8string());
        const auto size = stream.tellg();
        Require(size >= 16 && size <= 512ll * 1024 * 1024,
            "Invalid pack size: " + path.generic_u8string());
        stream.seekg(0);
        char magic[8];
        std::uint64_t checksum = 0;
        stream.read(magic, sizeof(magic));
        stream.read(reinterpret_cast<char*>(&checksum), sizeof(checksum));
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size) - 16);
        stream.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        Require(static_cast<bool>(stream) && std::string(magic, 8) == "RPACK001" &&
            HashBytes(bytes.data(), bytes.size()) == checksum,
            "Corrupt pack: " + path.generic_u8string());
        Json data = Json::from_cbor(bytes);
        Require(data.at("kind") == kind && data.at("version") == 1,
            "Unsupported pack version: " + path.generic_u8string());
        return data;
    }
}
