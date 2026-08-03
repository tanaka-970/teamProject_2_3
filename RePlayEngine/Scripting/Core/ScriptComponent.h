#pragma once

#include "ScriptFieldSchema.h"
#include "ScriptLanguage.h"
#include "ScriptTypes.h"
#include "ScriptValue.h"
#include "../../Object/Component/Component.h"
#include "../../Reflection/Registry/TypeGUID.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ReplayEngine::Scripting
{
    class IScriptServices;
    struct ScriptTypeDescriptor;

    // GameObject へ Lua / C# のスクリプトを取り付ける Component。
    //
    // ---------------------------------------------------------------------
    // 【Lua 用と C# 用を分けない】
    //
    //   言語は Backend の違いでしかない。GameObject が所有する実体は
    //   常にこの 1 種類で、Scene の保存形式も Inspector の経路も 1 本にする。
    //   分けると、保存・複製・Undo・エラー処理を 2 組ずつ書くことになる。
    //
    // ---------------------------------------------------------------------
    // 【ここに Lua / C# 固有の処理を書かない】
    //
    //   このクラスが知っているのは次だけ。
    //     - どの ScriptTypeID か
    //     - その Schema（Field の顔ぶれ）
    //     - 自分の Field 値
    //     - 自分のインスタンス Handle（ただの整数）
    //
    //   Lua State も managed オブジェクトも Backend の具象型も持たない。
    //   実行は IScriptServices 越しに Handle を渡すだけ。
    //
    // ---------------------------------------------------------------------
    // 【保存するもの / しないもの】
    //
    //   保存する（PropertyRegistry の静的登録 + Schema 由来の動的登録）
    //     __script.language / __script.asset / __script.class
    //     __script.execution_order / __script.type_id
    //     field.<ユーザーが宣言した名前>
    //
    //   保存しない（実行時専用）
    //     Schema への shared_ptr / ScriptInstanceHandle / 状態 / 直近エラー
    //
    // ---------------------------------------------------------------------
    // 【Field 値が失われない仕組み】
    //
    //   Schema がまだ解決できていない間（Lua ファイルが無い / C# が未 Compile /
    //   Edit Mode で Backend 未起動）、読み込んだ Field 値は pending_values_ へ
    //   そのまま預かる。保存時はそれをそのまま書き戻すので、
    //   「開いて保存しただけで設定値が消える」ことが起きない。
    //   Schema が解決できたら照合して流し込む。
    //
    //   Component::RetainUnknownProperties とは併用しない。二重管理になるため。
    class ScriptComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(ScriptComponent)

    public:
        // 型名を変えても Scene の参照が切れないようにするための固定値。
        // 一度決めたら二度と変えないこと。
        static constexpr Reflection::TypeGUID StaticTypeGUID() noexcept
        {
            return Reflection::MakeTypeGUID("7a3e1c94b06d4f28ae5137c0d9b28e41");
        }

        static constexpr const char* module_id = "RePlayEngine.Scripting";

        ScriptComponent() = default;
        ~ScriptComponent() override;

        // 型 ID を照合してから降りる。dynamic_cast を使わないのは、
        // Schema の getter / setter から毎回呼ばれる経路のため。
        static ScriptComponent* From(Core::Component& component) noexcept;
        static const ScriptComponent* From(const Core::Component& component) noexcept;

        // ---- 保存する設定 ---------------------------------------------------

        ScriptLanguage Language() const noexcept { return language_; }
        void SetLanguage(ScriptLanguage value);

        const std::string& ScriptAssetGUID() const noexcept { return asset_guid_; }
        void SetScriptAssetGUID(std::string value);

        // C# の完全修飾クラス名。Lua では空。
        const std::string& ClassName() const noexcept { return class_name_; }
        void SetClassName(std::string value);

        // Catalog に載っている Script Type をこの Component へ割り当てる。
        // Add Component から「Rotating Object」などを選んだときの入口。
        void AssignScriptType(const ScriptTypeDescriptor& descriptor);

        // 同一フレーム内の呼び出し順のヒント。
        //
        // Phase 1 では保存・表示だけで、実際のソートは行わない。
        // BehaviourComponent::execution_order と同じ扱いで、
        // Scene 全体の実行順対応は別作業として分離する。
        std::int32_t ExecutionOrder() const noexcept { return execution_order_; }
        void SetExecutionOrder(std::int32_t value) { execution_order_ = value; }

        // 現在のスクリプト型。asset_guid_ と class_name_ から導かれる。
        //
        // 保存もするのは、Asset が一時的に見つからない状態でも
        // 「どのスクリプト型だったか」を保てるようにするため。
        ScriptTypeID ScriptType() const noexcept { return script_type_; }

        // 保存値からの復元専用。導出結果より保存値を優先したい場合に使う。
        void RestoreScriptType(ScriptTypeID value);

        // ---- Field の読み書き（Schema の getter / setter から呼ばれる） ------
        //
        // saved_name は "field.RotationSpeed" 形式。

        ScriptValue ReadField(const std::string& saved_name) const;
        void WriteField(const std::string& saved_name, const ScriptValue& value);

        const ScriptFieldStorage& FieldValues() const noexcept { return field_values_; }
        const ScriptFieldStorage& PendingValues() const noexcept { return pending_values_; }

        // ---- Schema ---------------------------------------------------------

        const ScriptFieldSchemaRef& Schema() const noexcept { return schema_; }

        // Schema を差し替え、Field 値を新しい顔ぶれへ移送する。
        //
        // 呼んでよいのは ScriptRuntime の同期点だけ。
        // Inspector 描画中や Serialize 中に呼ぶと、
        // DynamicProperties() が返した配列の足元が崩れる。
        void BindSchema(ScriptFieldSchemaRef schema);

        // Schema を Catalog から引き直す。まだ無ければ何もしない。
        // 戻り値は「Schema を持っているか」。
        bool ResolveSchema();

        // ---- 動的プロパティ（案 A） -----------------------------------------
        //
        // Schema が持つ共有配列をそのまま指す。コピーしない。
        // 同じ ScriptTypeID の全インスタンスが同一のポインタを返す。
        const std::vector<Reflection::PropertyDesc>* DynamicProperties() const noexcept override;

        // Awake を「所有 GameObject が有効になってから」へ遅らせる。
        //
        // 既定（false）のままだと、Inactive な GameObject へ置いただけの
        // スクリプトにも Scene 開始時点で Awake が届く。
        // ユーザーが書くスクリプトでは Unity と同じ挙動の方が予測しやすい。
        //
        // 既存の Component / Behaviour は false のままなので影響を受けない。
        bool DeferAwakeUntilObjectActive() const noexcept override { return true; }

        // ---- 実行時の状態（保存しない） --------------------------------------

        ScriptStatus Status() const noexcept { return status_; }
        const std::string& LastError() const noexcept { return last_error_; }
        ScriptInstanceHandle InstanceHandle() const noexcept { return instance_; }
        bool HasInstance() const noexcept { return instance_ != invalid_script_instance_handle; }

        // Inspector のヘッダーに出す名前。
        //   Catalog に登録済み … "Rotating Object"
        //   未解決            … "Script (未解決)"
        //   未設定            … "Script"
        std::string DisplayLabel() const;

        // 直近のエラーを差し替える。ScriptRuntime が読み込み失敗を伝えるのに使う。
        void SetStatus(ScriptStatus status, std::string error_text = std::string());

    private:
        // ---- Component のライフサイクル --------------------------------------

        void OnAttach() override;
        void OnRuntimeAwake() override;
        void OnEnable() override;
        void OnStart() override;
        void OnFixedUpdate(float fixed_delta_time) override;
        void OnUpdate(float delta_time) override;
        void OnLateUpdate(float delta_time) override;
        void OnDisable() override;
        void OnRuntimeDestroy() override;
        void OnDetach() override;

        // ---- 保存 -------------------------------------------------------------

        void OnSerialize(Reflection::PropertyBag& output) const override;
        void OnDeserialize(const Reflection::PropertyBag& input) override;
        void OnPropertyChanged(const char* property_name) override;

        // ---- 内部 -------------------------------------------------------------

        IScriptServices* Services() const noexcept;

        // asset_guid_ / class_name_ / language_ から script_type_ を作り直す。
        void RefreshScriptType();

        // インスタンスを作って Field を流し込み、Awake まで通す。
        void CreateInstanceAndAwake();

        // インスタンスを片付ける。ユーザーの OnDestroy は
        // 「インスタンスがある場合だけ」呼ぶ。
        void DestroyInstanceIfAny();

        // Callback を 1 つ呼ぶ。インスタンスが無ければ何もしない。
        void InvokeCallback(ScriptCallback callback, const ScriptArguments& arguments);

        // field_values_ の全件をインスタンスへ流し込む。
        void PushAllFields();

        // ---- 保存する設定 -----------------------------------------------------

        ScriptLanguage language_ = ScriptLanguage::Lua;
        std::string asset_guid_;
        std::string class_name_;
        std::int32_t execution_order_ = 0;
        ScriptTypeID script_type_;

        // Schema が知っている Field の値。キーは保存名。
        ScriptFieldStorage field_values_;

        // Schema がまだ知らない（または解決前の）値の預かり箱。
        // 保存時にそのまま書き戻すので、値が失われない。
        ScriptFieldStorage pending_values_;

        // ---- 実行時専用（保存しない） ----------------------------------------

        ScriptFieldSchemaRef schema_;
        ScriptInstanceHandle instance_ = invalid_script_instance_handle;
        ScriptStatus status_ = ScriptStatus::Unassigned;
        std::string last_error_;

        // ScriptWorld へ登録済みか。二重登録・登録漏れを防ぐ。
        bool registered_ = false;
    };
}
