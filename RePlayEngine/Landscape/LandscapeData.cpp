#include "LandscapeData.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>

namespace ReplayEngine::Landscape
{
    bool LandscapeData::Initialize(int width, int height, float cell_size, float initial_height)
    {
        if (width < 2 || height < 2 || width > maximum_resolution ||
            height > maximum_resolution || !std::isfinite(cell_size) || cell_size <= 0.0f ||
            !std::isfinite(initial_height)) return false;
        width_ = width;
        height_ = height;
        cell_size_ = cell_size;
        heights_.assign(static_cast<std::size_t>(width_) * height_, initial_height);
        BuildChunks();
        return true;
    }

    bool LandscapeData::Valid() const noexcept
    {
        return width_ >= 2 && height_ >= 2 && cell_size_ > 0.0f &&
            heights_.size() == static_cast<std::size_t>(width_) * height_;
    }

    std::size_t LandscapeData::Index(int x, int z) const noexcept
    {
        return static_cast<std::size_t>(z) * static_cast<std::size_t>(width_) +
            static_cast<std::size_t>(x);
    }

    bool LandscapeData::Contains(int x, int z) const noexcept
    {
        return x >= 0 && z >= 0 && x < width_ && z < height_;
    }

    float LandscapeData::HeightAt(int x, int z) const noexcept
    {
        if (!Contains(x, z)) return 0.0f;
        return heights_[Index(x, z)];
    }

    float LandscapeData::HeightByIndex(std::size_t index) const noexcept
    {
        return index < heights_.size() ? heights_[index] : 0.0f;
    }

    bool LandscapeData::SetHeight(int x, int z, float value) noexcept
    {
        if (!Contains(x, z) || !std::isfinite(value)) return false;
        const std::size_t index = Index(x, z);
        if (heights_[index] == value) return false;
        heights_[index] = value;
        MarkSampleDirty(x, z);
        return true;
    }

    bool LandscapeData::SetHeightByIndex(std::size_t index, float value) noexcept
    {
        if (index >= heights_.size() || !std::isfinite(value)) return false;
        const int z = static_cast<int>(index / static_cast<std::size_t>(width_));
        const int x = static_cast<int>(index % static_cast<std::size_t>(width_));
        return SetHeight(x, z, value);
    }

    LandscapeChunk* LandscapeData::FindChunk(LandscapeChunkCoord coord) noexcept
    {
        for (LandscapeChunk& chunk : chunks_) if (chunk.coord == coord) return &chunk;
        return nullptr;
    }

    const LandscapeChunk* LandscapeData::FindChunk(LandscapeChunkCoord coord) const noexcept
    {
        for (const LandscapeChunk& chunk : chunks_) if (chunk.coord == coord) return &chunk;
        return nullptr;
    }

    void LandscapeData::MarkAllDirty() noexcept
    {
        for (LandscapeChunk& chunk : chunks_)
        {
            ++chunk.revision;
            chunk.render_dirty = true;
            chunk.collision_dirty = true;
            RecalculateChunkBounds(chunk);
        }
    }

    void LandscapeData::MarkSampleDirty(int x, int z) noexcept
    {
        if (!Contains(x, z)) return;
        const int min_chunk_x = (std::max)(0, (x - 1) / chunk_cell_count);
        const int max_chunk_x = (std::max)(0, x / chunk_cell_count);
        const int min_chunk_z = (std::max)(0, (z - 1) / chunk_cell_count);
        const int max_chunk_z = (std::max)(0, z / chunk_cell_count);
        for (int chunk_z = min_chunk_z; chunk_z <= max_chunk_z; ++chunk_z)
        {
            for (int chunk_x = min_chunk_x; chunk_x <= max_chunk_x; ++chunk_x)
            {
                LandscapeChunk* chunk = FindChunk({ chunk_x, chunk_z });
                if (chunk == nullptr) continue;
                ++chunk->revision;
                chunk->render_dirty = true;
                chunk->collision_dirty = true;
                RecalculateChunkBounds(*chunk);
            }
        }
    }

    void LandscapeData::RecalculateChunkBounds(LandscapeChunk& chunk) noexcept
    {
        const int start_x = chunk.coord.x * chunk_cell_count;
        const int start_z = chunk.coord.z * chunk_cell_count;
        const int end_x = (std::min)(width_ - 1, start_x + chunk_cell_count);
        const int end_z = (std::min)(height_ - 1, start_z + chunk_cell_count);
        float minimum = std::numeric_limits<float>::max();
        float maximum = -std::numeric_limits<float>::max();
        for (int z = start_z; z <= end_z; ++z)
        {
            for (int x = start_x; x <= end_x; ++x)
            {
                const float value = HeightAt(x, z);
                minimum = (std::min)(minimum, value);
                maximum = (std::max)(maximum, value);
            }
        }
        chunk.bounds_min = { start_x * cell_size_, minimum, start_z * cell_size_ };
        chunk.bounds_max = { end_x * cell_size_, maximum, end_z * cell_size_ };
    }

    void LandscapeData::BuildChunks()
    {
        chunks_.clear();
        const int count_x = (width_ - 1 + chunk_cell_count - 1) / chunk_cell_count;
        const int count_z = (height_ - 1 + chunk_cell_count - 1) / chunk_cell_count;
        chunks_.reserve(static_cast<std::size_t>(count_x) * count_z);
        for (int z = 0; z < count_z; ++z)
        {
            for (int x = 0; x < count_x; ++x)
            {
                LandscapeChunk chunk;
                chunk.coord = { x, z };
                RecalculateChunkBounds(chunk);
                chunks_.push_back(chunk);
            }
        }
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
        stream << width_ << ' ' << height_ << ' ' << std::setprecision(9) << cell_size_ << '\n';
        for (float height : heights_) stream << std::setprecision(9) << height << '\n';
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
        int version = 0, width = 0, height = 0;
        float cell_size = 0.0f;
        if (!(stream >> magic >> version) || magic != "REPLAY_LANDSCAPE" || version != current_version ||
            !(stream >> width >> height >> cell_size) ||
            !output.Initialize(width, height, cell_size, 0.0f))
        {
            error = "Landscape file formatが不正です。";
            return false;
        }
        for (float& value : output.heights_)
        {
            if (!(stream >> value) || !std::isfinite(value))
            {
                error = "Landscape height gridを読み取れません。";
                return false;
            }
        }
        output.MarkAllDirty();
        return true;
    }
}
