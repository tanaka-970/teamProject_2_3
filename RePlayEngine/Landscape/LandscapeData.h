#pragma once

#include "LandscapeChunk.h"

#include <DirectXMath.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Landscape
{
    // Landscape v2 の正式な頂点形式。
    //
    // v1 は (x,z)->height 1 個の Height Field だったため、洞窟・張り出し・
    // 同じ XZ に複数の面を置くことが構造的にできなかった。
    // v2 は頂点 + triangle index を保存する任意 Mesh を正とする。
    struct LandscapeVertex
    {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 normal{ 0.0f, 1.0f, 0.0f };
        DirectX::XMFLOAT2 uv{};
    };

    struct LandscapeRayHit
    {
        bool hit = false;
        float distance = 0.0f;
        std::size_t face_index = static_cast<std::size_t>(-1);
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 normal{ 0.0f, 1.0f, 0.0f };
    };

    class LandscapeData final
    {
    public:
        static constexpr int current_version = 2;
        static constexpr int chunk_cell_count = 32;
        static constexpr int maximum_resolution = 4097;
        static constexpr std::size_t maximum_vertices = 4u * 1024u * 1024u;
        static constexpr std::size_t maximum_indices = 12u * 1024u * 1024u;

        // 平面格子を生成する互換 API。
        // 生成後の正式データは vertices_/indices_ なので、Topology 編集後も
        // Height Field の制約へ戻ることはない。
        bool Initialize(int width, int height, float cell_size,
            float initial_height = 0.0f);
        bool InitializeMesh(std::vector<LandscapeVertex> vertices,
            std::vector<std::uint32_t> indices, float grid_cell_hint = 1.0f,
            int grid_width_hint = 0, int grid_height_hint = 0);
        bool Valid() const noexcept;

        int Width() const noexcept { return width_; }
        int Height() const noexcept { return height_; }
        float CellSize() const noexcept { return cell_size_; }

        // v1 互換: 最初に作った格子部分の頂点数。Topology で増えた頂点は含めない。
        std::size_t SampleCount() const noexcept;
        std::size_t Index(int x, int z) const noexcept;
        bool Contains(int x, int z) const noexcept;
        float HeightAt(int x, int z) const noexcept;
        float HeightByIndex(std::size_t index) const noexcept;
        bool SetHeight(int x, int z, float value) noexcept;
        bool SetHeightByIndex(std::size_t index, float value) noexcept;

        const std::vector<LandscapeVertex>& Vertices() const noexcept { return vertices_; }
        const std::vector<std::uint32_t>& Indices() const noexcept { return indices_; }
        std::size_t VertexCount() const noexcept { return vertices_.size(); }
        std::size_t FaceCount() const noexcept { return indices_.size() / 3; }
        std::uint64_t Revision() const noexcept { return revision_; }

        DirectX::XMFLOAT3 VertexPosition(std::size_t index) const noexcept;
        bool SetVertexPosition(std::size_t index, const DirectX::XMFLOAT3& position,
            bool finalize = true) noexcept;
        DirectX::XMFLOAT3 FaceNormal(std::size_t face_index) const noexcept;
        DirectX::XMFLOAT3 FaceCenter(std::size_t face_index) const noexcept;

        // Sculpt が多数頂点を一度に変更した後に 1 回だけ呼ぶ。
        void FinalizeGeometryEdit() noexcept;
        void RecalculateNormals() noexcept;
        void RecalculateBounds() noexcept;
        DirectX::XMFLOAT3 BoundsMin() const noexcept { return bounds_min_; }
        DirectX::XMFLOAT3 BoundsMax() const noexcept { return bounds_max_; }

        // ---- 任意 Topology 編集 ------------------------------------------
        bool SubdivideFace(std::size_t face_index);
        bool DeleteFace(std::size_t face_index);
        bool ExtrudeFace(std::size_t face_index, float distance);
        bool InsetFace(std::size_t face_index, float amount);

        // 選択した三角形を入口として、面法線と逆方向へ三角断面の Tunnel を作る。
        // 元の Face は削除されるので入口は開口し、終端だけ Cap される。
        bool CreateTunnelFromFace(std::size_t face_index, float depth,
            int segments = 4, float end_scale = 1.0f);

        // 2 本の edge 間を quad (2 triangles) で接続する。
        bool BridgeEdges(std::uint32_t a0, std::uint32_t a1,
            std::uint32_t b0, std::uint32_t b1);

        // Local-space triangle raycast。Editor Sculpt / Topology Picking と
        // Collision debug の共通入口として使う。
        bool Raycast(const DirectX::XMFLOAT3& origin,
            const DirectX::XMFLOAT3& direction, float max_distance,
            LandscapeRayHit& hit) const noexcept;

        // ---- 旧 Chunk cache API ------------------------------------------
        // v2 は任意 topology のため「格子 Chunk が正」という前提を捨て、
        // まず 1 logical chunk として扱う。Renderer / Collision の責務分離は保つ。
        const std::vector<LandscapeChunk>& Chunks() const noexcept { return chunks_; }
        std::vector<LandscapeChunk>& Chunks() noexcept { return chunks_; }
        LandscapeChunk* FindChunk(LandscapeChunkCoord coord) noexcept;
        const LandscapeChunk* FindChunk(LandscapeChunkCoord coord) const noexcept;
        void MarkAllDirty() noexcept;
        void MarkSampleDirty(int x, int z) noexcept;
        void RecalculateChunkBounds(LandscapeChunk& chunk) noexcept;

        // ---- 保存 / Migration --------------------------------------------
        bool Save(const std::filesystem::path& path, std::string& error) const;
        static bool Load(const std::filesystem::path& path, LandscapeData& output,
            std::string& error);

        // Component の Scene 保存用。改行を含まない compact text。
        std::string SerializeInline() const;
        bool DeserializeInline(const std::string& text, std::string& error);

    private:
        void BuildChunks();
        void TouchGeometry() noexcept;
        LandscapeVertex Midpoint(std::uint32_t a, std::uint32_t b) const noexcept;
        static bool IsFinite(const LandscapeVertex& vertex) noexcept;

        int width_ = 0;   // 初期 grid の hint。Topology の正規データではない。
        int height_ = 0;
        float cell_size_ = 1.0f;

        std::vector<LandscapeVertex> vertices_;
        std::vector<std::uint32_t> indices_;
        std::vector<LandscapeChunk> chunks_;
        std::uint64_t revision_ = 1;
        DirectX::XMFLOAT3 bounds_min_{};
        DirectX::XMFLOAT3 bounds_max_{};
    };
}
