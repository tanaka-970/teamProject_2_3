#include "ShaderAsset.h"

#include <algorithm>

namespace ReplayEngine::Rendering
{
    const char* ToString(ShaderDomain domain) noexcept
    {
        switch (domain)
        {
        case ShaderDomain::Surface:     return "surface";
        case ShaderDomain::Layer:       return "layer";
        case ShaderDomain::PostProcess: return "postprocess";
        default:                        return "surface";
        }
    }

    const char* ToString(ShaderVariant variant) noexcept
    {
        switch (variant)
        {
        case ShaderVariant::Static:  return "Static";
        case ShaderVariant::Skinned: return "Skinned";
        default:                     return "Static";
        }
    }

    bool TryParseShaderDomain(std::string_view text, ShaderDomain& out) noexcept
    {
        if (text == "surface")     { out = ShaderDomain::Surface;     return true; }
        if (text == "layer")       { out = ShaderDomain::Layer;       return true; }
        if (text == "postprocess") { out = ShaderDomain::PostProcess; return true; }
        return false;
    }

    const char* ToString(ShaderPropertyKind kind) noexcept
    {
        switch (kind)
        {
        case ShaderPropertyKind::Float:   return "float";
        case ShaderPropertyKind::Range:   return "range";
        case ShaderPropertyKind::Float2:  return "float2";
        case ShaderPropertyKind::Float3:  return "float3";
        case ShaderPropertyKind::Float4:  return "float4";
        case ShaderPropertyKind::Color:   return "color";
        case ShaderPropertyKind::Texture: return "texture";
        case ShaderPropertyKind::Toggle:  return "toggle";
        case ShaderPropertyKind::Enum:    return "enum";
        default:                          return "float";
        }
    }

    bool TryParseShaderPropertyKind(std::string_view text,
        ShaderPropertyKind& out) noexcept
    {
        if (text == "float")   { out = ShaderPropertyKind::Float;   return true; }
        if (text == "range")   { out = ShaderPropertyKind::Range;   return true; }
        if (text == "float2")  { out = ShaderPropertyKind::Float2;  return true; }
        if (text == "float3")  { out = ShaderPropertyKind::Float3;  return true; }
        if (text == "float4")  { out = ShaderPropertyKind::Float4;  return true; }
        if (text == "color")   { out = ShaderPropertyKind::Color;   return true; }
        if (text == "texture") { out = ShaderPropertyKind::Texture; return true; }
        if (text == "toggle")  { out = ShaderPropertyKind::Toggle;  return true; }
        if (text == "enum")    { out = ShaderPropertyKind::Enum;    return true; }
        return false;
    }

    std::uint32_t ShaderPropertySize(ShaderPropertyKind kind) noexcept
    {
        switch (kind)
        {
        case ShaderPropertyKind::Float:   return 4;
        case ShaderPropertyKind::Range:   return 4;
        case ShaderPropertyKind::Toggle:  return 4;
        case ShaderPropertyKind::Enum:    return 4;
        case ShaderPropertyKind::Float2:  return 8;
        case ShaderPropertyKind::Float3:  return 12;
        case ShaderPropertyKind::Float4:  return 16;
        case ShaderPropertyKind::Color:   return 16;

        // Texture は定数バッファに載らない。t レジスタで渡す。
        case ShaderPropertyKind::Texture: return 0;
        default:                          return 4;
        }
    }

    std::string ShaderProperty::SavedName() const
    {
        return "prop." + name;
    }

    ShaderPropertySchema::ShaderPropertySchema(ShaderID id,
        std::vector<ShaderProperty> properties, std::uint32_t revision)
        : id_(id)
        , revision_(revision)
        , properties_(std::move(properties))
    {
        // 定数バッファの大きさとテクスチャ数を数える。
        //
        // オフセットの割り当ては ShaderConstantPacker がやる（フェーズ 3）。
        // ここでは「properties_ に既に入っている値」から集計するだけ。
        // 二重に計算すると必ず食い違うので、計算場所を 1 つに保つこと。
        std::uint32_t end = 0;
        for (const ShaderProperty& item : properties_)
        {
            if (item.kind == ShaderPropertyKind::Texture)
            {
                ++texture_count_;
                continue;
            }
            end = (std::max)(end, item.constant_offset + item.constant_size);
        }

        // cbuffer は 16 バイト境界へ切り上げる決まり。
        constant_buffer_size_ = (end + 15u) & ~15u;
    }

    const ShaderProperty* ShaderPropertySchema::FindBySavedName(
        const std::string& saved) const noexcept
    {
        for (const ShaderProperty& item : properties_)
        {
            if (item.SavedName() == saved) return &item;
        }
        return nullptr;
    }

    const ShaderProperty* ShaderPropertySchema::FindByName(
        const std::string& name) const noexcept
    {
        for (const ShaderProperty& item : properties_)
        {
            if (item.name == name) return &item;
        }
        return nullptr;
    }

    std::string ShaderSourceInfo::DisplayName() const
    {
        if (!name.empty()) return name;
        return source_path.stem().u8string();
    }

    std::string ShaderSourceInfo::MenuPath() const
    {
        const std::string leaf = DisplayName();
        if (category.empty()) return leaf;

        // category が末尾に / を持っていても二重にしない。
        std::string prefix = category;
        while (!prefix.empty() && prefix.back() == '/') prefix.pop_back();
        if (prefix.empty()) return leaf;
        return prefix + "/" + leaf;
    }
}
