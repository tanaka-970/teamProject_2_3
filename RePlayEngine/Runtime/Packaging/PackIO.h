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

    // 条件を満たさない場合に例外を投げるヘルパー。
    // ファイル入出力などの成否を簡潔に検証するために使用する。
    inline void Require(bool condition, const std::string& message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    // FNV-1a 64bit ハッシュアルゴリズムでバイト列のハッシュ値を計算する。
    // hash には前回の計算結果を渡すことで連続したデータのハッシュを継続できる。
    // 初期値 14695981039346656037ull は FNV offset basis、
    // 乗数 1099511628211ull は FNV prime である。
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

    // 指定されたファイルの内容を読み込み、"サイズ:ハッシュ" 形式の文字列を返す。
    // 内容が同じファイルであれば同じ文字列が生成され、簡易的なフィンガープリントとして使える。
    inline std::string FileFingerprint(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        Require(static_cast<bool>(stream), "Cannot read: " + path.generic_u8string());

        // FNV-1a の初期値からハッシュ計算を開始する。
        std::uint64_t hash = 14695981039346656037ull;
        std::uint64_t size = 0;
        char buffer[65536];

        // 最後のブロックまで読み込み、読み取れたバイト数分だけハッシュとサイズを更新する。
        while (stream.read(buffer, sizeof(buffer)) || stream.gcount() != 0)
        {
            const auto count = static_cast<std::size_t>(stream.gcount());
            hash = HashBytes(reinterpret_cast<const std::uint8_t*>(buffer), count, hash);
            size += count;
        }

        Require(stream.eof() && !stream.bad(), "Read failed: " + path.generic_u8string());
        return std::to_string(size) + ":" + std::to_string(hash);
    }

    // JSON データを CBOR 形式にシリアライズし、オリジナルのパックファイルとして書き出す。
    // ファイル先頭には "RPACK001" という 8 バイトのマジックナンバーと、
    // 続く 8 バイトのチェックサムが付加される。
    inline void WritePack(const std::filesystem::path& path, const Json& data)
    {
        const auto bytes = Json::to_cbor(data);

        // ペイロード部分はファイル全体で 512MB 以下となるようにする。
        // 16 バイトのヘッダ分を差し引いたサイズがペイロードの上限となる。
        Require(bytes.size() <= 512ull * 1024 * 1024 - 16, "Pack exceeds the supported size: " + path.generic_u8string());

        const std::uint64_t checksum = HashBytes(bytes.data(), bytes.size());
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        Require(static_cast<bool>(stream), "Cannot create pack: " + path.generic_u8string());

        // ヘッダ書き込み：マジックナンバー + チェックサム
        stream.write("RPACK001", 8);
        stream.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));

        // CBOR ペイロード書き込み
        stream.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        stream.flush();

        Require(static_cast<bool>(stream), "Cannot write pack: " + path.generic_u8string());
        stream.close();
        Require(!stream.fail(), "Cannot close pack: " + path.generic_u8string());
    }

    // WritePack で書き出されたパックファイルを読み込み、JSON データを復元する。
    // kind には期待するパックの種別を指定し、ヘッダー内の kind/version が一致しない場合は例外を投げる。
    inline Json ReadPack(const std::filesystem::path& path, const char* kind)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        Require(static_cast<bool>(stream), "Missing pack: " + path.generic_u8string());

        // ファイルサイズを先頭に取得し、ヘッダー 16 バイトを含めた範囲で妥当性を確認する。
        const auto size = stream.tellg();
        Require(size >= 16 && size <= 512ll * 1024 * 1024,
            "Invalid pack size: " + path.generic_u8string());

        stream.seekg(0);
        char magic[8];
        std::uint64_t checksum = 0;

        // マジックナンバーとチェックサムの読み込み
        stream.read(magic, sizeof(magic));
        stream.read(reinterpret_cast<char*>(&checksum), sizeof(checksum));

        // 残りの領域を CBOR ペイロードとして読み込む。
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size) - 16);
        stream.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));

        // マジックナンバーとチェックサムが正しいことを確認する。
        Require(static_cast<bool>(stream) && std::string(magic, 8) == "RPACK001" &&
            HashBytes(bytes.data(), bytes.size()) == checksum,
            "Corrupt pack: " + path.generic_u8string());

        Json data = Json::from_cbor(bytes);
        Require(data.at("kind") == kind && data.at("version") == 1,
            "Unsupported pack version: " + path.generic_u8string());
        return data;
    }
}
