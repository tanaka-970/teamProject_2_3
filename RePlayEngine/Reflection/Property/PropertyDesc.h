#pragma once

#include "PropertyValue.h"
#include "../Registry/TypeGUID.h"
#include "../../Object/Component/Component.h"

#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ReplayEngine::Reflection
{
    // Timeline から動かせるかどうか。
    //
    // Sprite や Text のようにキーは打ちたいが補間できない値があるため、
    // bool ではなく 3 値にする。Motion Editor はこの値だけを見て、
    // Ease の表示可否を判断できる。
    enum class Animatable
    {
        None,
        Step,
        Interpolatable,
    };

    namespace Detail
    {
        constexpr Animatable DefaultAnimatableFor(PropertyType type) noexcept;
    }

    // Component のプロパティ 1 つ分の定義。
    //
    // ここに登録した内容は Inspector の描画と Scene の保存の「両方」から参照される。
    // 表示用と保存用に別々の定義を書かなくてよいようにするのがこのクラスの目的。
    class PropertyDesc final
    {
    public:
        using Getter = std::function<PropertyValue(const Core::Component&)>;
        using Setter = std::function<void(Core::Component&, const PropertyValue&)>;

        // Scene ファイルのキー。C++ のメンバ名と揃えるのが基本。
        // 一度決めたら変更しない（変更すると古い Scene の値が読めなくなる）。
        std::string name;

        // Inspector に出す名前。空なら name を使う。
        std::string display_name;
        std::string tooltip;

        PropertyType type = PropertyType::Float;

        bool has_range = false;
        double minimum = 0.0;
        double maximum = 0.0;
        double step = 0.0;

        bool serializable = true;
        bool editor_visible = true;
        bool read_only = false;

        // Enum 表示用のラベル。添字が値に対応する。
        std::vector<std::string> enum_labels;

        // ---- v11 で追加したメタ情報 ----------------------------------------
        //
        // 目的は「Inspector 側へ Component 型ごとの if / switch を増やさない」こと。
        // 描き方の違いはすべてここへ宣言し、汎用の描画処理が読み取る。

        // Inspector の折り畳み見出し。空なら Component 直下へ並べる。
        std::string category;

        // 数値に添える単位表記（"m" / "度/秒" など）。表示のみで値には影響しない。
        std::string unit;

        // 実行中だけ意味のある値。編集 Scene では読み取り専用として描く。
        bool runtime_only = false;

        // 既定では折り畳んでおく高度な設定。
        bool advanced = false;

        // AssetReference が受け付ける Asset の種類（AssetDatabase の AssetKind 名）。
        // 空なら種類を問わない。Picker の絞り込みと Validation の両方が使う。
        std::string asset_type;

        // ComponentReference が期待する Component 型。
        // 無効値なら型を問わない。型が違っても参照は保持し、Validation が警告する。
        TypeGUID expected_component_type;

        // Array の要素型。type が Array のときだけ意味を持つ。
        PropertyType array_element_type = PropertyType::Bool;

        // Timeline から動かせるかどうか。
        // MakeProperty / MakeAccessorProperty が PropertyType から既定値を入れる。
        Animatable animatable = Animatable::None;

        Getter getter;
        Setter setter;

        // ---- 連結設定 (C++17 準拠。指定イニシャライザは使わない) ------------

        PropertyDesc& Display(std::string value) { display_name = std::move(value); return *this; }
        PropertyDesc& Tooltip(std::string value) { tooltip = std::move(value); return *this; }

        PropertyDesc& Range(double low, double high)
        {
            has_range = true;
            minimum = low;
            maximum = high;
            return *this;
        }

        PropertyDesc& Step(double value) { step = value; return *this; }
        PropertyDesc& ReadOnly()
        {
            read_only = true;
            animatable = Animatable::None;
            return *this;
        }
        PropertyDesc& HiddenInEditor() { editor_visible = false; return *this; }
        PropertyDesc& NotSerializable()
        {
            serializable = false;
            animatable = Animatable::None;
            return *this;
        }

        // 内部表現はそのままに、意味だけを変える。
        //   XMFLOAT4 のメンバを色として扱う  -> AsColor()
        //   std::string のメンバを Asset として扱う -> AsAssetPath()
        //   int のメンバを列挙として扱う     -> AsEnum({...})
        PropertyDesc& AsColor()
        {
            type = PropertyType::Color;
            animatable = Detail::DefaultAnimatableFor(type);
            return *this;
        }
        PropertyDesc& AsQuaternion()
        {
            type = PropertyType::Quaternion;
            animatable = Detail::DefaultAnimatableFor(type);
            return *this;
        }
        PropertyDesc& AsAssetPath()
        {
            type = PropertyType::AssetPath;
            animatable = Detail::DefaultAnimatableFor(type);
            return *this;
        }

        // 内部は int のまま、Inspector での意味だけを変える。
        //   Layer 番号        -> AsCollisionLayer()     … Layer 名の一覧から選ぶ
        //   Layer のビット列  -> AsCollisionMask()      … Layer 名のチェックボックス
        //   collider_key      -> AsColliderReference()  … 同じ GameObject の Collider を選ぶ
        // どれも整数を直接いじらせないための指定。
        PropertyDesc& AsCollisionLayer()
        {
            type = PropertyType::CollisionLayer;
            animatable = Detail::DefaultAnimatableFor(type);
            return *this;
        }
        PropertyDesc& AsCollisionMask()
        {
            type = PropertyType::CollisionMask;
            animatable = Detail::DefaultAnimatableFor(type);
            return *this;
        }
        PropertyDesc& AsColliderReference()
        {
            type = PropertyType::ColliderReference;
            animatable = Detail::DefaultAnimatableFor(type);
            return *this;
        }

        PropertyDesc& AsEnum(std::vector<std::string> labels)
        {
            type = PropertyType::Enum;
            animatable = Detail::DefaultAnimatableFor(type);
            enum_labels = std::move(labels);
            return *this;
        }

        // ---- v11 で追加した連結設定 -----------------------------------------

        PropertyDesc& Category(std::string value) { category = std::move(value); return *this; }
        PropertyDesc& Unit(std::string value) { unit = std::move(value); return *this; }
        PropertyDesc& RuntimeOnly() { runtime_only = true; return *this; }
        PropertyDesc& Advanced() { advanced = true; return *this; }
        PropertyDesc& Animation(Animatable value)
        {
            animatable = (read_only || !serializable) ? Animatable::None : value;
            return *this;
        }
        PropertyDesc& NotAnimatable()
        {
            animatable = Animatable::None;
            return *this;
        }

        // AssetReference が受け付ける Asset の種類を絞る。
        PropertyDesc& OfAssetType(std::string kind)
        {
            asset_type = std::move(kind);
            return *this;
        }

        // ComponentReference が期待する Component 型を宣言する。
        PropertyDesc& OfComponentType(TypeGUID type_guid)
        {
            expected_component_type = type_guid;
            return *this;
        }

        const std::string& DisplayName() const noexcept
        {
            return display_name.empty() ? name : display_name;
        }

        bool Valid() const noexcept
        {
            return !name.empty() && static_cast<bool>(getter) && static_cast<bool>(setter);
        }

        // 値の取得。メンバの C++ 型で読んだあと、宣言された PropertyType へ寄せる。
        // AsColor() などで意味を変えていても正しいタグの値が返る。
        PropertyValue Capture(const Core::Component& component) const
        {
            if (!getter) return PropertyValue{};
            const PropertyValue raw = getter(component);
            PropertyValue tagged;
            if (raw.Type() != type && raw.ConvertTo(type, tagged)) return tagged;
            return raw;
        }

        // 値の反映。型が食い違っていても PropertyValue 側が既定値へ倒すので落ちない。
        void Apply(Core::Component& component, const PropertyValue& value) const
        {
            if (setter && !read_only) setter(component, value);
        }
    };

    namespace Detail
    {
        template<class> inline constexpr bool always_false_v = false;

        constexpr Animatable DefaultAnimatableFor(PropertyType type) noexcept
        {
            switch (type)
            {
            case PropertyType::Float:
            case PropertyType::Double:
            case PropertyType::Vector2:
            case PropertyType::Vector3:
            case PropertyType::Vector4:
            case PropertyType::Quaternion:
            case PropertyType::Color:
            case PropertyType::Int:
            case PropertyType::Int64:
            case PropertyType::UInt64:
                return Animatable::Interpolatable;

            case PropertyType::Bool:
            case PropertyType::String:
            case PropertyType::Enum:
            case PropertyType::CollisionLayer:
            case PropertyType::CollisionMask:
                return Animatable::Step;

            case PropertyType::AssetPath:
            case PropertyType::AssetReference:
            case PropertyType::SceneReference:
            case PropertyType::ObjectReference:
            case PropertyType::ComponentReference:
            case PropertyType::ColliderReference:
            case PropertyType::Array:
                return Animatable::None;
            }
            return Animatable::None;
        }

        // std::vector<T> かどうかの判定。配列プロパティの受け付けに使う。
        template<class T> struct VectorTraits : std::false_type
        {
            using Element = void;
        };
        template<class T> struct VectorTraits<std::vector<T>> : std::true_type
        {
            using Element = T;
        };

        // 単一値の型判定。配列の要素型もこれで決める（入れ子は許さない）。
        template<class M>
        constexpr PropertyType DeduceScalarPropertyType() noexcept
        {
            if constexpr (std::is_same_v<M, bool>)                    return PropertyType::Bool;
            else if constexpr (std::is_same_v<M, int>)                return PropertyType::Int;
            else if constexpr (std::is_same_v<M, std::int64_t>)       return PropertyType::Int64;
            else if constexpr (std::is_same_v<M, std::uint64_t>)      return PropertyType::UInt64;
            else if constexpr (std::is_same_v<M, float>)              return PropertyType::Float;
            else if constexpr (std::is_same_v<M, double>)             return PropertyType::Double;
            else if constexpr (std::is_same_v<M, std::string>)        return PropertyType::String;
            else if constexpr (std::is_same_v<M, DirectX::XMFLOAT2>)  return PropertyType::Vector2;
            else if constexpr (std::is_same_v<M, DirectX::XMFLOAT3>)  return PropertyType::Vector3;
            else if constexpr (std::is_same_v<M, DirectX::XMFLOAT4>)  return PropertyType::Vector4;
            else if constexpr (std::is_same_v<M, Core::ObjectID>)     return PropertyType::ObjectReference;
            else if constexpr (std::is_same_v<M, ObjectReference>)    return PropertyType::ObjectReference;
            else if constexpr (std::is_same_v<M, ComponentReference>) return PropertyType::ComponentReference;
            else if constexpr (std::is_same_v<M, AssetReference>)     return PropertyType::AssetReference;
            else if constexpr (std::is_same_v<M, SceneReference>)     return PropertyType::SceneReference;
            else static_assert(always_false_v<M>,
                "PropertyRegistry がまだ対応していないメンバ型です");
        }

        template<class M>
        constexpr PropertyType DeducePropertyType() noexcept
        {
            if constexpr (VectorTraits<M>::value) return PropertyType::Array;
            else return DeduceScalarPropertyType<M>();
        }

        // 配列の要素型。配列でないメンバでも呼べるよう、既定は Bool を返す。
        template<class M>
        constexpr PropertyType DeduceArrayElementType() noexcept
        {
            if constexpr (VectorTraits<M>::value)
            {
                return DeduceScalarPropertyType<typename VectorTraits<M>::Element>();
            }
            else
            {
                return PropertyType::Bool;
            }
        }

        template<class M>
        PropertyValue CaptureScalar(const M& value)
        {
            if constexpr (std::is_same_v<M, bool>)                    return PropertyValue::MakeBool(value);
            else if constexpr (std::is_same_v<M, int>)                return PropertyValue::MakeInt(value);
            else if constexpr (std::is_same_v<M, std::int64_t>)       return PropertyValue::MakeInt64(value);
            else if constexpr (std::is_same_v<M, std::uint64_t>)      return PropertyValue::MakeUInt64(value);
            else if constexpr (std::is_same_v<M, float>)              return PropertyValue::MakeFloat(value);
            else if constexpr (std::is_same_v<M, double>)             return PropertyValue::MakeDouble(value);
            else if constexpr (std::is_same_v<M, std::string>)        return PropertyValue::MakeString(value);
            else if constexpr (std::is_same_v<M, DirectX::XMFLOAT2>)  return PropertyValue::MakeVector2(value);
            else if constexpr (std::is_same_v<M, DirectX::XMFLOAT3>)  return PropertyValue::MakeVector3(value);
            else if constexpr (std::is_same_v<M, DirectX::XMFLOAT4>)  return PropertyValue::MakeVector4(value);
            else if constexpr (std::is_same_v<M, Core::ObjectID>)     return PropertyValue::MakeObjectReference(value);
            else if constexpr (std::is_same_v<M, ObjectReference>)    return PropertyValue::MakeObjectReference(value.object);
            else if constexpr (std::is_same_v<M, ComponentReference>) return PropertyValue::MakeComponentReference(value);
            else if constexpr (std::is_same_v<M, AssetReference>)     return PropertyValue::MakeAssetReference(value.guid);
            else if constexpr (std::is_same_v<M, SceneReference>)     return PropertyValue::MakeSceneReference(value.guid);
            else static_assert(always_false_v<M>, "未対応のメンバ型です");
        }

        template<class M>
        void ApplyScalar(M& target, const PropertyValue& value)
        {
            if constexpr (std::is_same_v<M, bool>)                    target = value.AsBool(target);
            else if constexpr (std::is_same_v<M, int>)                target = value.AsInt(target);
            else if constexpr (std::is_same_v<M, std::int64_t>)       target = value.AsInt64(target);
            else if constexpr (std::is_same_v<M, std::uint64_t>)      target = value.AsUInt64(target);
            else if constexpr (std::is_same_v<M, float>)              target = value.AsFloat(target);
            else if constexpr (std::is_same_v<M, double>)             target = value.AsDouble(target);
            else if constexpr (std::is_same_v<M, std::string>)        target = value.AsString();
            else if constexpr (std::is_same_v<M, DirectX::XMFLOAT2>)  target = value.AsVector2();
            else if constexpr (std::is_same_v<M, DirectX::XMFLOAT3>)  target = value.AsVector3();
            else if constexpr (std::is_same_v<M, DirectX::XMFLOAT4>)  target = value.AsVector4();
            else if constexpr (std::is_same_v<M, Core::ObjectID>)     target = value.AsObjectReference();
            else if constexpr (std::is_same_v<M, ObjectReference>)    target.object = value.AsObjectReference();
            else if constexpr (std::is_same_v<M, ComponentReference>) target = value.AsComponentReference();
            else if constexpr (std::is_same_v<M, AssetReference>)     target.guid = value.AsAssetReference().guid;
            else if constexpr (std::is_same_v<M, SceneReference>)     target.guid = value.AsSceneReference().guid;
            else static_assert(always_false_v<M>, "未対応のメンバ型です");
        }

        template<class M>
        PropertyValue CaptureMember(const M& value)
        {
            if constexpr (VectorTraits<M>::value)
            {
                std::vector<PropertyValue> elements;
                elements.reserve(value.size());
                for (const auto& element : value) elements.push_back(CaptureScalar(element));
                return PropertyValue::MakeArray(DeduceArrayElementType<M>(), std::move(elements));
            }
            else
            {
                return CaptureScalar(value);
            }
        }

        template<class M>
        void ApplyMember(M& target, const PropertyValue& value)
        {
            if constexpr (VectorTraits<M>::value)
            {
                // 配列でない値が来た場合は既存の中身を維持する。
                // 型が変わっただけで保存済みの要素を全部失わないため。
                if (!value.IsArray()) return;

                using Element = typename VectorTraits<M>::Element;
                const std::vector<PropertyValue>& elements = value.ArrayElements();

                M restored;
                restored.reserve(elements.size());
                for (const PropertyValue& element : elements)
                {
                    Element item{};
                    ApplyScalar(item, element);
                    restored.push_back(std::move(item));
                }
                target = std::move(restored);
            }
            else
            {
                ApplyScalar(target, value);
            }
        }
    }

    // メンバ変数ポインタから PropertyDesc を作る。
    //
    //   PropertyRegistry::Register<RotatorComponent>(
    //       MakeProperty("degrees_per_second", &RotatorComponent::degrees_per_second)
    //           .Display("回転速度 (度/秒)").Range(-1440.0, 1440.0).Step(1.0));
    //
    // getter / setter は型 ID を照合してから実体へ触るので、
    // 万一違う型の Component を渡されても未定義動作にならない。
    template<class C, class M>
    PropertyDesc MakeProperty(std::string name, M C::* member)
    {
        static_assert(std::is_base_of_v<Core::Component, C>,
            "C must derive from ReplayEngine::Core::Component");

        PropertyDesc desc;
        desc.name = std::move(name);
        desc.type = Detail::DeducePropertyType<M>();
        desc.array_element_type = Detail::DeduceArrayElementType<M>();
        desc.animatable = Detail::DefaultAnimatableFor(desc.type);

        desc.getter = [member](const Core::Component& component) -> PropertyValue
        {
            if (component.TypeID() != C::StaticTypeID()) return PropertyValue{};
            return Detail::CaptureMember(static_cast<const C&>(component).*member);
        };

        desc.setter = [member](Core::Component& component, const PropertyValue& value)
        {
            if (component.TypeID() != C::StaticTypeID()) return;
            Detail::ApplyMember(static_cast<C&>(component).*member, value);
        };

        return desc;
    }

    // メンバ変数に直接対応しないプロパティ用。
    // 値の実体が別の場所にある場合（TransformComponent が GameObject の Transform を
    // 指しているような場合）や、単位変換を挟む場合に使う。
    //
    //   MakeAccessorProperty<TransformComponent>("position", PropertyType::Vector3,
    //       [](const TransformComponent& c) { return PropertyValue::MakeVector3(c.Position()); },
    //       [](TransformComponent& c, const PropertyValue& v) { c.SetPosition(v.AsVector3()); });
    template<class C, class GetFn, class SetFn>
    PropertyDesc MakeAccessorProperty(std::string name, PropertyType type,
        GetFn get, SetFn set)
    {
        static_assert(std::is_base_of_v<Core::Component, C>,
            "C must derive from ReplayEngine::Core::Component");

        PropertyDesc desc;
        desc.name = std::move(name);
        desc.type = type;
        desc.animatable = Detail::DefaultAnimatableFor(type);

        desc.getter = [get](const Core::Component& component) -> PropertyValue
        {
            if (component.TypeID() != C::StaticTypeID()) return PropertyValue{};
            return get(static_cast<const C&>(component));
        };

        desc.setter = [set](Core::Component& component, const PropertyValue& value)
        {
            if (component.TypeID() != C::StaticTypeID()) return;
            set(static_cast<C&>(component), value);
        };

        return desc;
    }
}
