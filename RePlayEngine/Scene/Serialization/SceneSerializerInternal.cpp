#include "SceneSerializer.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <istream>
#include <limits>
#include <locale>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>
#include "SceneSerializerInternal.h"

namespace ReplayEngine::Scene::Serialization::Detail
{
        using Reflection::PropertyBag;
        using Reflection::PropertyType;
        using Reflection::PropertyValue;

        float Sanitize(float value) noexcept { return std::isfinite(value) ? value : 0.0f; }
        double Sanitize(double value) noexcept { return std::isfinite(value) ? value : 0.0; }

        bool Expect(std::istream& stream, const char* expected, std::string& error)
        {
            std::string token;
            if (!(stream >> token) || token != expected)
            {
                error = std::string("Scene ファイルの形式が不正です。'") + expected +
                    "' が見つかりません。";
                return false;
            }
            return true;
        }

        void WriteFloat3(std::ostream& stream, const DirectX::XMFLOAT3& value)
        {
            stream << Sanitize(value.x) << ' ' << Sanitize(value.y) << ' ' << Sanitize(value.z);
        }

        bool ReadFloat3(std::istream& stream, DirectX::XMFLOAT3& value)
        {
            return static_cast<bool>(stream >> value.x >> value.y >> value.z);
        }

        // 値の本体だけを書く。名前も型名も書かない。
        //
        // 名前・型名の出力と分けている理由:
        //   配列の要素は「型名 1 つ + 値の並び」として書くため、
        //   要素ごとに型名を繰り返さない。本体の書式を 1 か所にまとめておけば、
        //   単一値と配列要素で書式がずれることがない。
        //
        // type は「宣言された型」。value 側が別の型を持っていても、
        // 宣言された型の As* で取り出すので書式は必ず一致する。
        bool WriteValueBody(std::ostream& stream, PropertyType type, const PropertyValue& value)
        {
            switch (type)
            {
            case PropertyType::Bool:
                stream << (value.AsBool() ? 1 : 0);
                break;
            case PropertyType::Int:
            case PropertyType::Enum:
            case PropertyType::CollisionLayer:
            case PropertyType::CollisionMask:
            case PropertyType::ColliderReference:
                // どれも内部表現は int。書式は同じで、型名だけが違う。
                stream << value.AsInt();
                break;
            case PropertyType::Int64:
                stream << value.AsInt64();
                break;
            case PropertyType::UInt64:
                stream << value.AsUInt64();
                break;
            case PropertyType::Float:
                stream << Sanitize(value.AsFloat());
                break;
            case PropertyType::Double:
                stream << Sanitize(value.AsDouble());
                break;
            case PropertyType::String:
            case PropertyType::AssetPath:
            case PropertyType::AssetReference:
            case PropertyType::SceneReference:
                // 参照系はどれも「識別子の文字列」。書式は共通で、意味だけが型名で決まる。
                stream << std::quoted(value.AsString());
                break;
            case PropertyType::Vector2:
            {
                const DirectX::XMFLOAT2 vector = value.AsVector2();
                stream << Sanitize(vector.x) << ' ' << Sanitize(vector.y);
                break;
            }
            case PropertyType::Vector3:
                WriteFloat3(stream, value.AsVector3());
                break;
            case PropertyType::Vector4:
            case PropertyType::Quaternion:
            case PropertyType::Color:
            {
                const DirectX::XMFLOAT4 vector = value.AsVector4();
                stream << Sanitize(vector.x) << ' ' << Sanitize(vector.y) << ' '
                    << Sanitize(vector.z) << ' ' << Sanitize(vector.w);
                break;
            }
            case PropertyType::ObjectReference:
                stream << value.AsObjectReference().Value();
                break;
            case PropertyType::ComponentReference:
            {
                // 所有 ObjectID と、その GameObject 内で安定した Component ID の組。
                const Reflection::ComponentReference reference = value.AsComponentReference();
                stream << reference.owner.Value() << ' ' << reference.component;
                break;
            }
            case PropertyType::Array:
                // 配列の入れ子は未対応。ここへ来るのは呼び出し側の誤りなので書かない。
                return false;
            }
            return static_cast<bool>(stream);
        }

        bool ReadValueBody(std::istream& stream, PropertyType type, PropertyValue& out,
            std::string& error)
        {
            switch (type)
            {
            case PropertyType::Bool:
            {
                int raw = 0;
                if (!(stream >> raw)) { error = "bool を読み取れません。"; return false; }
                out = PropertyValue::MakeBool(raw != 0);
                return true;
            }
            case PropertyType::Int:
            case PropertyType::Enum:
            case PropertyType::CollisionLayer:
            case PropertyType::CollisionMask:
            case PropertyType::ColliderReference:
            {
                int raw = 0;
                if (!(stream >> raw)) { error = "int を読み取れません。"; return false; }
                switch (type)
                {
                case PropertyType::Enum:
                    out = PropertyValue::MakeEnum(raw); break;
                case PropertyType::CollisionLayer:
                    out = PropertyValue::MakeCollisionLayer(raw); break;
                case PropertyType::CollisionMask:
                    out = PropertyValue::MakeCollisionMask(raw); break;
                case PropertyType::ColliderReference:
                    out = PropertyValue::MakeColliderReference(raw); break;
                default:
                    out = PropertyValue::MakeInt(raw); break;
                }
                return true;
            }
            case PropertyType::Int64:
            {
                std::int64_t raw = 0;
                if (!(stream >> raw)) { error = "int64 を読み取れません。"; return false; }
                out = PropertyValue::MakeInt64(raw);
                return true;
            }
            case PropertyType::UInt64:
            {
                std::uint64_t raw = 0;
                if (!(stream >> raw)) { error = "uint64 を読み取れません。"; return false; }
                out = PropertyValue::MakeUInt64(raw);
                return true;
            }
            case PropertyType::Float:
            {
                float raw = 0.0f;
                if (!(stream >> raw)) { error = "float を読み取れません。"; return false; }
                out = PropertyValue::MakeFloat(Sanitize(raw));
                return true;
            }
            case PropertyType::Double:
            {
                double raw = 0.0;
                if (!(stream >> raw)) { error = "double を読み取れません。"; return false; }
                out = PropertyValue::MakeDouble(Sanitize(raw));
                return true;
            }
            case PropertyType::String:
            case PropertyType::AssetPath:
            case PropertyType::AssetReference:
            case PropertyType::SceneReference:
            {
                std::string raw;
                if (!(stream >> std::quoted(raw)))
                {
                    error = "文字列を読み取れません。";
                    return false;
                }
                switch (type)
                {
                case PropertyType::AssetPath:
                    out = PropertyValue::MakeAssetPath(std::move(raw)); break;
                case PropertyType::AssetReference:
                    out = PropertyValue::MakeAssetReference(std::move(raw)); break;
                case PropertyType::SceneReference:
                    out = PropertyValue::MakeSceneReference(std::move(raw)); break;
                default:
                    out = PropertyValue::MakeString(std::move(raw)); break;
                }
                return true;
            }
            case PropertyType::Vector2:
            {
                DirectX::XMFLOAT2 raw{ 0.0f, 0.0f };
                if (!(stream >> raw.x >> raw.y)) { error = "vec2 を読み取れません。"; return false; }
                raw.x = Sanitize(raw.x); raw.y = Sanitize(raw.y);
                out = PropertyValue::MakeVector2(raw);
                return true;
            }
            case PropertyType::Vector3:
            {
                DirectX::XMFLOAT3 raw{ 0.0f, 0.0f, 0.0f };
                if (!ReadFloat3(stream, raw)) { error = "vec3 を読み取れません。"; return false; }
                raw.x = Sanitize(raw.x); raw.y = Sanitize(raw.y); raw.z = Sanitize(raw.z);
                out = PropertyValue::MakeVector3(raw);
                return true;
            }
            case PropertyType::Vector4:
            case PropertyType::Quaternion:
            case PropertyType::Color:
            {
                DirectX::XMFLOAT4 raw{ 0.0f, 0.0f, 0.0f, 1.0f };
                if (!(stream >> raw.x >> raw.y >> raw.z >> raw.w))
                {
                    error = "vec4 を読み取れません。";
                    return false;
                }
                raw.x = Sanitize(raw.x); raw.y = Sanitize(raw.y);
                raw.z = Sanitize(raw.z); raw.w = Sanitize(raw.w);
                if (type == PropertyType::Quaternion) out = PropertyValue::MakeQuaternion(raw);
                else if (type == PropertyType::Color) out = PropertyValue::MakeColor(raw);
                else                                  out = PropertyValue::MakeVector4(raw);
                return true;
            }
            case PropertyType::ObjectReference:
            {
                Core::ObjectID::ValueType raw = 0;
                if (!(stream >> raw)) { error = "オブジェクト参照を読み取れません。"; return false; }
                out = PropertyValue::MakeObjectReference(Core::ObjectID(raw));
                return true;
            }
            case PropertyType::ComponentReference:
            {
                Core::ObjectID::ValueType raw_owner = 0;
                std::uint32_t raw_component = 0;
                if (!(stream >> raw_owner >> raw_component))
                {
                    error = "Component 参照を読み取れません。";
                    return false;
                }
                Reflection::ComponentReference reference;
                reference.owner = Core::ObjectID(raw_owner);
                reference.component = raw_component;
                out = PropertyValue::MakeComponentReference(reference);
                return true;
            }
            case PropertyType::Array:
                error = "配列の入れ子には対応していません。";
                return false;
            }

            error = "プロパティ型の処理に失敗しました。";
            return false;
        }

        bool WriteProperty(std::ostream& stream, const std::string& name,
            const PropertyValue& value)
        {
            stream << "    PROPERTY " << std::quoted(name) << ' '
                << Reflection::ToString(value.Type());

            if (value.Type() == PropertyType::Array)
            {
                // 書式: array <要素型名> <個数> <値0> <値1> ...
                const PropertyType element_type = value.ArrayElementType();
                const std::vector<PropertyValue>& elements = value.ArrayElements();

                stream << ' ' << Reflection::ToString(element_type)
                    << ' ' << elements.size();
                for (const PropertyValue& element : elements)
                {
                    stream << ' ';
                    if (!WriteValueBody(stream, element_type, element)) return false;
                }
            }
            else
            {
                stream << ' ';
                if (!WriteValueBody(stream, value.Type(), value)) return false;
            }

            stream << '\n';
            return static_cast<bool>(stream);
        }

        bool ReadProperty(std::istream& stream, PropertyBag& bag, std::string& error)
        {
            std::string name;
            std::string type_text;
            if (!(stream >> std::quoted(name) >> type_text))
            {
                error = "プロパティ名または型を読み取れません。";
                return false;
            }

            PropertyType type = PropertyType::Bool;
            if (!Reflection::TryParsePropertyType(type_text, type))
            {
                // 将来追加された型を古いビルドで読んだ場合。
                // 値の並びが分からないので、ここで打ち切る。
                error = "未知のプロパティ型です: " + type_text;
                return false;
            }

            if (type == PropertyType::Array)
            {
                std::string element_type_text;
                std::size_t element_count = 0;
                if (!(stream >> element_type_text >> element_count))
                {
                    error = "配列の要素型または個数を読み取れません。";
                    return false;
                }
                if (element_count > maximum_array_elements)
                {
                    error = "配列の要素数が不正です。";
                    return false;
                }

                PropertyType element_type = PropertyType::Bool;
                if (!Reflection::TryParsePropertyType(element_type_text, element_type) ||
                    Reflection::IsContainerType(element_type))
                {
                    error = "配列の要素型が不正です: " + element_type_text;
                    return false;
                }

                std::vector<PropertyValue> elements;
                elements.reserve(element_count);
                for (std::size_t index = 0; index < element_count; ++index)
                {
                    PropertyValue element;
                    if (!ReadValueBody(stream, element_type, element, error)) return false;
                    elements.push_back(std::move(element));
                }

                bag.Set(name, PropertyValue::MakeArray(element_type, std::move(elements)));
                return true;
            }

            PropertyValue value;
            if (!ReadValueBody(stream, type, value, error)) return false;
            bag.Set(name, std::move(value));
            return true;
        }
}
