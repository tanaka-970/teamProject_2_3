// glTF/GLB を DX12 提出用の CPU Geometry と Material URI へ変換する。

#include "gltf_model.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <utility>

using namespace DirectX;

gltf_model::gltf_model(const std::string& filename)
{
    source_filename_ = filename;
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    if (LoadMeshCache(filename))
    {
        timings_.mesh_from_cache = true;
        loaded_ = true;
    }
    else
    {
        loaded_ = Load(filename);
        if (loaded_) SaveMeshCache(filename);
    }
    timings_.total_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    lods_ready_.store(loaded_);
}

gltf_model::~gltf_model() = default;

bool gltf_model::HasAlphaMaskMaterials() const noexcept
{
    for (const Material& material : materials_)
        if (material.alpha_mode == 1) return true;
    return false;
}

bool gltf_model::ComputeBounds(XMFLOAT3& minimum, XMFLOAT3& maximum) const noexcept
{
    bool found = false;
    for (const Primitive& primitive : primitives_)
    {
        if (primitive.index_count == 0 && primitive.vertex_count == 0) continue;

        if (!found)
        {
            minimum = primitive.bounds_minimum;
            maximum = primitive.bounds_maximum;
            found = true;
            continue;
        }

        minimum.x = (std::min)(minimum.x, primitive.bounds_minimum.x);
        minimum.y = (std::min)(minimum.y, primitive.bounds_minimum.y);
        minimum.z = (std::min)(minimum.z, primitive.bounds_minimum.z);
        maximum.x = (std::max)(maximum.x, primitive.bounds_maximum.x);
        maximum.y = (std::max)(maximum.y, primitive.bounds_maximum.y);
        maximum.z = (std::max)(maximum.z, primitive.bounds_maximum.z);
    }
    return found;
}
