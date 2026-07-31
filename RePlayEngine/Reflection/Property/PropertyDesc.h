#pragma once

#include "PropertyValue.h"
#include "../../Object/Component/Component.h"

#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ReplayEngine::Reflection
{
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
        PropertyDesc& ReadOnly() { read_only = true; return *this; }
        PropertyDesc& HiddenInEditor() { editor_visible = false; return *this; }
        PropertyDesc& NotSerializable() { serializable = false; return *this; }

        // 内部表現はそのままに、意味だけを変える。
        //   XMFLOAT4 のメンバを色として扱う  -> AsColor()
        //   std::string のメンバを Asset として扱う -> AsAssetPath()
        //   int のメンバを列挙として扱う     -> AsEnum({...})
        PropertyDesc& AsColor() { type = PropertyType::Color; return *this; }
        PropertyDesc& AsQuaternion() { type = PropertyType::Quaternion; return *this; }
        PropertyDesc& AsAssetPath() { type = PropertyType::AssetPath; return *this; }

        PropertyDesc& AsEnum(std::vector<std::string> labels)
        {
            type = PropertyType::Enum;
            enum_labels = std::move(labels);
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

        template<class M>
        constexpr PropertyType DeducePropertyType() noexcept
        {
            if constexpr (std::is_same_v<M, bool>)                    return PropertyType::Bool;
            else if constexpr (std::is_same_v<M, int>)                return PropertyType::Int;
            else if constexpr (std::is_same_v<M, float>)              return PropertyType::Float;
            else if constexpr (std::is_same_v<M, double>)             return PropertyType::Double;
            else if constexpr (std::is_same_v<M, std::string>)        return PropertyType::String;
            else if constexpr (std::is_same_v<M, DirectX::XMFLOAT2>)  return PropertyType::Vector2;
            else if constexpr (std::is_same_v<M, DirectX::XMFLOAT3>)  return PropertyType::Vector3;
            else if constexpr (std::is_same_v<M, DirectX::XMFLOAT4>)  return PropertyType::Vector4;
            else if constexpr (std::is_same_v<M, Core::ObjectID>)     return PropertyType::ObjectReference;
            else static_assert(always_false_v<M>,
                "PropertyRegistry がまだ対応していないメンバ型です");
        }

        template<class M>
        PropertyValue CaptureMember(const M& value)
        {
            if constexpr (std::is_same_v<M, bool>)                    return PropertyValue::MakeBool(value);
            else if constexpr (std::is_same_v<M, int>)                return PropertyValue::MakeInt(value);
            else if constexpr (std::is_same_v<M, float>)              return PropertyValue::MakeFloat(value);
            else if constexpr (std::is_same_v<M, double>)             return PropertyValue::MakeDouble(value);
            else if constexpr (std::is_same_v<M, std::string>)        return PropertyValue::MakeString(value);
            else if constexpr (std::is_same_v<M, DirectX::XMFLOAT2>)  return PropertyValue::MakeVector2(value);
            else if constexpr (std::is_same_v<M, DirectX::XMFLOAT3>)  return PropertyValue::MakeVector3(value);
            else if constexpr (std::is_same_v<M, DirectX::XMFLOAT4>)  return PropertyValue::MakeVector4(value);
            else if constexpr (std::is_same_v<M, Core::ObjectID>)     return PropertyValue::MakeObjectReference(value);
            else static_assert(always_false_v<M>, "未対応のメンバ型です");
        }

        template<class M>
        void ApplyMember(M& target, const PropertyValue& value)
        {
            if constexpr (std::is_same_v<M, bool>)                    target = value.AsBool(target);
            else if constexpr (std::is_same_v<M, int>)                target = value.AsInt(target);
            else if constexpr (std::is_same_v<M, float>)              target = value.AsFloat(target);
            else if constexpr (std::is_same_v<M, double>)             target = value.AsDouble(target);
            else if constexpr (std::is_same_v<M, std::string>)        target = value.AsString();
            else if constexpr (std::is_same_v<M, DirectX::XMFLOAT2>)  target = value.AsVector2();
            else if constexpr (std::is_same_v<M, DirectX::XMFLOAT3>)  target = value.AsVector3();
            else if constexpr (std::is_same_v<M, DirectX::XMFLOAT4>)  target = value.AsVector4();
            else if constexpr (std::is_same_v<M, Core::ObjectID>)     target = value.AsObjectReference();
            else static_assert(always_false_v<M>, "未対応のメンバ型です");
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
