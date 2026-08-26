#pragma once

#include <directxmath.h>

#include <cstdint>
#include <string>
#include <vector>

class static_mesh
{
public:
    struct vertex
    {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 normal{};
        DirectX::XMFLOAT2 texcoord{};
    };

    struct subset
    {
        std::wstring usemtl;
        std::uint32_t index_start{ 0 };
        std::uint32_t index_count{ 0 };
    };

    struct material
    {
        std::wstring name;
        DirectX::XMFLOAT4 Ka{ 0.2f, 0.2f, 0.2f, 1.0f };
        DirectX::XMFLOAT4 Kd{ 0.8f, 0.8f, 0.8f, 1.0f };
        DirectX::XMFLOAT4 Ks{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::wstring texture_filenames[2];
    };

    std::vector<subset> subsets;
    std::vector<material> materials;
    DirectX::XMFLOAT3 bounding_box[2]{};

    const std::vector<vertex>& cpu_vertices() const noexcept { return cpu_vertices_; }
    const std::vector<std::uint32_t>& cpu_indices() const noexcept { return cpu_indices_; }

    static bool can_load(const wchar_t* obj_filename, std::wstring* out_reason = nullptr);
    bool is_loaded() const noexcept { return loaded_; }
    const std::wstring& load_error() const noexcept { return load_error_; }

    static_mesh(const wchar_t* obj_filename, bool flipping_v_coordinates);
    static_mesh(const std::vector<vertex>& vertices,
        const std::vector<std::uint32_t>& indices);

    bool update_procedural_geometry(const std::vector<vertex>& vertices,
        const std::vector<std::uint32_t>& indices);

private:
    bool SetCpuGeometry(const std::vector<vertex>& vertices,
        const std::vector<std::uint32_t>& indices);
    void RecalculateBounds() noexcept;

    std::vector<vertex> cpu_vertices_;
    std::vector<std::uint32_t> cpu_indices_;
    bool loaded_{ false };
    std::wstring load_error_;
};
