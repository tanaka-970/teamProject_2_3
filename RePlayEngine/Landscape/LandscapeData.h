#pragma once

#include "LandscapeChunk.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Landscape
{
    class LandscapeData final
    {
    public:
        static constexpr int current_version = 1;
        static constexpr int chunk_cell_count = 32;
        static constexpr int maximum_resolution = 4097;

        bool Initialize(int width, int height, float cell_size, float initial_height = 0.0f);
        bool Valid() const noexcept;

        int Width() const noexcept { return width_; }
        int Height() const noexcept { return height_; }
        float CellSize() const noexcept { return cell_size_; }
        std::size_t SampleCount() const noexcept { return heights_.size(); }
        std::size_t Index(int x, int z) const noexcept;
        bool Contains(int x, int z) const noexcept;
        float HeightAt(int x, int z) const noexcept;
        float HeightByIndex(std::size_t index) const noexcept;
        bool SetHeight(int x, int z, float value) noexcept;
        bool SetHeightByIndex(std::size_t index, float value) noexcept;

        const std::vector<float>& Heights() const noexcept { return heights_; }
        const std::vector<LandscapeChunk>& Chunks() const noexcept { return chunks_; }
        std::vector<LandscapeChunk>& Chunks() noexcept { return chunks_; }
        LandscapeChunk* FindChunk(LandscapeChunkCoord coord) noexcept;
        const LandscapeChunk* FindChunk(LandscapeChunkCoord coord) const noexcept;

        void MarkAllDirty() noexcept;
        void MarkSampleDirty(int x, int z) noexcept;
        void RecalculateChunkBounds(LandscapeChunk& chunk) noexcept;

        bool Save(const std::filesystem::path& path, std::string& error) const;
        static bool Load(const std::filesystem::path& path, LandscapeData& output,
            std::string& error);

    private:
        void BuildChunks();

        int width_ = 0;
        int height_ = 0;
        float cell_size_ = 1.0f;
        std::vector<float> heights_;
        std::vector<LandscapeChunk> chunks_;
    };
}
