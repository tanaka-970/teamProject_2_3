#pragma once

#include "SceneSerializer.h"

#include <cstddef>
#include <iosfwd>
#include <string>

namespace ReplayEngine::Scene::Serialization::Detail
{
    // これは SceneSerializer の分割内部で共有する保存形式ヘルパであり、外部から使うものではない。
    inline constexpr const char* magic_token = "REPLAY_SCENE";
    inline constexpr std::size_t maximum_objects = 200000;
    inline constexpr std::size_t maximum_components_per_object = 512;
    inline constexpr std::size_t maximum_properties_per_component = 512;
    inline constexpr std::size_t maximum_array_elements = 65536;

    float Sanitize(float value) noexcept;
    double Sanitize(double value) noexcept;
    bool Expect(std::istream& stream, const char* expected, std::string& error);
    void WriteFloat3(std::ostream& stream, const DirectX::XMFLOAT3& value);
    bool ReadFloat3(std::istream& stream, DirectX::XMFLOAT3& value);
    bool WriteValueBody(std::ostream& stream, Reflection::PropertyType type,
        const Reflection::PropertyValue& value);
    bool ReadValueBody(std::istream& stream, Reflection::PropertyType type,
        Reflection::PropertyValue& out, std::string& error);
    bool WriteProperty(std::ostream& stream, const std::string& name,
        const Reflection::PropertyValue& value);
    bool ReadProperty(std::istream& stream, Reflection::PropertyBag& bag,
        std::string& error);
}
