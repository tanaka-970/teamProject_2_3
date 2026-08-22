#pragma once

#include "ScriptTypes.h"
#include "ScriptValue.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ReplayEngine::Scripting
{
    // スクリプトが宣言した公開変数 1 つ分の定義。
    //
    // PropertyDesc と分けてある理由:
    //   PropertyDesc は「Component のメンバへの getter / setter」を持つ実行用の形で、
    //   既定値を持たない（C++ ではメンバの初期化子が既定値なので不要だった）。
    //   スクリプトには "default = 90.0" を保持する場所が要る。
    //
    //   PropertyDesc へ default_value を足す手もあるが、
    //   既存の全 Component が使わないメンバを 1 つ抱えることになるので、
    //   スクリプト側の Schema が持つ形にした。
    struct ScriptFieldDefinition final
    {
        // スクリプトが書いた名前。保存名は "field." + これ。
        std::string name;

        // Inspector 表示名。空なら HumanizeFieldName(name) を使う。
        std::string display_name;
        std::string tooltip;

        ScriptValueType type = ScriptValueType::Float;

        // 未設定のときは type の既定値（0 / false / 空文字 / 無効 ObjectID）になる。
        ScriptValue default_value;

        bool has_range = false;
        double minimum = 0.0;
        double maximum = 0.0;

        bool visible_in_inspector = true;
        bool serializable = true;
        bool read_only = false;

        // AssetReference のときに Picker で絞り込む Asset 種別名。空なら問わない。
        std::string asset_type;

        // Inspector の折り畳み見出し。空なら Component 直下へ並べる。
        std::string category;

        // Enum のときの表示ラベル。添字が値に対応する。
        std::vector<std::string> enum_labels;

        // ---- 組み立てを短くするための連結設定（C++17。指定イニシャライザは使わない） ----

        static ScriptFieldDefinition Make(std::string field_name, ScriptValueType field_type)
        {
            ScriptFieldDefinition definition;
            definition.name = std::move(field_name);
            definition.type = field_type;
            return definition;
        }

        ScriptFieldDefinition& Display(std::string value)
        {
            display_name = std::move(value);
            return *this;
        }

        ScriptFieldDefinition& Tooltip(std::string value)
        {
            tooltip = std::move(value);
            return *this;
        }

        ScriptFieldDefinition& Default(ScriptValue value)
        {
            default_value = std::move(value);
            return *this;
        }

        ScriptFieldDefinition& Range(double low, double high)
        {
            has_range = true;
            minimum = low;
            maximum = high;
            return *this;
        }

        ScriptFieldDefinition& HiddenInInspector()
        {
            visible_in_inspector = false;
            return *this;
        }

        ScriptFieldDefinition& NotSerializable()
        {
            serializable = false;
            return *this;
        }

        ScriptFieldDefinition& ReadOnly()
        {
            read_only = true;
            return *this;
        }

        ScriptFieldDefinition& OfAssetType(std::string kind)
        {
            asset_type = std::move(kind);
            return *this;
        }

        // Inspector に出す名前。display_name が空なら name を機械的に整形する。
        std::string ResolvedDisplayName() const;

        // 保存名。"field." + name。
        std::string SavedName() const;
    };

    // 1 つのスクリプト型の Field 構成。
    //
    // ---------------------------------------------------------------------
    // 【インスタンスごとに作らないこと】
    //
    //   同じ RotatingObject を 1000 体の GameObject へ付けたとき、
    //   PropertyDesc（std::function を 2 つ持つ）を 1000 セット確保すると
    //   構築コストもメモリも無駄になる。
    //
    //   そこで Descs() が持つ getter / setter は
    //   「インスタンスを捕捉せず、保存名だけを捕捉して
    //     引数の Component& から ScriptComponent へ降りて名前で引く」形にしてある。
    //   これにより Schema は ScriptTypeID ごとに 1 個で足り、
    //   全インスタンスが同じ実体を共有できる。
    //
    //   共有できていることは --validate-script-core で
    //   「100 体の DynamicProperties() が同一ポインタを返す」形で検査する。
    //
    // ---------------------------------------------------------------------
    // 【差し替えについて】
    //
    //   ホットリロードでは新しい Schema を作って shared_ptr を差し替える。
    //   既存の Schema は書き換えない（const）。
    //   使用中のインスタンスが古い Schema を参照していても、
    //   参照が切れるまで実体が生き続けるので、描画中に足元が崩れない。
    //
    //   差し替えを行ってよいのは ScriptRuntime の同期点だけ。
    //   Inspector 描画中や Serialize 中には行わない。
    class ScriptFieldSchema final
    {
    public:
        // 空の Schema。Field を 1 つも持たないスクリプト用。
        static std::shared_ptr<const ScriptFieldSchema> MakeEmpty(
            ScriptTypeID type_id, std::uint32_t revision);

        // Field 定義から Schema を作る。
        //
        // 弾く条件（どれも「捨てる」ではなく「その Field だけ落とす」）:
        //   - 名前が空
        //   - 名前が重複している（先勝ち）
        //   - 名前へ予約接頭辞が含まれている
        //   - 配列などのコンテナ型（初期版では未対応）
        // 落とした件数と理由は rejected へ入る。呼び出し側が診断へ出す。
        static std::shared_ptr<const ScriptFieldSchema> Build(
            ScriptTypeID type_id, std::uint32_t revision,
            std::vector<ScriptFieldDefinition> fields,
            std::vector<std::string>* rejected = nullptr);

        ScriptTypeID TypeID() const noexcept { return type_id_; }

        // 差し替えのたびに 1 増える。値の移送が必要かの判断に使う。
        std::uint32_t Revision() const noexcept { return revision_; }

        const std::vector<ScriptFieldDefinition>& Fields() const noexcept { return fields_; }

        // Component::DynamicProperties() がそのまま返す配列。
        // fields_ と同じ並び・同じ件数。
        const std::vector<Reflection::PropertyDesc>& Descs() const noexcept { return descs_; }

        std::size_t FieldCount() const noexcept { return fields_.size(); }
        bool Empty() const noexcept { return fields_.empty(); }

        // "RotationSpeed" で引く。
        const ScriptFieldDefinition* FindField(std::string_view name) const noexcept;

        // "field.RotationSpeed" で引く。
        const ScriptFieldDefinition* FindBySavedName(std::string_view saved_name) const noexcept;

        // 既定値をすべて詰めた PropertyBag を作る。保存名がキーになる。
        // 新規追加した ScriptComponent の初期値と、値の移送で埋まらなかった分に使う。
        ScriptFieldStorage MakeDefaultStorage() const;

        // 型に応じた既定値。ScriptFieldDefinition::default_value が未設定のときに使う。
        static ScriptValue MakeTypeDefault(ScriptValueType type);

    private:
        ScriptFieldSchema() = default;

        void BuildDescs();

        ScriptTypeID type_id_;
        std::uint32_t revision_ = 0;

        std::vector<ScriptFieldDefinition> fields_;
        std::vector<Reflection::PropertyDesc> descs_;
    };

    using ScriptFieldSchemaRef = std::shared_ptr<const ScriptFieldSchema>;
}
