#include "LandscapeData.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

using namespace DirectX;

namespace ReplayEngine::Landscape
{
    std::string LandscapeData::SerializeInline() const
    {
        if (!Valid()) return {};
        std::ostringstream stream;
        stream << std::setprecision(9);
        stream << "RPLM2 " << width_ << ' ' << height_ << ' ' << cell_size_ << ' '
            << vertices_.size() << ' ' << indices_.size();
        for (const LandscapeVertex& vertex : vertices_)
        {
            // normal は load 後に再計算できるので position + uv のみ保存。
            stream << ' ' << vertex.position.x << ' ' << vertex.position.y << ' '
                << vertex.position.z << ' ' << vertex.uv.x << ' ' << vertex.uv.y;
        }
        for (std::uint32_t index : indices_) stream << ' ' << index;
        return stream.str();
    }

    bool LandscapeData::DeserializeInline(const std::string& text, std::string& error)
    {
        std::istringstream stream(text);
        std::string signature;
        int width = 0, height = 0;
        float cell = 1.0f;
        std::size_t vertex_count = 0, index_count = 0;
        if (!(stream >> signature >> width >> height >> cell >> vertex_count >> index_count) ||
            signature != "RPLM2" || vertex_count < 3 || vertex_count > maximum_vertices ||
            index_count < 3 || index_count > maximum_indices || index_count % 3 != 0 ||
            !std::isfinite(cell) || cell <= 0.0f)
        {
            error = "Landscape inline data header が不正です。";
            return false;
        }

        std::vector<LandscapeVertex> vertices(vertex_count);
        for (LandscapeVertex& vertex : vertices)
        {
            if (!(stream >> vertex.position.x >> vertex.position.y >> vertex.position.z >>
                vertex.uv.x >> vertex.uv.y) || !IsFinite(vertex))
            {
                error = "Landscape inline vertex を読み取れません。";
                return false;
            }
        }
        std::vector<std::uint32_t> indices(index_count);
        for (std::uint32_t& index : indices)
        {
            std::uint64_t raw = 0;
            if (!(stream >> raw) || raw >= vertex_count)
            {
                error = "Landscape inline index を読み取れません。";
                return false;
            }
            index = static_cast<std::uint32_t>(raw);
        }
        if (!InitializeMesh(std::move(vertices), std::move(indices), cell, width, height))
        {
            error = "Landscape inline mesh を初期化できません。";
            return false;
        }
        return true;
    }

    bool LandscapeData::Save(const std::filesystem::path& path, std::string& error) const
    {
        if (!Valid()) { error = "LandscapeDataが無効です。"; return false; }
        std::error_code filesystem_error;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), filesystem_error);
        if (filesystem_error) { error = "Landscape保存Folderを作成できません。"; return false; }

        const std::filesystem::path temporary = path.string() + ".tmp";
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) { error = "Landscape temporary fileを作成できません。"; return false; }
        stream << "REPLAY_LANDSCAPE " << current_version << '\n';
        stream << SerializeInline() << '\n';
        stream.flush();
        if (!stream) { error = "Landscape書き込みに失敗しました。"; return false; }
        stream.close();

        LandscapeData verification;
        if (!Load(temporary, verification, error)) return false;
        const std::filesystem::path backup = path.string() + ".bak";
        const bool existing = std::filesystem::exists(path, filesystem_error) && !filesystem_error;
        if (existing)
        {
            std::error_code ignored;
            std::filesystem::remove(backup, ignored);
            std::filesystem::rename(path, backup, filesystem_error);
            if (filesystem_error) { error = "Landscape backupを作成できません。"; return false; }
        }
        filesystem_error.clear();
        std::filesystem::rename(temporary, path, filesystem_error);
        if (filesystem_error)
        {
            if (existing)
            {
                std::error_code ignored;
                std::filesystem::rename(backup, path, ignored);
            }
            error = "Landscapeを安全に差し替えられません。";
            return false;
        }
        return true;
    }

    bool LandscapeData::Load(const std::filesystem::path& path, LandscapeData& output,
        std::string& error)
    {
        std::ifstream stream(path, std::ios::binary);
        std::string magic;
        int version = 0;
        if (!(stream >> magic >> version) || magic != "REPLAY_LANDSCAPE")
        {
            error = "Landscape file signature が不正です。";
            return false;
        }

        if (version == 1)
        {
            // v1: width height cell + height value x width*height。
            int width = 0, height = 0;
            float cell = 0.0f;
            if (!(stream >> width >> height >> cell) || !output.Initialize(width, height, cell, 0.0f))
            {
                error = "Landscape v1 header が不正です。";
                return false;
            }
            for (std::size_t index = 0; index < output.SampleCount(); ++index)
            {
                float value = 0.0f;
                if (!(stream >> value) || !std::isfinite(value))
                {
                    error = "Landscape v1 height gridを読み取れません。";
                    return false;
                }
                output.vertices_[index].position.y = value;
            }
            // 読み込んだ時点で v2 arbitrary mesh へ変換済み。
            output.FinalizeGeometryEdit();
            return true;
        }

        if (version != current_version)
        {
            error = "未対応の Landscape version です: " + std::to_string(version);
            return false;
        }

        std::string inline_data;
        std::getline(stream >> std::ws, inline_data);
        if (!output.DeserializeInline(inline_data, error)) return false;
        output.MarkAllDirty();
        return true;
    }
}
