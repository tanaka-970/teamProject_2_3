#include "ShaderConstantPacker.h"

#include <cstring>
#include <sstream>

namespace ReplayEngine::Rendering
{
    namespace
    {
        // HLSL の型名。cbuffer 宣言の生成に使う。
        const char* HlslTypeName(ShaderPropertyKind kind) noexcept
        {
            switch (kind)
            {
            case ShaderPropertyKind::Float:  return "float";
            case ShaderPropertyKind::Range:  return "float";
            case ShaderPropertyKind::Toggle: return "float";
            case ShaderPropertyKind::Enum:   return "float";
            case ShaderPropertyKind::Float2: return "float2";
            case ShaderPropertyKind::Float3: return "float3";
            case ShaderPropertyKind::Float4: return "float4";
            case ShaderPropertyKind::Color:  return "float4";
            default:                         return "float";
            }
        }
    }

    void ShaderConstantPacker::AssignOffsets(
        std::vector<ShaderProperty>& properties, std::uint32_t& out_buffer_size)
    {
        std::uint32_t cursor = 0;
        std::uint32_t texture_index = 0;

        for (ShaderProperty& property : properties)
        {
            if (property.kind == ShaderPropertyKind::Texture)
            {
                // テクスチャは定数バッファに載らない。t レジスタで渡す。
                property.constant_offset = 0;
                property.constant_size = 0;
                property.texture_slot = material_texture_base_slot + texture_index;
                ++texture_index;
                continue;
            }

            const std::uint32_t size = ShaderPropertySize(property.kind);

            // 16 バイト境界をまたぐなら次の境界へ送る。
            //
            // float4 も同じ規則で処理できる。
            // 境界にいないと残りが 16 未満になるので、必ず送られる。
            const std::uint32_t used = cursor % 16u;
            const std::uint32_t remaining = (used == 0) ? 16u : (16u - used);
            if (remaining < size) cursor = Align16(cursor);

            property.constant_offset = cursor;
            property.constant_size = size;
            property.texture_slot = 0;
            cursor += size;
        }

        // バッファ全体も 16 の倍数へ切り上げる。
        // 端数のまま CreateBuffer すると D3D11 が失敗する。
        out_buffer_size = Align16(cursor);
    }

    void ShaderConstantPacker::Pack(const ShaderPropertySchema& schema,
        ValueLookup lookup, void* user, std::vector<std::uint8_t>& out_bytes)
    {
        out_bytes.assign(schema.ConstantBufferSize(), std::uint8_t{ 0 });
        if (out_bytes.empty()) return;

        for (const ShaderProperty& property : schema.Properties())
        {
            if (property.kind == ShaderPropertyKind::Texture) continue;
            if (property.constant_size == 0) continue;

            // 範囲外へ書かない。
            //
            // Schema と properties の食い違いは設計上起きないはずだが、
            // 起きたときにメモリを壊すと原因が追えなくなる。
            // 静かに飛ばして、壊すよりは値が欠ける方を選ぶ。
            const std::size_t end =
                static_cast<std::size_t>(property.constant_offset) +
                property.constant_size;
            if (end > out_bytes.size()) continue;

            DirectX::XMFLOAT4 value = property.default_value;
            if (lookup != nullptr)
            {
                DirectX::XMFLOAT4 found{};
                if (lookup(property.SavedName(), found, user)) value = found;
            }

            std::memcpy(out_bytes.data() + property.constant_offset,
                &value, property.constant_size);
        }
    }

    void ShaderConstantPacker::PackDefaults(const ShaderPropertySchema& schema,
        std::vector<std::uint8_t>& out_bytes)
    {
        Pack(schema, nullptr, nullptr, out_bytes);
    }

    std::string ShaderConstantPacker::GenerateHlslDeclaration(
        const ShaderPropertySchema& schema)
    {
        std::ostringstream stream;

        stream << "// ---- ここから ShaderConstantPacker の自動生成 ----\n";
        stream << "// このブロックは手で書かないこと。\n";
        stream << "// #pragma property の宣言から毎回作り直される。\n";

        // 定数が 1 つも無くても cbuffer は出す。
        // 出さないと、あとで property を足したときだけ
        // レジスタ番号がずれることになる。
        stream << "cbuffer REPLAY_MATERIAL_CB : register(b"
               << material_constant_register << ")\n{\n";

        std::uint32_t cursor = 0;
        int padding_index = 0;

        for (const ShaderProperty& property : schema.Properties())
        {
            if (property.kind == ShaderPropertyKind::Texture) continue;
            if (property.constant_size == 0) continue;

            // 送られた分の隙間を埋める。
            // 埋めないと HLSL 側のオフセットが C++ 側とずれる。
            if (property.constant_offset > cursor)
            {
                std::uint32_t gap = property.constant_offset - cursor;
                while (gap >= 4)
                {
                    stream << "    float _replay_pad" << padding_index++ << ";\n";
                    gap -= 4;
                    cursor += 4;
                }
            }

            stream << "    " << HlslTypeName(property.kind) << ' '
                   << property.name << ";\n";
            cursor = property.constant_offset + property.constant_size;
        }

        // 末尾も 16 の倍数まで埋める。
        const std::uint32_t total = schema.ConstantBufferSize();
        while (cursor < total)
        {
            stream << "    float _replay_pad" << padding_index++ << ";\n";
            cursor += 4;
        }

        stream << "};\n";

        // テクスチャ。t レジスタは AssignOffsets が決めた番号を使う。
        for (const ShaderProperty& property : schema.Properties())
        {
            if (property.kind != ShaderPropertyKind::Texture) continue;
            stream << "Texture2D " << property.name
                   << " : register(t" << property.texture_slot << ");\n";
        }

        stream << "// ---- 自動生成ここまで ----\n";
        return stream.str();
    }
}
