// static_mesh のうち、OBJ / MTL からの読み込みだけを持つ。
//
//   static_mesh.cpp          ... OBJ / MTL の検証・読み込み（このファイル）
//   static_mesh_runtime.cpp  ... Procedural geometry の生成・更新と描画

#include "shader.h"
#include "misc.h"
#include "static_mesh.h"

#include <fstream>
#include <vector>
#include <cwctype>

#include <filesystem>
#include "texture.h"
#include "../render/motion_vector_context.h"
#include "../../RePlayEngine/Rendering/RenderStats.h"

using namespace DirectX;

bool static_mesh::can_load(const wchar_t* obj_filename, std::wstring* out_reason)
{
	const auto fail = [out_reason](std::wstring reason)
	{
		if (out_reason != nullptr) *out_reason = std::move(reason);
		return false;
	};

	if (obj_filename == nullptr || obj_filename[0] == L'\0')
	{
		return fail(L"パスが空です");
	}

	const std::filesystem::path path(obj_filename);

	// static_mesh は Wavefront OBJ 専用。FBX や glTF を渡されても解釈できない。
	std::wstring extension = path.extension().wstring();
	for (wchar_t& character : extension)
	{
		character = static_cast<wchar_t>(::towlower(character));
	}
	if (extension != L".obj")
	{
		return fail(L"static_mesh が対応するのは .obj のみです（渡された拡張子: " +
			(extension.empty() ? std::wstring(L"なし") : extension) + L"）: " + path.wstring());
	}

	std::error_code filesystem_error;
	if (!std::filesystem::exists(path, filesystem_error) || filesystem_error)
	{
		return fail(L"ファイルが見つかりません: " + path.wstring());
	}
	if (std::filesystem::is_directory(path, filesystem_error))
	{
		return fail(L"ディレクトリが指定されています: " + path.wstring());
	}

	if (out_reason != nullptr) out_reason->clear();
	return true;
}

static_mesh::static_mesh(ID3D11Device* device, const wchar_t* obj_filename, bool flipping_v_coordinates/*UNIT.14*/)
{
	std::vector<vertex> vertices;
	std::vector<uint32_t> indices;
	uint32_t current_index{ 0 };

	std::vector<XMFLOAT3> positions;
	std::vector<XMFLOAT3> normals;
	std::vector<XMFLOAT2> texcoords;
	std::vector<std::wstring> mtl_filenames;

	// 読み込めないパスはここで打ち切る。
	// 以前はそのまま先へ進み、空の subsets に対して rbegin()-> を実行して
	// 「can't decrement value-initialized vector iterator」で二次障害になっていた。
	if (!can_load(obj_filename, &load_error_))
	{
		OutputDebugStringW((L"[static_mesh] 読み込みを中止しました: " + load_error_ + L"\n").c_str());
		return;
	}

	std::wifstream fin(obj_filename);
	if (!fin)
	{
		// can_load を通った＝ファイルは存在するのに開けない場合。
		// 権限やロックなど本当に想定外の状況なので、ここは assert を残す。
		_ASSERT_EXPR(fin, L"OBJ file exists but could not be opened.");
		load_error_ = L"OBJ ファイルを開けません: " + std::wstring(obj_filename);
		OutputDebugStringW((L"[static_mesh] " + load_error_ + L"\n").c_str());
		return;
	}
	wchar_t command[256];
	while (fin)
	{
		fin >> command;
		if (0 == wcscmp(command, L"v"))
		{
			// v x y z w
			// 
			// Specifies a geometric vertex and its x y z coordinates.Rational
			// curves and surfaces require a fourth homogeneous coordinate, also
			// called the weight.
			// 
			// x y z are the x, y, and z coordinates for the vertex.These are
			// floating point numbers that define the position of the vertex in
			// three dimensions.
			// 
			// w is the weight required for rational curves and surfaces.It is
			// not required for non - rational curves and surfaces.If you do not
			// specify a value for w, the default is 1.0.
			float x, y, z;
			fin >> x >> y >> z;
			positions.push_back({ x, y, z });
			fin.ignore(1024, L'\n');
		}
		else if (0 == wcscmp(command, L"vt"))
		{
			// vt u v w
			// 
			// Specifies a texture vertex and its coordinates.A 1D texture
			// requires only u texture coordinates, a 2D texture requires both u
			// and v texture coordinates, and a 3D texture requires all three
			// coordinates.
			// 
			// u is the value for the horizontal direction of the texture.
			// 
			// v is an optional argument.
			// 
			// v is the value for the vertical direction of the texture.The
			// default is 0.
			// 
			// w is an optional argument.
			// 
			// w is a value for the depth of the texture.The default is 0.
			float u, v;
			fin >> u >> v;
			texcoords.push_back({ u, flipping_v_coordinates ? 1.0f - v : v });
			fin.ignore(1024, L'\n');
		}
		else if (0 == wcscmp(command, L"vn"))
		{
			// vn i j k
			// 
			// Specifies a normal vector with components i, j, and k.
			// 
			// Vertex normals affect the smooth - shading and rendering of geometry.0
			// For polygons, vertex normals are used in place of the actual facet
			// normals.For surfaces, vertex normals are interpolated over the
			// entire surface and replace the actual analytic surface normal.
			// 
			// When vertex normals are present, they supersede smoothing groups.
			// 
			// i j k are the i, j, and k coordinates for the vertex normal.They
			// are floating point numbers.
			FLOAT i, j, k;
			fin >> i >> j >> k;
			normals.push_back({ i, j, k });
			fin.ignore(1024, L'\n');
		}
		else if (0 == wcscmp(command, L"f"))
		{
			//f  v1 / vt1 / vn1   v2 / vt2 / vn2   v3 / vt3 / vn3 . . .
			//
			// optionally include the texture vertex and vertex normal reference
			// numbers.
			// 
			// The reference numbers for the vertices, texture vertices, and
			// vertex normals must be separated by slashes(/ ).There is no space
			// between the number and the slash.
			// 
			// v is the reference number for a vertex in the face element.A
			// minimum of three vertices are required.
			// 
			// vt is an optional argument.
			// 
			// vt is the reference number for a texture vertex in the face
			// element.It always follows the first slash.
			// 
			// vn is an optional argument.
			// 
			// vn is the reference number for a vertex normal in the face element.
			// It must always follow the second slash.
			// 
			// Face elements use surface normals to indicate their orientation.If
			// vertices are ordered counterclockwise around the face, both the
			// face and the normal will point toward the viewer.If the vertex
			// ordering is clockwise, both will point away from the viewer.If
			// vertex normals are assigned, they should point in the general
			// direction of the surface normal, otherwise unpredictable results
			// may occur.
			//
			// If a face has a texture map assigned to it and no texture vertices
			// are assigned in the f statement, the texture map is ignored when
			// the element is rendered.
			for (size_t i = 0; i < 3; i++)
			{
				vertex vertex;
				size_t v, vt, vn;

				fin >> v;
				vertex.position = positions.at(v - 1);
				if (L'/' == fin.peek())
				{
					fin.ignore(1);
					if (L'/' != fin.peek())
					{
						fin >> vt;
						vertex.texcoord = texcoords.at(vt - 1);
					}
					if (L'/' == fin.peek())
					{
						fin.ignore(1);
						fin >> vn;
						vertex.normal = normals.at(vn - 1);
					}
				}
				vertices.push_back(vertex);
				indices.push_back(current_index++);
			}
			fin.ignore(1024, L'\n');
		}
		else if (0 == wcscmp(command, L"mtllib"))
		{
			// mtllib filename1 filename2 . . .
			// Specifies the material library file for the material definitions
			// set with the usemtl statement.You can specify multiple filenames
			// with mtllib.If multiple filenames are specified, the first file
			// listed is searched first for the material definition, the second
			// file is searched next, and so on.
			wchar_t mtllib[256];
			fin >> mtllib;
			mtl_filenames.push_back(mtllib);
		}
		else if (0 == wcscmp(command, L"usemtl"))
		{
			wchar_t usemtl[MAX_PATH]{ 0 };
			fin >> usemtl;
			subsets.push_back({ usemtl, static_cast<uint32_t>(indices.size()), 0 });
		}
		else
		{
			fin.ignore(1024, L'\n');
		}
	}
	fin.close();

	// subsets が空のまま rbegin() を参照すると、MSVC のデバッグイテレータが
	// 「can't decrement value-initialized vector iterator」で停止する。
	// usemtl が 1 つも無い OBJ や、途中で解析が止まった場合に起こり得るので必ず確認する。
	if (!subsets.empty())
	{
		std::vector<subset>::reverse_iterator iterator = subsets.rbegin();
		iterator->index_count = static_cast<uint32_t>(indices.size()) - iterator->index_start;
		for (iterator = subsets.rbegin() + 1; iterator != subsets.rend(); ++iterator)
		{
			iterator->index_count = (iterator - 1)->index_start - iterator->index_start;
		}
	}

	// 頂点が 1 つも取れていない OBJ で先へ進むと、
	// 0 バイトの頂点バッファ作成に失敗して別の assert になる。ここで打ち切る。
	if (vertices.empty())
	{
		load_error_ = L"OBJ に頂点がありません: " + std::wstring(obj_filename);
		OutputDebugStringW((L"[static_mesh] " + load_error_ + L"\n").c_str());
		return;
	}

	// mtllib が書かれていない OBJ もある。空 vector への添字アクセスを避ける。
	bool material_library_opened = false;
	if (!mtl_filenames.empty())
	{
		std::filesystem::path mtl_filename(obj_filename);
		mtl_filename.replace_filename(std::filesystem::path(mtl_filenames[0]).filename());
		fin.open(mtl_filename);
		material_library_opened = static_cast<bool>(fin);
	}

	while (material_library_opened && fin)
	{
		fin >> command;

		// newmtl より前に map_Kd / Ka / Kd / Ks などが現れる壊れた .mtl があると、
		// 空の materials に対して rbegin()-> を実行して
		// 「can't decrement value-initialized vector iterator」で停止する。
		// 現在のマテリアルが無い間は、newmtl 以外の行を読み飛ばす。
		const bool has_current_material = !materials.empty();
		if (!has_current_material && 0 != wcscmp(command, L"newmtl"))
		{
			fin.ignore(1024, L'\n');
			continue;
		}

		if (0 == wcscmp(command, L"map_Kd"))
		{
			// map_Kd - options args filename
			//
			// Specifies that a color texture file or color procedural texture file is
			// linked to the diffuse reflectivity of the material.During rendering,
			// the map_Kd value is multiplied by the Kd value.
			//
			// "filename" is the name of a color texture file(.mpc), a color
			// procedural texture file(.cxc), or an image file.
			fin.ignore();
			wchar_t map_Kd[256];
			fin >> map_Kd;

			std::filesystem::path path(obj_filename);
			path.replace_filename(std::filesystem::path(map_Kd).filename());
			materials.rbegin()->texture_filenames[0] = path;
			fin.ignore(1024, L'\n');
		}
		else if (0 == wcscmp(command, L"map_bump") || 0 == wcscmp(command, L"bump"))
		{
			// map_bump - options args filename
			//
			// Specifies that a bump texture file or a bump procedural texture file is
			// linked to the material.
			//
			// "filename" is the name of a bump texture file(.mpb), a bump procedural
			// texture file(.cxb), or an image file.
			fin.ignore();
			wchar_t map_bump[256];
			fin >> map_bump;

			std::filesystem::path path(obj_filename);
			path.replace_filename(std::filesystem::path(map_bump).filename());
			materials.rbegin()->texture_filenames[1] = path;
			fin.ignore(1024, L'\n');
		}
		else if (0 == wcscmp(command, L"newmtl"))
		{
			// The folowing syntax describes the material name statement.
			//
			//	newmtl name
			//
			// Specifies the start of a material description and assigns a name to the
			// material.An.mtl file must have one newmtl statement at the start of
			// each material description.
			// "name" is the name of the material.Names may be any length but
			// cannot include blanks.Underscores may be used in material names.material material;
			fin.ignore();
			wchar_t newmtl[256];
			material material;
			fin >> newmtl;
			material.name = newmtl;
			materials.push_back(material);
		}
		else if (0 == wcscmp(command, L"Ka"))
		{
			// Ka r g b
			//
			// The Ka statement specifies the ambient reflectivity using RGB values.
			// "r g b" are the values for the red, green, and blue components of the
			// color.The g and b arguments are optional.If only r is specified,
			// then g, and b are assumed to be equal to r.The r g b values are
			// normally in the range of 0.0 to 1.0.Values outside this range increase
			// or decrease the relectivity accordingly.
			float r, g, b;
			fin >> r >> g >> b;
			materials.rbegin()->Ka = { r, g, b, 1 };
			fin.ignore(1024, L'\n');
		}
		else if (0 == wcscmp(command, L"Kd"))
		{
			// Kd r g b
			//
			// The Kd statement specifies the diffuse reflectivity using RGB values.
			// "r g b" are the values for the red, green, and blue components of the
			// atmosphere.The g and b arguments are optional.If only r is
			// specified, then g, and b are assumed to be equal to r.The r g b values
			// are normally in the range of 0.0 to 1.0.Values outside this range
			// increase or decrease the relectivity accordingly.
			float r, g, b;
			fin >> r >> g >> b;
			materials.rbegin()->Kd = { r, g, b, 1 };
			fin.ignore(1024, L'\n');
		}
		else if (0 == wcscmp(command, L"Ks"))
		{
			// Ks r g b
			//
			// The Ks statement specifies the specular reflectivity using RGB values.
			// "r g b" are the values for the red, green, and blue components of the
			// atmosphere.The g and b arguments are optional.If only r is
			// specified, then g, and b are assumed to be equal to r.The r g b values
			// are normally in the range of 0.0 to 1.0.Values outside this range
			// increase or decrease the relectivity accordingly.
			float r, g, b;
			fin >> r >> g >> b;
			materials.rbegin()->Ks = { r, g, b, 1 };
			fin.ignore(1024, L'\n');
		}
		else
		{
			fin.ignore(1024, L'\n');
		}
	}
	fin.close();

	create_com_buffers(device, vertices.data(), vertices.size(), indices.data(), indices.size());

	HRESULT hr{ S_OK };

	D3D11_INPUT_ELEMENT_DESC input_element_desc[]
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	create_vs_from_cso(device, "static_mesh_vs.cso", vertex_shader.GetAddressOf(), input_layout.GetAddressOf(), input_element_desc, ARRAYSIZE(input_element_desc));
	create_ps_from_cso(device, "static_mesh_ps.cso", pixel_shader.GetAddressOf());

	D3D11_BUFFER_DESC buffer_desc{};
	buffer_desc.ByteWidth = sizeof(constants);
	buffer_desc.Usage = D3D11_USAGE_DEFAULT;
	buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	hr = device->CreateBuffer(&buffer_desc, nullptr, constant_buffer.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	// TAAのモーションベクター用の定数バッファ(b6)。
	buffer_desc.ByteWidth = sizeof(motion_vectors::ObjectConstants);
	hr = device->CreateBuffer(&buffer_desc, nullptr,
		motion_object_constant_buffer.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	if (materials.size() == 0)
	{
		for (const subset& subset : subsets)
		{
			materials.push_back({ subset.usemtl });
		}
	}

	D3D11_TEXTURE2D_DESC texture2d_desc{};
	for (material& material : materials)
	{
		if (material.texture_filenames[0].size() > 0)
		{
			load_texture_from_file(device, material.texture_filenames[0].c_str(), material.shader_resource_views[0].GetAddressOf(), &texture2d_desc);
		}
		else
		{
			make_dummy_texture(device, material.shader_resource_views[0].GetAddressOf(), 0xFFFFFFFF, 16);
		}
		if (material.texture_filenames[1].size() > 0)
		{
			load_texture_from_file(device, material.texture_filenames[1].c_str(), material.shader_resource_views[1].GetAddressOf(), &texture2d_desc);
		}
		else
		{
			make_dummy_texture(device, material.shader_resource_views[1].GetAddressOf(), 0xFFFF7F7F, 16);
		}
	}

	for (const vertex& v : vertices)
	{
		bounding_box[0].x = std::min<float>(bounding_box[0].x, v.position.x);
		bounding_box[0].y = std::min<float>(bounding_box[0].y, v.position.y);
		bounding_box[0].z = std::min<float>(bounding_box[0].z, v.position.z);
		bounding_box[1].x = std::max<float>(bounding_box[1].x, v.position.x);
		bounding_box[1].y = std::max<float>(bounding_box[1].y, v.position.y);
		bounding_box[1].z = std::max<float>(bounding_box[1].z, v.position.z);
	}

	// ここまで到達したときだけ描画可能とみなす。
	// 呼び出し側は is_loaded() が false のメッシュを描画してはいけない。
	loaded_ = true;
}
