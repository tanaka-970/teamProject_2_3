#pragma once

#include "ScriptBackend.h"
#include "ScriptFieldSchema.h"
#include "ScriptTypeCatalog.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ReplayEngine::Scripting
{
    // Phase 1 の検証用 Backend。Lua も .NET も使わない。
    //
    // ---------------------------------------------------------------------
    // 【なぜ Mock が要るか】
    //
    //   共通基盤（Schema の共有 / 保存 / Clone / Undo / ライフサイクル順序 /
    //   型が解決できないときの値の保護）は、Lua や C# の有無と関係なく
    //   成立していなければならない。
    //
    //   Mock で先に固めておけば、Phase 2 以降で Lua が落ちたときに
    //   「Lua の問題か、基盤の問題か」を切り分けられる。
    //
    // ---------------------------------------------------------------------
    // 本番ビルドにも入るが、ScriptRuntime へ挿さなければ何も起きない。
    // 挿すのは Validation と、Editor の検証メニューだけ。
    class MockScriptBackend final : public IScriptBackend
    {
    public:
        explicit MockScriptBackend(ScriptLanguage language) noexcept;
        ~MockScriptBackend() override;

        // ---- IScriptBackend ---------------------------------------------------

        ScriptLanguage Language() const noexcept override { return language_; }
        const char* BackendName() const noexcept override { return "Mock"; }

        bool Initialize() override;
        void Shutdown() override;
        bool Initialized() const noexcept override { return initialized_; }

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

        std::size_t LiveInstanceCount() const noexcept override { return instances_.size(); }

        const std::string& LastErrorMessage() const noexcept override { return last_error_; }
        const std::string& LastErrorFile() const noexcept override { return last_error_file_; }
        int LastErrorLine() const noexcept override { return last_error_line_; }

        // ---- 検証用の操作 ------------------------------------------------------

        // 型の Field 構成を差し替える。次の LoadType から効く。
        void SetTypeFields(ScriptTypeID type_id, std::vector<ScriptFieldDefinition> fields);

        // 型を一時的に解決不能にする。LoadType が失敗し、
        // Catalog は最後に成功した Schema を保ったままになる。
        void SetTypeResolvable(ScriptTypeID type_id, bool resolvable);

        // 指定した Callback で必ず失敗させる。エラー処理の検証に使う。
        void SetCallbackFails(ScriptCallback callback, bool fails);

        // その Callback を「未定義」として扱う。NotImplemented が返る。
        void SetCallbackImplemented(ScriptCallback callback, bool implemented);

        // ---- 呼び出し記録 ------------------------------------------------------

        struct CallEntry
        {
            ScriptInstanceHandle instance = invalid_script_instance_handle;
            ScriptTypeID type;
            Core::ObjectID object;
            Core::ComponentStableID component = Core::invalid_component_stable_id;
            ScriptCallback callback = ScriptCallback::Awake;
            float delta_time = 0.0f;
        };

        const std::vector<CallEntry>& CallLog() const noexcept { return call_log_; }
        void ClearCallLog() noexcept { call_log_.clear(); }

        // 「Awake, OnEnable, Start」のような並びが、この順に含まれているか。
        bool CallLogContainsInOrder(const std::vector<ScriptCallback>& sequence) const;

        // 指定 Callback の呼び出し回数。
        std::size_t CountCalls(ScriptCallback callback) const noexcept;
        std::size_t CountCalls(ScriptInstanceHandle instance, ScriptCallback callback) const noexcept;

        // 生成・破棄の累計。対になっているかの検証に使う。
        std::uint64_t CreatedCount() const noexcept { return created_count_; }
        std::uint64_t DestroyedCount() const noexcept { return destroyed_count_; }

    private:
        struct TypeState
        {
            ScriptTypeID type_id;
            std::vector<ScriptFieldDefinition> fields;
            bool resolvable = true;
        };

        struct InstanceState
        {
            ScriptInstanceHandle handle = invalid_script_instance_handle;
            ScriptTypeID type_id;
            Core::ObjectID object;
            Core::ComponentStableID component = Core::invalid_component_stable_id;
            ScriptFieldStorage values;
        };

        TypeState& EnsureType(ScriptTypeID type_id);
        const TypeState* FindType(ScriptTypeID type_id) const noexcept;

        InstanceState* FindInstance(ScriptInstanceHandle handle) noexcept;
        const InstanceState* FindInstance(ScriptInstanceHandle handle) const noexcept;

        ScriptLanguage language_ = ScriptLanguage::Lua;

        std::vector<TypeState> types_;
        std::vector<InstanceState> instances_;
        std::vector<CallEntry> call_log_;

        // 再利用しない。破棄した Handle が別の実体を掴むことがない。
        ScriptInstanceHandle next_handle_ = 1;

        bool callback_fails_[script_callback_count]{};
        bool callback_implemented_[script_callback_count]{};

        std::string last_error_;
        std::string last_error_file_;
        int last_error_line_ = 0;

        std::uint64_t created_count_ = 0;
        std::uint64_t destroyed_count_ = 0;

        bool initialized_ = false;
    };

    // 検証で使う 2 種類のスクリプト型。
    //
    // Field 構成をわざと変えてある。
    // 「同じ ScriptComponent 型でも、Script Type ごとに違う Field が
    //   表示・保存される」ことを確かめるため。
    namespace MockScriptTypes
    {
        // Lua 相当。AssetGUID がそのまま ScriptTypeID になる。
        const char* RotatingObjectAssetGUID() noexcept;
        ScriptTypeID RotatingObjectTypeID() noexcept;
        ScriptTypeDescriptor RotatingObject();

        //   RotationSpeed : float   default 90.0  range 0..720
        //   LocalSpace    : bool    default true
        std::vector<ScriptFieldDefinition> RotatingObjectFields();

        // 差し替え検証用。Field を 1 つ増やし、1 つの型を変え、1 つ消した版。
        //   RotationSpeed : float  （据え置き）
        //   LocalSpace    : int    （bool から型変更）
        //   SpinAxis      : vec3   （新規）
        //   ※ 旧 LocalSpace の bool 値は ConvertTo で寄る
        std::vector<ScriptFieldDefinition> RotatingObjectFieldsV2();

        // C# 相当。Asset GUID + 完全修飾クラス名から導出する。
        const char* DoorControllerAssetGUID() noexcept;
        const char* DoorControllerClassName() noexcept;
        ScriptTypeID DoorControllerTypeID() noexcept;
        ScriptTypeDescriptor DoorController();

        //   OpenAngle : float             default 90.0  range 0..180
        //   Target    : GameObject 参照   default 無効
        std::vector<ScriptFieldDefinition> DoorControllerFields();

        // 予約接頭辞の衝突検証用。
        // language / class_name / script_asset という名前の Field を宣言する。
        // 保存名は field.language などになり、__script.language とは衝突しない。
        std::vector<ScriptFieldDefinition> ReservedNameProbeFields();
    }
}
