#include "ShaderPack.h"
#include "ShaderLibrary.h"
#include "ShaderConstantPacker.h"
#include "../../Runtime/Packaging/PackIO.h"
#include "../Materials/MaterialAsset.h"

#include <atomic>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <cwctype>
#include <cctype>

namespace ReplayEngine::Rendering::ShaderPack
{
    namespace
    {
        using Runtime::Packaging::Json;
        using Runtime::Packaging::Require;
        using Records = std::map<std::string, std::vector<std::uint8_t>>;
        std::atomic<bool> standalone{ false };
        std::mutex state_mutex;
        Records runtime_records;
        Records editor_records;
        ShaderCatalog runtime_catalog;
        std::filesystem::path content_root;
        bool loaded = false;

        struct Capture
        {
            Records records;
            std::vector<std::string> failures;
        };
        thread_local Capture* capture = nullptr;

        struct ExportScope
        {
            Capture data;
            ExportScope() { Require(capture == nullptr, "Nested shader export"); capture = &data; }
            ~ExportScope() { capture = nullptr; }
        };

        std::string Utf8(std::wstring_view text)
        {
            if (text.empty()) return {};
            const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            Require(count > 0, "Invalid shader key text");
            std::string result(static_cast<std::size_t>(count), '\0');
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                static_cast<int>(text.size()), result.data(), count, nullptr, nullptr);
            return result;
        }

        Json Properties(const std::vector<ShaderProperty>& properties)
        {
            Json list = Json::array();
            for (const auto& p : properties)
                list.push_back({ {"name", p.name}, {"display", p.display_name},
                    {"category", p.category}, {"tooltip", p.tooltip}, {"kind", p.kind},
                    {"minimum", p.minimum}, {"maximum", p.maximum},
                    {"default", { p.default_value.x, p.default_value.y, p.default_value.z, p.default_value.w }},
                    {"texture", p.default_texture}, {"enum", p.enum_names},
                    {"offset", p.constant_offset}, {"size", p.constant_size}, {"slot", p.texture_slot} });
            return list;
        }

        std::vector<ShaderProperty> ReadProperties(const Json& list)
        {
            Require(list.is_array() && list.size() <= 4096, "Invalid shader properties");
            std::vector<ShaderProperty> properties;
            std::set<std::string> names;
            for (const auto& j : list)
            {
                ShaderProperty p;
                p.name = j.at("name").get<std::string>();
                Require(!p.name.empty() && names.insert(p.name).second, "Duplicate shader property: " + p.name);
                p.display_name = j.at("display").get<std::string>();
                p.category = j.at("category").get<std::string>();
                p.tooltip = j.at("tooltip").get<std::string>();
                const int kind = j.at("kind").get<int>();
                Require(kind >= 0 && kind <= static_cast<int>(ShaderPropertyKind::Enum), "Invalid shader property kind");
                p.kind = static_cast<ShaderPropertyKind>(kind);
                p.minimum = j.at("minimum").get<float>();
                p.maximum = j.at("maximum").get<float>();
                const auto value = j.at("default").get<std::vector<float>>();
                Require(value.size() == 4, "Invalid shader default");
                p.default_value = { value[0], value[1], value[2], value[3] };
                p.default_texture = j.at("texture").get<std::string>();
                p.enum_names = j.at("enum").get<std::vector<std::string>>();
                p.constant_offset = j.at("offset").get<std::uint32_t>();
                p.constant_size = j.at("size").get<std::uint32_t>();
                p.texture_slot = j.at("slot").get<std::uint32_t>();
                Require(p.constant_offset <= 65536 && p.constant_size <= 65536 &&
                    p.constant_offset + p.constant_size <= 65536 && p.texture_slot <= 4096,
                    "Invalid shader property layout: " + p.name);
                properties.push_back(std::move(p));
            }
            return properties;
        }

        DX12::D3D12ShaderCompileOptions VariantOptions(ShaderVariant variant)
        {
            DX12::D3D12ShaderCompileOptions options;
            options.include_directories = { std::filesystem::path("Shader") / "Include", "Shader" };
            options.defines.push_back({ L"REPLAY_SKINNED", variant == ShaderVariant::Skinned ? L"1" : L"0" });
            return options;
        }

        std::string VariantKey(const ShaderCatalog::Entry& entry, ShaderVariant variant,
            const std::string& entry_point)
        {
            const std::wstring wide(entry_point.begin(), entry_point.end());
            return Key(entry.info.source_path, wide, L"ps_6_0", VariantOptions(variant));
        }

        Json SaveCatalog(const ShaderCatalog& catalog, const Records& records)
        {
            Json list = Json::array();
            for (const auto& entry : catalog.All())
            {
                Require(entry.schema && entry.AllCompiled(), "Incomplete shader: " + entry.info.id.ToString());
                const auto& info = entry.info;
                Json passes = Json::array();
                for (const auto& p : info.passes)
                    passes.push_back({ {"name", p.name}, {"entry", p.entry_point}, {"blend", p.blend} });
                Json j = { {"source", RelativeSource(info.source_path)}, {"id", info.id.ToString()},
                    {"name", info.name}, {"category", info.category}, {"domain", info.domain},
                    {"lighting", info.lighting_model}, {"lighting_valid", info.lighting_model_valid},
                    {"properties", Properties(info.properties)}, {"passes", passes},
                    {"schema_id", entry.schema->TypeID().ToString()}, {"revision", entry.schema->Revision()},
                    {"schema_properties", Properties(entry.schema->Properties())} };
                for (int i = 0; i < shader_variant_count; ++i)
                {
                    const auto variant = static_cast<ShaderVariant>(i);
                    if (!entry.UsesVariant(variant)) continue;
                    Require(records.count(VariantKey(entry, variant, "main")) != 0,
                        "Missing shader key: " + VariantKey(entry, variant, "main"));
                    for (const auto& pass : info.passes)
                        Require(records.count(VariantKey(entry, variant, pass.entry_point)) != 0,
                            "Missing shader key: " + VariantKey(entry, variant, pass.entry_point));
                }
                list.push_back(std::move(j));
            }
            return list;
        }

        ShaderCatalog ReadCatalog(const Json& list, const Records& records,
            const std::filesystem::path& root)
        {
            Require(list.is_array() && list.size() <= 65536, "Invalid shader catalog");
            ShaderCatalog catalog;
            for (const auto& j : list)
            {
                ShaderCatalog::Entry entry;
                auto& info = entry.info;
                const auto relative = std::filesystem::u8path(j.at("source").get<std::string>());
                Require(!relative.empty() && !relative.is_absolute(), "Invalid shader path");
                for (const auto& part : relative) Require(part != "..", "Invalid shader path");
                info.source_path = root / "Shader" / relative;
                Require(ShaderID::TryParse(j.at("id").get<std::string>(), info.id) &&
                    info.id.IsValid() && catalog.Find(info.id) == nullptr, "Invalid or duplicate shader ID");
                info.name = j.at("name").get<std::string>();
                info.category = j.at("category").get<std::string>();
                const int domain = j.at("domain").get<int>();
                const int lighting = j.at("lighting").get<int>();
                Require(domain >= 0 && domain <= 2 && lighting >= 0 && lighting <= 2, "Unknown shader domain or lighting");
                info.domain = static_cast<ShaderDomain>(domain);
                info.lighting_model = static_cast<ShaderLightingModel>(lighting);
                info.lighting_model_valid = j.at("lighting_valid").get<bool>();
                Require(info.lighting_model_valid, "Invalid shader lighting");
                info.properties = ReadProperties(j.at("properties"));
                ShaderID schema_id;
                Require(ShaderID::TryParse(j.at("schema_id").get<std::string>(), schema_id) &&
                    schema_id == info.id, "Shader schema ID mismatch");
                entry.schema = std::make_shared<ShaderPropertySchema>(schema_id,
                    ReadProperties(j.at("schema_properties")), j.at("revision").get<std::uint32_t>());
                for (const auto& p : j.at("passes"))
                {
                    ShaderPassInfo pass;
                    pass.name = p.at("name").get<std::string>();
                    pass.entry_point = p.at("entry").get<std::string>();
                    const int blend = p.at("blend").get<int>();
                    Require(!pass.entry_point.empty() && blend >= 0 && blend <= 3, "Invalid shader pass");
                    pass.blend = static_cast<ShaderPassBlend>(blend);
                    info.passes.push_back(pass);
                    ShaderCatalog::PassResult result;
                    result.info = pass;
                    entry.passes.push_back(std::move(result));
                }
                const auto restore = [&](ShaderCatalog::VariantResult& result,
                    ShaderVariant variant, const std::string& entry_point)
                {
                    const auto key = VariantKey(entry, variant, entry_point);
                    const auto found = records.find(key);
                    Require(found != records.end() && !found->second.empty(), "Missing shader key: " + key);
                    result.bytecode = std::make_shared<const std::vector<std::uint8_t>>(found->second);
                    result.compiled = result.ever_compiled = true;
                };
                for (int i = 0; i < shader_variant_count; ++i)
                {
                    const auto variant = static_cast<ShaderVariant>(i);
                    if (!entry.UsesVariant(variant)) continue;
                    restore(entry.At(variant), variant, "main");
                    for (auto& pass : entry.passes) restore(pass.At(variant), variant, pass.info.entry_point);
                }
                catalog.Register(std::move(entry));
            }
            return catalog;
        }
    }

    bool IsStandalone() noexcept { return standalone.load(); }
    bool IsRecordingExport() noexcept { return capture != nullptr; }

    DX12::D3D12ShaderCompileOptions SurfaceOptions(bool debug)
    {
        DX12::D3D12ShaderCompileOptions options;
        options.debug = debug;
        options.optimize = !debug;
        options.include_directories = { std::filesystem::current_path() / "Shader",
            std::filesystem::current_path() / "Shader" / "Include" };
        options.defines.push_back({ L"REPLAY_SKINNED", L"0" });
        return options;
    }

    DX12::D3D12ShaderCompileOptions UIOptions(bool debug)
    {
        DX12::D3D12ShaderCompileOptions options;
        options.debug = debug;
        options.optimize = !debug;
        return options;
    }

    std::string ComposeSource(const std::string& body, const std::string& declaration,
        const std::filesystem::path& path, bool ui)
    {
        return std::string(ui ? "#line 1 \"REPLAY_GENERATED\"\n"
            : "#line 1 \"REPLAY_DX12_GENERATED\"\n") + declaration +
            (ui ? "" : "\n") + "#line 1 \"" +
            (ui ? path.generic_u8string() : path.generic_string()) + "\"\n" + body;
    }

    std::string RelativeSource(const std::filesystem::path& source)
    {
        const auto normalized = source.lexically_normal();
        if (!content_root.empty())
        {
            const auto absolute = normalized.is_absolute()
                ? normalized : std::filesystem::absolute(normalized).lexically_normal();
            const auto root = std::filesystem::absolute(content_root / "Shader").lexically_normal();
            const auto from_root = absolute.lexically_relative(root);
            bool contained = !from_root.empty() && !from_root.is_absolute();
            for (const auto& part : from_root) if (part == "..") contained = false;
            if (contained) return from_root.generic_u8string();
        }
        std::filesystem::path relative;
        bool found = false;
        for (const auto& part : normalized)
        {
            std::wstring name = part.wstring();
            for (auto& c : name) c = static_cast<wchar_t>(std::towlower(c));
            if (!found && name == L"shader") { found = true; continue; }
            if (found) relative /= part;
        }
        return (found ? (relative.empty() ? std::filesystem::path(".") : relative)
            : normalized).generic_u8string();
    }

    std::string Key(const std::filesystem::path& source, std::wstring_view entry,
        std::wstring_view target, const DX12::D3D12ShaderCompileOptions& options)
    {
        Json defines = Json::array();
        for (const auto& define : options.defines)
            defines.push_back({ Utf8(define.name), Utf8(define.value) });
        Json includes = Json::array();
        for (const auto& include : options.include_directories)
            includes.push_back(RelativeSource(include));
        return Json::array({ RelativeSource(source), Utf8(entry), Utf8(target),
            defines, options.debug, options.optimize, options.warnings_as_errors, includes }).dump();
    }

    DX12::D3D12ShaderCompileResult Lookup(const std::filesystem::path& source,
        std::wstring_view entry, std::wstring_view target,
        const DX12::D3D12ShaderCompileOptions& requested)
    {
        auto options = requested;
        // 配布パックは GPU debug layer の指定にかかわらず Release bytecode を使う。
        options.debug = false;
        options.optimize = true;
        const auto key = Key(source, entry, target, options);
        const std::lock_guard<std::mutex> lock(state_mutex);
        DX12::D3D12ShaderCompileResult result;
        const auto found = runtime_records.find(key);
        if (loaded && found != runtime_records.end())
        {
            result.bytecode = found->second;
            result.succeeded = true;
            result.status = S_OK;
        }
        else
        {
            result.diagnostics = "Missing ShaderCache.replaypack key: " + key;
            OutputDebugStringA(result.diagnostics.c_str());
            std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
        }
        return result;
    }

    void Record(const std::filesystem::path& source, std::wstring_view entry,
        std::wstring_view target, const DX12::D3D12ShaderCompileOptions& options,
        const DX12::D3D12ShaderCompileResult& result)
    {
        const auto key = Key(source, entry, target, options);
        if (capture != nullptr)
        {
            if (!result.succeeded || result.bytecode.empty())
                capture->failures.push_back(key + ": " + result.diagnostics);
            else
            {
                const auto existing = capture->records.find(key);
                if (existing != capture->records.end() && existing->second != result.bytecode)
                    capture->failures.push_back("Conflicting shader key: " + key);
                capture->records[key] = result.bytecode;
            }
            return;
        }
        if (result.succeeded && !result.bytecode.empty())
        {
            const std::lock_guard<std::mutex> lock(state_mutex);
            editor_records[key] = result.bytecode;
        }
    }

    bool Configure(bool packaged, const std::filesystem::path& root, std::string& error)
    {
        const std::lock_guard<std::mutex> lock(state_mutex);
        standalone = packaged;
        loaded = false;
        runtime_records.clear();
        runtime_catalog.Clear();
        content_root = root;
        if (!packaged) return true;
        try
        {
            const auto data = Runtime::Packaging::ReadPack(root / "resources" / "ShaderCache.replaypack", "shaders");
            for (const auto& record : data.at("bytecode"))
            {
                const auto key = record.at("key").get<std::string>();
                Require(record.at("bytes").is_binary(), "Invalid shader bytecode: " + key);
                const auto& bytes = record.at("bytes").get_binary();
                Require(!bytes.empty() && runtime_records.emplace(key,
                    std::vector<std::uint8_t>(bytes.begin(), bytes.end())).second, "Invalid or duplicate shader key: " + key);
            }
            Require(!runtime_records.empty(), "Shader pack contains no bytecode");
            runtime_catalog = ReadCatalog(data.at("catalog"), runtime_records, root);
            loaded = true;
            return true;
        }
        catch (const std::exception& exception)
        {
            runtime_records.clear();
            error = exception.what();
            return false;
        }
    }

    bool RestoreCatalog(ShaderCatalog& catalog, std::string& error)
    {
        const std::lock_guard<std::mutex> lock(state_mutex);
        if (!IsStandalone() || !loaded) { error = "Shader pack is not loaded"; return false; }
        catalog = runtime_catalog;
        return true;
    }

    bool Build(const std::filesystem::path& root, const std::filesystem::path& output,
        std::vector<std::string>& errors)
    {
        const auto initial_errors = errors.size();
        try
        {
            Require(!IsStandalone(), "Cannot export from standalone");
            ExportScope scope;
            DX12::D3D12ShaderCompiler compiler;
            Require(compiler.Initialize(DX12::D3D12ShaderCompiler::FindDefaultLibraryPath()), "Export DXC initialization failed");
            struct BuiltinRequest { const wchar_t* file; const wchar_t* entry; const wchar_t* target; };
            static const BuiltinRequest requests[] = {
#include <ShaderPackInventory.inl>
            };
            for (const auto& request : requests)
            {
                const auto path = root / "Shader" / request.file;
                const auto result = compiler.CompileFile(path, request.entry, request.target, false);
                if (!result.succeeded)
                    errors.push_back("Missing shader key: " + Key(path, request.entry, request.target, {}) + ": " + result.diagnostics);
            }
            ShaderLibrary library;
            library.SetLogSink([&](const std::string& severity, const std::string& message,
                const std::filesystem::path& file, int line)
            {
                if (severity == "Error")
                    errors.push_back(file.generic_u8string() + ":" + std::to_string(line) + ": " + message);
            });
            const auto report = library.ScanAll(root);
            if (report.failed || report.duplicate_ids || report.compile_failed)
                errors.push_back(report.Summary());
            for (const auto& entry : library.Catalog().All())
            {
                for (int i = 0; i < shader_variant_count; ++i)
                {
                    const auto variant = static_cast<ShaderVariant>(i);
                    if (!entry.UsesVariant(variant)) continue;
                    const auto check = [&](const std::string& entry_point)
                    {
                        const auto key = VariantKey(entry, variant, entry_point);
                        if (scope.data.records.count(key) == 0) errors.push_back("Missing shader key: " + key);
                    };
                    check("main");
                    for (const auto& pass : entry.info.passes) check(pass.entry_point);
                }
            }
            for (const auto& entry : library.Catalog().All())
            {
                if (!entry.schema || (entry.info.domain != ShaderDomain::PostProcess &&
                    entry.info.domain != ShaderDomain::Surface)) continue;
                const bool ui = entry.info.domain == ShaderDomain::PostProcess;
                const auto relative = RelativeSource(entry.info.source_path);
                if (!ui && relative.find("/BuiltIn/") != std::string::npos) continue;
                bool has_texture = false;
                for (const auto& property : entry.schema->Properties())
                    has_texture |= property.kind == ShaderPropertyKind::Texture;
                if (ui && has_texture) continue;
                std::ifstream file(entry.info.source_path, std::ios::binary);
                Require(static_cast<bool>(file), "Cannot read generated shader: " + relative);
                std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                if (source.compare(0, 3, "\xef\xbb\xbf") == 0) source.erase(0, 3);
                auto declaration = ShaderConstantPacker::GenerateHlslDeclaration(*entry.schema);
                if (ui)
                {
                    const auto material_register = "register(b" + std::to_string(ShaderConstantPacker::material_constant_register) + ")";
                    const auto position = declaration.find(material_register);
                    Require(position != std::string::npos, "Missing UI constant register: " + relative);
                    declaration.replace(position, material_register.size(), "register(b1)");
                }
                const auto options = ui ? UIOptions(false) : SurfaceOptions(false);
                const auto result = compiler.CompileSource(ComposeSource(source, declaration, entry.info.source_path, ui),
                    entry.info.source_path, L"main", L"ps_6_0", options);
                if (!result.succeeded)
                    errors.push_back("Missing generated shader key: " + Key(entry.info.source_path, L"main", L"ps_6_0", options) + ": " + result.diagnostics);
            }
            errors.insert(errors.end(), scope.data.failures.begin(), scope.data.failures.end());
            if (errors.size() != initial_errors) return false;
            const Json catalog = SaveCatalog(library.Catalog(), scope.data.records);
            (void)ReadCatalog(catalog, scope.data.records, root);
            Json records = Json::array();
            for (const auto& record : scope.data.records)
                records.push_back({ {"key", record.first}, {"bytes", Json::binary(record.second)} });
            Runtime::Packaging::WritePack(output, { {"kind", "shaders"}, {"version", 1},
                {"catalog", catalog}, {"bytecode", records} });
            return true;
        }
        catch (const std::exception& exception) { errors.push_back(exception.what()); return false; }
    }

    bool ValidateReferences(const std::filesystem::path& root, std::vector<std::string>& errors)
    {
        const auto initial_errors = errors.size();
        try
        {
            const auto data = Runtime::Packaging::ReadPack(root / "resources" / "ShaderCache.replaypack", "shaders");
            std::set<std::string> ids;
            for (const auto& entry : data.at("catalog")) ids.insert(entry.at("id").get<std::string>());
            for (const auto& file : std::filesystem::recursive_directory_iterator(root / "resources"))
            {
                if (!file.is_regular_file()) continue;
                auto extension = file.path().extension().u8string();
                for (auto& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (extension != ".replaymaterial") continue;
                MaterialAsset material;
                std::string error;
                if (!MaterialAsset::Load(file.path(), material, error))
                {
                    errors.push_back(file.path().generic_u8string() + ": " + error);
                    continue;
                }
                const auto check = [&](const std::string& guid)
                {
                    ShaderID id;
                    if (!ShaderID::TryParse(guid, id) || ids.count(id.ToString()) == 0)
                        errors.push_back("Missing shader catalog key: " + guid + " / " + file.path().generic_u8string());
                };
                if (!material.shader_guid.empty()) check(material.shader_guid);
                for (const auto& layer : material.layers.Layers())
                    if (layer.EffectiveShader().IsValid()) check(layer.EffectiveShader().ToString());
            }
        }
        catch (const std::exception& exception) { errors.push_back(exception.what()); }
        return errors.size() == initial_errors;
    }
}
