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

// hostfxr／Managed API 接続と Backend 初期化の関数本体

    CSharpScriptBackend::CSharpScriptBackend(std::filesystem::path project_root,
        bool packaged_mode)
        : project_root_(NormalizeRoot(std::move(project_root))),
          packaged_mode_(packaged_mode)
    {
    }

    CSharpScriptBackend::~CSharpScriptBackend()
    {
        Shutdown();
    }

    bool CSharpScriptBackend::Initialize()
    {
        if (initialized_) return true;

        if (packaged_mode_)
        {
            if (!LoadHost()) return false;
            if (!LoadManagedApi()) return false;
            if (!ResolveManagedEntryPoints()) return false;
            if (!SetNativeApi()) return false;

            initialized_ = true;
            const std::filesystem::path game_assembly =
                GameScriptsAssemblyPathForMode(project_root_, true);
            std::error_code filesystem_error;
            if (std::filesystem::exists(game_assembly, filesystem_error) &&
                !filesystem_error)
            {
                last_build_.succeeded = true;
                last_build_.exit_code = 0;
                last_build_.output_assembly = game_assembly;
                ReloadLastBuiltAssembly();
            }
            return true;
        }

        std::string error;
        if (!CSharpProject::EnsureProjectFiles(project_root_, error))
        {
            SetLastError(error);
            return false;
        }

        if (CSharpProject::ManagedApiBuildRequired(project_root_))
        {
            const CSharpBuildResult managed_build =
                CSharpProject::BuildManagedApi(project_root_);
            if (!managed_build.succeeded)
            {
                last_build_ = managed_build;
                SetLastError(managed_build.output_text.empty()
                    ? "Managed API build failed." : managed_build.output_text);
                return false;
            }
        }

        if (!LoadHost()) return false;
        if (!LoadManagedApi()) return false;
        if (!ResolveManagedEntryPoints()) return false;
        if (!SetNativeApi()) return false;

        initialized_ = true;

        // 変更がなければ dotnet を起動せず、前回の Assembly を直接ロードする。
        // 初回・ソース変更後・既存 Assembly のロード失敗時だけビルドへ戻る。
        const std::filesystem::path game_assembly =
            CSharpProject::GameScriptsAssemblyPath(project_root_);
        if (!CSharpProject::GameScriptsBuildRequired(project_root_))
        {
            last_build_.succeeded = true;
            last_build_.exit_code = 0;
            last_build_.output_assembly = game_assembly;
            if (ReloadLastBuiltAssembly()) return true;
        }

        // User script のコンパイル失敗で Editor 起動を止めない。
        // 成功していた旧 Assembly がある場合は Managed 側が保持する。
        CompileAndReload(nullptr);
        return true;
    }

    void CSharpScriptBackend::Shutdown()
    {
        g_event_subscriptions.clear();

        if (set_native_api_ != nullptr)
        {
            set_native_api_fn set_api =
                reinterpret_cast<set_native_api_fn>(set_native_api_);
            set_api(nullptr);
        }

        if (assembly_loaded_)
        {
            std::string output;
            UnloadGameAssembly(output);
        }

        type_states_.clear();
        instance_types_.clear();
        if (g_runtime_context == runtime_context_) g_runtime_context = nullptr;
        runtime_context_ = nullptr;

#ifdef _WIN32
        if (hostfxr_library_ != nullptr)
        {
            FreeLibrary(static_cast<HMODULE>(hostfxr_library_));
        }
#endif
        hostfxr_library_ = nullptr;
        host_context_ = nullptr;
        load_assembly_and_get_function_pointer_ = nullptr;

        set_native_api_ = nullptr;
        load_assembly_ = nullptr;
        unload_assembly_ = nullptr;
        describe_type_ = nullptr;
        create_instance_ = nullptr;
        destroy_instance_ = nullptr;
        invoke_ = nullptr;
        set_field_ = nullptr;
        get_field_ = nullptr;
        set_time_ = nullptr;
        live_instance_count_ = nullptr;
        last_error_function_ = nullptr;

        initialized_ = false;
        assembly_loaded_ = false;
    }

    void CSharpScriptBackend::BindRuntimeContext(Runtime::RuntimeContext* context) noexcept
    {
        runtime_context_ = context;
        g_runtime_context = context;
    }

    bool CSharpScriptBackend::LoadHost()
    {
#ifndef _WIN32
        SetLastError("hostfxr backend is only implemented on Windows in this project.");
        return false;
#else
        if (hostfxr_library_ != nullptr) return true;

        const std::filesystem::path hostfxr = FindHostFxr();
        if (hostfxr.empty())
        {
            SetLastError("hostfxr.dll was not found. Install .NET 8 SDK/runtime.");
            return false;
        }

        HMODULE module = LoadLibraryW(hostfxr.wstring().c_str());
        if (module == nullptr)
        {
            SetLastError("hostfxr.dll could not be loaded: " +
                hostfxr.generic_u8string());
            return false;
        }

        hostfxr_library_ = module;
        return true;
#endif
    }

    bool CSharpScriptBackend::LoadManagedApi()
    {
#ifndef _WIN32
        return false;
#else
        hostfxr_initialize_for_runtime_config_fn initialize =
            reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
                ProcAddress(hostfxr_library_, "hostfxr_initialize_for_runtime_config"));
        hostfxr_get_runtime_delegate_fn get_delegate =
            reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
                ProcAddress(hostfxr_library_, "hostfxr_get_runtime_delegate"));
        hostfxr_close_fn close =
            reinterpret_cast<hostfxr_close_fn>(
                ProcAddress(hostfxr_library_, "hostfxr_close"));

        if (initialize == nullptr || get_delegate == nullptr || close == nullptr)
        {
            SetLastError("hostfxr exports are incomplete.");
            return false;
        }

        const std::filesystem::path runtime_config =
            ManagedApiRuntimeConfigPathForMode(project_root_, packaged_mode_);
        if (!std::filesystem::exists(runtime_config))
        {
            SetLastError("Managed API runtimeconfig is missing: " +
                runtime_config.generic_u8string());
            return false;
        }

        void* context = nullptr;
        const std::int32_t init_result =
            initialize(runtime_config.wstring().c_str(), nullptr, &context);
        if (init_result < 0 || context == nullptr)
        {
            SetLastError("hostfxr_initialize_for_runtime_config failed: " +
                std::to_string(init_result));
            return false;
        }

        void* load_delegate = nullptr;
        const std::int32_t delegate_result = get_delegate(context,
            hdt_load_assembly_and_get_function_pointer, &load_delegate);
        close(context);

        if (delegate_result < 0 || load_delegate == nullptr)
        {
            SetLastError("hostfxr_get_runtime_delegate failed: " +
                std::to_string(delegate_result));
            return false;
        }

        load_assembly_and_get_function_pointer_ = load_delegate;
        return true;
#endif
    }

    bool CSharpScriptBackend::ResolveManagedEntryPoints()
    {
#ifndef _WIN32
        return false;
#else
        if (load_assembly_and_get_function_pointer_ == nullptr) return false;

        load_assembly_and_get_function_pointer_fn load_function =
            reinterpret_cast<load_assembly_and_get_function_pointer_fn>(
                load_assembly_and_get_function_pointer_);
        const std::filesystem::path assembly =
            ManagedApiAssemblyPathForMode(project_root_, packaged_mode_);
        const std::wstring assembly_path = assembly.wstring();
        const wchar_t* type_name = L"ReplayEngine.NativeBridge, RePlayEngine.Managed";
        const wchar_t* unmanaged_callers_only =
            reinterpret_cast<const wchar_t*>(static_cast<intptr_t>(-1));

        const auto resolve = [&](const wchar_t* method, void** target) -> bool
        {
            *target = nullptr;
            const int result = load_function(assembly_path.c_str(), type_name,
                method, unmanaged_callers_only, nullptr, target);
            if (result < 0 || *target == nullptr)
            {
                SetLastError("Managed API entry point was not resolved: " +
                    ToUtf8(std::wstring(method)) + " (" + std::to_string(result) + ")");
                return false;
            }
            return true;
        };

        return resolve(L"SetNativeApi", &set_native_api_) &&
            resolve(L"LoadAssembly", &load_assembly_) &&
            resolve(L"UnloadAssembly", &unload_assembly_) &&
            resolve(L"DescribeType", &describe_type_) &&
            resolve(L"CreateInstance", &create_instance_) &&
            resolve(L"DestroyInstance", &destroy_instance_) &&
            resolve(L"Invoke", &invoke_) &&
            resolve(L"SetField", &set_field_) &&
            resolve(L"GetField", &get_field_) &&
            resolve(L"SetTime", &set_time_) &&
            resolve(L"LiveInstanceCount", &live_instance_count_) &&
            resolve(L"LastError", &last_error_function_);
#endif
    }

    bool CSharpScriptBackend::SetNativeApi()
    {
        if (set_native_api_ == nullptr) return false;
        NativeApiTable table = MakeNativeApiTable();
        const int result = reinterpret_cast<set_native_api_fn>(set_native_api_)(&table);
        if (result == 0)
        {
            SetLastError("Managed API rejected native callback table.");
            return false;
        }
        return true;
    }

}
