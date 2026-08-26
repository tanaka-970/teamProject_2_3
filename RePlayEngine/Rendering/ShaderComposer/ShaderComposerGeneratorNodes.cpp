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
                case ShaderComposerNodeKind::Gradient:
                {
                    Value a = Input(*node, 0, { ShaderComposerValueType::Float, "0.0f", true });
                    Value b = Input(*node, 1, { ShaderComposerValueType::Float, "1.0f", true });
                    Value t = Input(*node, 2, { ShaderComposerValueType::Float, "0.5f", true });
                    ShaderComposerValueType type = MergeTypes(a.type, b.type);
                    if (type == ShaderComposerValueType::Invalid) { AddError(node->id,
                        node->kind == ShaderComposerNodeKind::Gradient
                            ? "Gradient A/B type が互換ではありません"
                            : "Lerp A/B type が互換ではありません"); break; }
                    const std::string tx = t.type == ShaderComposerValueType::Float
                        ? t.expression : Convert(t, type);
                    if (tx.empty()) { AddError(node->id,
                        node->kind == ShaderComposerNodeKind::Gradient
                            ? "Gradient T type が互換ではありません"
                            : "Lerp T type が互換ではありません"); break; }
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
                case ShaderComposerNodeKind::PixelSize:
                    emit(ShaderComposerValueType::Float2, "target_size.zw"); break;
                case ShaderComposerNodeKind::AspectRatio:
                    emit(ShaderComposerValueType::Float,
                        "(target_size.x / max(target_size.y, 1.0f))"); break;
                case ShaderComposerNodeKind::Sin:
                case ShaderComposerNodeKind::Cos:
                case ShaderComposerNodeKind::Abs:
                case ShaderComposerNodeKind::OneMinus:
                {
                    Value a = Input(*node, 0, { ShaderComposerValueType::Float, "0.0f", true });
                    const char* fn = node->kind == ShaderComposerNodeKind::Sin ? "sin" :
                        node->kind == ShaderComposerNodeKind::Cos ? "cos" : "abs";
                    if (node->kind == ShaderComposerNodeKind::OneMinus)
                        emit(a.type, "(1.0f - " + a.expression + ")");
                    else
                        emit(a.type, std::string(fn) + "(" + a.expression + ")");
                    break;
                }
                case ShaderComposerNodeKind::Step:
                case ShaderComposerNodeKind::Minimum:
                case ShaderComposerNodeKind::Maximum:
                {
                    Value a = Input(*node, 0, { ShaderComposerValueType::Float, "0.0f", true });
                    Value b = Input(*node, 1, { ShaderComposerValueType::Float, "0.0f", true });
                    ShaderComposerValueType type = MergeTypes(a.type, b.type);
                    if (type == ShaderComposerValueType::Invalid)
                    { AddError(node->id, "2入力の型が互換ではありません"); break; }
                    const std::string ax = Convert(a, type);
                    const std::string bx = Convert(b, type);
                    const char* fn = node->kind == ShaderComposerNodeKind::Step ? "step" :
                        node->kind == ShaderComposerNodeKind::Minimum ? "min" : "max";
                    emit(type, std::string(fn) + "(" + ax + ", " + bx + ")");
                    break;
                }
                case ShaderComposerNodeKind::Smoothstep:
                {
                    Value a = Input(*node, 0, { ShaderComposerValueType::Float, "0.0f", true });
                    Value b = Input(*node, 1, { ShaderComposerValueType::Float, "1.0f", true });
                    Value x = Input(*node, 2, { ShaderComposerValueType::Float, "0.5f", true });
                    ShaderComposerValueType type = MergeTypes(a.type, b.type);
                    type = MergeTypes(type, x.type);
                    if (type == ShaderComposerValueType::Invalid)
                    { AddError(node->id, "Smoothstep input type が互換ではありません"); break; }
                    emit(type, "smoothstep(" + Convert(a, type) + ", " +
                        Convert(b, type) + ", " + Convert(x, type) + ")");
                    break;
                }
                case ShaderComposerNodeKind::Clamp:
                {
                    Value x = Input(*node, 0, { ShaderComposerValueType::Float, "0.0f", true });
                    Value lo = Input(*node, 1, { ShaderComposerValueType::Float, "0.0f", true });
                    Value hi = Input(*node, 2, { ShaderComposerValueType::Float, "1.0f", true });
                    ShaderComposerValueType type = MergeTypes(x.type, lo.type);
                    type = MergeTypes(type, hi.type);
                    if (type == ShaderComposerValueType::Invalid)
                    { AddError(node->id, "Clamp input type が互換ではありません"); break; }
                    emit(type, "clamp(" + Convert(x, type) + ", " +
                        Convert(lo, type) + ", " + Convert(hi, type) + ")");
                    break;
                }
                case ShaderComposerNodeKind::Dot:
                {
                    Value a = Input(*node, 0, { ShaderComposerValueType::Float2, "float2(0,0)", true });
                    Value b = Input(*node, 1, { ShaderComposerValueType::Float2, "float2(0,0)", true });
                    ShaderComposerValueType type = MergeTypes(a.type, b.type);
                    if (type == ShaderComposerValueType::Invalid || type == ShaderComposerValueType::Float)
                    { AddError(node->id, "Dot は同じvector型を接続してください"); break; }
                    emit(ShaderComposerValueType::Float,
                        "dot(" + Convert(a, type) + ", " + Convert(b, type) + ")");
                    break;
                }
                case ShaderComposerNodeKind::Length:
                {
                    Value a = Input(*node, 0, { ShaderComposerValueType::Float2, "float2(0,0)", true });
                    emit(ShaderComposerValueType::Float, "length(" + a.expression + ")"); break;
                }
                case ShaderComposerNodeKind::Remap:
                {
                    Value value = Input(*node, 0, { ShaderComposerValueType::Float, "0.0f", true });
                    Value in_min = Input(*node, 1, { ShaderComposerValueType::Float, "0.0f", true });
                    Value in_max = Input(*node, 2, { ShaderComposerValueType::Float, "1.0f", true });
                    Value out_min = Input(*node, 3, { ShaderComposerValueType::Float, "0.0f", true });
                    Value out_max = Input(*node, 4, { ShaderComposerValueType::Float, "1.0f", true });
                    ShaderComposerValueType type = value.type;
                    const std::string i0 = Convert(in_min, type), i1 = Convert(in_max, type);
                    const std::string o0 = Convert(out_min, type), o1 = Convert(out_max, type);
                    if (i0.empty() || i1.empty() || o0.empty() || o1.empty())
                    { AddError(node->id, "Remap input type が互換ではありません"); break; }
                    emit(type, "(" + o0 + " + (" + value.expression + " - " + i0 +
                        ") / max(abs(" + i1 + " - " + i0 + "), 1.0e-6f) * (" +
                        o1 + " - " + o0 + "))");
                    break;
                }
                case ShaderComposerNodeKind::RotateUV:
                {
                    Value uv = Input(*node, 0, { ShaderComposerValueType::Float2, "pin.texcoord", true });
                    Value angle = Input(*node, 1, { ShaderComposerValueType::Float, "0.0f", true });
                    Value center = Input(*node, 2, { ShaderComposerValueType::Float2,
                        "float2(" + FloatLiteral(node->vector2.x) + "," +
                        FloatLiteral(node->vector2.y) + ")", true });
                    const std::string uv2 = Convert(uv, ShaderComposerValueType::Float2);
                    const std::string a1 = Convert(angle, ShaderComposerValueType::Float);
                    const std::string c2 = Convert(center, ShaderComposerValueType::Float2);
                    if (uv2.empty() || a1.empty() || c2.empty())
                    { AddError(node->id, "Rotate UV input type が不正です"); break; }
                    const std::string d = "(" + uv2 + " - " + c2 + ")";
                    emit(ShaderComposerValueType::Float2,
                        "(" + c2 + " + float2(" + d + ".x*cos(" + a1 + ") - " + d +
                        ".y*sin(" + a1 + "), " + d + ".x*sin(" + a1 + ") + " + d +
                        ".y*cos(" + a1 + ")))");
                    break;
                }
                case ShaderComposerNodeKind::PolarUV:
                {
                    Value uv = Input(*node, 0, { ShaderComposerValueType::Float2, "pin.texcoord", true });
                    const std::string uv2 = Convert(uv, ShaderComposerValueType::Float2);
                    if (uv2.empty()) { AddError(node->id, "Polar UV は float2 が必要です"); break; }
                    const std::string d = "(" + uv2 + " - float2(0.5f,0.5f))";
                    emit(ShaderComposerValueType::Float2,
                        "float2(atan2(" + d + ".y," + d + ".x)/6.28318530718f+0.5f, length(" +
                        d + ")*2.0f)");
                    break;
                }
                case ShaderComposerNodeKind::Component:
                {
                    Value a = Input(*node, 0, { ShaderComposerValueType::Float4, "float4(0,0,0,0)", true });
                    int channel = static_cast<int>(std::round(node->value));
                    channel = (std::max)(0, (std::min)(3, channel));
                    const char* swizzle[] = { ".x", ".y", ".z", ".w" };
                    emit(ShaderComposerValueType::Float, "(" + Convert(a,
                        ShaderComposerValueType::Float4) + ")" + swizzle[channel]);
                    break;
                }
                case ShaderComposerNodeKind::Combine4:
                {
                    Value x = Input(*node, 0, { ShaderComposerValueType::Float, "0.0f", true });
                    Value y = Input(*node, 1, { ShaderComposerValueType::Float, "0.0f", true });
                    Value z = Input(*node, 2, { ShaderComposerValueType::Float, "0.0f", true });
                    Value w = Input(*node, 3, { ShaderComposerValueType::Float, "1.0f", true });
                    emit(ShaderComposerValueType::Float4, "float4(" +
                        Convert(x, ShaderComposerValueType::Float) + "," +
                        Convert(y, ShaderComposerValueType::Float) + "," +
                        Convert(z, ShaderComposerValueType::Float) + "," +
                        Convert(w, ShaderComposerValueType::Float) + ")");
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
