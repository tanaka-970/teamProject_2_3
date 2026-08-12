#include "CSharpScriptBackend.h"

#include "../Core/ScriptValue.h"
#include "../../Runtime/API/RuntimeContext.h"
#include "../../Runtime/Core/RuntimeResult.h"
#include "../../Runtime/Events/EventBus.h"

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstring>
#include <deque>
#include <cstdlib>
#include <iterator>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif
#include "CSharpScriptBackendHostInternal.h"
#include "CSharpScriptBackendInternal.h"
#include "CSharpScriptBackendNativeInternal.h"
namespace ReplayEngine::Scripting::CSharp
{
    using namespace Detail;

// Assembly 操作と C# build reload の関数本体

    std::filesystem::path CSharpScriptBackend::ShadowCopyAssembly(
        const std::filesystem::path& assembly_path, std::string& error) const
    {
        std::error_code filesystem_error;
        if (!std::filesystem::exists(assembly_path, filesystem_error) ||
            filesystem_error)
        {
            error = "C# build output is missing: " +
                assembly_path.generic_u8string();
            return {};
        }

        const auto ticks = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const std::filesystem::path cache_root =
            project_root_ / "Saved" / "ManagedAssemblyCache";

        for (int attempt = 0; attempt < 16; ++attempt)
        {
            const std::filesystem::path folder =
                cache_root / (std::to_string(ticks) + "_" + std::to_string(attempt));
            std::filesystem::create_directories(folder, filesystem_error);
            if (filesystem_error)
            {
                error = "C# Assembly cache folder create failed: " +
                    folder.generic_u8string();
                return {};
            }

            const std::filesystem::path destination = folder / assembly_path.filename();
            std::filesystem::copy_file(assembly_path, destination,
                std::filesystem::copy_options::overwrite_existing, filesystem_error);
            if (filesystem_error)
            {
                if (std::filesystem::exists(destination)) continue;
                error = "C# Assembly shadow copy failed: " +
                    filesystem_error.message();
                return {};
            }

            const std::array<std::string, 2> sidecars = { ".pdb", ".deps.json" };
            for (const std::string& extension : sidecars)
            {
                std::filesystem::path source_sidecar = assembly_path;
                source_sidecar.replace_extension(extension);
                if (!std::filesystem::exists(source_sidecar, filesystem_error) ||
                    filesystem_error)
                {
                    filesystem_error.clear();
                    continue;
                }

                std::filesystem::path destination_sidecar = destination;
                destination_sidecar.replace_extension(extension);
                std::filesystem::copy_file(source_sidecar, destination_sidecar,
                    std::filesystem::copy_options::overwrite_existing,
                    filesystem_error);
                filesystem_error.clear();
            }

            return destination;
        }

        error = "C# Assembly shadow copy cache path could not be allocated.";
        return {};
    }

    bool CSharpScriptBackend::LoadGameAssembly(
        const std::filesystem::path& assembly_path, std::string& output)
    {
        if (load_assembly_ == nullptr) return false;

        std::string copy_error;
        const std::filesystem::path load_path =
            ShadowCopyAssembly(assembly_path, copy_error);
        if (load_path.empty())
        {
            output = copy_error;
            SetLastError(copy_error);
            return false;
        }

        std::array<char, text_buffer_size> buffer{};
        std::vector<char> path = Utf8CString(load_path);
        const int result = reinterpret_cast<load_assembly_fn>(load_assembly_)(
            path.data(), buffer.data(), static_cast<int>(buffer.size()));
        output = ReadOutputBuffer(buffer);
        if (result == 0)
        {
            RefreshLastError();
            if (last_error_.empty()) SetLastError(output);
            return false;
        }

        assembly_loaded_ = true;
        g_event_subscriptions.clear();
        type_states_.clear();
        instance_types_.clear();
        last_error_.clear();
        last_error_file_.clear();
        last_error_line_ = 0;
        return true;
    }

    bool CSharpScriptBackend::UnloadGameAssembly(std::string& output)
    {
        if (unload_assembly_ == nullptr) return false;

        std::array<char, text_buffer_size> buffer{};
        const int result = reinterpret_cast<unload_assembly_fn>(unload_assembly_)(
            buffer.data(), static_cast<int>(buffer.size()));
        output = ReadOutputBuffer(buffer);
        if (result == 0)
        {
            RefreshLastError();
            if (last_error_.empty()) SetLastError(output);
            return false;
        }

        assembly_loaded_ = false;
        type_states_.clear();
        instance_types_.clear();
        return true;
    }

    bool CSharpScriptBackend::CompileAndReload(CSharpBuildResult* out_build)
    {
        last_build_ = CSharpProject::BuildGameScripts(project_root_);
        if (out_build != nullptr) *out_build = last_build_;

        if (!last_build_.succeeded)
        {
            SetLastError(last_build_.output_text.empty()
                ? "C# build failed." : last_build_.output_text);
            return false;
        }

        std::string output;
        if (!LoadGameAssembly(last_build_.output_assembly, output))
        {
            if (last_error_.empty()) SetLastError(output);
            return false;
        }

        return true;
    }

    bool CSharpScriptBackend::ReloadLastBuiltAssembly()
    {
        if (!last_build_.succeeded || last_build_.output_assembly.empty())
        {
            SetLastError("No successful C# build is available for reload.");
            return false;
        }
        std::error_code error;
        if (!std::filesystem::exists(last_build_.output_assembly, error) || error)
        {
            SetLastError("Last C# build output is missing: " +
                last_build_.output_assembly.generic_u8string());
            return false;
        }

        std::string output;
        return LoadGameAssembly(last_build_.output_assembly, output);
    }

}
