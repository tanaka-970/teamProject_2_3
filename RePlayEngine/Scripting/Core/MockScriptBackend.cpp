#include "MockScriptBackend.h"

#include <algorithm>
#include <utility>

namespace ReplayEngine::Scripting
{
    namespace
    {
        int CallbackIndex(ScriptCallback callback) noexcept
        {
            const int index = static_cast<int>(callback);
            if (index < 0 || index >= script_callback_count) return 0;
            return index;
        }
    }

    MockScriptBackend::MockScriptBackend(ScriptLanguage language) noexcept
        : language_(language)
    {
        // 既定では全 Callback が「定義されている」ものとして扱う。
        for (bool& implemented : callback_implemented_) implemented = true;
    }

    MockScriptBackend::~MockScriptBackend()
    {
        Shutdown();
    }

    bool MockScriptBackend::Initialize()
    {
        initialized_ = true;
        return true;
    }

    void MockScriptBackend::Shutdown()
    {
        // Lua Registry 参照や GCHandle に相当するものを、ここで必ず空にする。
        instances_.clear();
        types_.clear();
        call_log_.clear();
        initialized_ = false;
    }

    // -----------------------------------------------------------------------
    // 型
    // -----------------------------------------------------------------------

    MockScriptBackend::TypeState& MockScriptBackend::EnsureType(ScriptTypeID type_id)
    {
        for (TypeState& state : types_)
        {
            if (state.type_id == type_id) return state;
        }

        TypeState state;
        state.type_id = type_id;
        types_.push_back(std::move(state));
        return types_.back();
    }

    const MockScriptBackend::TypeState* MockScriptBackend::FindType(
        ScriptTypeID type_id) const noexcept
    {
        for (const TypeState& state : types_)
        {
            if (state.type_id == type_id) return &state;
        }
        return nullptr;
    }

    void MockScriptBackend::SetTypeFields(ScriptTypeID type_id,
        std::vector<ScriptFieldDefinition> fields)
    {
        EnsureType(type_id).fields = std::move(fields);
    }

    void MockScriptBackend::SetTypeResolvable(ScriptTypeID type_id, bool resolvable)
    {
        EnsureType(type_id).resolvable = resolvable;
    }

    void MockScriptBackend::SetCallbackFails(ScriptCallback callback, bool fails)
    {
        callback_fails_[CallbackIndex(callback)] = fails;
    }

    void MockScriptBackend::SetCallbackImplemented(ScriptCallback callback, bool implemented)
    {
        callback_implemented_[CallbackIndex(callback)] = implemented;
    }

    ScriptLoadResult MockScriptBackend::LoadType(const ScriptTypeDescriptor& descriptor,
        std::uint32_t schema_revision)
    {
        if (!initialized_)
        {
            return ScriptLoadResult::Failure("Mock Backend が初期化されていません。");
        }
        if (descriptor.language != language_)
        {
            return ScriptLoadResult::Failure("この Backend が扱う言語ではありません。");
        }

        const TypeState* state = FindType(descriptor.type_id);
        if (state == nullptr)
        {
            // Field を 1 つも設定していない型は「空の Schema」として扱う。
            // 実行はできるが Inspector には何も出ない、という状態を再現する。
            return ScriptLoadResult::Success(
                ScriptFieldSchema::MakeEmpty(descriptor.type_id, schema_revision));
        }

        if (!state->resolvable)
        {
            // Lua ファイルが消えた / C# の Compile が通っていない状態の再現。
            last_error_ = "スクリプトを読み込めません（検証のため意図的に失敗させています）。";
            last_error_file_ = descriptor.script_name + DefaultScriptExtension(language_);
            last_error_line_ = 1;
            return ScriptLoadResult::Failure(last_error_, last_error_file_, last_error_line_);
        }

        std::vector<std::string> rejected;
        ScriptFieldSchemaRef schema = ScriptFieldSchema::Build(
            descriptor.type_id, schema_revision, state->fields, &rejected);

        return ScriptLoadResult::Success(std::move(schema));
    }

    bool MockScriptBackend::CanInstantiate(ScriptTypeID type_id) const
    {
        if (!initialized_) return false;

        const TypeState* state = FindType(type_id);
        // 未登録の型でも空の Schema として動かせるようにする。
        return state == nullptr || state->resolvable;
    }

    // -----------------------------------------------------------------------
    // インスタンス
    // -----------------------------------------------------------------------

    MockScriptBackend::InstanceState* MockScriptBackend::FindInstance(
        ScriptInstanceHandle handle) noexcept
    {
        for (InstanceState& state : instances_)
        {
            if (state.handle == handle) return &state;
        }
        return nullptr;
    }

    const MockScriptBackend::InstanceState* MockScriptBackend::FindInstance(
        ScriptInstanceHandle handle) const noexcept
    {
        for (const InstanceState& state : instances_)
        {
            if (state.handle == handle) return &state;
        }
        return nullptr;
    }

    ScriptInstanceHandle MockScriptBackend::CreateInstance(const ScriptInstanceRequest& request)
    {
        if (!initialized_) return invalid_script_instance_handle;
        if (!CanInstantiate(request.type_id)) return invalid_script_instance_handle;

        InstanceState state;
        state.handle = next_handle_++;
        state.type_id = request.type_id;
        state.object = request.owner_object;
        state.component = request.owner_component;

        const ScriptInstanceHandle handle = state.handle;
        instances_.push_back(std::move(state));
        ++created_count_;
        return handle;
    }

    void MockScriptBackend::DestroyInstance(ScriptInstanceHandle instance)
    {
        for (std::size_t index = 0; index < instances_.size(); ++index)
        {
            if (instances_[index].handle != instance) continue;

            instances_[index] = std::move(instances_.back());
            instances_.pop_back();
            ++destroyed_count_;
            return;
        }
    }

    ScriptInvokeResult MockScriptBackend::Invoke(ScriptInstanceHandle instance,
        ScriptCallback callback, const ScriptArguments& arguments)
    {
        if (!initialized_) return ScriptInvokeResult::BackendUnavailable;

        InstanceState* state = FindInstance(instance);
        if (state == nullptr) return ScriptInvokeResult::NoInstance;

        if (!callback_implemented_[CallbackIndex(callback)])
        {
            // 定義されていない Callback。エラーではない。
            return ScriptInvokeResult::NotImplemented;
        }

        // 呼ばれた順序をそのまま記録する。Validation はこれを読む。
        CallEntry entry;
        entry.instance = instance;
        entry.type = state->type_id;
        entry.object = state->object;
        entry.component = state->component;
        entry.callback = callback;
        entry.delta_time = arguments.delta_time;
        call_log_.push_back(entry);

        if (callback_fails_[CallbackIndex(callback)])
        {
            last_error_ = std::string(ToString(callback)) +
                " で検証用の実行時エラーを発生させました。";
            last_error_file_ = "MockScript" + std::string(DefaultScriptExtension(language_));
            last_error_line_ = 42;
            return ScriptInvokeResult::RuntimeError;
        }

        return ScriptInvokeResult::Ok;
    }

    bool MockScriptBackend::SetField(ScriptInstanceHandle instance,
        const std::string& saved_name, const ScriptValue& value)
    {
        InstanceState* state = FindInstance(instance);
        if (state == nullptr) return false;

        state->values.Set(saved_name, value);
        return true;
    }

    bool MockScriptBackend::GetField(ScriptInstanceHandle instance,
        const std::string& saved_name, ScriptValue& out) const
    {
        const InstanceState* state = FindInstance(instance);
        if (state == nullptr) return false;

        const ScriptValue* stored = state->values.Find(saved_name);
        if (stored == nullptr) return false;

        out = *stored;
        return true;
    }

    // -----------------------------------------------------------------------
    // 呼び出し記録
    // -----------------------------------------------------------------------

    bool MockScriptBackend::CallLogContainsInOrder(
        const std::vector<ScriptCallback>& sequence) const
    {
        std::size_t matched = 0;
        for (const CallEntry& entry : call_log_)
        {
            if (matched >= sequence.size()) break;
            if (entry.callback == sequence[matched]) ++matched;
        }
        return matched == sequence.size();
    }

    std::size_t MockScriptBackend::CountCalls(ScriptCallback callback) const noexcept
    {
        std::size_t count = 0;
        for (const CallEntry& entry : call_log_)
        {
            if (entry.callback == callback) ++count;
        }
        return count;
    }

    std::size_t MockScriptBackend::CountCalls(ScriptInstanceHandle instance,
        ScriptCallback callback) const noexcept
    {
        std::size_t count = 0;
        for (const CallEntry& entry : call_log_)
        {
            if (entry.instance == instance && entry.callback == callback) ++count;
        }
        return count;
    }

    // -----------------------------------------------------------------------
    // 検証用のスクリプト型
    // -----------------------------------------------------------------------

    namespace MockScriptTypes
    {
        const char* RotatingObjectAssetGUID() noexcept
        {
            return "a1b2c3d4e5f60718293a4b5c6d7e8f90";
        }

        ScriptTypeID RotatingObjectTypeID() noexcept
        {
            return MakeLuaScriptTypeID(RotatingObjectAssetGUID());
        }

        std::vector<ScriptFieldDefinition> RotatingObjectFields()
        {
            std::vector<ScriptFieldDefinition> fields;

            fields.push_back(
                ScriptFieldDefinition::Make("RotationSpeed", ScriptValueType::Float)
                .Default(ScriptValue::MakeFloat(90.0f))
                .Range(0.0, 720.0)
                .Tooltip("1 秒あたりの回転角（度）。"));

            fields.push_back(
                ScriptFieldDefinition::Make("LocalSpace", ScriptValueType::Bool)
                .Default(ScriptValue::MakeBool(true)));

            return fields;
        }

        std::vector<ScriptFieldDefinition> RotatingObjectFieldsV2()
        {
            std::vector<ScriptFieldDefinition> fields;

            // 据え置き。値が保たれること。
            fields.push_back(
                ScriptFieldDefinition::Make("RotationSpeed", ScriptValueType::Float)
                .Default(ScriptValue::MakeFloat(90.0f))
                .Range(0.0, 720.0));

            // bool -> int の型変更。ConvertTo で寄ること。
            fields.push_back(
                ScriptFieldDefinition::Make("LocalSpace", ScriptValueType::Int)
                .Default(ScriptValue::MakeInt(0)));

            // 新規追加。既定値で埋まること。
            fields.push_back(
                ScriptFieldDefinition::Make("SpinAxis", ScriptValueType::Vector3)
                .Default(ScriptValue::MakeVector3({ 0.0f, 1.0f, 0.0f })));

            return fields;
        }

        ScriptTypeDescriptor RotatingObject()
        {
            ScriptTypeDescriptor descriptor;
            descriptor.type_id = RotatingObjectTypeID();
            descriptor.language = ScriptLanguage::Lua;
            descriptor.script_name = "RotatingObject";
            descriptor.display_name = "Rotating Object";
            descriptor.asset_guid = RotatingObjectAssetGUID();
            return descriptor;
        }

        const char* DoorControllerAssetGUID() noexcept
        {
            return "b2c3d4e5f60718293a4b5c6d7e8f9011";
        }

        const char* DoorControllerClassName() noexcept
        {
            return "Game.DoorController";
        }

        ScriptTypeID DoorControllerTypeID() noexcept
        {
            return MakeCSharpScriptTypeID(DoorControllerAssetGUID(), DoorControllerClassName());
        }

        std::vector<ScriptFieldDefinition> DoorControllerFields()
        {
            std::vector<ScriptFieldDefinition> fields;

            fields.push_back(
                ScriptFieldDefinition::Make("OpenAngle", ScriptValueType::Float)
                .Default(ScriptValue::MakeFloat(90.0f))
                .Range(0.0, 180.0));

            fields.push_back(
                ScriptFieldDefinition::Make("Target", ScriptValueType::ObjectReference));

            return fields;
        }

        ScriptTypeDescriptor DoorController()
        {
            ScriptTypeDescriptor descriptor;
            descriptor.type_id = DoorControllerTypeID();
            descriptor.language = ScriptLanguage::CSharp;
            descriptor.script_name = "DoorController";
            descriptor.display_name = "Door Controller";
            descriptor.asset_guid = DoorControllerAssetGUID();
            descriptor.class_name = DoorControllerClassName();
            return descriptor;
        }

        std::vector<ScriptFieldDefinition> ReservedNameProbeFields()
        {
            // ユーザーが管理情報と同じ名前を使っても衝突しないことを確かめる。
            // 保存名は field.language / field.class_name / field.script_asset になる。
            std::vector<ScriptFieldDefinition> fields;

            fields.push_back(
                ScriptFieldDefinition::Make("language", ScriptValueType::String)
                .Default(ScriptValue::MakeString("nihongo")));

            fields.push_back(
                ScriptFieldDefinition::Make("class_name", ScriptValueType::String)
                .Default(ScriptValue::MakeString("MyClass")));

            fields.push_back(
                ScriptFieldDefinition::Make("script_asset", ScriptValueType::Int)
                .Default(ScriptValue::MakeInt(7)));

            fields.push_back(
                ScriptFieldDefinition::Make("execution_order", ScriptValueType::Float)
                .Default(ScriptValue::MakeFloat(1.5f)));

            return fields;
        }
    }
}
