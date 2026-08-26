#include "static_mesh.h"

#include <utility>

static_mesh::static_mesh(const std::vector<vertex>& vertices,
    const std::vector<std::uint32_t>& indices)
{
    subsets.push_back({ L"Procedural", 0, static_cast<std::uint32_t>(indices.size()) });
    material value{};
    value.name = L"Procedural";
    materials.push_back(std::move(value));
    loaded_ = SetCpuGeometry(vertices, indices);
    if (!loaded_) load_error_ = L"Procedural mesh の CPU geometry が不正です。";
}

bool static_mesh::update_procedural_geometry(const std::vector<vertex>& vertices,
    const std::vector<std::uint32_t>& indices)
{
    if (!SetCpuGeometry(vertices, indices)) return false;
    if (subsets.empty()) subsets.push_back({ L"Procedural", 0, static_cast<std::uint32_t>(indices.size()) });
    subsets.front().index_start = 0;
    subsets.front().index_count = static_cast<std::uint32_t>(indices.size());
    loaded_ = true;
    load_error_.clear();
    return true;
}
