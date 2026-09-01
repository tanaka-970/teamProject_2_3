#include "static_mesh.h"

#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <cwctype>
#include <windows.h>

using namespace DirectX;

namespace
{
    struct ObjIndex
    {
        int position = 0;
        int texcoord = 0;
        int normal = 0;
    };

    bool ParseObjIndex(const std::wstring& token, ObjIndex& out)
    {
        if (token.empty()) return false;
        std::wstring part;
        std::wstringstream stream(token);
        if (!std::getline(stream, part, L'/') || part.empty()) return false;
        out.position = std::stoi(part);
        if (std::getline(stream, part, L'/') && !part.empty()) out.texcoord = std::stoi(part);
        if (std::getline(stream, part, L'/') && !part.empty()) out.normal = std::stoi(part);
        return out.position > 0;
    }

    template<class T>
    const T* OneBased(const std::vector<T>& values, int index) noexcept
    {
        if (index <= 0 || static_cast<std::size_t>(index) > values.size()) return nullptr;
        return &values[static_cast<std::size_t>(index - 1)];
    }
}

bool static_mesh::can_load(const wchar_t* obj_filename, std::wstring* out_reason)
{
    const auto fail = [out_reason](std::wstring reason)
    {
        if (out_reason != nullptr) *out_reason = std::move(reason);
        return false;
    };
    if (obj_filename == nullptr || obj_filename[0] == L'\0') return fail(L"パスが空です");
    const std::filesystem::path path(obj_filename);
    std::wstring extension = path.extension().wstring();
    for (wchar_t& character : extension)
        character = static_cast<wchar_t>(::towlower(character));
    if (extension != L".obj") return fail(L"static_mesh が対応するのは .obj のみです: " + path.wstring());
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error) return fail(L"ファイルが見つかりません: " + path.wstring());
    if (std::filesystem::is_directory(path, error)) return fail(L"ディレクトリが指定されています: " + path.wstring());
    if (out_reason != nullptr) out_reason->clear();
    return true;
}

static_mesh::static_mesh(const wchar_t* obj_filename, bool flipping_v_coordinates)
{
    if (!can_load(obj_filename, &load_error_)) return;
    std::wifstream input(obj_filename);
    if (!input)
    {
        load_error_ = L"OBJ ファイルを開けません: " + std::wstring(obj_filename);
        return;
    }

    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT3> normals;
    std::vector<XMFLOAT2> texcoords;
    std::vector<vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::wstring> material_libraries;
    std::wstring line;
    std::wstring active_material;

    while (std::getline(input, line))
    {
        std::wistringstream row(line);
        std::wstring command;
        row >> command;
        if (command.empty() || command[0] == L'#') continue;
        if (command == L"v")
        {
            XMFLOAT3 value{};
            if (row >> value.x >> value.y >> value.z) positions.push_back(value);
        }
        else if (command == L"vt")
        {
            XMFLOAT2 value{};
            if (row >> value.x >> value.y)
            {
                if (flipping_v_coordinates) value.y = 1.0f - value.y;
                texcoords.push_back(value);
            }
        }
        else if (command == L"vn")
        {
            XMFLOAT3 value{};
            if (row >> value.x >> value.y >> value.z) normals.push_back(value);
        }
        else if (command == L"mtllib")
        {
            std::wstring name;
            if (row >> name) material_libraries.push_back(name);
        }
        else if (command == L"usemtl")
        {
            row >> active_material;
            if (!subsets.empty())
            {
                subset& previous = subsets.back();
                previous.index_count = static_cast<std::uint32_t>(indices.size()) - previous.index_start;
            }
            subsets.push_back({ active_material, static_cast<std::uint32_t>(indices.size()), 0 });
        }
        else if (command == L"f")
        {
            std::vector<ObjIndex> face;
            std::wstring token;
            while (row >> token)
            {
                ObjIndex parsed{};
                try
                {
                    if (ParseObjIndex(token, parsed)) face.push_back(parsed);
                }
                catch (...) {}
            }
            if (face.size() < 3) continue;
            for (std::size_t triangle = 1; triangle + 1 < face.size(); ++triangle)
            {
                const ObjIndex triangle_indices[3]{ face[0], face[triangle], face[triangle + 1] };
                for (const ObjIndex& source : triangle_indices)
                {
                    const XMFLOAT3* position = OneBased(positions, source.position);
                    if (position == nullptr) continue;
                    vertex value{};
                    value.position = *position;
                    if (const XMFLOAT3* normal = OneBased(normals, source.normal)) value.normal = *normal;
                    if (const XMFLOAT2* texcoord = OneBased(texcoords, source.texcoord)) value.texcoord = *texcoord;
                    vertices.push_back(value);
                    indices.push_back(static_cast<std::uint32_t>(vertices.size() - 1));
                }
            }
        }
    }
    if (!subsets.empty())
    {
        subset& last = subsets.back();
        last.index_count = static_cast<std::uint32_t>(indices.size()) - last.index_start;
    }
    if (subsets.empty() && !indices.empty()) subsets.push_back({ L"Default", 0, static_cast<std::uint32_t>(indices.size()) });

    for (const std::wstring& library : material_libraries)
    {
        std::filesystem::path material_path(obj_filename);
        material_path.replace_filename(std::filesystem::path(library).filename());
        std::wifstream material_input(material_path);
        if (!material_input) continue;
        material* current = nullptr;
        while (std::getline(material_input, line))
        {
            std::wistringstream row(line);
            std::wstring command;
            row >> command;
            if (command == L"newmtl")
            {
                material value{};
                row >> value.name;
                materials.push_back(std::move(value));
                current = &materials.back();
            }
            else if (current != nullptr && command == L"Ka") row >> current->Ka.x >> current->Ka.y >> current->Ka.z;
            else if (current != nullptr && command == L"Kd") row >> current->Kd.x >> current->Kd.y >> current->Kd.z;
            else if (current != nullptr && command == L"Ks") row >> current->Ks.x >> current->Ks.y >> current->Ks.z;
            else if (current != nullptr && command == L"map_Kd")
            {
                std::wstring name;
                row >> name;
                material_path.replace_filename(std::filesystem::path(name).filename());
                current->texture_filenames[0] = material_path.wstring();
            }
            else if (current != nullptr && (command == L"map_bump" || command == L"bump"))
            {
                std::wstring name;
                row >> name;
                material_path.replace_filename(std::filesystem::path(name).filename());
                current->texture_filenames[1] = material_path.wstring();
            }
        }
    }
    if (materials.empty())
    {
        for (const subset& entry : subsets)
        {
            material value{};
            value.name = entry.usemtl;
            materials.push_back(std::move(value));
        }
    }
    if (!SetCpuGeometry(vertices, indices))
    {
        if (load_error_.empty()) load_error_ = L"OBJ に有効な三角形がありません: " + std::wstring(obj_filename);
        return;
    }
    loaded_ = true;
}

void static_mesh::RecalculateBounds() noexcept
{
    bounding_box[0] = { FLT_MAX, FLT_MAX, FLT_MAX };
    bounding_box[1] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (const vertex& value : cpu_vertices_)
    {
        bounding_box[0].x = (std::min)(bounding_box[0].x, value.position.x);
        bounding_box[0].y = (std::min)(bounding_box[0].y, value.position.y);
        bounding_box[0].z = (std::min)(bounding_box[0].z, value.position.z);
        bounding_box[1].x = (std::max)(bounding_box[1].x, value.position.x);
        bounding_box[1].y = (std::max)(bounding_box[1].y, value.position.y);
        bounding_box[1].z = (std::max)(bounding_box[1].z, value.position.z);
    }
}

bool static_mesh::SetCpuGeometry(const std::vector<vertex>& vertices,
    const std::vector<std::uint32_t>& indices)
{
    if (vertices.empty() || indices.empty() || indices.size() % 3 != 0) return false;
    for (std::uint32_t index : indices) if (index >= vertices.size()) return false;
    cpu_vertices_ = vertices;
    cpu_indices_ = indices;
    RecalculateBounds();
    return true;
}
