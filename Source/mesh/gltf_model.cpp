#include "gltf_model.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_IMPLEMENTATION
#include "tinygltf-release/tiny_gltf.h"

#include "shader.h"
#include "texture.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <functional>

using namespace DirectX;

namespace
{
    const unsigned char* AccessorBytes(const tinygltf::Model& model,
        const tinygltf::Accessor& accessor, size_t& stride)
    {
        if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size())) return nullptr;
        const auto& view = model.bufferViews[accessor.bufferView];
        if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size())) return nullptr;
        const auto& buffer = model.buffers[view.buffer];
        stride = accessor.ByteStride(view);
        const size_t offset = view.byteOffset + accessor.byteOffset;
        return offset < buffer.data.size() ? buffer.data.data() + offset : nullptr;
    }

    bool ReadFloatVector(const tinygltf::Model& model, int accessor_index,
        int component_count, std::vector<float>& output)
    {
        if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size())) return false;
        const auto& accessor = model.accessors[accessor_index];
        if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) return false;
        size_t stride = 0;
        const unsigned char* bytes = AccessorBytes(model, accessor, stride);
        if (!bytes || stride < sizeof(float) * component_count) return false;
        output.resize(accessor.count * component_count);
        for (size_t i = 0; i < accessor.count; ++i)
        {
            const float* value = reinterpret_cast<const float*>(bytes + i * stride);
            for (int c = 0; c < component_count; ++c) output[i * component_count + c] = value[c];
        }
        return true;
    }

    bool ReadIndices(const tinygltf::Model& model, int accessor_index, std::vector<uint32_t>& output)
    {
        if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size())) return false;
        const auto& accessor = model.accessors[accessor_index];
        size_t stride = 0;
        const unsigned char* bytes = AccessorBytes(model, accessor, stride);
        if (!bytes) return false;
        output.resize(accessor.count);
        for (size_t i = 0; i < accessor.count; ++i)
        {
            const unsigned char* value = bytes + i * stride;
            switch (accessor.componentType)
            {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: output[i] = *value; break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: output[i] = *reinterpret_cast<const uint16_t*>(value); break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: output[i] = *reinterpret_cast<const uint32_t*>(value); break;
            default: return false;
            }
        }
        return true;
    }

    XMMATRIX NodeLocal(const tinygltf::Node& node)
    {
        if (node.matrix.size() == 16)
        {
            XMFLOAT4X4 source{};
            float* dst = &source._11;
            for (size_t i = 0; i < 16; ++i) dst[i] = static_cast<float>(node.matrix[i]);
            return XMMatrixTranspose(XMLoadFloat4x4(&source));
        }
        const XMMATRIX scale = node.scale.size() == 3
            ? XMMatrixScaling(static_cast<float>(node.scale[0]), static_cast<float>(node.scale[1]), static_cast<float>(node.scale[2]))
            : XMMatrixIdentity();
        const XMMATRIX rotation = node.rotation.size() == 4
            ? XMMatrixRotationQuaternion(XMVectorSet(static_cast<float>(node.rotation[0]), static_cast<float>(node.rotation[1]),
                static_cast<float>(node.rotation[2]), static_cast<float>(node.rotation[3])))
            : XMMatrixIdentity();
        const XMMATRIX translation = node.translation.size() == 3
            ? XMMatrixTranslation(static_cast<float>(node.translation[0]), static_cast<float>(node.translation[1]), static_cast<float>(node.translation[2]))
            : XMMatrixIdentity();
        return scale * rotation * translation;
    }

    template<class VertexT>
    void GenerateNormals(std::vector<VertexT>& vertices, const std::vector<uint32_t>& indices)
    {
        for (auto& vertex : vertices) vertex.normal = { 0, 0, 0 };
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
            if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size()) continue;
            const XMVECTOR p0 = XMLoadFloat3(&vertices[a].position);
            const XMVECTOR p1 = XMLoadFloat3(&vertices[b].position);
            const XMVECTOR p2 = XMLoadFloat3(&vertices[c].position);
            const XMVECTOR normal = XMVector3Cross(p1 - p0, p2 - p0);
            for (uint32_t index : { a, b, c })
            {
                XMVECTOR sum = XMLoadFloat3(&vertices[index].normal) + normal;
                XMStoreFloat3(&vertices[index].normal, sum);
            }
        }
        for (auto& vertex : vertices)
            XMStoreFloat3(&vertex.normal, XMVector3Normalize(XMLoadFloat3(&vertex.normal)));
    }
}

gltf_model::gltf_model(ID3D11Device* device, const std::string& filename)
{
    loaded_ = Load(device, filename);
}

bool gltf_model::Load(ID3D11Device* device, const std::string& filename)
{
    if (!device) { error_ = "Direct3D device is null"; return false; }
    std::string extension = std::filesystem::path(filename).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".gltf" && extension != ".glb")
    {
        error_ = "Unsupported model extension: " + extension;
        return false;
    }

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string warning;
    const bool parsed = extension == ".glb"
        ? loader.LoadBinaryFromFile(&model, &error_, &warning, filename)
        : loader.LoadASCIIFromFile(&model, &error_, &warning, filename);
    if (!warning.empty()) OutputDebugStringA(("[glTF] " + warning + "\n").c_str());
    if (!parsed) return false;
    has_skins_ = !model.skins.empty();
    has_animations_ = !model.animations.empty();

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, texcoord), D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    create_vs_from_cso(device, "static_mesh_vs.cso", vertex_shader_.GetAddressOf(),
        input_layout_.GetAddressOf(), layout, _countof(layout));
    create_ps_from_cso(device, "static_mesh_ps.cso", pixel_shader_.GetAddressOf());
    D3D11_BUFFER_DESC constant_desc{};
    constant_desc.ByteWidth = sizeof(Constants);
    constant_desc.Usage = D3D11_USAGE_DEFAULT;
    constant_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&constant_desc, nullptr, constant_buffer_.GetAddressOf()))) return false;
    make_dummy_texture(device, white_texture_.GetAddressOf(), 0xffffffff, 4);

    std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> image_views(model.images.size());
    for (size_t i = 0; i < model.images.size(); ++i)
    {
        const auto& image = model.images[i];
        if (image.image.empty() || image.width <= 0 || image.height <= 0) continue;
        std::vector<uint8_t> rgba(static_cast<size_t>(image.width) * image.height * 4);
        const int components = image.component > 0 ? image.component : 4;
        for (int p = 0; p < image.width * image.height; ++p)
        {
            rgba[p * 4 + 0] = image.image[p * components + 0];
            rgba[p * 4 + 1] = components > 1 ? image.image[p * components + 1] : rgba[p * 4 + 0];
            rgba[p * 4 + 2] = components > 2 ? image.image[p * components + 2] : rgba[p * 4 + 0];
            rgba[p * 4 + 3] = components > 3 ? image.image[p * components + 3] : 255;
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(image.width); desc.Height = static_cast<UINT>(image.height);
        desc.MipLevels = 1; desc.ArraySize = 1; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{ rgba.data(), static_cast<UINT>(image.width * 4), 0 };
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (SUCCEEDED(device->CreateTexture2D(&desc, &initial, texture.GetAddressOf())))
            device->CreateShaderResourceView(texture.Get(), nullptr, image_views[i].GetAddressOf());
    }

    materials_.resize((std::max)(size_t{ 1 }, model.materials.size()));
    for (size_t i = 0; i < model.materials.size(); ++i)
    {
        const auto& source = model.materials[i].pbrMetallicRoughness;
        if (source.baseColorFactor.size() == 4)
            materials_[i].base_color = { static_cast<float>(source.baseColorFactor[0]), static_cast<float>(source.baseColorFactor[1]),
                static_cast<float>(source.baseColorFactor[2]), static_cast<float>(source.baseColorFactor[3]) };
        const int texture_index = source.baseColorTexture.index;
        if (texture_index >= 0 && texture_index < static_cast<int>(model.textures.size()))
        {
            const int image_index = model.textures[texture_index].source;
            if (image_index >= 0 && image_index < static_cast<int>(image_views.size()))
                materials_[i].base_color_texture = image_views[image_index];
        }
    }

    std::vector<XMMATRIX> globals(model.nodes.size(), XMMatrixIdentity());
    std::vector<bool> visited(model.nodes.size(), false);
    std::function<void(int, XMMATRIX)> visit = [&](int index, XMMATRIX parent)
    {
        if (index < 0 || index >= static_cast<int>(model.nodes.size())) return;
        globals[index] = NodeLocal(model.nodes[index]) * parent;
        visited[index] = true;
        for (int child : model.nodes[index].children) visit(child, globals[index]);
    };
    const int scene_index = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (scene_index >= 0 && scene_index < static_cast<int>(model.scenes.size()))
        for (int root : model.scenes[scene_index].nodes) visit(root, XMMatrixIdentity());
    for (size_t i = 0; i < model.nodes.size(); ++i) if (!visited[i]) visit(static_cast<int>(i), XMMatrixIdentity());

    for (size_t node_index = 0; node_index < model.nodes.size(); ++node_index)
    {
        const auto& node = model.nodes[node_index];
        if (node.mesh < 0 || node.mesh >= static_cast<int>(model.meshes.size())) continue;
        for (const auto& source : model.meshes[node.mesh].primitives)
        {
            if (source.mode != TINYGLTF_MODE_TRIANGLES) continue;
            const auto position_it = source.attributes.find("POSITION");
            if (position_it == source.attributes.end()) continue;
            std::vector<float> positions, normals, texcoords;
            if (!ReadFloatVector(model, position_it->second, 3, positions)) continue;
            const auto normal_it = source.attributes.find("NORMAL");
            const auto texcoord_it = source.attributes.find("TEXCOORD_0");
            const bool has_normals = normal_it != source.attributes.end() && ReadFloatVector(model, normal_it->second, 3, normals);
            if (texcoord_it != source.attributes.end()) ReadFloatVector(model, texcoord_it->second, 2, texcoords);
            std::vector<Vertex> vertices(positions.size() / 3);
            for (size_t i = 0; i < vertices.size(); ++i)
            {
                vertices[i].position = { -positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2] };
                if (has_normals) vertices[i].normal = { -normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2] };
                if (texcoords.size() >= (i + 1) * 2) vertices[i].texcoord = { texcoords[i * 2], texcoords[i * 2 + 1] };
            }
            std::vector<uint32_t> indices;
            if (!ReadIndices(model, source.indices, indices))
            {
                indices.resize(vertices.size());
                for (size_t i = 0; i < indices.size(); ++i) indices[i] = static_cast<uint32_t>(i);
            }
            for (size_t i = 0; i + 2 < indices.size(); i += 3) std::swap(indices[i + 1], indices[i + 2]);
            if (!has_normals) GenerateNormals(vertices, indices);

            Primitive primitive{};
        // glTFの右手座標系を取り込むとき、頂点をX軸で反転する。
        // 移動と回転の整合性を保つため、ノード変換も同じ軸で反転する。
            const XMMATRIX reflection = XMMatrixScaling(-1.0f, 1.0f, 1.0f);
            XMStoreFloat4x4(&primitive.node_transform,
                reflection * globals[node_index] * reflection);
            primitive.index_count = static_cast<uint32_t>(indices.size());
            primitive.material = source.material;

            const XMMATRIX node_transform = XMLoadFloat4x4(&primitive.node_transform);
            for (size_t index = 0; index + 2 < indices.size(); index += 3)
            {
                const uint32_t triangle_indices[3]
                {
                    indices[index], indices[index + 1], indices[index + 2]
                };
                if (triangle_indices[0] >= vertices.size() ||
                    triangle_indices[1] >= vertices.size() ||
                    triangle_indices[2] >= vertices.size()) continue;
                ReplayEngine::Physics::Triangle triangle{};
                for (int vertex_index = 0; vertex_index < 3; ++vertex_index)
                {
                    XMStoreFloat3(&triangle.vertices[vertex_index], XMVector3TransformCoord(
                        XMLoadFloat3(&vertices[triangle_indices[vertex_index]].position),
                        node_transform));
                }
                collision_triangles_.push_back(triangle);
            }
            D3D11_BUFFER_DESC desc{};
            desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            desc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(Vertex));
            D3D11_SUBRESOURCE_DATA initial{ vertices.data(), 0, 0 };
            if (FAILED(device->CreateBuffer(&desc, &initial, primitive.vertex_buffer.GetAddressOf()))) continue;
            desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
            desc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint32_t));
            initial.pSysMem = indices.data();
            if (FAILED(device->CreateBuffer(&desc, &initial, primitive.index_buffer.GetAddressOf()))) continue;
            primitives_.push_back(std::move(primitive));
        }
    }
    if (primitives_.empty()) { error_ = "glTF contains no supported triangle primitives"; return false; }
    return vertex_shader_ && pixel_shader_ && input_layout_ && constant_buffer_;
}

void gltf_model::render(ID3D11DeviceContext* context, const XMFLOAT4X4& world,
    const XMFLOAT4& tint, ID3D11PixelShader* alternative_pixel_shader)
{
    if (!loaded_ || !context) return;
    context->IASetInputLayout(input_layout_.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex_shader_.Get(), nullptr, 0);
    context->PSSetShader(alternative_pixel_shader ? alternative_pixel_shader : pixel_shader_.Get(), nullptr, 0);
    for (const auto& primitive : primitives_)
    {
        const Material* material = primitive.material >= 0 && primitive.material < static_cast<int>(materials_.size())
            ? &materials_[primitive.material] : &materials_[0];
        UINT stride = sizeof(Vertex), offset = 0;
        context->IASetVertexBuffers(0, 1, primitive.vertex_buffer.GetAddressOf(), &stride, &offset);
        context->IASetIndexBuffer(primitive.index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        ID3D11ShaderResourceView* texture = material->base_color_texture
            ? material->base_color_texture.Get() : white_texture_.Get();
        context->PSSetShaderResources(0, 1, &texture);
        Constants constants{};
        XMStoreFloat4x4(&constants.world,
            XMLoadFloat4x4(&primitive.node_transform) * XMLoadFloat4x4(&world));
        XMStoreFloat4(&constants.material_color,
            XMLoadFloat4(&tint) * XMLoadFloat4(&material->base_color));
        context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr, &constants, 0, 0);
        context->VSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
        context->PSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
        context->DrawIndexed(primitive.index_count, 0, 0);
    }
}
