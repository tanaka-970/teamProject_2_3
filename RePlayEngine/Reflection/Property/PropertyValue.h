#pragma once

#include "References.h"
#include "../../Core/ObjectID/ObjectID.h"

#include <DirectXMath.h>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace ReplayEngine::Reflection
{
    // Property が取り得る型。
    //
    // 保存形式とも Editor の描画方法とも 1 対 1 で対応させる。
    // Vector4 / Quaternion / Color は内部表現がどれも XMFLOAT4 だが、
    // Editor での描き方（カラーピッカーか数値 4 つか）と保存時の意味が違うので型を分ける。
    //
    // 【追加時の約束】
    //   末尾へ足すこと。Scene ファイルへ書かれるのは下の文字列名なので
    //   enum の並び順は保存互換に影響しないが、Editor 側の表があるため
    //   途中へ挿入すると差分が読みにくくなる。
    enum class PropertyType
    {
        Bool,
        Int,
        Float,
        Double,
        String,
        Vector2,
        Vector3,
        Vector4,
        Quaternion,
        Color,
        Enum,            // 内部は int。表示は PropertyDesc の enum_labels を使う
        AssetPath,       // 内部は string。旧形式。プロジェクト相対パス
        ObjectReference, // 内部は ObjectID。GameObject 間参照

        // 以下 3 つは内部表現がすべて int だが、Inspector での描き方が違う。
        // 整数を直接いじらせないために型を分けている。
        CollisionLayer,    // 内部は int。Layer 名の一覧から選ぶ
        CollisionMask,     // 内部は int のビット列。Layer 名のチェックボックス
        ColliderReference, // 内部は int（collider_key）。同じ GameObject の Collider を選ぶ

        // ---- v11 で追加 ---------------------------------------------------
        //
        // 将来 C# の field をそのまま保存できるようにするための拡張。
        // C# 側の byte / sbyte / short / ushort / int / uint / long / ulong は
        // Int64 / UInt64 へ広げて保持する。情報を失わずに 1 往復できる。
        Int64,             // 内部は std::int64_t
        UInt64,            // 内部は std::uint64_t

        // AssetPath との違い: こちらは AssetGUID を保存する。
        // Asset を Rename / Move しても参照が切れない。
        AssetReference,    // 内部は string (AssetGUID)
        SceneReference,    // 内部は string (Scene AssetGUID)

        // 所有 ObjectID + GameObject 内で安定した ComponentStableID の組。
        ComponentReference,

        // 同じ型の値の並び。要素型は PropertyValue 側が保持する。
        //
        // Dictionary と入れ子の構造体は今回未対応。
        // 対応するときは要素として Array を入れ子にできる形へ広げる想定で、
        // 「要素型 + 要素列」という入れ物の形は変えずに済むようにしてある。
        Array,
    };

    const char* ToString(PropertyType type) noexcept;
    bool TryParsePropertyType(const std::string& text, PropertyType& out) noexcept;

    // 値を持たない入れ物型かどうか（Array のみ）。
    // 保存処理が「単一値として書けるか」を判断するのに使う。
    bool IsContainerType(PropertyType type) noexcept;

    // Property 1 つ分の値。
    //
    // Component の実データそのものではなく、
    // 「Component <-> Editor」「Component <-> Scene ファイル」を行き来する際の
    // 受け渡し用の箱として使う。
    class PropertyValue final
    {
    public:
        using Storage = std::variant<
            bool,
            int,
            std::int64_t,
            std::uint64_t,
            float,
            double,
            std::string,
            DirectX::XMFLOAT2,
            DirectX::XMFLOAT3,
            DirectX::XMFLOAT4,
            Core::ObjectID,
            ComponentReference>;

        // 特殊メンバは .cpp で定義する。
        // 配列要素として std::vector<PropertyValue> を持つため、
        // 自分自身がまだ不完全型である位置で暗黙生成されないようにするため。
        PropertyValue();
        ~PropertyValue();
        PropertyValue(const PropertyValue& other);
        PropertyValue(PropertyValue&& other) noexcept;
        PropertyValue& operator=(const PropertyValue& other);
        PropertyValue& operator=(PropertyValue&& other) noexcept;

        static PropertyValue MakeBool(bool value);
        static PropertyValue MakeInt(int value);
        static PropertyValue MakeInt64(std::int64_t value);
        static PropertyValue MakeUInt64(std::uint64_t value);
        static PropertyValue MakeFloat(float value);
        static PropertyValue MakeDouble(double value);
        static PropertyValue MakeString(std::string value);
        static PropertyValue MakeVector2(const DirectX::XMFLOAT2& value);
        static PropertyValue MakeVector3(const DirectX::XMFLOAT3& value);
        static PropertyValue MakeVector4(const DirectX::XMFLOAT4& value);
        static PropertyValue MakeQuaternion(const DirectX::XMFLOAT4& value);
        static PropertyValue MakeColor(const DirectX::XMFLOAT4& value);
        static PropertyValue MakeEnum(int value);
        static PropertyValue MakeAssetPath(std::string value);
        static PropertyValue MakeObjectReference(Core::ObjectID value);
        static PropertyValue MakeCollisionLayer(int value);
        static PropertyValue MakeCollisionMask(int value);
        static PropertyValue MakeColliderReference(int value);

        // ---- v11 -----------------------------------------------------------

        static PropertyValue MakeAssetReference(std::string guid);
        static PropertyValue MakeSceneReference(std::string guid);
        static PropertyValue MakeComponentReference(const ComponentReference& value);

        // 要素型を明示して配列を作る。
        // 要素の型が element_type と違う場合、その要素は取り出し時に既定値へ倒れる。
        static PropertyValue MakeArray(PropertyType element_type,
            std::vector<PropertyValue> elements);

        PropertyType Type() const noexcept { return type_; }

        // 型が違う場合は既定値を返す。壊れた Scene ファイルを読んでも例外を投げない。
        bool AsBool(bool fallback = false) const noexcept;
        int AsInt(int fallback = 0) const noexcept;
        std::int64_t AsInt64(std::int64_t fallback = 0) const noexcept;
        std::uint64_t AsUInt64(std::uint64_t fallback = 0) const noexcept;
        float AsFloat(float fallback = 0.0f) const noexcept;
        double AsDouble(double fallback = 0.0) const noexcept;
        const std::string& AsString() const noexcept;
        DirectX::XMFLOAT2 AsVector2() const noexcept;
        DirectX::XMFLOAT3 AsVector3() const noexcept;
        DirectX::XMFLOAT4 AsVector4() const noexcept;
        Core::ObjectID AsObjectReference() const noexcept;
        ComponentReference AsComponentReference() const noexcept;
        AssetReference AsAssetReference() const;
        SceneReference AsSceneReference() const;

        // ---- 配列 -----------------------------------------------------------

        bool IsArray() const noexcept { return type_ == PropertyType::Array; }
        PropertyType ArrayElementType() const noexcept { return array_element_type_; }
        const std::vector<PropertyValue>& ArrayElements() const noexcept
        {
            return array_elements_;
        }

        // 数値が有限かどうか。NaN / Infinity を保存前に検出するために使う。
        // 数値以外の型では常に true。配列は全要素を再帰的に確かめる。
        bool IsFinite() const noexcept;

        // 型が一致しないときに、意味を保ったまま寄せられる範囲で変換する。
        // Scene ファイル側でプロパティの型が変わった場合の救済に使う。
        // 変換できない組み合わせでは false を返し、呼び出し側が既定値を維持できるようにする。
        bool ConvertTo(PropertyType target, PropertyValue& out) const;

        // 型ごとの規則で a と b を混ぜる。
        // t は 0..1 に丸めない。型が食い違う場合は a を返し、例外は投げない。
        static PropertyValue Lerp(const PropertyValue& a, const PropertyValue& b, float t);

    private:
        PropertyType type_ = PropertyType::Bool;
        Storage storage_{ false };

        // 配列専用。Array 以外では常に空。
        //
        // variant の選択肢にせず独立したメンバにしている理由:
        //   variant の選択肢へ自分自身を含む vector を入れると、
        //   型の完全性の要求が処理系ごとに微妙に変わる。
        //   独立したメンバなら「vector は不完全型を要素にできる」という
        //   C++17 の保証だけに乗れる。
        PropertyType array_element_type_ = PropertyType::Bool;
        std::vector<PropertyValue> array_elements_;
    };

    // 値が等しいかどうか。
    //
    // ここに 1 つだけ置く理由:
    //   以前は Prefab の override 検出と Inspector の差分表示が、
    //   それぞれ自前の switch で同じ判定を書いていた。
    //   PropertyType を 1 つ足すたびに 2 か所を直す必要があり、
    //   片方を直し忘れると「override が検出されない」という
    //   気づきにくい壊れ方をする。
    //
    // 浮動小数点は完全一致ではなく許容差で比べる。
    // 保存・読み込みを往復した値が、丸めのせいで override 扱いになるのを防ぐため。
    bool ValuesEqual(const PropertyValue& a, const PropertyValue& b) noexcept;
}
