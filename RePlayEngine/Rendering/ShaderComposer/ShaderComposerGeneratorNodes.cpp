#include "ShaderComposerGenerator.h"
#include "ShaderComposerGeneratorInternal.h"
#include "../Shaders/ShaderConstantPacker.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace ReplayEngine::Rendering
{
    namespace Detail
    {
        Value Generator::Input(const ShaderComposerNode& node, std::uint32_t pin,
            const Value& fallback)
            {
                const std::uint64_t key = (node.id << 16) ^ pin;
                const auto found = inputs.find(key);
                if (found == inputs.end()) return fallback;
                return Build(found->second->from_node);
            }

        Value Generator::Build(std::uint64_t id)
            {
                const auto memo_found = memo.find(id);
                if (memo_found != memo.end()) return memo_found->second;
                if (!visiting.insert(id).second)
                {
                    AddError(id, "Graph に cycle があります");
                    return {};
                }
                const ShaderComposerNode* node = asset.FindNode(id);
                if (node == nullptr)
                {
                    AddError(id, "Connection source node がありません");
                    visiting.erase(id);
                    return {};
                }

                Value result;
                const std::string var = "replay_n" + std::to_string(node->id);
                auto emit = [&](ShaderComposerValueType type, const std::string& expression)
                {
                    statements << "    " << TypeName(type) << ' ' << var << " = " << expression << ";\n";
                    result = { type, var, true };
                };

                switch (node->kind)
                {
                case ShaderComposerNodeKind::UV:
                    emit(ShaderComposerValueType::Float2, "pin.texcoord"); break;
                case ShaderComposerNodeKind::Time:
                    emit(ShaderComposerValueType::Float, "frame_params.z"); break;
                case ShaderComposerNodeKind::Normal:
                    emit(ShaderComposerValueType::Float3, "normalize(pin.world_normal.xyz)"); break;
                case ShaderComposerNodeKind::ViewDirection:
                    emit(ShaderComposerValueType::Float3,
                        "normalize(camera_position.xyz - pin.world_position.xyz)"); break;
                case ShaderComposerNodeKind::Float:
                    emit(ShaderComposerValueType::Float, FloatLiteral(node->value)); break;
                case ShaderComposerNodeKind::Color:
                    emit(ShaderComposerValueType::Float4, "float4(" + FloatLiteral(node->color.x) + "," +
                        FloatLiteral(node->color.y) + "," + FloatLiteral(node->color.z) + "," + FloatLiteral(node->color.w) + ")"); break;
                case ShaderComposerNodeKind::FloatProperty:
                    emit(ShaderComposerValueType::Float, node->name); break;
                case ShaderComposerNodeKind::ColorProperty:
                    emit(ShaderComposerValueType::Float4, node->name); break;
                case ShaderComposerNodeKind::TextureProperty:
                {
                    Value uv = Input(*node, 0, { ShaderComposerValueType::Float2, "pin.texcoord", true });
                    const std::string uv2 = Convert(uv, ShaderComposerValueType::Float2);
                    if (uv2.empty()) AddError(node->id, "Texture UV input は float2 に変換できません");
                    else emit(ShaderComposerValueType::Float4,
                        node->name + ".Sample(replay_composer_sampler, " + uv2 + ")");
                    break;
                }
                case ShaderComposerNodeKind::Add:
                case ShaderComposerNodeKind::Subtract:
                case ShaderComposerNodeKind::Multiply:
                case ShaderComposerNodeKind::Divide:
                {
                    Value a = Input(*node, 0, { ShaderComposerValueType::Float, "0.0f", true });
                    Value b = Input(*node, 1, { ShaderComposerValueType::Float, "0.0f", true });
                    ShaderComposerValueType type = MergeTypes(a.type, b.type);
                    if (type == ShaderComposerValueType::Invalid)
                    {
                        AddError(node->id, "Math node は同じ vector size または scalar と vector を接続してください");
                        break;
                    }
                    const std::string ax = Convert(a, type), bx = Convert(b, type);
                    const char* op = node->kind == ShaderComposerNodeKind::Add ? "+" :
                        node->kind == ShaderComposerNodeKind::Subtract ? "-" :
                        node->kind == ShaderComposerNodeKind::Multiply ? "*" : "/";
                    const std::string right = node->kind == ShaderComposerNodeKind::Divide
                        ? "(" + bx + " + 1.0e-6f)" : bx;
                    emit(type, "(" + ax + " " + op + " " + right + ")");
                    break;
                }
                case ShaderComposerNodeKind::Lerp:
                {
                    Value a = Input(*node, 0, { ShaderComposerValueType::Float, "0.0f", true });
                    Value b = Input(*node, 1, { ShaderComposerValueType::Float, "1.0f", true });
                    Value t = Input(*node, 2, { ShaderComposerValueType::Float, "0.5f", true });
                    ShaderComposerValueType type = MergeTypes(a.type, b.type);
                    if (type == ShaderComposerValueType::Invalid) { AddError(node->id, "Lerp A/B type が互換ではありません"); break; }
                    const std::string tx = t.type == ShaderComposerValueType::Float
                        ? t.expression : Convert(t, type);
                    if (tx.empty()) { AddError(node->id, "Lerp T type が互換ではありません"); break; }
                    emit(type, "lerp(" + Convert(a, type) + ", " + Convert(b, type) + ", " + tx + ")");
                    break;
                }
                case ShaderComposerNodeKind::Saturate:
                {
                    Value a = Input(*node, 0, { ShaderComposerValueType::Float, "0.0f", true });
                    emit(a.type, "saturate(" + a.expression + ")"); break;
                }
                case ShaderComposerNodeKind::Power:
                {
                    Value a = Input(*node, 0, { ShaderComposerValueType::Float, "1.0f", true });
                    Value b = Input(*node, 1, { ShaderComposerValueType::Float, "2.0f", true });
                    ShaderComposerValueType type = a.type;
                    const std::string bx = b.type == ShaderComposerValueType::Float
                        ? b.expression : Convert(b, type);
                    if (bx.empty()) { AddError(node->id, "Power exponent type が互換ではありません"); break; }
                    emit(type, "pow(max(" + a.expression + ", 0.0f), " + bx + ")"); break;
                }
                case ShaderComposerNodeKind::Fresnel:
                {
                    Value n = Input(*node, 0, { ShaderComposerValueType::Float3, "normalize(pin.world_normal.xyz)", true });
                    Value v = Input(*node, 1, { ShaderComposerValueType::Float3, "normalize(camera_position.xyz - pin.world_position.xyz)", true });
                    Value p = Input(*node, 2, { ShaderComposerValueType::Float, FloatLiteral(node->value), true });
                    const std::string n3 = Convert(n, ShaderComposerValueType::Float3);
                    const std::string v3 = Convert(v, ShaderComposerValueType::Float3);
                    const std::string p1 = Convert(p, ShaderComposerValueType::Float);
                    if (n3.empty() || v3.empty() || p1.empty()) { AddError(node->id, "Fresnel input type が不正です"); break; }
                    emit(ShaderComposerValueType::Float,
                        "pow(saturate(1.0f - dot(normalize(" + n3 + "), normalize(" + v3 + "))), max(" + p1 + ", 0.0001f))");
                    break;
                }
                case ShaderComposerNodeKind::UVScroll:
                {
                    Value uv = Input(*node, 0, { ShaderComposerValueType::Float2, "pin.texcoord", true });
                    Value speed = Input(*node, 1, { ShaderComposerValueType::Float2,
                        "float2(" + FloatLiteral(node->vector2.x) + "," + FloatLiteral(node->vector2.y) + ")", true });
                    Value time = Input(*node, 2, { ShaderComposerValueType::Float, "frame_params.z", true });
                    const std::string uv2 = Convert(uv, ShaderComposerValueType::Float2);
                    const std::string speed2 = Convert(speed, ShaderComposerValueType::Float2);
                    const std::string time1 = Convert(time, ShaderComposerValueType::Float);
                    if (uv2.empty() || speed2.empty() || time1.empty()) { AddError(node->id, "UV Scroll input type が不正です"); break; }
                    emit(ShaderComposerValueType::Float2, "(" + uv2 + " + " + speed2 + " * " + time1 + ")");
                    break;
                }
                case ShaderComposerNodeKind::Noise:
                {
                    Value uv = Input(*node, 0, { ShaderComposerValueType::Float2, "pin.texcoord", true });
                    Value scale = Input(*node, 1, { ShaderComposerValueType::Float, FloatLiteral(node->value), true });
                    const std::string uv2 = Convert(uv, ShaderComposerValueType::Float2);
                    const std::string scale1 = Convert(scale, ShaderComposerValueType::Float);
                    if (uv2.empty() || scale1.empty()) { AddError(node->id, "Noise input type が不正です"); break; }
                    emit(ShaderComposerValueType::Float, "replay_composer_noise(" + uv2 + " * " + scale1 + ")");
                    break;
                }
                case ShaderComposerNodeKind::Dissolve:
                {
                    Value value = Input(*node, 0, { ShaderComposerValueType::Float, "1.0f", true });
                    Value threshold = Input(*node, 1, { ShaderComposerValueType::Float, FloatLiteral(node->value), true });
                    Value edge = Input(*node, 2, { ShaderComposerValueType::Float, FloatLiteral(node->minimum), true });
                    const std::string v = Convert(value, ShaderComposerValueType::Float);
                    const std::string t = Convert(threshold, ShaderComposerValueType::Float);
                    const std::string e = Convert(edge, ShaderComposerValueType::Float);
                    if (v.empty() || t.empty() || e.empty()) { AddError(node->id, "Dissolve input type が不正です"); break; }
                    emit(ShaderComposerValueType::Float,
                        "smoothstep(" + t + " - max(" + e + ", 1.0e-5f), " + t + " + max(" + e + ", 1.0e-5f), " + v + ")");
                    break;
                }
                case ShaderComposerNodeKind::SurfaceOutput:
                case ShaderComposerNodeKind::LayerOutput:
                    AddError(node->id, "Output node を別 node の入力へ接続できません"); break;
                default:
                    AddError(node->id, "未対応 node kind"); break;
                }

                visiting.erase(id);
                memo[id] = result;
                return result;
            }
    }
}
