#pragma once

#include "CSharpProject.h"
#include "../Core/ScriptBackend.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace ReplayEngine::Scripting::CSharp
{
    class CSharpScriptBackend final : public IScriptBackend
    {
    public:
        explicit CSharpScriptBackend(std::filesystem::path project_root);
        ~CSharpScriptBackend() override;

        CSharpScriptBackend(const CSharpScriptBackend&) = delete;
        CSharpScriptBackend& operator=(const CSharpScriptBackend&) = delete;

        ScriptLanguage Language() const noexcept override { return ScriptLanguage::CSharp; }
        const char* BackendName() const noexcept override { return "C# / hostfxr CoreCLR"; }

        bool Initialize() override;
        void Shutdown() override;
        bool Initialized() const noexcept override { return initialized_; }

        void BindRuntimeContext(Runtime::RuntimeContext* context) noexcept override;

        ScriptLoadResult LoadType(const ScriptTypeDescriptor& descriptor,
            std::uint32_t schema_revision) override;
        bool CanInstantiate(ScriptTypeID type_id) const override;

        ScriptInstanceHandle CreateInstance(const ScriptInstanceRequest& request) override;
        void DestroyInstance(ScriptInstanceHandle instance) override;

        ScriptInvokeResult Invoke(ScriptInstanceHandle instance,
            ScriptCallback callback, const ScriptArguments& arguments) override;

        bool SetField(ScriptInstanceHandle instance,
            const std::string& saved_name, const ScriptValue& value) override;
        bool GetField(ScriptInstanceHandle instance,
            const std::string& saved_name, ScriptValue& out) const override;

        std::size_t LiveInstanceCount() const noexcept override;

        const std::string& LastErrorMessage() const noexcept override { return last_error_; }
        const std::string& LastErrorFile() const noexcept override { return last_error_file_; }
        int LastErrorLine() const noexcept override { return last_error_line_; }

        bool CompileAndReload(CSharpBuildResult* out_build = nullptr);
        bool ReloadLastBuiltAssembly();
        const CSharpBuildResult& LastBuildResult() const noexcept { return last_build_; }
        bool AssemblyLoaded() const noexcept { return assembly_loaded_; }

    private:
        struct TypeState final
        {
            ScriptFieldSchemaRef schema;
            std::unordered_map<std::string, ScriptValueType> field_types;
        };

        bool LoadHost();
        bool LoadManagedApi();
        bool ResolveManagedEntryPoints();
        bool SetNativeApi();
        bool LoadGameAssembly(const std::filesystem::path& assembly_path,
            std::string& output);
        bool UnloadGameAssembly(std::string& output);

        void RefreshLastError() const;
        void SetLastError(std::string message,
            std::string file = std::string(), int line = 0) const;

        std::filesystem::path project_root_;
        Runtime::RuntimeContext* runtime_context_ = nullptr;

        void* hostfxr_library_ = nullptr;
        void* host_context_ = nullptr;
        void* load_assembly_and_get_function_pointer_ = nullptr;

        void* set_native_api_ = nullptr;
        void* load_assembly_ = nullptr;
        void* unload_assembly_ = nullptr;
        void* describe_type_ = nullptr;
        void* create_instance_ = nullptr;
        void* destroy_instance_ = nullptr;
        void* invoke_ = nullptr;
        void* set_field_ = nullptr;
        void* get_field_ = nullptr;
        void* set_time_ = nullptr;
        void* live_instance_count_ = nullptr;
        void* last_error_function_ = nullptr;

        std::unordered_map<ScriptTypeID, TypeState> type_states_;
        std::unordered_map<ScriptInstanceHandle, ScriptTypeID> instance_types_;

        CSharpBuildResult last_build_;
        mutable std::string last_error_;
        mutable std::string last_error_file_;
        mutable int last_error_line_ = 0;

        bool initialized_ = false;
        bool assembly_loaded_ = false;
    };
}
