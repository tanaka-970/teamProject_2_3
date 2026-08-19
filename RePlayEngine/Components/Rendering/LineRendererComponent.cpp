#include "LineRendererComponent.h"

#include "../../Assets/AssetDatabase.h"
#include "../../Rendering/RenderStats.h"
#include "../../Reflection/Property/PropertyBag.h"
#include "../../Reflection/Property/PropertyValue.h"
#include "../../../Source/core/shader.h"
#include "../../../Source/core/texture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>

namespace
{
    using DirectX::XMFLOAT3;
    using DirectX::XMFLOAT4;
    using DirectX::XMVECTOR;

    float Distance(const XMFLOAT3& a, const XMFLOAT3& b) noexcept
    {
        const float x = b.x - a.x;
        const float y = b.y - a.y;
        const float z = b.z - a.z;
        return std::sqrt(x * x + y * y + z * z);
    }

    XMFLOAT3 Lerp3(const XMFLOAT3& a, const XMFLOAT3& b, float t) noexcept
    {
        return { a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t };
    }

    XMFLOAT4 Lerp4(const XMFLOAT4& a, const XMFLOAT4& b, float t) noexcept
    {
        return { a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t };
    }

    XMFLOAT3 NormalizeOr(const XMFLOAT3& value, const XMFLOAT3& fallback) noexcept
    {
        const XMVECTOR vector = DirectX::XMLoadFloat3(&value);
        if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(vector)) <= 0.00000001f)
            return fallback;
        XMFLOAT3 result{};
        DirectX::XMStoreFloat3(&result, DirectX::XMVector3Normalize(vector));
        return result;
    }

    XMFLOAT3 CatmullRom(const XMFLOAT3& p0, const XMFLOAT3& p1,
        const XMFLOAT3& p2, const XMFLOAT3& p3, float t) noexcept
    {
        const float t2 = t * t;
        const float t3 = t2 * t;
        return {
            0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t +
                (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
                (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3),
            0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t +
                (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
                (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3),
            0.5f * ((2.0f * p1.z) + (-p0.z + p2.z) * t +
                (2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2 +
                (-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * t3)
        };
    }

    void SetDebugName(ID3D11DeviceChild* object, const char* name)
    {
        if (object == nullptr || name == nullptr) return;
        object->SetPrivateData(WKPDID_D3DDebugObjectName,
            static_cast<UINT>(std::strlen(name)), name);
    }

    std::string PointPropertyName(int index)
    {
        char buffer[48]{};
        std::snprintf(buffer, sizeof(buffer), "points[%d]", index);
        return std::string(buffer);
    }

    bool ParsePointPropertyName(const std::string& name, int& index)
    {
        constexpr const char* prefix = "points[";
        if (name.compare(0, 7, prefix) != 0 || name.empty() || name.back() != ']')
            return false;
        if (name.size() == 8) return false;
        int parsed = 0;
        for (std::size_t character = 7; character + 1 < name.size(); ++character)
        {
            if (name[character] < '0' || name[character] > '9') return false;
            const int digit = name[character] - '0';
            if (parsed > ((std::numeric_limits<int>::max)() - 1 - digit) / 10)
                return false;
            parsed = parsed * 10 + digit;
        }
        index = parsed;
        return true;
    }

    ReplayEngine::Reflection::PropertyDesc MakePointProperty(int index)
    {
        using namespace ReplayEngine;
        Reflection::PropertyDesc desc;
        desc.name = PointPropertyName(index);
        desc.display_name = "点 " + std::to_string(index);
        desc.tooltip = "ラインを通すローカル座標。Motion から 1 点ずつ動かせる。";
        desc.type = Reflection::PropertyType::Vector3;
        desc.animatable = Reflection::Animatable::Interpolatable;
        desc.serializable = true;
        desc.getter = [index](const Core::Component& component)
        {
            if (component.TypeID() != Components::LineRendererComponent::StaticTypeID())
                return Reflection::PropertyValue{};
            const auto& line = static_cast<const Components::LineRendererComponent&>(component);
            if (index < 0 || static_cast<std::size_t>(index) >= line.points.size())
                return Reflection::PropertyValue{};
            return Reflection::PropertyValue::MakeVector3(
                line.points[static_cast<std::size_t>(index)]);
        };
        desc.setter = [index](Core::Component& component,
            const Reflection::PropertyValue& value)
        {
            if (component.TypeID() != Components::LineRendererComponent::StaticTypeID())
                return;
            auto& line = static_cast<Components::LineRendererComponent&>(component);
            if (index < 0 || static_cast<std::size_t>(index) >= line.points.size())
                return;
            line.points[static_cast<std::size_t>(index)] = value.AsVector3();
        };
        return desc;
    }
}

namespace ReplayEngine::Rendering
{
    std::vector<DirectX::XMFLOAT3> BuildCatmullRomLinePath(
        const std::vector<DirectX::XMFLOAT3>& control_points,
        int smoothing, bool closed)
    {
        if (control_points.size() < 2 || smoothing <= 0) return control_points;

        const int subdivisions = smoothing >= 255 ? 256 : smoothing + 1;
        const std::size_t segment_count = closed
            ? control_points.size() : control_points.size() - 1;
        if (segment_count > ((std::numeric_limits<std::size_t>::max)() - 1) /
            static_cast<std::size_t>(subdivisions))
        {
            return {};
        }

        std::vector<DirectX::XMFLOAT3> result;
        try
        {
            result.reserve(segment_count * static_cast<std::size_t>(subdivisions) + 1);
            for (std::size_t segment = 0; segment < segment_count; ++segment)
            {
                const std::size_t p1_index = segment;
                const std::size_t p2_index = (segment + 1) % control_points.size();
                const std::size_t p0_index = segment > 0 ? segment - 1
                    : (closed ? control_points.size() - 1 : p1_index);
                const std::size_t p3_index = segment + 2 < control_points.size()
                    ? segment + 2 : (closed ? (segment + 2) % control_points.size()
                        : p2_index);

                // Catmull-Rom は制御点を必ず通るため、手置きパスの位置と
                // 描画線がずれない。開いた端では端点を複製して全区間を残す。
                for (int division = 0; division < subdivisions; ++division)
                {
                    const float t = static_cast<float>(division) /
                        static_cast<float>(subdivisions);
                    result.push_back(CatmullRom(control_points[p0_index],
                        control_points[p1_index], control_points[p2_index],
                        control_points[p3_index], t));
                }
            }
            if (!closed) result.push_back(control_points.back());
        }
        catch (...)
        {
            result.clear();
        }
        return result;
    }

    bool LineStrokeRenderer::Initialize(ID3D11Device* device)
    {
        if (device == nullptr) return false;
        if (device_.Get() == device && vertex_shader_ && pixel_shader_ && input_layout_ &&
            white_texture_)
        {
            return true;
        }

        Release();
        device_ = device;
        D3D11_INPUT_ELEMENT_DESC input_elements[]{
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
                D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
                D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (FAILED(create_vs_from_cso(device, "line_stroke_vs.cso",
            vertex_shader_.GetAddressOf(), input_layout_.GetAddressOf(),
            input_elements, static_cast<UINT>(
                sizeof(input_elements) / sizeof(input_elements[0])))))
        {
            Release();
            return false;
        }
        if (FAILED(create_ps_from_cso(device, "line_stroke_ps.cso",
            pixel_shader_.GetAddressOf())))
        {
            Release();
            return false;
        }
        if (FAILED(make_dummy_texture(device, white_texture_.GetAddressOf(),
            0xFFFFFFFFu, 1)))
        {
            Release();
            return false;
        }
        SetDebugName(vertex_shader_.Get(), "LineStrokeRenderer.VS");
        SetDebugName(pixel_shader_.Get(), "LineStrokeRenderer.PS");
        SetDebugName(input_layout_.Get(), "LineStrokeRenderer.InputLayout");
        return true;
    }

    void LineStrokeRenderer::Release() noexcept
    {
        texture_cache_.clear();
        vertices_.clear();
        vertex_capacity_ = 0;
        white_texture_.Reset();
        vertex_buffer_.Reset();
        input_layout_.Reset();
        pixel_shader_.Reset();
        vertex_shader_.Reset();
        device_.Reset();
    }

    bool LineStrokeRenderer::EnsureVertexCapacity(ID3D11Device* device,
        std::size_t vertex_count)
    {
        if (vertex_count <= vertex_capacity_) return true;
        if (vertex_count > (std::numeric_limits<UINT>::max)() / sizeof(Vertex))
            return false;

        std::size_t next_capacity = (std::max)(std::size_t{ 6 }, vertex_capacity_);
        while (next_capacity < vertex_count)
        {
            if (next_capacity > (std::numeric_limits<UINT>::max)() /
                (sizeof(Vertex) * 2))
            {
                next_capacity = vertex_count;
                break;
            }
            next_capacity *= 2;
        }

        // GetAddressOf は既存ポインタを解放しないため、伸長前に必ず Reset する。
        vertex_buffer_.Reset();
        vertex_capacity_ = 0;
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * next_capacity);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&desc, nullptr,
            vertex_buffer_.GetAddressOf())))
        {
            return false;
        }
        SetDebugName(vertex_buffer_.Get(), "LineStrokeRenderer.VertexBuffer");
        vertex_capacity_ = next_capacity;
        return true;
    }

    ID3D11ShaderResourceView* LineStrokeRenderer::TextureFor(
        const std::string& guid, const Assets::AssetDatabase* asset_database)
    {
        if (guid.empty() || asset_database == nullptr) return white_texture_.Get();
        const auto cached = texture_cache_.find(guid);
        if (cached != texture_cache_.end()) return cached->second.Get();

        const Assets::AssetRecord* record = asset_database->FindByGuid(guid);
        if (record == nullptr || record->kind != Assets::AssetKind::Image)
            return white_texture_.Get();
        const std::filesystem::path path = record->cache_path.empty()
            ? record->source_path : record->cache_path;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> loaded;
        if (FAILED(load_texture_from_file(device_.Get(), path.wstring().c_str(),
            loaded.GetAddressOf(), nullptr)) || !loaded)
        {
            return white_texture_.Get();
        }
        try
        {
            const auto inserted = texture_cache_.emplace(guid, loaded);
            return inserted.first->second.Get();
        }
        catch (...)
        {
            return white_texture_.Get();
        }
    }

    bool LineStrokeRenderer::Draw(ID3D11Device* device,
        ID3D11DeviceContext* context, const Assets::AssetDatabase* asset_database,
        ID3D11SamplerState* sampler,
        const std::vector<DirectX::XMFLOAT3>& points,
        const std::vector<float>& point_alpha,
        const LineStrokeStyle& style,
        const DirectX::XMFLOAT3& camera_position)
    {
        if (device == nullptr || context == nullptr || points.size() < 2)
            return false;
        if (!Initialize(device)) return false;

        const std::size_t segment_count = style.closed ? points.size() : points.size() - 1;
        if (segment_count == 0 || segment_count >
            (std::numeric_limits<std::size_t>::max)() / 6)
        {
            return false;
        }

        std::vector<float> distances;
        std::vector<DirectX::XMFLOAT3> right_vectors;
        try
        {
            distances.reserve(segment_count + 1);
            distances.push_back(0.0f);
            for (std::size_t segment = 0; segment < segment_count; ++segment)
            {
                distances.push_back(distances.back() + Distance(points[segment],
                    points[(segment + 1) % points.size()]));
            }
            if (distances.back() <= 0.0001f || !std::isfinite(distances.back()))
                return false;

            right_vectors.resize(points.size());
            for (std::size_t point = 0; point < points.size(); ++point)
            {
                const std::size_t previous = point > 0 ? point - 1
                    : (style.closed ? points.size() - 1 : point);
                const std::size_t next = point + 1 < points.size() ? point + 1
                    : (style.closed ? 0 : point);
                const XMFLOAT3 tangent = NormalizeOr({
                    points[next].x - points[previous].x,
                    points[next].y - points[previous].y,
                    points[next].z - points[previous].z }, { 1.0f, 0.0f, 0.0f });
                const XMFLOAT3 facing = style.billboard
                    ? XMFLOAT3{ camera_position.x - points[point].x,
                        camera_position.y - points[point].y,
                        camera_position.z - points[point].z }
                    : XMFLOAT3{ 0.0f, 1.0f, 0.0f };
                XMVECTOR right = DirectX::XMVector3Cross(
                    DirectX::XMLoadFloat3(&tangent), DirectX::XMLoadFloat3(&facing));
                if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(right)) <=
                    0.00000001f)
                {
                    const XMFLOAT3 fallback_axis{ 0.0f, 1.0f, 0.0f };
                    right = DirectX::XMVector3Cross(DirectX::XMLoadFloat3(&tangent),
                        DirectX::XMLoadFloat3(&fallback_axis));
                }
                if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(right)) <=
                    0.00000001f)
                {
                    const XMFLOAT3 fallback_axis{ 1.0f, 0.0f, 0.0f };
                    right = DirectX::XMVector3Cross(DirectX::XMLoadFloat3(&tangent),
                        DirectX::XMLoadFloat3(&fallback_axis));
                }
                DirectX::XMStoreFloat3(&right_vectors[point],
                    DirectX::XMVector3Normalize(right));
            }

            vertices_.clear();
            vertices_.reserve(segment_count * 6);
        }
        catch (...)
        {
            return false;
        }

        const float total_length = distances.back();
        const float base_start = (std::max)(0.0f, (std::min)(1.0f, style.trim_start));
        const float base_end = (std::max)(0.0f, (std::min)(1.0f, style.trim_end));
        const float span = (std::max)(0.0f, base_end - base_start);
        if (span <= 0.0f) return false;

        float interval_start[2]{};
        float interval_end[2]{};
        int interval_count = 0;
        if (span >= 0.9999f)
        {
            interval_start[0] = 0.0f;
            interval_end[0] = total_length;
            interval_count = 1;
        }
        else
        {
            float start = std::fmod(base_start + style.trim_offset, 1.0f);
            if (start < 0.0f) start += 1.0f;
            const float end = start + span;
            interval_start[interval_count] = start * total_length;
            interval_end[interval_count] = (std::min)(end, 1.0f) * total_length;
            ++interval_count;
            if (end > 1.0f)
            {
                interval_start[interval_count] = 0.0f;
                interval_end[interval_count] = (end - 1.0f) * total_length;
                ++interval_count;
            }
        }

        const auto alpha_at = [&](std::size_t index) noexcept
        {
            return index < point_alpha.size()
                ? (std::max)(0.0f, (std::min)(1.0f, point_alpha[index])) : 1.0f;
        };
        const auto append_vertex = [&](const XMFLOAT3& position,
            const XMFLOAT4& color, float u, float v)
        {
            vertices_.push_back({ position, color, { u, v } });
        };

        try
        {
            for (std::size_t segment = 0; segment < segment_count; ++segment)
            {
                const float segment_start = distances[segment];
                const float segment_end = distances[segment + 1];
                if (segment_end <= segment_start) continue;
                const std::size_t next = (segment + 1) % points.size();
                for (int interval = 0; interval < interval_count; ++interval)
                {
                    const float clipped_start = (std::max)(segment_start,
                        interval_start[interval]);
                    const float clipped_end = (std::min)(segment_end,
                        interval_end[interval]);
                    if (clipped_end <= clipped_start) continue;

                    const float ta = (clipped_start - segment_start) /
                        (segment_end - segment_start);
                    const float tb = (clipped_end - segment_start) /
                        (segment_end - segment_start);
                    const XMFLOAT3 a = Lerp3(points[segment], points[next], ta);
                    const XMFLOAT3 b = Lerp3(points[segment], points[next], tb);
                    const XMFLOAT3 right_a = NormalizeOr(Lerp3(right_vectors[segment],
                        right_vectors[next], ta), right_vectors[segment]);
                    const XMFLOAT3 right_b = NormalizeOr(Lerp3(right_vectors[segment],
                        right_vectors[next], tb), right_vectors[next]);
                    const float normalized_a = clipped_start / total_length;
                    const float normalized_b = clipped_end / total_length;
                    const float width_a = (std::max)(0.0f, style.width_start +
                        (style.width_end - style.width_start) * normalized_a);
                    const float width_b = (std::max)(0.0f, style.width_start +
                        (style.width_end - style.width_start) * normalized_b);
                    XMFLOAT4 color_a = style.fill_mode == 1
                        ? Lerp4(style.fill_color, style.fill_color_2, normalized_a)
                        : style.fill_color;
                    XMFLOAT4 color_b = style.fill_mode == 1
                        ? Lerp4(style.fill_color, style.fill_color_2, normalized_b)
                        : style.fill_color;
                    color_a.w *= alpha_at(segment) +
                        (alpha_at(next) - alpha_at(segment)) * ta;
                    color_b.w *= alpha_at(segment) +
                        (alpha_at(next) - alpha_at(segment)) * tb;
                    const float half_a = width_a * 0.5f;
                    const float half_b = width_b * 0.5f;
                    const XMFLOAT3 a0{ a.x - right_a.x * half_a,
                        a.y - right_a.y * half_a, a.z - right_a.z * half_a };
                    const XMFLOAT3 a1{ a.x + right_a.x * half_a,
                        a.y + right_a.y * half_a, a.z + right_a.z * half_a };
                    const XMFLOAT3 b0{ b.x - right_b.x * half_b,
                        b.y - right_b.y * half_b, b.z - right_b.z * half_b };
                    const XMFLOAT3 b1{ b.x + right_b.x * half_b,
                        b.y + right_b.y * half_b, b.z + right_b.z * half_b };
                    const float tiling_length = (std::max)(0.0001f, style.uv_tiling);
                    const float u0 = (style.uv_mode == 1
                        ? clipped_start / tiling_length : normalized_a) + style.uv_scroll;
                    const float u1 = (style.uv_mode == 1
                        ? clipped_end / tiling_length : normalized_b) + style.uv_scroll;
                    append_vertex(a0, color_a, u0, 0.0f);
                    append_vertex(a1, color_a, u0, 1.0f);
                    append_vertex(b1, color_b, u1, 1.0f);
                    append_vertex(a0, color_a, u0, 0.0f);
                    append_vertex(b1, color_b, u1, 1.0f);
                    append_vertex(b0, color_b, u1, 0.0f);
                }
            }
        }
        catch (...)
        {
            vertices_.clear();
            return false;
        }

        if (vertices_.empty() || !EnsureVertexCapacity(device, vertices_.size()))
            return false;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(vertex_buffer_.Get(), 0,
            D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            return false;
        }
        std::memcpy(mapped.pData, vertices_.data(), vertices_.size() * sizeof(Vertex));
        context->Unmap(vertex_buffer_.Get(), 0);

        const UINT stride = sizeof(Vertex);
        const UINT offset = 0;
        ID3D11ShaderResourceView* texture = TextureFor(style.texture_guid,
            asset_database);
        context->IASetVertexBuffers(0, 1, vertex_buffer_.GetAddressOf(),
            &stride, &offset);
        context->IASetInputLayout(input_layout_.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
        context->VSSetShader(vertex_shader_.Get(), nullptr, 0);
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
        context->PSSetShader(pixel_shader_.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &texture);
        context->PSSetSamplers(0, 1, &sampler);
        ReplayEngine::Rendering::Stats().CountDraw(
            static_cast<std::uint32_t>(vertices_.size()));
        context->Draw(static_cast<UINT>(vertices_.size()), 0);
        ID3D11ShaderResourceView* null_texture = nullptr;
        context->PSSetShaderResources(0, 1, &null_texture);
        return true;
    }
}

namespace ReplayEngine::Components
{
    LineRendererComponent::LineRendererComponent()
    {
        ResizePoints();
        RebuildDynamicProperties();
    }

    const std::vector<Reflection::PropertyDesc>*
        LineRendererComponent::DynamicProperties() const noexcept
    {
        return dynamic_properties_.empty() ? nullptr : &dynamic_properties_;
    }

    void LineRendererComponent::OnSerialize(Reflection::PropertyBag& output) const
    {
        output.Set("point_count", Reflection::PropertyValue::MakeInt(
            static_cast<int>(points.size())));
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            output.Set(PointPropertyName(static_cast<int>(index)),
                Reflection::PropertyValue::MakeVector3(points[index]));
        }
    }

    void LineRendererComponent::OnDeserialize(const Reflection::PropertyBag& input)
    {
        int inferred_count = point_count;
        if (const Reflection::PropertyValue* stored = input.Find("point_count"))
            inferred_count = stored->AsInt(inferred_count);
        for (const Reflection::PropertyBag::Entry& entry : input.Entries())
        {
            int index = 0;
            if (ParsePointPropertyName(entry.name, index))
                inferred_count = (std::max)(inferred_count, index + 1);
        }
        point_count = inferred_count;
        ResizePoints();
        RebuildDynamicProperties();
        for (const Reflection::PropertyBag::Entry& entry : input.Entries())
        {
            int index = 0;
            if (!ParsePointPropertyName(entry.name, index) || index < 0 ||
                static_cast<std::size_t>(index) >= points.size())
            {
                continue;
            }
            points[static_cast<std::size_t>(index)] = entry.value.AsVector3();
        }
    }

    void LineRendererComponent::OnPropertyChanged(const char* property_name)
    {
        if (property_name == nullptr || std::string(property_name) == "point_count")
        {
            ResizePoints();
            RebuildDynamicProperties();
        }
    }

    Rendering::LineStrokeStyle LineRendererComponent::StrokeStyle() const
    {
        Rendering::LineStrokeStyle style;
        style.width_start = width_start;
        style.width_end = width_end;
        style.billboard = billboard;
        style.uv_mode = uv_mode;
        style.uv_tiling = uv_tiling;
        style.uv_scroll = uv_scroll;
        style.texture_guid = texture.guid;
        style.fill_color = fill_color;
        style.fill_color_2 = fill_color_2;
        style.fill_mode = fill_mode;
        style.trim_start = trim_start;
        style.trim_end = trim_end;
        style.trim_offset = trim_offset;
        style.closed = closed;
        return style;
    }

    void LineRendererComponent::ResizePoints()
    {
        if (point_count < 0) point_count = 0;
        const std::size_t previous_size = points.size();
        try
        {
            points.resize(static_cast<std::size_t>(point_count));
            for (std::size_t index = previous_size; index < points.size(); ++index)
                points[index] = { static_cast<float>(index), 0.0f, 0.0f };
        }
        catch (...)
        {
            point_count = static_cast<int>((std::min)(points.size(),
                static_cast<std::size_t>((std::numeric_limits<int>::max)())));
        }
    }

    void LineRendererComponent::RebuildDynamicProperties()
    {
        dynamic_properties_.clear();
        try
        {
            dynamic_properties_.reserve(points.size());
            for (std::size_t index = 0; index < points.size(); ++index)
                dynamic_properties_.push_back(MakePointProperty(static_cast<int>(index)));
        }
        catch (...)
        {
            dynamic_properties_.clear();
        }
    }
}
