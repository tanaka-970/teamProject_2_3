#include "skinned_mesh.h"

#include "tinygltf-release/tiny_gltf.h"
#include "../../RePlayEngine/Assets/TextureCompressor.h"

// OutputDebugStringA の宣言。以前は d3d11.h 経由で入っていた。
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <numeric>

using namespace DirectX;

namespace
{
    struct GltfFileContext
    {
        std::filesystem::path base_directory;
    };

    std::filesystem::path ResolveGltfFile(const std::string& filename, void* user_data)
    {
        const auto* context = static_cast<const GltfFileContext*>(user_data);
        std::filesystem::path path = std::filesystem::u8path(filename);
        if (path.is_absolute() || context == nullptr) return path;
        return context->base_directory / path;
    }

    bool ReadWideFile(std::vector<unsigned char>& bytes,
        const std::filesystem::path& filename, std::string* error)
    {
        std::ifstream stream(filename, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            if (error) *error = "file open failed";
            return false;
        }
        const std::streamoff size = stream.tellg();
        if (size < 0)
        {
            if (error) *error = "file size failed";
            return false;
        }
        bytes.resize(static_cast<std::size_t>(size));
        stream.seekg(0, std::ios::beg);
        if (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()), size))
        {
            if (error) *error = "file read failed";
            return false;
        }
        return true;
    }

    struct NodePose
    {
        XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
        XMFLOAT4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
        XMFLOAT3 translation{ 0.0f, 0.0f, 0.0f };
    };

    const unsigned char* BufferViewData(const tinygltf::Model& model,
        int view_index, std::size_t byte_offset, std::size_t& available)
    {
        available = 0;
        if (view_index < 0 || view_index >= static_cast<int>(model.bufferViews.size()))
            return nullptr;
        const auto& view = model.bufferViews[static_cast<std::size_t>(view_index)];
        if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size())) return nullptr;
        const auto& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];
        if (byte_offset > view.byteLength) return nullptr;
        const std::size_t offset = view.byteOffset + byte_offset;
        if (offset > buffer.data.size()) return nullptr;
        available = (std::min)(view.byteLength - byte_offset, buffer.data.size() - offset);
        return buffer.data.data() + offset;
    }

    const unsigned char* AccessorData(const tinygltf::Model& model,
        const tinygltf::Accessor& accessor, std::size_t& stride, std::size_t& available)
    {
        if (accessor.bufferView < 0 ||
            accessor.bufferView >= static_cast<int>(model.bufferViews.size())) return nullptr;
        const auto& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
        const int byte_stride = accessor.ByteStride(view);
        if (byte_stride <= 0) return nullptr;
        stride = static_cast<std::size_t>(byte_stride);
        return BufferViewData(model, accessor.bufferView, accessor.byteOffset, available);
    }

    int ComponentCount(int type)
    {
        switch (type)
        {
        case TINYGLTF_TYPE_SCALAR: return 1;
        case TINYGLTF_TYPE_VEC2: return 2;
        case TINYGLTF_TYPE_VEC3: return 3;
        case TINYGLTF_TYPE_VEC4: return 4;
        case TINYGLTF_TYPE_MAT4: return 16;
        default: return 0;
        }
    }

    double ReadComponent(const unsigned char* value, int component_type, bool normalized)
    {
        switch (component_type)
        {
        case TINYGLTF_COMPONENT_TYPE_BYTE:
        {
            const auto v = *reinterpret_cast<const std::int8_t*>(value);
            return normalized ? (std::max)(-1.0, static_cast<double>(v) / 127.0) : v;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        {
            const auto v = *reinterpret_cast<const std::uint8_t*>(value);
            return normalized ? static_cast<double>(v) / 255.0 : v;
        }
        case TINYGLTF_COMPONENT_TYPE_SHORT:
        {
            const auto v = *reinterpret_cast<const std::int16_t*>(value);
            return normalized ? (std::max)(-1.0, static_cast<double>(v) / 32767.0) : v;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        {
            const auto v = *reinterpret_cast<const std::uint16_t*>(value);
            return normalized ? static_cast<double>(v) / 65535.0 : v;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        {
            const auto v = *reinterpret_cast<const std::uint32_t*>(value);
            return normalized ? static_cast<double>(v) / 4294967295.0 : v;
        }
        case TINYGLTF_COMPONENT_TYPE_FLOAT:
            return *reinterpret_cast<const float*>(value);
        case TINYGLTF_COMPONENT_TYPE_DOUBLE:
            return *reinterpret_cast<const double*>(value);
        default:
            return 0.0;
        }
    }

    std::size_t ComponentSize(int component_type)
    {
        switch (component_type)
        {
        case TINYGLTF_COMPONENT_TYPE_BYTE:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return 1;
        case TINYGLTF_COMPONENT_TYPE_SHORT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return 2;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        case TINYGLTF_COMPONENT_TYPE_FLOAT: return 4;
        case TINYGLTF_COMPONENT_TYPE_DOUBLE: return 8;
        default: return 0;
        }
    }

    bool ReadAccessor(const tinygltf::Model& model, int accessor_index,
        int wanted_components, std::vector<float>& values)
    {
        if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size()))
            return false;
        const auto& accessor = model.accessors[static_cast<std::size_t>(accessor_index)];
        if (ComponentCount(accessor.type) < wanted_components)
            return false;
        const std::size_t component_size = ComponentSize(accessor.componentType);
        if (component_size == 0) return false;
        values.assign(accessor.count * static_cast<std::size_t>(wanted_components), 0.0f);

        if (accessor.bufferView >= 0)
        {
            std::size_t stride = 0, available = 0;
            const unsigned char* bytes = AccessorData(model, accessor, stride, available);
            const std::size_t element_size = component_size * static_cast<std::size_t>(wanted_components);
            const std::size_t required = accessor.count == 0 ? 0 :
                (accessor.count - 1) * stride + element_size;
            if (!bytes || stride < element_size || required > available) return false;
            for (std::size_t i = 0; i < accessor.count; ++i)
            {
                for (int c = 0; c < wanted_components; ++c)
                {
                    values[i * static_cast<std::size_t>(wanted_components) + c] =
                        static_cast<float>(ReadComponent(bytes + i * stride + c * component_size,
                            accessor.componentType, accessor.normalized));
                }
            }
        }
        else if (!accessor.sparse.isSparse)
            return false;

        if (accessor.sparse.isSparse)
        {
            const std::size_t sparse_count = accessor.sparse.count > 0
                ? static_cast<std::size_t>(accessor.sparse.count) : 0;
            if (sparse_count > accessor.count) return false;
            std::size_t index_available = 0, value_available = 0;
            const unsigned char* index_bytes = BufferViewData(model,
                accessor.sparse.indices.bufferView, accessor.sparse.indices.byteOffset,
                index_available);
            const unsigned char* value_bytes = BufferViewData(model,
                accessor.sparse.values.bufferView, accessor.sparse.values.byteOffset,
                value_available);
            const std::size_t index_size = ComponentSize(accessor.sparse.indices.componentType);
            const std::size_t value_stride = component_size *
                static_cast<std::size_t>(ComponentCount(accessor.type));
            if (!index_bytes || !value_bytes || index_size == 0 || value_stride == 0 ||
                sparse_count * index_size > index_available ||
                sparse_count * value_stride > value_available) return false;
            for (std::size_t sparse = 0; sparse < sparse_count; ++sparse)
            {
                const std::size_t destination = static_cast<std::size_t>(ReadComponent(
                    index_bytes + sparse * index_size,
                    accessor.sparse.indices.componentType, false));
                if (destination >= accessor.count) return false;
                for (int c = 0; c < wanted_components; ++c)
                {
                    values[destination * static_cast<std::size_t>(wanted_components) + c] =
                        static_cast<float>(ReadComponent(
                            value_bytes + sparse * value_stride + c * component_size,
                            accessor.componentType, accessor.normalized));
                }
            }
        }
        return true;
    }

    bool ReadIndices(const tinygltf::Model& model, int accessor_index,
        std::vector<std::uint32_t>& values)
    {
        if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size()))
            return false;
        const auto& accessor = model.accessors[static_cast<std::size_t>(accessor_index)];
        std::size_t stride = 0, available = 0;
        const unsigned char* bytes = AccessorData(model, accessor, stride, available);
        const std::size_t component_size = ComponentSize(accessor.componentType);
        const std::size_t required = accessor.count == 0 ? 0 :
            (accessor.count - 1) * stride + component_size;
        if (!bytes || component_size == 0 || required > available) return false;
        values.resize(accessor.count);
        for (std::size_t i = 0; i < accessor.count; ++i)
            values[i] = static_cast<std::uint32_t>(ReadComponent(
                bytes + i * stride, accessor.componentType, false));
        return true;
    }

    XMMATRIX OriginalLocal(const tinygltf::Node& node)
    {
        if (node.matrix.size() == 16)
        {
            XMFLOAT4X4 value{};
            float* destination = &value._11;
            for (std::size_t i = 0; i < 16; ++i)
                destination[i] = static_cast<float>(node.matrix[i]);
            return XMLoadFloat4x4(&value);
        }
        const XMMATRIX scale = node.scale.size() == 3
            ? XMMatrixScaling(static_cast<float>(node.scale[0]),
                static_cast<float>(node.scale[1]), static_cast<float>(node.scale[2]))
            : XMMatrixIdentity();
        const XMMATRIX rotation = node.rotation.size() == 4
            ? XMMatrixRotationQuaternion(XMVectorSet(static_cast<float>(node.rotation[0]),
                static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2]),
                static_cast<float>(node.rotation[3])))
            : XMMatrixIdentity();
        const XMMATRIX translation = node.translation.size() == 3
            ? XMMatrixTranslation(static_cast<float>(node.translation[0]),
                static_cast<float>(node.translation[1]), static_cast<float>(node.translation[2]))
            : XMMatrixIdentity();
        return scale * rotation * translation;
    }

    XMMATRIX ReflectTransform(FXMMATRIX original)
    {
        const XMMATRIX reflection = XMMatrixScaling(-1.0f, 1.0f, 1.0f);
        return reflection * original * reflection;
    }

    NodePose Decompose(FXMMATRIX matrix)
    {
        NodePose pose;
        XMVECTOR scale, rotation, translation;
        if (XMMatrixDecompose(&scale, &rotation, &translation, matrix))
        {
            XMStoreFloat3(&pose.scale, scale);
            XMStoreFloat4(&pose.rotation, XMQuaternionNormalize(rotation));
            XMStoreFloat3(&pose.translation, translation);
        }
        return pose;
    }

    XMMATRIX Compose(const NodePose& pose)
    {
        return XMMatrixScaling(pose.scale.x, pose.scale.y, pose.scale.z) *
            XMMatrixRotationQuaternion(XMLoadFloat4(&pose.rotation)) *
            XMMatrixTranslation(pose.translation.x, pose.translation.y, pose.translation.z);
    }

    std::filesystem::path TextureCachePath(const std::vector<std::uint8_t>& rgba,
        int width, int height, ReplayEngine::Assets::TextureCompressor::Format format)
    {
        std::uint64_t hash = 1469598103934665603ull;
        const auto mix_byte = [&hash](unsigned char value)
        {
            hash ^= value;
            hash *= 1099511628211ull;
        };
        const auto mix_u32 = [&mix_byte](std::uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
                mix_byte(static_cast<unsigned char>(value >> (i * 8)));
        };
        mix_u32(static_cast<std::uint32_t>(width));
        mix_u32(static_cast<std::uint32_t>(height));
        mix_u32(static_cast<std::uint32_t>(format));
        for (const std::uint8_t value : rgba) mix_byte(value);
        char text[24]{};
        sprintf_s(text, "%016llx", static_cast<unsigned long long>(hash));
        return std::filesystem::path("Saved") / "Cache" / "GltfTextures" /
            (std::string(text) + ".dds");
    }

    bool ImageRgba(const tinygltf::Image& image, std::vector<std::uint8_t>& rgba)
    {
        if (image.width <= 0 || image.height <= 0 || image.image.empty() || image.component <= 0)
            return false;
        const std::size_t pixels = static_cast<std::size_t>(image.width) * image.height;
        if (image.image.size() < pixels * static_cast<std::size_t>(image.component)) return false;
        rgba.resize(pixels * 4);
        for (std::size_t i = 0; i < pixels; ++i)
        {
            const auto* source = image.image.data() + i * image.component;
            auto* destination = rgba.data() + i * 4;
            destination[0] = source[0];
            destination[1] = image.component > 1 ? source[1] : source[0];
            destination[2] = image.component > 2 ? source[2] : source[0];
            destination[3] = image.component > 3 ? source[3] : 255;
        }
        return true;
    }

    int ImageIndex(const tinygltf::Model& model, int texture_index)
    {
        if (texture_index < 0 || texture_index >= static_cast<int>(model.textures.size())) return -1;
        const int image = model.textures[static_cast<std::size_t>(texture_index)].source;
        return image >= 0 && image < static_cast<int>(model.images.size()) ? image : -1;
    }

    double ExtensionNumber(const tinygltf::Value& extension, const char* name, double fallback)
    {
        if (!extension.IsObject() || !extension.Has(name)) return fallback;
        const tinygltf::Value& value = extension.Get(name);
        return value.IsNumber() ? value.GetNumberAsDouble() : fallback;
    }

    struct UvTransform
    {
        float offset_x = 0.0f;
        float offset_y = 0.0f;
        float scale_x = 1.0f;
        float scale_y = 1.0f;
        float rotation = 0.0f;
        int texcoord = -1;
    };

    UvTransform ReadUvTransform(const tinygltf::TextureInfo& texture)
    {
        UvTransform result;
        const auto found = texture.extensions.find("KHR_texture_transform");
        if (found == texture.extensions.end() || !found->second.IsObject()) return result;
        const tinygltf::Value& extension = found->second;
        if (extension.Has("offset") && extension.Get("offset").IsArray() &&
            extension.Get("offset").ArrayLen() >= 2)
        {
            result.offset_x = static_cast<float>(extension.Get("offset").Get(0).GetNumberAsDouble());
            result.offset_y = static_cast<float>(extension.Get("offset").Get(1).GetNumberAsDouble());
        }
        if (extension.Has("scale") && extension.Get("scale").IsArray() &&
            extension.Get("scale").ArrayLen() >= 2)
        {
            result.scale_x = static_cast<float>(extension.Get("scale").Get(0).GetNumberAsDouble());
            result.scale_y = static_cast<float>(extension.Get("scale").Get(1).GetNumberAsDouble());
        }
        result.rotation = static_cast<float>(ExtensionNumber(extension, "rotation", 0.0));
        if (extension.Has("texCoord") && extension.Get("texCoord").IsNumber())
            result.texcoord = extension.Get("texCoord").GetNumberAsInt();
        return result;
    }

    int ValueTextureIndex(const tinygltf::Value& extension, const char* name)
    {
        if (!extension.IsObject() || !extension.Has(name)) return -1;
        const tinygltf::Value& texture = extension.Get(name);
        if (!texture.IsObject() || !texture.Has("index") || !texture.Get("index").IsNumber()) return -1;
        return texture.Get("index").GetNumberAsInt();
    }

    void ReadColorArray(const tinygltf::Value& extension, const char* name,
        float* destination, int count)
    {
        if (!extension.IsObject() || !extension.Has(name)) return;
        const tinygltf::Value& values = extension.Get(name);
        if (!values.IsArray() || values.ArrayLen() < static_cast<std::size_t>(count)) return;
        for (int i = 0; i < count; ++i)
            if (values.Get(static_cast<std::size_t>(i)).IsNumber())
                destination[i] = static_cast<float>(values.Get(static_cast<std::size_t>(i)).GetNumberAsDouble());
    }

    void GenerateNormals(std::vector<skinned_mesh::vertex>& vertices,
        const std::vector<std::uint32_t>& indices)
    {
        for (auto& vertex : vertices) vertex.normal = { 0.0f, 0.0f, 0.0f };
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const auto a = indices[i], b = indices[i + 1], c = indices[i + 2];
            if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size()) continue;
            const XMVECTOR normal = XMVector3Cross(
                XMLoadFloat3(&vertices[b].position) - XMLoadFloat3(&vertices[a].position),
                XMLoadFloat3(&vertices[c].position) - XMLoadFloat3(&vertices[a].position));
            for (const std::uint32_t index : { a, b, c })
                XMStoreFloat3(&vertices[index].normal,
                    XMLoadFloat3(&vertices[index].normal) + normal);
        }
        for (auto& vertex : vertices)
            XMStoreFloat3(&vertex.normal, XMVector3Normalize(XMLoadFloat3(&vertex.normal)));
    }

    std::size_t FindFrame(const std::vector<float>& timeline, float time)
    {
        if (timeline.size() < 2 || time <= timeline.front()) return 0;
        const auto next = std::upper_bound(timeline.begin(), timeline.end(), time);
        if (next == timeline.end()) return timeline.size() - 1;
        return static_cast<std::size_t>((next - timeline.begin()) - 1);
    }

    XMVECTOR SampleChannel(const std::vector<float>& timeline,
        const std::vector<float>& output, int components, const std::string& interpolation,
        float time, bool quaternion)
    {
        if (timeline.empty() || output.empty())
            return quaternion ? XMQuaternionIdentity() : XMVectorZero();
        const bool cubic = interpolation == "CUBICSPLINE";
        const std::size_t frame = FindFrame(timeline, time);
        const std::size_t next = (std::min)(frame + 1, timeline.size() - 1);
        const std::size_t value_stride = static_cast<std::size_t>(components) * (cubic ? 3 : 1);
        const auto load = [&](std::size_t key, int part)
        {
            const std::size_t offset = key * value_stride +
                static_cast<std::size_t>(cubic ? part * components : 0);
            float value[4]{ 0.0f, 0.0f, 0.0f, quaternion ? 1.0f : 0.0f };
            for (int c = 0; c < components && offset + c < output.size(); ++c)
                value[c] = output[offset + c];
            return XMVectorSet(value[0], value[1], value[2], value[3]);
        };
        if (frame == next || interpolation == "STEP") return load(frame, cubic ? 1 : 0);
        const float duration = timeline[next] - timeline[frame];
        const float factor = duration > 1.0e-7f
            ? (std::max)(0.0f, (std::min)(1.0f, (time - timeline[frame]) / duration)) : 0.0f;
        if (cubic)
        {
            const float t2 = factor * factor;
            const float t3 = t2 * factor;
            const XMVECTOR p0 = load(frame, 1);
            const XMVECTOR m0 = load(frame, 2) * duration;
            const XMVECTOR p1 = load(next, 1);
            const XMVECTOR m1 = load(next, 0) * duration;
            XMVECTOR value = p0 * (2.0f * t3 - 3.0f * t2 + 1.0f) +
                m0 * (t3 - 2.0f * t2 + factor) +
                p1 * (-2.0f * t3 + 3.0f * t2) + m1 * (t3 - t2);
            return quaternion ? XMQuaternionNormalize(value) : value;
        }
        return quaternion
            ? XMQuaternionSlerp(load(frame, 0), load(next, 0), factor)
            : XMVectorLerp(load(frame, 0), load(next, 0), factor);
    }
}

const skinned_mesh::gltf_material_info* skinned_mesh::GltfMaterial(
    uint64_t unique_id) const noexcept
{
    const auto found = gltf_materials_.find(unique_id);
    return found == gltf_materials_.end() ? nullptr : &found->second;
}

bool skinned_mesh::HasDoubleSidedMaterials() const noexcept
{
    for (const auto& entry : gltf_materials_)
        if (entry.second.double_sided) return true;
    return false;
}

bool skinned_mesh::HasAlphaMaskMaterials() const noexcept
{
    for (const auto& entry : gltf_materials_)
        if (entry.second.alpha_mode != 0) return true;
    return false;
}

bool skinned_mesh::import_gltf(const std::filesystem::path& filename, float requested_sampling_rate)
{
    tinygltf::TinyGLTF loader;
    GltfFileContext file_context{ filename.parent_path() };
    tinygltf::FsCallbacks callbacks;
    callbacks.FileExists = [](const std::string& path, void* user_data)
    {
        std::error_code error;
        return std::filesystem::is_regular_file(ResolveGltfFile(path, user_data), error);
    };
    callbacks.ExpandFilePath = [](const std::string& path, void*) { return path; };
    callbacks.ReadWholeFile = [](std::vector<unsigned char>* output, std::string* error,
        const std::string& path, void* user_data)
    {
        return output != nullptr && ReadWideFile(*output, ResolveGltfFile(path, user_data), error);
    };
    callbacks.WriteWholeFile = [](std::string* error, const std::string&,
        const std::vector<unsigned char>&, void*)
    {
        if (error) *error = "glTF import is read-only";
        return false;
    };
    callbacks.GetFileSizeInBytes = [](std::size_t* output, std::string* error,
        const std::string& path, void* user_data)
    {
        std::error_code filesystem_error;
        const auto size = std::filesystem::file_size(
            ResolveGltfFile(path, user_data), filesystem_error);
        if (filesystem_error)
        {
            if (error) *error = "file size failed";
            return false;
        }
        if (output) *output = static_cast<std::size_t>(size);
        return output != nullptr;
    };
    callbacks.user_data = &file_context;
    std::string callback_error;
    if (!loader.SetFsCallbacks(std::move(callbacks), &callback_error))
        return false;

    tinygltf::Model model;
    std::string error, warning;
    std::string extension = filename.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    std::vector<unsigned char> source_bytes;
    const bool source_read = ReadWideFile(source_bytes, filename, &error);
    const bool loaded = source_read && (extension == ".glb"
        ? loader.LoadBinaryFromMemory(&model, &error, &warning, source_bytes.data(),
            static_cast<unsigned int>(source_bytes.size()), "")
        : loader.LoadASCIIFromString(&model, &error, &warning,
            reinterpret_cast<const char*>(source_bytes.data()),
            static_cast<unsigned int>(source_bytes.size()), ""));
    if (!warning.empty()) OutputDebugStringA(("[glTF Skin] " + warning + "\n").c_str());
    if (!loaded)
    {
        OutputDebugStringA(("[glTF Skin] " + error + "\n").c_str());
        return false;
    }

    imported_gltf_ = true;
    const std::size_t node_count = model.nodes.size();
    std::vector<int> parents(node_count, -1);
    for (std::size_t i = 0; i < node_count; ++i)
        for (int child : model.nodes[i].children)
            if (child >= 0 && child < static_cast<int>(node_count)) parents[child] = static_cast<int>(i);

    std::vector<int> ordered_to_original;
    std::vector<int> original_to_ordered(node_count, -1);
    std::function<void(int)> append_node = [&](int original)
    {
        if (original < 0 || original >= static_cast<int>(node_count) ||
            original_to_ordered[original] >= 0) return;
        const int parent = parents[original];
        if (parent >= 0) append_node(parent);
        original_to_ordered[original] = static_cast<int>(ordered_to_original.size());
        ordered_to_original.push_back(original);
        for (int child : model.nodes[original].children) append_node(child);
    };
    for (std::size_t i = 0; i < node_count; ++i) if (parents[i] < 0) append_node(static_cast<int>(i));
    for (std::size_t i = 0; i < node_count; ++i) append_node(static_cast<int>(i));

    std::vector<NodePose> original_bind(node_count);
    std::vector<NodePose> reflected_bind(node_count);
    std::vector<XMFLOAT4X4> bind_globals(node_count);
    scene_view.nodes.clear();
    scene_view.nodes.reserve(node_count);
    for (std::size_t ordered = 0; ordered < ordered_to_original.size(); ++ordered)
    {
        const int original = ordered_to_original[ordered];
        original_bind[original] = Decompose(OriginalLocal(model.nodes[original]));
        reflected_bind[ordered] = Decompose(ReflectTransform(OriginalLocal(model.nodes[original])));
        scene::node scene_node;
        scene_node.unique_id = static_cast<std::uint64_t>(original) + 1;
        scene_node.name = model.nodes[original].name.empty()
            ? "Node" + std::to_string(original) : model.nodes[original].name;
        scene_node.parent_index = parents[original] < 0
            ? -1 : original_to_ordered[parents[original]];
        scene_view.nodes.push_back(std::move(scene_node));
        const XMMATRIX parent = scene_view.nodes.back().parent_index < 0
            ? XMMatrixIdentity()
            : XMLoadFloat4x4(&bind_globals[static_cast<std::size_t>(scene_view.nodes.back().parent_index)]);
        XMStoreFloat4x4(&bind_globals[ordered], Compose(reflected_bind[ordered]) * parent);
    }

    std::error_code filesystem_error;
    std::filesystem::create_directories(
        std::filesystem::path("Saved") / "Cache" / "GltfTextures", filesystem_error);
    std::map<std::pair<int, int>, std::string> cached_images;
    const auto cache_image = [&](int image_index, int usage,
        ReplayEngine::Assets::TextureCompressor::Format format) -> std::string
    {
        if (image_index < 0 || image_index >= static_cast<int>(model.images.size())) return {};
        const auto key = std::make_pair(image_index, usage);
        if (const auto found = cached_images.find(key); found != cached_images.end()) return found->second;
        std::vector<std::uint8_t> rgba;
        const auto& image = model.images[static_cast<std::size_t>(image_index)];
        if (!ImageRgba(image, rgba)) return {};
        const std::filesystem::path destination = TextureCachePath(
            rgba, image.width, image.height, format);
        if (!std::filesystem::exists(destination))
        {
            const auto result = ReplayEngine::Assets::TextureCompressor::CompressRgba(
                rgba.data(), image.width, image.height, destination, format);
            if (!result.succeeded) return {};
        }
        const std::string value = std::filesystem::absolute(destination).u8string();
        cached_images.emplace(key, value);
        return value;
    };

    materials.clear();
    gltf_materials_.clear();
    for (std::size_t i = 0; i < model.materials.size(); ++i)
    {
        const auto& source = model.materials[i];
        const auto& pbr = source.pbrMetallicRoughness;
        const std::uint64_t id = static_cast<std::uint64_t>(i) + 1;
        material value;
        value.unique_id = id;
        value.name = source.name.empty() ? "Material" + std::to_string(i) : source.name;
        value.Kd = { static_cast<float>(pbr.baseColorFactor[0]),
            static_cast<float>(pbr.baseColorFactor[1]),
            static_cast<float>(pbr.baseColorFactor[2]),
            static_cast<float>(pbr.baseColorFactor[3]) };
        gltf_material_info info;
        info.metallic = static_cast<float>(pbr.metallicFactor);
        info.roughness = static_cast<float>(pbr.roughnessFactor);
        info.occlusion = static_cast<float>(source.occlusionTexture.strength);
        info.emissive = { static_cast<float>(source.emissiveFactor[0]),
            static_cast<float>(source.emissiveFactor[1]),
            static_cast<float>(source.emissiveFactor[2]) };
        info.alpha_mode = source.alphaMode == "MASK" ? 1 : source.alphaMode == "BLEND" ? 2 : 0;
        info.alpha_cutoff = static_cast<float>(source.alphaCutoff);
        info.double_sided = source.doubleSided;
        info.normal_scale = static_cast<float>(source.normalTexture.scale);
        if (const auto found = source.extensions.find("KHR_materials_emissive_strength");
            found != source.extensions.end())
            info.emissive_strength = static_cast<float>(
                ExtensionNumber(found->second, "emissiveStrength", 1.0));
        info.unlit = source.extensions.find("KHR_materials_unlit") != source.extensions.end();
        int base_texture_index = pbr.baseColorTexture.index;
        if (const auto found = source.extensions.find("KHR_materials_pbrSpecularGlossiness");
            found != source.extensions.end())
        {
            const double glossiness = ExtensionNumber(found->second, "glossinessFactor", 1.0);
            info.roughness = static_cast<float>((std::max)(0.0, (std::min)(1.0, 1.0 - glossiness)));
            float diffuse[4]{ value.Kd.x, value.Kd.y, value.Kd.z, value.Kd.w };
            ReadColorArray(found->second, "diffuseFactor", diffuse, 4);
            value.Kd = { diffuse[0], diffuse[1], diffuse[2], diffuse[3] };
            const int extension_texture = ValueTextureIndex(found->second, "diffuseTexture");
            if (extension_texture >= 0) base_texture_index = extension_texture;
            float specular[3]{ 1.0f, 1.0f, 1.0f };
            ReadColorArray(found->second, "specularFactor", specular, 3);
            const float maximum_specular = (std::max)(specular[0], (std::max)(specular[1], specular[2]));
            info.metallic = (std::max)(0.0f, (std::min)(1.0f,
                (maximum_specular - 0.04f) / 0.96f));
        }
        value.texture_filenames[0] = cache_image(ImageIndex(model, base_texture_index), 0,
            info.alpha_mode == 0 && value.Kd.w >= 0.999f
                ? ReplayEngine::Assets::TextureCompressor::Format::BC1
                : ReplayEngine::Assets::TextureCompressor::Format::BC3);
        value.texture_filenames[1] = cache_image(ImageIndex(model, source.normalTexture.index), 1,
            ReplayEngine::Assets::TextureCompressor::Format::BC5);
        const int orm_image = ImageIndex(model, pbr.metallicRoughnessTexture.index) >= 0
            ? ImageIndex(model, pbr.metallicRoughnessTexture.index)
            : ImageIndex(model, source.occlusionTexture.index);
        value.texture_filenames[2] = cache_image(orm_image, 2,
            ReplayEngine::Assets::TextureCompressor::Format::BC1);
        value.texture_filenames[3] = cache_image(ImageIndex(model, source.emissiveTexture.index), 3,
            ReplayEngine::Assets::TextureCompressor::Format::BC1);
        materials.emplace(id, std::move(value));
        gltf_materials_.emplace(id, info);
    }
    const std::uint64_t default_material_id = static_cast<std::uint64_t>(model.materials.size()) + 1;
    if (materials.empty() || materials.find(default_material_id) == materials.end())
    {
        material fallback;
        fallback.unique_id = default_material_id;
        fallback.name = "Default glTF Material";
        fallback.Kd = { 1.0f, 1.0f, 1.0f, 1.0f };
        materials.emplace(default_material_id, fallback);
        gltf_materials_.emplace(default_material_id, gltf_material_info{});
    }

    meshes.clear();
    for (std::size_t ordered_node = 0; ordered_node < ordered_to_original.size(); ++ordered_node)
    {
        const int original_node = ordered_to_original[ordered_node];
        const auto& node = model.nodes[original_node];
        if (node.mesh < 0 || node.mesh >= static_cast<int>(model.meshes.size())) continue;
        const auto& source_mesh = model.meshes[static_cast<std::size_t>(node.mesh)];
        for (std::size_t primitive_index = 0; primitive_index < source_mesh.primitives.size(); ++primitive_index)
        {
            const auto& primitive = source_mesh.primitives[primitive_index];
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES) continue;
            const auto position_attribute = primitive.attributes.find("POSITION");
            if (position_attribute == primitive.attributes.end()) continue;
            std::vector<float> positions, normals, tangents, texcoords, joints, weights;
            std::vector<float> morph_positions, morph_normals;
            if (!ReadAccessor(model, position_attribute->second, 3, positions)) continue;
            if (const auto found = primitive.attributes.find("NORMAL"); found != primitive.attributes.end())
                ReadAccessor(model, found->second, 3, normals);
            if (const auto found = primitive.attributes.find("TANGENT"); found != primitive.attributes.end())
                ReadAccessor(model, found->second, 4, tangents);
            int texcoord_set = 0;
            UvTransform uv_transform;
            if (primitive.material >= 0 && primitive.material < static_cast<int>(model.materials.size()))
            {
                const auto& base_texture = model.materials[static_cast<std::size_t>(primitive.material)]
                    .pbrMetallicRoughness.baseColorTexture;
                texcoord_set = base_texture.texCoord;
                uv_transform = ReadUvTransform(base_texture);
                if (uv_transform.texcoord >= 0) texcoord_set = uv_transform.texcoord;
            }
            const std::string texcoord_name = "TEXCOORD_" + std::to_string(texcoord_set);
            if (const auto found = primitive.attributes.find(texcoord_name); found != primitive.attributes.end())
                ReadAccessor(model, found->second, 2, texcoords);
            if (const auto found = primitive.attributes.find("JOINTS_0"); found != primitive.attributes.end())
                ReadAccessor(model, found->second, 4, joints);
            if (const auto found = primitive.attributes.find("WEIGHTS_0"); found != primitive.attributes.end())
                ReadAccessor(model, found->second, 4, weights);
            // 現行頂点形式はtarget 0をGPUで直接処理する。追加targetは読み込みを
            // 失敗させず無視し、既存FBXの頂点・cereal形式には影響させない。
            if (!primitive.targets.empty())
            {
                const auto& target = primitive.targets.front();
                if (const auto found = target.find("POSITION"); found != target.end())
                    ReadAccessor(model, found->second, 3, morph_positions);
                if (const auto found = target.find("NORMAL"); found != target.end())
                    ReadAccessor(model, found->second, 3, morph_normals);
            }

            mesh destination;
            destination.unique_id = (static_cast<std::uint64_t>(original_node) + 1) * 65536ull + primitive_index;
            destination.name = (source_mesh.name.empty() ? "Mesh" + std::to_string(node.mesh) : source_mesh.name) +
                "_" + std::to_string(primitive_index);
            destination.node_index = static_cast<int64_t>(ordered_node);
            destination.default_global_transform = bind_globals[ordered_node];
            if (!node.weights.empty())
                destination.default_morph_weight = static_cast<float>(node.weights.front());
            else if (!source_mesh.weights.empty())
                destination.default_morph_weight = static_cast<float>(source_mesh.weights.front());
            destination.vertices.resize(positions.size() / 3);
            for (std::size_t vertex_index = 0; vertex_index < destination.vertices.size(); ++vertex_index)
            {
                auto& vertex = destination.vertices[vertex_index];
                vertex.position = { -positions[vertex_index * 3], positions[vertex_index * 3 + 1],
                    positions[vertex_index * 3 + 2] };
                if (normals.size() >= (vertex_index + 1) * 3)
                    vertex.normal = { -normals[vertex_index * 3], normals[vertex_index * 3 + 1],
                        normals[vertex_index * 3 + 2] };
                if (tangents.size() >= (vertex_index + 1) * 4)
                    vertex.tangent = { -tangents[vertex_index * 4], tangents[vertex_index * 4 + 1],
                        tangents[vertex_index * 4 + 2], -tangents[vertex_index * 4 + 3] };
                if (morph_positions.size() >= (vertex_index + 1) * 3)
                    vertex.morph_position = { -morph_positions[vertex_index * 3],
                        morph_positions[vertex_index * 3 + 1],
                        morph_positions[vertex_index * 3 + 2] };
                if (morph_normals.size() >= (vertex_index + 1) * 3)
                    vertex.morph_normal = { -morph_normals[vertex_index * 3],
                        morph_normals[vertex_index * 3 + 1],
                        morph_normals[vertex_index * 3 + 2] };
                if (texcoords.size() >= (vertex_index + 1) * 2)
                {
                    const float u = texcoords[vertex_index * 2] * uv_transform.scale_x;
                    const float v = texcoords[vertex_index * 2 + 1] * uv_transform.scale_y;
                    const float cosine = std::cos(uv_transform.rotation);
                    const float sine = std::sin(uv_transform.rotation);
                    vertex.texcoord = { uv_transform.offset_x + cosine * u - sine * v,
                        uv_transform.offset_y + sine * u + cosine * v };
                }
                float total_weight = 0.0f;
                for (int influence = 0; influence < MAX_BONE_INFLUENCES; ++influence)
                {
                    if (joints.size() >= vertex_index * 4 + influence + 1)
                        vertex.bone_indices[influence] = static_cast<std::uint32_t>(
                            (std::max)(0.0f, joints[vertex_index * 4 + influence]));
                    vertex.bone_weights[influence] = weights.size() >= vertex_index * 4 + influence + 1
                        ? (std::max)(0.0f, weights[vertex_index * 4 + influence]) : (influence == 0 ? 1.0f : 0.0f);
                    total_weight += vertex.bone_weights[influence];
                }
                if (total_weight > 1.0e-7f)
                    for (float& weight : vertex.bone_weights) weight /= total_weight;
            }
            if (!ReadIndices(model, primitive.indices, destination.indices))
            {
                destination.indices.resize(destination.vertices.size());
                std::iota(destination.indices.begin(), destination.indices.end(), 0u);
            }
            for (std::size_t i = 0; i + 2 < destination.indices.size(); i += 3)
                std::swap(destination.indices[i + 1], destination.indices[i + 2]);
            if (normals.empty()) GenerateNormals(destination.vertices, destination.indices);
            for (const auto& vertex : destination.vertices)
            {
                destination.bounding_box[0].x = (std::min)(destination.bounding_box[0].x, vertex.position.x);
                destination.bounding_box[0].y = (std::min)(destination.bounding_box[0].y, vertex.position.y);
                destination.bounding_box[0].z = (std::min)(destination.bounding_box[0].z, vertex.position.z);
                destination.bounding_box[1].x = (std::max)(destination.bounding_box[1].x, vertex.position.x);
                destination.bounding_box[1].y = (std::max)(destination.bounding_box[1].y, vertex.position.y);
                destination.bounding_box[1].z = (std::max)(destination.bounding_box[1].z, vertex.position.z);
            }
            const std::uint64_t material_id = primitive.material >= 0
                ? static_cast<std::uint64_t>(primitive.material) + 1 : default_material_id;
            mesh::subset subset;
            subset.material_unique_id = material_id;
            subset.material_name = materials[material_id].name;
            subset.index_count = static_cast<std::uint32_t>(destination.indices.size());
            destination.subsets.push_back(std::move(subset));

            if (node.skin >= 0 && node.skin < static_cast<int>(model.skins.size()))
            {
                const auto& skin = model.skins[static_cast<std::size_t>(node.skin)];
                std::vector<float> inverse_bind;
                ReadAccessor(model, skin.inverseBindMatrices, 16, inverse_bind);
                destination.bind_pose.bones.resize((std::min)(skin.joints.size(),
                    static_cast<std::size_t>(MAX_BONES)));
                for (std::size_t joint = 0; joint < destination.bind_pose.bones.size(); ++joint)
                {
                    const int original_joint = skin.joints[joint];
                    auto& bone = destination.bind_pose.bones[joint];
                    bone.unique_id = static_cast<std::uint64_t>(original_joint) + 1;
                    bone.name = original_joint >= 0 && original_joint < static_cast<int>(model.nodes.size())
                        ? model.nodes[original_joint].name : "Joint" + std::to_string(joint);
                    bone.node_index = original_joint >= 0 && original_joint < static_cast<int>(original_to_ordered.size())
                        ? original_to_ordered[original_joint] : 0;
                    const int parent = original_joint >= 0 && original_joint < static_cast<int>(parents.size())
                        ? parents[original_joint] : -1;
                    bone.parent_index = -1;
                    for (std::size_t candidate = 0; candidate < destination.bind_pose.bones.size(); ++candidate)
                        if (skin.joints[candidate] == parent) { bone.parent_index = static_cast<int64_t>(candidate); break; }
                    if (inverse_bind.size() >= (joint + 1) * 16)
                    {
                        XMFLOAT4X4 original_matrix{};
                        std::memcpy(&original_matrix, inverse_bind.data() + joint * 16, sizeof(original_matrix));
                        XMStoreFloat4x4(&bone.offset_transform,
                            ReflectTransform(XMLoadFloat4x4(&original_matrix)));
                    }
                }
            }
            meshes.push_back(std::move(destination));
        }
    }

    const float sampling_rate = requested_sampling_rate > 0.0f ? requested_sampling_rate : 30.0f;
    animation_clips.clear();
    for (std::size_t animation_index = 0; animation_index < model.animations.size(); ++animation_index)
    {
        const auto& source_animation = model.animations[animation_index];
        struct ChannelData
        {
            int node = -1;
            std::string path;
            std::string interpolation;
            std::vector<float> times;
            std::vector<float> values;
            int components = 0;
        };
        std::vector<ChannelData> channels;
        float start = (std::numeric_limits<float>::max)();
        float stop = 0.0f;
        for (const auto& source_channel : source_animation.channels)
        {
            if (source_channel.sampler < 0 ||
                source_channel.sampler >= static_cast<int>(source_animation.samplers.size())) continue;
            const auto& sampler = source_animation.samplers[static_cast<std::size_t>(source_channel.sampler)];
            ChannelData channel;
            channel.node = source_channel.target_node;
            channel.path = source_channel.target_path;
            channel.interpolation = sampler.interpolation.empty() ? "LINEAR" : sampler.interpolation;
            channel.components = channel.path == "rotation" ? 4 : channel.path == "weights" ? 1 : 3;
            if (!ReadAccessor(model, sampler.input, 1, channel.times) ||
                !ReadAccessor(model, sampler.output, channel.components, channel.values) ||
                channel.times.empty()) continue;
            start = (std::min)(start, channel.times.front());
            stop = (std::max)(stop, channel.times.back());
            channels.push_back(std::move(channel));
        }
        if (channels.empty()) continue;
        std::vector<float> default_morph_weights(node_count, 0.0f);
        for (std::size_t node_index = 0; node_index < node_count; ++node_index)
        {
            const auto& animation_node = model.nodes[node_index];
            if (!animation_node.weights.empty())
                default_morph_weights[node_index] = static_cast<float>(animation_node.weights.front());
            else if (animation_node.mesh >= 0 &&
                animation_node.mesh < static_cast<int>(model.meshes.size()) &&
                !model.meshes[static_cast<std::size_t>(animation_node.mesh)].weights.empty())
            {
                default_morph_weights[node_index] = static_cast<float>(
                    model.meshes[static_cast<std::size_t>(animation_node.mesh)].weights.front());
            }
        }
        animation clip;
        clip.name = source_animation.name.empty()
            ? "Animation" + std::to_string(animation_index) : source_animation.name;
        clip.sampling_rate = sampling_rate;
        const int frame_count = (std::max)(1, static_cast<int>(std::ceil((stop - start) * sampling_rate)));
        clip.sequence.reserve(static_cast<std::size_t>(frame_count));
        for (int frame = 0; frame < frame_count; ++frame)
        {
            const float time = start + static_cast<float>(frame) / sampling_rate;
            std::vector<NodePose> pose = original_bind;
            std::vector<float> morph_weights = default_morph_weights;
            for (const ChannelData& channel : channels)
            {
                if (channel.node < 0 || channel.node >= static_cast<int>(pose.size())) continue;
                const XMVECTOR value = SampleChannel(channel.times, channel.values, channel.components,
                    channel.interpolation, time, channel.path == "rotation");
                if (channel.path == "weights") morph_weights[channel.node] = XMVectorGetX(value);
                else if (channel.path == "scale") XMStoreFloat3(&pose[channel.node].scale, value);
                else if (channel.path == "rotation") XMStoreFloat4(&pose[channel.node].rotation, value);
                else if (channel.path == "translation") XMStoreFloat3(&pose[channel.node].translation, value);
            }
            animation::keyframe keyframe;
            keyframe.nodes.resize(node_count);
            for (std::size_t ordered = 0; ordered < ordered_to_original.size(); ++ordered)
            {
                const int original = ordered_to_original[ordered];
                const NodePose converted = Decompose(ReflectTransform(Compose(pose[original])));
                auto& destination = keyframe.nodes[ordered];
                destination.scaling = converted.scale;
                destination.rotation = converted.rotation;
                destination.translation = converted.translation;
                destination.morph_weight = morph_weights[original];
                const XMMATRIX parent = scene_view.nodes[ordered].parent_index < 0
                    ? XMMatrixIdentity()
                    : XMLoadFloat4x4(&keyframe.nodes[
                        static_cast<std::size_t>(scene_view.nodes[ordered].parent_index)].global_transform);
                XMStoreFloat4x4(&destination.global_transform, Compose(converted) * parent);
            }
            clip.sequence.push_back(std::move(keyframe));
        }
        animation_clips.push_back(std::move(clip));
    }
    return !meshes.empty();
}
